/* Guest HANDLEs, synchronization objects and threads.

   Object state and wait-all acquisition remain serialized under the original mutex.
   Optional targeted notifications wake only waiters whose object changed. Both
   paths share readiness and acquisition. Fast zero-time polls skip clock reads.
   An empty semaphore can report timeout before taking the shared lock. */
#include "rt.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define ARG(i) rt_arg(ctx, i)
#define RET(v) (ctx->eax = (uint32_t)(v))
#define INVALID_HANDLE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 0x102u
#define WAIT_FAILED 0xFFFFFFFFu
#define STILL_ACTIVE 259u

typedef struct RtObj {
    int type;
    void *data;                 /* files and find handles (rt_files.c) */
    /* events */
    int manual, signaled;
    /* mutexes */
    uint32_t owner, count;
    /* semaphores */
    int32_t sem, sem_max;
    /* threads */
    RtThread *thread;
    int done, suspended;
    uint32_t exit_code, start, param;
} RtObj;

#define MAX_HANDLES 65536
static RtObj *handles[MAX_HANDLES];
static HostMutex *sync_lock;
static HostCond *sync_cond;

/* Every registration is added/removed while holding sync_lock. The sleeping
   wait owns its condition variable; signalers never retain a stack pointer.
   A mode change is safe even while legacy or targeted waiters remain asleep. */
typedef struct SyncWaiter {
    struct SyncWaiter *next;
    HostCond *cond;
    const uint32_t *handles;
    uint32_t count;
    RtObj *suspended_thread;
    int single_infinite;
    int notified;
} SyncWaiter;
static SyncWaiter *sync_waiters;
static unsigned legacy_waiters;
typedef struct SyncCondition {
    struct SyncCondition *next;
    HostCond *cond;
} SyncCondition;
/* Recycle under sync_lock: allocation is bounded by simultaneous blocked
   targeted waits, not by the number of host threads ever created. */
static SyncCondition *unused_conditions;
enum { SYNC_TARGETED = 1, SYNC_FAST_POLL = 2 };
static int sync_mode, semaphore_wake_one;
static uint64_t sync_sleeps, sync_wakes, sync_notifications;
static uint64_t sync_target_signals, sync_legacy_signals;
static uint64_t wake_one_eligible, wake_one_signals, wake_one_pending, wake_one_empty;
static uint64_t wake_one_fallback_mode, wake_one_fallback_count, wake_one_fallback_waitset;

void oa_sync_set_wake_one(int enabled) {
    __atomic_store_n(&semaphore_wake_one, enabled != 0, __ATOMIC_RELAXED);
}

int oa_sync_get_wake_one(void) {
    return __atomic_load_n(&semaphore_wake_one, __ATOMIC_RELAXED);
}

void oa_sync_set_mode(int mode) {
    __atomic_store_n(&sync_mode, mode & (SYNC_TARGETED | SYNC_FAST_POLL), __ATOMIC_RELAXED);
}
int oa_sync_get_mode(void) {
    return __atomic_load_n(&sync_mode, __ATOMIC_RELAXED);
}


static void sync_init(void)
{
    if (!sync_lock) { sync_lock = host_mutex_new(); sync_cond = host_cond_new(); }
}

/* Handle values: 0x100 + 4 * index, never 0 or INVALID_HANDLE_VALUE. */
uint32_t rt_handle_new(int type, void *data)
{
    sync_init();
    RtObj *o = calloc(1, sizeof *o);
    o->type = type;
    o->data = data;
    host_lock(sync_lock);
    for (uint32_t i = 1; i < MAX_HANDLES; i++)
        if (!__atomic_load_n(&handles[i], __ATOMIC_RELAXED)) {
            __atomic_store_n(&handles[i], o, __ATOMIC_RELEASE);
            host_unlock(sync_lock);
            return 0x100 + 4 * i;
        }
    host_unlock(sync_lock);
    rt_fatal("out of guest handles");
}

static RtObj *obj(uint32_t h)
{
    uint32_t i = (h - 0x100) / 4;
    if (h < 0x100 || (h & 3) || i >= MAX_HANDLES) return NULL;
    return __atomic_load_n(&handles[i], __ATOMIC_ACQUIRE);
}

