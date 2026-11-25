#ifndef OA_DYNAMIC_RESOLUTION_H
#define OA_DYNAMIC_RESOLUTION_H
#include <stdint.h>

typedef struct {
    uint64_t window_start;
    uint64_t last_frame;
    unsigned frames;
    unsigned fast_windows;
    /* Recovery on a display that caps the frame rate at 60 Hz. There, frames
     * arrive every 16.7 ms however much headroom the device has, so they can
     * never look "fast". Steady windows at the target instead earn a probe
     * one step up; a probe that turns slow at once doubles the next wait.
     * These fields survive oa_resolution_reset(): a level change resets the
     * sampling window, not what the last probe taught us. */
    unsigned steady_windows;
    unsigned probe_windows;
    uint64_t probe_time;
} OaResolutionTiming;

enum { OA_RESOLUTION_FULL, OA_RESOLUTION_MEDIUM, OA_RESOLUTION_LOW };

enum {
    OA_RESOLUTION_PROBE_WINDOWS = 10,     /* first probe after 10 steady seconds */
    OA_RESOLUTION_PROBE_MAX_WINDOWS = 120 /* back off to at most two minutes */
};

static inline void oa_resolution_reset(OaResolutionTiming *timing)
{
    timing->window_start = 0;
    timing->last_frame = 0;
    timing->frames = 0;
    timing->fast_windows = 0;
    timing->steady_windows = 0;
}

/* The ceiling is the configured rendering size. The floor is two thirds of that size. */
static inline unsigned oa_resolution_dimension(unsigned ceiling, unsigned level)
{
    return (unsigned)((uint64_t)ceiling * (6 - level) / 6);
}

static inline unsigned oa_resolution_next(OaResolutionTiming *timing, uint64_t now, unsigned level)
{
    static const uint64_t sample_ns = 1000000000;
    static const uint64_t interrupted_ns = 250000000;
    static const uint64_t slow_frame_ns = 17800000;
    static const uint64_t fast_frame_ns = 14500000;
    /* A probe that turns slow within this time did not have the headroom. */
    static const uint64_t failed_probe_ns = 4000000000ull;
    if (!timing->probe_windows) timing->probe_windows = OA_RESOLUTION_PROBE_WINDOWS;
    if (!timing->last_frame || now - timing->last_frame > interrupted_ns) {
        oa_resolution_reset(timing);
        timing->window_start = now;
        timing->last_frame = now;
        return level;
    }
    timing->last_frame = now;
    ++timing->frames;
    uint64_t elapsed = now - timing->window_start;
    if (elapsed < sample_ns) return level;

    uint64_t average = elapsed / timing->frames;
    timing->window_start = now;
    timing->frames = 0;
    if (average > slow_frame_ns) {
        timing->fast_windows = 0;
        timing->steady_windows = 0;
        if (timing->probe_time && now - timing->probe_time < failed_probe_ns) {
            timing->probe_windows *= 2;
            if (timing->probe_windows > OA_RESOLUTION_PROBE_MAX_WINDOWS)
                timing->probe_windows = OA_RESOLUTION_PROBE_MAX_WINDOWS;
        }
        timing->probe_time = 0;
        return level < OA_RESOLUTION_LOW ? level + 1 : level;
    }
    if (timing->probe_time && now - timing->probe_time >= failed_probe_ns) {
        /* The last probe held, so the next one needn't wait as long. */
        timing->probe_time = 0;
        timing->probe_windows = OA_RESOLUTION_PROBE_WINDOWS;
    }
    if (level == OA_RESOLUTION_FULL) {
        timing->fast_windows = 0;
        timing->steady_windows = 0;
        return level;
    }
    if (average >= fast_frame_ns) {
        timing->fast_windows = 0;
        if (++timing->steady_windows < timing->probe_windows) return level;
        timing->steady_windows = 0;
        timing->probe_time = now;
        return level - 1;
    }
    timing->steady_windows = 0;
    if (++timing->fast_windows < 4) return level;
    timing->fast_windows = 0;
    return level - 1;
}
#endif
