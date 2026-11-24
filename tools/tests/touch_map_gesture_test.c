#include <assert.h>
#include "../../native/android/app/veil_map_gesture.h"

int main(void)
{
    MapGesture g;
    /* A hover/press on a travel icon must not send a click before release. */
    map_begin(&g, 100, 100, 10, 1000);
    assert(map_move(&g, 105, 101) == MAP_NONE);
    assert(map_end(&g, 103, 102, 1250) == MAP_TAP);
    map_begin(&g, 100, 100, 10, 1000);
    assert(map_end(&g, 102, 100, 1450) == MAP_WAYPOINT);

    map_begin(&g, 100, 100, 10, 1000);
    assert(map_move(&g, 112, 100) == MAP_START_DRAG);
    assert(map_move(&g, 100, 100) == MAP_NONE);
    /* Moving out and back, including an extended dwell, stays a drag. */
    assert(map_end(&g, 100, 100, 2000) == MAP_END_DRAG);

    map_begin(&g, 100, 100, 10, 1000);
    assert(!map_second_finger(&g, 100));
    assert(map_pinch_steps(&g, 105) == 0);
    assert(map_pinch_steps(&g, 150) == 4);
    assert(map_pinch_steps(&g, 150) == 0);
    assert(map_pinch_steps(&g, 98) < 0);
    assert(map_end(&g, 102, 100, 1800) == MAP_NONE);

    map_begin(&g, 100, 100, 10, 1000);
    assert(map_move(&g, 115, 100) == MAP_START_DRAG);
    assert(map_second_finger(&g, 0));
    assert(map_pinch_steps(&g, 0) == 0);
    assert(map_pinch_steps(&g, 200) == 0);
    g.state = MAP_DRAIN;  /* extra/lifted pointer: consume until all fingers lift */
    assert(map_pinch_steps(&g, 400) == 0);
    assert(map_end(&g, 120, 105, 1800) == MAP_NONE);
    return 0;
}