void *rt_handle_data(uint32_t h, int type)
{
    RtObj *o = obj(h);
    return o && o->type == type ? o->data : NULL;
}

void rt_handle_close(uint32_t h)
{
    uint32_t i = (h - 0x100) / 4;
    if (h < 0x100 || (h & 3) || i >= MAX_HANDLES) return;
    host_lock(sync_lock);
    /* Threads keep running. The object remains allocated for in-flight users. */
    __atomic_store_n(&handles[i], NULL, __ATOMIC_RELEASE);
    host_unlock(sync_lock);
}

/* Caller holds sync_lock. Readiness is checked before registration while the
   same lock is held, and host_cond_wait atomically unlocks and sleeps. */
static void sync_sleep(const uint32_t *hs, uint32_t n, RtObj *suspended, uint32_t ms,
                       int single_infinite)
{
    SyncWaiter waiter = {0};
    int targeted = (oa_sync_get_mode() & SYNC_TARGETED) != 0;
    SyncCondition *slot = NULL;
    if (targeted) {
        slot = unused_conditions;
        if (slot) unused_conditions = slot->next;
        else {
            slot = calloc(1, sizeof *slot);
            if (!slot) rt_fatal("cannot allocate synchronization condition");
            slot->cond = host_cond_new();
        }
    }
    waiter.cond = targeted ? slot->cond : sync_cond;
    waiter.handles = hs;
    waiter.count = n;
    waiter.suspended_thread = suspended;
    waiter.single_infinite = single_infinite;
    waiter.next = sync_waiters;
    sync_waiters = &waiter;
    if (!targeted) legacy_waiters++;
    sync_sleeps++;
    host_cond_wait(waiter.cond, sync_lock, ms);
    sync_wakes++;
    SyncWaiter **link = &sync_waiters;
    while (*link && *link != &waiter) link = &(*link)->next;
    if (!*link) rt_fatal("missing synchronization waiter");
    *link = waiter.next;
    if (!targeted) legacy_waiters--;
    if (slot) {
        slot->next = unused_conditions;
        unused_conditions = slot;
    }
}

static int waits_on(const SyncWaiter *waiter, RtObj *changed)
{
    if (waiter->suspended_thread == changed) return 1;
    for (uint32_t i = 0; i < waiter->count; ++i)
        if (obj(waiter->handles[i]) == changed) return 1;
    return 0;
}

/* Preserve notifications to sleepers that entered under either mode. */
static void notify(RtObj *changed)
{
    int targeted = (oa_sync_get_mode() & SYNC_TARGETED) != 0;
    sync_notifications++;
    if (!targeted || legacy_waiters) {
        host_cond_broadcast(sync_cond);
        sync_legacy_signals++;
    }
    for (SyncWaiter *w = sync_waiters; w; w = w->next) {
        if (w->cond == sync_cond) continue;
        if (!targeted || waits_on(w, changed)) {
            w->notified = 1;
            host_cond_broadcast(w->cond);
            sync_target_signals++;
        }
    }
}

/* sync_lock covers release, registration, selection, and acquisition.
   A signalled infinite waiter stays registered until it reacquires the lock.
   It rechecks readiness and either consumes the token or finds it consumed. */
static void notify_semaphore_release(RtObj *changed, int32_t previous,
                                     int32_t released, int32_t next)
{
    if (!oa_sync_get_wake_one()) {
        notify(changed);
        return;
    }
    if (oa_sync_get_mode() != SYNC_TARGETED || legacy_waiters) {
        ++wake_one_fallback_mode;
        notify(changed);
        return;
    }
    if (previous != 0 || released != 1 || next != 1) {
        ++wake_one_fallback_count;
        notify(changed);
        return;
    }
    SyncWaiter *oldest = NULL;
    int matching = 0;
    for (SyncWaiter *w = sync_waiters; w; w = w->next) {
        if (!waits_on(w, changed)) continue;
        if (!w->single_infinite) {
            ++wake_one_fallback_waitset;
            notify(changed);
            return;
        }
        matching = 1;
        if (!w->notified) oldest = w;
    }
    ++sync_notifications;
    if (!matching) {
        ++wake_one_empty;
        return;
    }
    ++wake_one_eligible;
    if (!oldest) {
        /* Every matching waiter is already returning from its condition wait. */
        ++wake_one_pending;
        return;
    }
    oldest->notified = 1;
    host_cond_broadcast(oldest->cond); /* One waiter owns this private condition. */
    ++sync_target_signals;
    ++wake_one_signals;
}

