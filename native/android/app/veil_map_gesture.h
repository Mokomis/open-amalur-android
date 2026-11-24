#ifndef OA_VEIL_MAP_GESTURE_H
#define OA_VEIL_MAP_GESTURE_H

#include <math.h>
#include <stdint.h>

enum MapFingerState { MAP_IDLE, MAP_HOLD, MAP_DRAG, MAP_PINCH, MAP_DRAIN };
enum MapFingerResult { MAP_NONE, MAP_TAP, MAP_WAYPOINT, MAP_START_DRAG, MAP_END_DRAG };
enum { MAP_WAYPOINT_COMMAND = 17, MAP_DRAG_COMMAND = 18, MAP_MAX_ZOOM_STEPS = 8 };

typedef struct {
    enum MapFingerState state;
    uint64_t down_ms;
    int x, y;
    float slop, span;
} MapGesture;

static inline void map_begin(MapGesture *g, int x, int y, float slop, uint64_t now)
{
    *g = (MapGesture){.state = MAP_HOLD, .down_ms = now, .x = x, .y = y, .slop = slop};
}

static inline int map_moved(const MapGesture *g, int x, int y)
{
    float dx = (float)x - g->x, dy = (float)y - g->y;
    return dx * dx + dy * dy > g->slop * g->slop;
}

static inline enum MapFingerResult map_move(MapGesture *g, int x, int y)
{
    if (g->state != MAP_HOLD || !map_moved(g, x, y)) return MAP_NONE;
    g->state = MAP_DRAG;
    return MAP_START_DRAG;
}

static inline int map_second_finger(MapGesture *g, float span)
{
    int end_drag = g->state == MAP_DRAG;
    g->state = MAP_PINCH;
    g->span = span;
    return end_drag;
}

/* Preserve fractional travel between Android samples. The threshold applies
 * to the finger separation and is independent of screen resolution. */
static inline int map_pinch_steps(MapGesture *g, float span)
{
    if (g->state != MAP_PINCH || span < 1) return 0;
    if (g->span < 1) g->span = span;
    int steps = 0;
    const float ratio = 1.10f;
    while (steps < MAP_MAX_ZOOM_STEPS && span >= g->span * ratio) {
        g->span *= ratio;
        ++steps;
    }
    while (steps > -MAP_MAX_ZOOM_STEPS && span * ratio <= g->span) {
        g->span /= ratio;
        --steps;
    }
    return steps;
}

static inline enum MapFingerResult map_end(MapGesture *g, int x, int y, uint64_t now)
{
    enum MapFingerState state = g->state;
    g->state = MAP_IDLE;
    if (state == MAP_DRAG) return MAP_END_DRAG;
    if (state != MAP_HOLD || map_moved(g, x, y)) return MAP_NONE;
    return now - g->down_ms >= 450 ? MAP_WAYPOINT : MAP_TAP;
}

#endif
