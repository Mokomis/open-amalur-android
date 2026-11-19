#ifndef OA_DYNAMIC_RESOLUTION_H
#define OA_DYNAMIC_RESOLUTION_H
#include <stdint.h>

typedef struct {
    uint64_t window_start;
    uint64_t last_frame;
    unsigned frames;
    unsigned fast_windows;
} OaResolutionTiming;

enum { OA_RESOLUTION_FULL, OA_RESOLUTION_MEDIUM, OA_RESOLUTION_LOW };

static inline void oa_resolution_reset(OaResolutionTiming *timing)
{
    *timing = (OaResolutionTiming){0};
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
        return level < OA_RESOLUTION_LOW ? level + 1 : level;
    }
    if (average >= fast_frame_ns || level == OA_RESOLUTION_FULL) {
        timing->fast_windows = 0;
        return level;
    }
    if (++timing->fast_windows < 4) return level;
    timing->fast_windows = 0;
    return level - 1;
}
#endif