void oa_sync_report(void)
{
    sync_init();
    host_lock(sync_lock);
    uint64_t sleeps=sync_sleeps, wakes=sync_wakes, notifications=sync_notifications;
    uint64_t targeted=sync_target_signals, legacy=sync_legacy_signals;
    uint64_t eligible=wake_one_eligible, signals=wake_one_signals;
    uint64_t pending=wake_one_pending, empty=wake_one_empty;
    uint64_t fallback_mode=wake_one_fallback_mode, fallback_count=wake_one_fallback_count;
    uint64_t fallback_waitset=wake_one_fallback_waitset;
    host_unlock(sync_lock);
    rt_log("SYNC_REPORT mode=%d sleeps=%llu wakes=%llu changes=%llu targeted=%llu legacy=%llu",
           oa_sync_get_mode(), (unsigned long long)sleeps, (unsigned long long)wakes,
           (unsigned long long)notifications, (unsigned long long)targeted,
           (unsigned long long)legacy);
    const int enabled = oa_sync_get_wake_one();
    if (enabled || eligible || empty || fallback_mode || fallback_count || fallback_waitset)
        rt_log("SYNC_WAKE_ONE enabled=%d eligible=%llu signals=%llu pending=%llu empty=%llu fallback_mode=%llu fallback_count=%llu fallback_waitset=%llu",
               enabled, (unsigned long long)eligible, (unsigned long long)signals,
               (unsigned long long)pending, (unsigned long long)empty,
               (unsigned long long)fallback_mode, (unsigned long long)fallback_count,
               (unsigned long long)fallback_waitset);
}

/* ---- waiting ---- */

static int is_signaled(RtObj *o, uint32_t tid)
{
    switch (o->type) {
    case RT_H_EVENT: return o->signaled;
    case RT_H_MUTEX: return o->count == 0 || o->owner == tid;
    case RT_H_SEMAPHORE: return __atomic_load_n(&o->sem, __ATOMIC_RELAXED) > 0;
    case RT_H_THREAD: return o->done;
    default: return 1; /* files and others: signaled */
    }
}

static void acquire(RtObj *o, uint32_t tid)
{
    switch (o->type) {
    case RT_H_EVENT: if (!o->manual) o->signaled = 0; break;
    case RT_H_MUTEX: o->owner = tid; o->count++; break;
    case RT_H_SEMAPHORE: __atomic_fetch_sub(&o->sem, 1, __ATOMIC_RELAXED); break;
    default: break;
    }
}

