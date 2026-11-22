#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef OA_ENGINE_CORE_TEST_BACKEND
#include "engine_core_fixture_platform.h"
#else
#include <sched.h>
#include <unistd.h>

static int engine_get_affinity(cpu_set_t *mask)
{
    CPU_ZERO(mask);
    return sched_getaffinity(0, sizeof(*mask), mask) ? errno : 0;
}

static int engine_set_affinity(const cpu_set_t *mask)
{
    return sched_setaffinity(0, sizeof(*mask), mask) ? errno : 0;
}

static int engine_cpu_capacity(unsigned cpu, unsigned *capacity)
{
    char path[96];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%u/cpu_capacity", cpu);
    FILE *file = fopen(path, "r");
    if (!file) return errno;
    const int ok = fscanf(file, "%u", capacity) == 1;
    fclose(file);
    return ok && *capacity ? 0 : EINVAL;
}

static int engine_tid(void) { return gettid(); }
#endif

extern void rt_log(const char *, ...) __attribute__((format(printf, 1, 2)));
extern int oa_fast_engine_cores;

enum { RETRY_TICKS = 60, MASK_TEXT_SIZE = 3 + (CPU_SETSIZE + 3) / 4 };

typedef struct {
    cpu_set_t original;
    int mode;
    int pending;
    int saved;
    int restricted;
    int recovering;
    unsigned retry;
    const char *last_issue;
} EngineAffinity;

static _Thread_local EngineAffinity affinity[2];

/* Use the two highest distinct tiers from this role's original allowed CPUs. */
static int capacity_groups(const cpu_set_t *allowed, cpu_set_t *highest, cpu_set_t *second,
                           unsigned *high_capacity, unsigned *second_capacity)
{
    CPU_ZERO(highest);
    CPU_ZERO(second);
    *high_capacity = *second_capacity = 0;
    int remaining = CPU_COUNT(allowed);
    for (unsigned cpu = 0; cpu < CPU_SETSIZE && remaining; ++cpu) {
        if (!CPU_ISSET(cpu, allowed)) continue;
        --remaining;
        unsigned value = 0;
        const int error = engine_cpu_capacity(cpu, &value);
        if (error) return error;
        if (value > *high_capacity) {
            *second = *highest;
            *second_capacity = *high_capacity;
            *high_capacity = value;
            CPU_ZERO(highest);
        } else if (value < *high_capacity && value > *second_capacity) {
            *second_capacity = value;
            CPU_ZERO(second);
        }
        if (value == *high_capacity) CPU_SET(cpu, highest);
        else if (value == *second_capacity) CPU_SET(cpu, second);
    }
    return 0;
}

static void mask_text(const cpu_set_t *mask, char out[MASK_TEXT_SIZE])
{
    static const char hex[] = "0123456789abcdef";
    unsigned size = 2;
    out[0] = '0';
    out[1] = 'x';
    for (int base = (CPU_SETSIZE - 1) / 4 * 4; base >= 0; base -= 4) {
        unsigned digit = 0;
        for (int bit = 0; bit < 4 && base + bit < CPU_SETSIZE; ++bit)
            if (CPU_ISSET(base + bit, mask)) digit |= 1u << bit;
        if (digit || size > 2 || !base) out[size++] = hex[digit];
    }
    out[size] = 0;
}

static int set_exact(const cpu_set_t *target)
{
    cpu_set_t actual;
    const int set_error = engine_set_affinity(target);
    const int get_error = engine_get_affinity(&actual);
    if (!get_error && CPU_EQUAL(&actual, target)) return 0;
    if (set_error) return set_error;
    return get_error ? get_error : EAGAIN;
}

static int restore(EngineAffinity *state)
{
    if (!state->restricted) return 0;
    const int error = set_exact(&state->original);
    if (!error) state->restricted = state->recovering = 0;
    return error;
}

static void retry(EngineAffinity *state, unsigned role, const char *issue, int error)
{
    state->retry = RETRY_TICKS;
    if (state->last_issue && !strcmp(state->last_issue, issue)) return;
    state->last_issue = issue;
    rt_log("ENGINE_CORES role=%u tid=%d mode=%d status=%s error=%d retry_ticks=%u",
           role, engine_tid(), state->mode, issue, error, state->retry);
}

