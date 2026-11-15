#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include "rt.h"

typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
typedef struct AWorkDuration AWorkDuration;

extern int oa_adpf_mode;
unsigned oa_adpf_active_mask;
uint64_t oa_adpf_reports[2];

static pthread_once_t hint_once = PTHREAD_ONCE_INIT;
static pthread_key_t hint_key;
static APerformanceHintManager *manager;
static APerformanceHintSession *(*create_session)(APerformanceHintManager *, const int32_t *, size_t, int64_t);
static void (*close_session)(APerformanceHintSession *);
static int (*set_efficiency)(APerformanceHintSession *, _Bool);
static int (*report_elapsed)(APerformanceHintSession *, int64_t);
static int (*report_duration)(APerformanceHintSession *, AWorkDuration *);
static AWorkDuration *(*create_duration)(void);
static void (*release_duration)(AWorkDuration *);
static void (*set_start)(AWorkDuration *, int64_t);
static void (*set_total)(AWorkDuration *, int64_t);
static void (*set_cpu)(AWorkDuration *, int64_t);

typedef struct {
    APerformanceHintSession *session;
    AWorkDuration *duration;
    int mode;
    int error_logged;
    int64_t start, cpu_start;
} HintSession;

static _Thread_local HintSession local[2];

static void close_hint(HintSession *s, unsigned index)
{
    if (!s->session)
        return;
    close_session(s->session);
    if (s->duration)
        release_duration(s->duration);
    __atomic_fetch_and(&oa_adpf_active_mask, ~(1u << index), __ATOMIC_RELAXED);
    rt_log("ADPF role=%u tid=%d closed mode=%d", index+1, gettid(), s->mode);
    s->session = NULL;
    s->duration = NULL;
    s->start = 0;
}

static void close_thread_hints(void *ptr)
{
    HintSession *sessions = ptr;
    for (unsigned i=0; i<2; ++i)
        close_hint(&sessions[i], i);
}

static void load_hints(void)
{
    void *lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib)
        return;
    APerformanceHintManager *(*get_manager)(void) = dlsym(lib,"APerformanceHint_getManager");
    create_session = dlsym(lib,"APerformanceHint_createSession");
    close_session = dlsym(lib,"APerformanceHint_closeSession");
    set_efficiency = dlsym(lib,"APerformanceHint_setPreferPowerEfficiency");
    report_elapsed = dlsym(lib,"APerformanceHint_reportActualWorkDuration");
    report_duration = dlsym(lib,"APerformanceHint_reportActualWorkDuration2");
    create_duration = dlsym(lib,"AWorkDuration_create");
    release_duration = dlsym(lib,"AWorkDuration_release");
    set_start = dlsym(lib,"AWorkDuration_setWorkPeriodStartTimestampNanos");
    set_total = dlsym(lib,"AWorkDuration_setActualTotalDurationNanos");
    set_cpu = dlsym(lib,"AWorkDuration_setActualCpuDurationNanos");
    if (get_manager && create_session && close_session && report_elapsed &&
        !pthread_key_create(&hint_key,close_thread_hints))
        manager = get_manager();
}

static int64_t now_ns(clockid_t clock)
{
    struct timespec t;
    if (clock_gettime(clock,&t))
        return 0;
    return (int64_t)t.tv_sec * INT64_C(1000000000) + t.tv_nsec;
}

static void change_hint(HintSession *s, unsigned index, int mode)
{
    close_hint(s,index);
    s->mode = mode;
    s->error_logged = 0;
    if (!mode)
        return;
    pthread_once(&hint_once,load_hints);
    if (!manager || (mode == 2 && !set_efficiency) ||
        pthread_setspecific(hint_key,local)) {
        rt_log("ADPF role=%u unavailable mode=%d",index+1,mode);
        return;
    }
    const int32_t tid = gettid();
    /* Each role reports its measured work within the 60 Hz frame budget. */
    const int64_t target = INT64_C(16666667);
    s->session = create_session(manager,&tid,1,target);
    if (!s->session) {
        rt_log("ADPF role=%u tid=%d create failed mode=%d",index+1,tid,mode);
        return;
    }
    if (mode == 2) {
        const int rc = set_efficiency(s->session,1);
        if (rc) {
            rt_log("ADPF role=%u tid=%d efficiency rejected error=%d",index+1,tid,rc);
            close_hint(s,index);
            return;
        }
    }
    if (create_duration && release_duration && report_duration && set_start && set_total && set_cpu)
        s->duration = create_duration();
    __atomic_fetch_or(&oa_adpf_active_mask,1u<<index,__ATOMIC_RELAXED);
    rt_log("ADPF role=%u tid=%d mode=%d target=%lld detailed=%d",
           index+1,tid,mode,(long long)target,s->duration!=NULL);
}

void oa_hint_begin(unsigned role)
{
    HintSession *s = &local[role-1];
    const int mode = __atomic_load_n(&oa_adpf_mode,__ATOMIC_RELAXED);
    if (s->mode != mode)
        change_hint(s,role-1,mode);
    if (s->session) {
        s->start = now_ns(CLOCK_MONOTONIC);
        if (s->duration)
            s->cpu_start = now_ns(CLOCK_THREAD_CPUTIME_ID);
    }
}

void oa_hint_end(unsigned role)
{
    HintSession *s = &local[role-1];
    if (!s->start)
        return;
    const int64_t elapsed = now_ns(CLOCK_MONOTONIC) - s->start;
    int rc;
    if (elapsed <= 0) {
        s->start = 0;
        return;
    }
    if (s->duration) {
        int64_t cpu = now_ns(CLOCK_THREAD_CPUTIME_ID) - s->cpu_start;
        if (cpu <= 0) {
            s->start = 0;
            return;
        }
        if (cpu > elapsed)
            cpu = elapsed;
        set_start(s->duration,s->start);
        set_total(s->duration,elapsed);
        set_cpu(s->duration,cpu);
        rc = report_duration(s->session,s->duration);
    } else {
        rc = report_elapsed(s->session,elapsed);
    }
    s->start = 0;
    if (!rc)
        __atomic_fetch_add(&oa_adpf_reports[role-1],1,__ATOMIC_RELAXED);
    else if (!s->error_logged) {
        s->error_logged = 1;
        rt_log("ADPF role=%u mode=%d report failed error=%d",role,s->mode,rc);
    }
}