static uint32_t wait_objects(uint32_t n, const uint32_t *hs, int all, uint32_t ms,
                             int single)
{
    sync_init();
    uint32_t tid = rt_thread_self()->id;
    int fast_poll = ms == 0 && (oa_sync_get_mode() & SYNC_FAST_POLL);
    if (fast_poll && n == 1) {
        RtObj *o = obj(hs[0]);
        /* A negative observation can linearize before a concurrent release.
           A ready semaphore must still be consumed under sync_lock. */
        if (o && o->type == RT_H_SEMAPHORE && __atomic_load_n(&o->sem, __ATOMIC_ACQUIRE) <= 0)
            return WAIT_TIMEOUT;
    }
    uint64_t deadline = ms == 0xFFFFFFFFu || fast_poll ? 0 : host_ticks_ns() + (uint64_t)ms * 1000000ull;
    host_lock(sync_lock);
    for (;;) {
        RtObj *os[64];
        uint32_t ready = 0, first = n;
        for (uint32_t i = 0; i < n; i++) {
            os[i] = obj(hs[i]);
            if (!os[i]) { host_unlock(sync_lock); return WAIT_FAILED; }
            if (is_signaled(os[i], tid)) { ready++; if (first == n) first = i; }
        }
        if (all ? ready == n : ready > 0) {
            if (all) for (uint32_t i = 0; i < n; i++) acquire(os[i], tid);
            else acquire(os[first], tid);
            host_unlock(sync_lock);
            return WAIT_OBJECT_0 + (all ? 0 : first);
        }
        if (fast_poll) { host_unlock(sync_lock); return WAIT_TIMEOUT; }
        uint32_t left = 0xFFFFFFFFu;
        if (ms != 0xFFFFFFFFu) {
            uint64_t now = host_ticks_ns();
            if (now >= deadline) { host_unlock(sync_lock); return WAIT_TIMEOUT; }
            left = (uint32_t)((deadline - now + 999999) / 1000000);
        }
        sync_sleep(hs, n, NULL, left, single && ms == 0xFFFFFFFFu);
    }
}

static void k_WaitForSingleObject(X86Ctx *ctx)
{
    uint32_t h = ARG(0);
    if (h == 0xFFFFFFFEu || h == 0xFFFFFFFFu) { RET(WAIT_TIMEOUT); return; } /* own thread/process */
    RET(wait_objects(1, &h, 0, ARG(1), 1));
}

static void k_WaitForSingleObjectEx(X86Ctx *ctx) { k_WaitForSingleObject(ctx); }

static void k_WaitForMultipleObjects(X86Ctx *ctx)
{
    uint32_t n = ARG(0), hs[64];
    if (n == 0 || n > 64) { RET(WAIT_FAILED); return; }
    for (uint32_t i = 0; i < n; i++) hs[i] = ld32(ARG(1) + 4 * i);
    RET(wait_objects(n, hs, ARG(2) != 0, ARG(3), 0));
}


/* ---- events, mutexes, semaphores ---- */

static void k_CreateEvent(X86Ctx *ctx)
{
    uint32_t h = rt_handle_new(RT_H_EVENT, NULL);
    RtObj *o = obj(h);
    o->manual = ARG(1) != 0;
    o->signaled = ARG(2) != 0;
    RET(h);
}

static void k_OpenEvent(X86Ctx *ctx)
{
    st32(rt_thread_self()->teb + 0x34, 2); /* named objects are not shared: not found */
    RET(0);
}

static void set_event(uint32_t h, int state, int pulse)
{
    RtObj *o = obj(h);
    if (!o || o->type != RT_H_EVENT) return;
    host_lock(sync_lock);
    o->signaled = state;
    notify(o);
    if (pulse) o->signaled = 0; /* wakes current waiters only on the next check: approximate */
    host_unlock(sync_lock);
}

void rt_event_set(uint32_t h) { sync_init(); set_event(h, 1, 0); }

static void k_SetEvent(X86Ctx *ctx) { set_event(ARG(0), 1, 0); RET(1); }
static void k_ResetEvent(X86Ctx *ctx) { set_event(ARG(0), 0, 0); RET(1); }
static void k_PulseEvent(X86Ctx *ctx) { set_event(ARG(0), 1, 1); RET(1); }

/* WSA events are manual-reset Win32 events. */
static void ws_WSACreateEvent(X86Ctx *ctx)
{
    uint32_t h = rt_handle_new(RT_H_EVENT, NULL);
    obj(h)->manual = 1;
    RET(h);
}

/* WSAWaitForMultipleEvents(n, events, all, timeout, alertable): no APCs, so alertable is ignored. */
static void ws_WSAWaitForMultipleEvents(X86Ctx *ctx)
{
    uint32_t n = ARG(0), hs[64];
    if (n == 0 || n > 64) { RET(WAIT_FAILED); return; }
    for (uint32_t i = 0; i < n; i++) hs[i] = ld32(ARG(1) + 4 * i);
    RET(wait_objects(n, hs, ARG(2) != 0, ARG(3), 0));
}