static void settle_auto(EngineAffinity *state, unsigned role, const char *reason)
{
    const int had_restriction = state->restricted;
    const int error = restore(state);
    if (error) {
        state->recovering = 1;
        retry(state, role, "restore_failed", error);
        return;
    }
    if (state->saved) {
        char prior[MASK_TEXT_SIZE];
        mask_text(&state->original, prior);
        rt_log("ENGINE_CORES role=%u tid=%d mode=%d status=auto restored=%d mask=%s reason=%s",
               role, engine_tid(), state->mode, had_restriction, prior, reason);
    }
    state->saved = state->pending = 0;
}

void oa_engine_core_tick(unsigned role)
{
    if (role < 1 || role > 2) return;
    EngineAffinity *state = &affinity[role - 1];
    const int mode = __atomic_load_n(&oa_fast_engine_cores, __ATOMIC_RELAXED);
    if (mode != state->mode) {
        state->mode = mode;
        state->pending = 1;
        state->retry = 0;
        state->last_issue = NULL;
    }
    if (!state->pending || (state->retry && --state->retry)) return;
    if (state->recovering) {
        const int error = restore(state);
        if (error) {
            retry(state, role, "restore_failed", error);
            return;
        }
    }
    if (mode < (int)role || mode > 5) {
        settle_auto(state, role, "mode");
        return;
    }
    if (!state->saved) {
        const int error = engine_get_affinity(&state->original);
        if (error) {
            retry(state, role, "get_failed", error);
            return;
        }
        state->saved = 1;
    }

    cpu_set_t highest, second;
    unsigned high_capacity, second_capacity;
    const int topology_error = capacity_groups(&state->original, &highest, &second,
                                                &high_capacity, &second_capacity);
    if (topology_error) {
        const int error = restore(state);
        if (error) state->recovering = 1;
        retry(state, role, error ? "restore_failed" : "capacity_unavailable",
              error ? error : topology_error);
        return;
    }
    if (mode >= 3 && !second_capacity) {
        settle_auto(state, role, "one_capacity_tier");
        return;
    }

    const int use_second = mode >= 4 || (mode == 3 && role == 2);
    const cpu_set_t *target = use_second ? &second : &highest;
    cpu_set_t mixed;
    if (mode == 5 && role == 2 && CPU_COUNT(&second) >= 3) {
        CPU_ZERO(&mixed);
        // Give simulation one fast CPU and two same-tier fallback CPUs.
        for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            if (CPU_ISSET(cpu, &highest)) {
                CPU_SET(cpu, &mixed);
                break;
            }
        }
        unsigned alternatives = 0;
        for (unsigned cpu = CPU_SETSIZE; cpu > 0 && alternatives < 2; --cpu) {
            if (CPU_ISSET(cpu - 1, &second)) {
                CPU_SET(cpu - 1, &mixed);
                ++alternatives;
            }
        }
        target = &mixed;
    }
    const int count = CPU_COUNT(target);
    /* Preserve the legacy simulation guard for modes 1 and 2. */
    if (!count || (mode < 3 && count < (int)role) || CPU_EQUAL(target, &state->original)) {
        settle_auto(state, role, "legacy_capacity_guard");
        return;
    }

    state->restricted = 1;
    const int apply_error = set_exact(target);
    if (apply_error) {
        const int error = restore(state);
        if (error) state->recovering = 1;
        retry(state, role, error ? "restore_failed" : "apply_failed", error ? error : apply_error);
        return;
    }
    state->pending = 0;
    char applied[MASK_TEXT_SIZE], prior[MASK_TEXT_SIZE];
    mask_text(target, applied);
    mask_text(&state->original, prior);
    rt_log("ENGINE_CORES role=%u tid=%d applied=1 mode=%d status=applied cpus=%d/%d capacity=%u mask=%s prior=%s",
           role, engine_tid(), mode, count, CPU_COUNT(&state->original),
           (use_second && target != &mixed) ? second_capacity : high_capacity, applied, prior);
}

/* Read-only original-mask export for bounded helper work. Never changes caller affinity. */
int oa_engine_original_allowed(unsigned role,void *output,size_t bytes){
 if(role<1||role>2||bytes!=sizeof(cpu_set_t))return EINVAL;
 EngineAffinity *state=&affinity[role-1];
 if(state->saved){memcpy(output,&state->original,sizeof(cpu_set_t));return 0;}
 return engine_get_affinity((cpu_set_t*)output);
}
