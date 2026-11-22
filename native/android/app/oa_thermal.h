#ifndef OA_THERMAL_H
#define OA_THERMAL_H
/* Lowers the dynamic resolution ceiling before Android throttles the device.
 *
 * Android reports a thermal status and, from Android 12, a "headroom" forecast
 * where 1.0 means the device is about to throttle severely. When dynamic
 * resolution is on, the renderer keeps its level at or below the floor chosen
 * here, so a hot device trades pixels for frame rate early instead of
 * stuttering once the system clamps the CPU and GPU. */
#include <stdint.h>

/* Values of AThermalStatus in <android/thermal.h>. */
enum { OA_THERMAL_MODERATE = 2, OA_THERMAL_SEVERE = 3 };

/* Returns the resolution floor (0 full, 1 medium, 2 low) for the reported state.
 * headroom is NaN when Android cannot forecast. The floor rises immediately and
 * falls one step at a time, only once the device has cooled below the threshold. */
static inline unsigned oa_thermal_floor(float headroom, int status, unsigned floor)
{
    int known = headroom == headroom;
    unsigned want = 0;
    if (status >= OA_THERMAL_SEVERE || (known && headroom >= 0.95f)) want = 2;
    else if (status >= OA_THERMAL_MODERATE || (known && headroom >= 0.85f)) want = 1;
    if (want >= floor) return want;
    float release = floor == 2 ? 0.85f : 0.75f;
    int status_limit = floor == 2 ? OA_THERMAL_SEVERE : OA_THERMAL_MODERATE;
    if (status < status_limit && (!known || headroom < release)) return floor - 1;
    return floor;
}

#ifdef __ANDROID__
#include <dlfcn.h>
#include <math.h>

typedef struct {
    int tried;
    void *manager;
    int (*status)(void *);
    float (*headroom)(void *, int);
    uint64_t next_ns;
    unsigned floor;
    int last_status;
    float last_headroom;
} OaThermal;

/* Polls at most every two seconds; Android rate-limits headroom queries.
 * The NDK functions are looked up at runtime because the APK still supports
 * Android 10, which predates them. */
static inline unsigned oa_thermal_poll(OaThermal *t, uint64_t now_ns)
{
    if (!t->tried) {
        t->tried = 1;
        void *android = dlopen("libandroid.so", RTLD_NOW);
        void *(*acquire)(void) = android ? (void *(*)(void))dlsym(android, "AThermal_acquireManager") : 0;
        if (acquire) {
            t->manager = acquire();
            t->status = (int (*)(void *))dlsym(android, "AThermal_getCurrentThermalStatus");
            t->headroom = (float (*)(void *, int))dlsym(android, "AThermal_getThermalHeadroom");
        }
    }
    if (!t->manager || now_ns < t->next_ns) return t->floor;
    t->next_ns = now_ns + 2000000000ull;
    t->last_status = t->status ? t->status(t->manager) : 0;
    t->last_headroom = t->headroom ? t->headroom(t->manager, 10) : NAN;
    t->floor = oa_thermal_floor(t->last_headroom, t->last_status, t->floor);
    return t->floor;
}
#else
/* Other platforms have no thermal API wired up yet: never raise the floor. */
typedef struct { unsigned floor; int last_status; float last_headroom; } OaThermal;
static inline unsigned oa_thermal_poll(OaThermal *t, uint64_t now_ns)
{
    (void)now_ns;
    return t->floor;
}
#endif
#endif