static void k_CreateMutex(X86Ctx *ctx)
{
    uint32_t h = rt_handle_new(RT_H_MUTEX, NULL);
    if (ARG(1)) { RtObj *o = obj(h); o->owner = rt_thread_self()->id; o->count = 1; }
    RET(h);
}

static void k_ReleaseMutex(X86Ctx *ctx)
{
    RtObj *o = obj(ARG(0));
    if (!o || o->type != RT_H_MUTEX || o->owner != rt_thread_self()->id || !o->count) { RET(0); return; }
    host_lock(sync_lock);
    if (!--o->count) o->owner = 0;
    notify(o);
    host_unlock(sync_lock);
    RET(1);
}

static void k_CreateSemaphore(X86Ctx *ctx)
{
    uint32_t h = rt_handle_new(RT_H_SEMAPHORE, NULL);
    RtObj *o = obj(h);
    __atomic_store_n(&o->sem, (int32_t)ARG(1), __ATOMIC_RELAXED);
    o->sem_max = (int32_t)ARG(2);
    RET(h);
}

static void k_ReleaseSemaphore(X86Ctx *ctx)
{
    RtObj *o = obj(ARG(0));
    if (!o || o->type != RT_H_SEMAPHORE) { RET(0); return; }
    host_lock(sync_lock);
    int32_t previous = __atomic_load_n(&o->sem, __ATOMIC_RELAXED);
    if (ARG(2)) st32(ARG(2), (uint32_t)previous);
    int32_t released = (int32_t)ARG(1);
    int32_t next = previous + released;
    if (next > o->sem_max) { host_unlock(sync_lock); RET(0); return; }
    __atomic_store_n(&o->sem, next, __ATOMIC_RELEASE);
    notify_semaphore_release(o, previous, released, next);
    host_unlock(sync_lock);
    RET(1);
}

/* ---- threads ---- */

static __thread jmp_buf *exit_jmp;
static __thread RtObj *self_obj;

static void thread_main(void *arg)
{
    RtObj *o = arg;
    rt_thread_bind(o->thread);
    self_obj = o;
    host_lock(sync_lock);
    while (o->suspended) sync_sleep(NULL, 0, o, 0xFFFFFFFFu, 0);
    host_unlock(sync_lock);
    jmp_buf jb;
    exit_jmp = &jb;
    if (!setjmp(jb)) {
        uint32_t p = o->param;
        o->exit_code = rt_call(&o->thread->ctx, o->start, 1, &p);
    }
    host_lock(sync_lock);
    o->done = 1;
    notify(o);
    host_unlock(sync_lock);
}

static void k_CreateThread(X86Ctx *ctx)
{
    uint32_t stack = ARG(1) ? ARG(1) : (1u << 20);
    if (stack < (256u << 10)) stack = 256u << 10;
    RtThread *t = rt_thread_new((stack + 0xFFFF) & ~0xFFFFu);
    uint32_t h = rt_handle_new(RT_H_THREAD, NULL);
    RtObj *o = obj(h);
    o->thread = t;
    o->start = ARG(2);
    o->param = ARG(3);
    o->suspended = (ARG(4) & 4) != 0; /* CREATE_SUSPENDED */
    o->exit_code = STILL_ACTIVE;
    if (ARG(5)) st32(ARG(5), t->id);
    rt_log("CreateThread start %#x param %#x id %#x%s", o->start, o->param, t->id, o->suspended ? " (suspended)" : "");
    if (!host_thread_start(thread_main, o)) rt_fatal("cannot start a host thread");
    RET(h);
}

static void k_ResumeThread(X86Ctx *ctx)
{
    RtObj *o = obj(ARG(0));
    if (!o || o->type != RT_H_THREAD) { RET(0xFFFFFFFFu); return; }
    host_lock(sync_lock);
    uint32_t prev = (uint32_t)o->suspended;
    o->suspended = 0;
    notify(o);
    host_unlock(sync_lock);
    RET(prev);
}

static void k_SuspendThread(X86Ctx *ctx)
{
    rt_log("SuspendThread(%#x): not supported, ignored", ARG(0));
    RET(0);
}

