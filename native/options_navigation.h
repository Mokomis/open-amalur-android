#ifndef OA_OPTIONS_NAVIGATION_H
#define OA_OPTIONS_NAVIGATION_H

#include <stdint.h>

/* XInput's directional and face-button bits are also used by rt_android_pad. */
enum {
    OA_NAV_UP = 0x0001, OA_NAV_DOWN = 0x0002,
    OA_NAV_LEFT = 0x0004, OA_NAV_RIGHT = 0x0008,
    OA_NAV_LB = 0x0100, OA_NAV_RB = 0x0200,
    OA_NAV_A = 0x1000,
};

typedef struct {
    uint16_t previous, blocked;
    unsigned visible, settings;
} OaOptionsNavigation;

static inline void oa_options_navigation(OaOptionsNavigation *nav, int visible,
                                         uint16_t *buttons, uint8_t *left_trigger,
                                         uint8_t *right_trigger, int16_t *left_x,
                                         int16_t *left_y)
{
    if (!visible) {
        *nav = (OaOptionsNavigation){0};
        return;
    }
    if (!nav->visible) {
        nav->visible = 1;
        nav->settings = 0; /* Options opens on its category list. */
    }

    uint16_t directions = *buttons & (OA_NAV_UP | OA_NAV_DOWN | OA_NAV_LEFT |
                                      OA_NAV_RIGHT | OA_NAV_A);
    if (*left_y > 18000) directions |= OA_NAV_UP;
    if (*left_y < -18000) directions |= OA_NAV_DOWN;
    if (*left_x < -18000) directions |= OA_NAV_LEFT;
    if (*left_x > 18000) directions |= OA_NAV_RIGHT;
    uint16_t pressed = directions & ~nav->previous;
    nav->blocked &= directions;

    if (!nav->settings && (pressed & (OA_NAV_RIGHT | OA_NAV_A))) {
        nav->settings = 1;
        nav->blocked |= directions;
    } else if (nav->settings && (pressed & OA_NAV_LEFT)) {
        nav->settings = 0;
        nav->blocked |= directions;
    }
    nav->previous = directions;

    if (*buttons & OA_NAV_LB) *left_trigger = UINT8_MAX;
    if (*buttons & OA_NAV_RB) *right_trigger = UINT8_MAX;
    *buttons &= ~(OA_NAV_LB | OA_NAV_RB);

    if (!nav->settings) {
        if ((directions & OA_NAV_UP) && !(nav->blocked & OA_NAV_UP))
            *left_trigger = UINT8_MAX;
        if ((directions & OA_NAV_DOWN) && !(nav->blocked & OA_NAV_DOWN))
            *right_trigger = UINT8_MAX;
        *buttons &= ~(OA_NAV_UP | OA_NAV_DOWN | OA_NAV_LEFT |
                      OA_NAV_RIGHT | OA_NAV_A);
        *left_x = *left_y = 0;
    } else {
        *buttons &= ~nav->blocked;
        if (nav->blocked & (OA_NAV_LEFT | OA_NAV_RIGHT)) *left_x = 0;
    }
}

#endif