static void k_ExitThread(X86Ctx *ctx)
{
    if (!exit_jmp) rt_fatal("ExitThread on the main thread (code %u)", ARG(0));
    self_obj->exit_code = ARG(0);
    longjmp(*exit_jmp, 1);
}

static void k_GetExitCodeThread(X86Ctx *ctx)
{
    RtObj *o = obj(ARG(0));
    if (!o || o->type != RT_H_THREAD) { RET(0); return; }
    st32(ARG(1), o->done ? o->exit_code : STILL_ACTIVE);
    RET(1);
}

static void k_TerminateThread(X86Ctx *ctx)
{
    rt_log("TerminateThread(%#x): not supported, ignored", ARG(0));
    RET(0);
}

static void k_CloseHandle(X86Ctx *ctx)
{
    uint32_t h = ARG(0);
    extern void rt_file_close(uint32_t h);
    rt_file_close(h);
    rt_handle_close(h);
    RET(1);
}

static void k_DuplicateHandle(X86Ctx *ctx)
{
    /* The same object under the same value: CloseHandle of either closes both (approximate). */
    uint32_t src = ARG(1);
    if (src == 0xFFFFFFFEu) src = rt_handle_new(RT_H_THREAD, NULL); /* own thread: a handle that never signals */
    if (ARG(3)) st32(ARG(3), src);
    RET(1);
}

static const HleDef sync_defs[] = {
    {"kernel32.dll", "WaitForSingleObject", k_WaitForSingleObject, 2},
    {"kernel32.dll", "WaitForSingleObjectEx", k_WaitForSingleObjectEx, 3},
    {"kernel32.dll", "WaitForMultipleObjects", k_WaitForMultipleObjects, 4},
    {"kernel32.dll", "CreateEventA", k_CreateEvent, 4},
    {"kernel32.dll", "CreateEventW", k_CreateEvent, 4},
    {"kernel32.dll", "OpenEventA", k_OpenEvent, 3},
    {"kernel32.dll", "OpenEventW", k_OpenEvent, 3},
    {"kernel32.dll", "SetEvent", k_SetEvent, 1},
    {"kernel32.dll", "ResetEvent", k_ResetEvent, 1},
    {"kernel32.dll", "PulseEvent", k_PulseEvent, 1},
    {"kernel32.dll", "CreateMutexA", k_CreateMutex, 3},
    {"kernel32.dll", "CreateMutexW", k_CreateMutex, 3},
    {"kernel32.dll", "ReleaseMutex", k_ReleaseMutex, 1},
    {"kernel32.dll", "CreateSemaphoreA", k_CreateSemaphore, 4},
    {"kernel32.dll", "CreateSemaphoreW", k_CreateSemaphore, 4},
    {"kernel32.dll", "ReleaseSemaphore", k_ReleaseSemaphore, 3},
    {"kernel32.dll", "CreateThread", k_CreateThread, 6},
    {"kernel32.dll", "ResumeThread", k_ResumeThread, 1},
    {"kernel32.dll", "SuspendThread", k_SuspendThread, 1},
    {"kernel32.dll", "ExitThread", k_ExitThread, 1},
    {"kernel32.dll", "GetExitCodeThread", k_GetExitCodeThread, 2},
    {"kernel32.dll", "TerminateThread", k_TerminateThread, 2},
    {"kernel32.dll", "CloseHandle", k_CloseHandle, 1},
    {"ws2_32.dll", "WSACreateEvent", ws_WSACreateEvent, 0}, {"ws2_32.dll", "WSACloseEvent", k_CloseHandle, 1},
    {"ws2_32.dll", "WSASetEvent", k_SetEvent, 1}, {"ws2_32.dll", "WSAResetEvent", k_ResetEvent, 1},
    {"ws2_32.dll", "WSAWaitForMultipleEvents", ws_WSAWaitForMultipleEvents, 5},
    {"kernel32.dll", "DuplicateHandle", k_DuplicateHandle, 7},
};

void rt_sync_register(void) { rt_hle_register(sync_defs, sizeof sync_defs / sizeof *sync_defs); }
