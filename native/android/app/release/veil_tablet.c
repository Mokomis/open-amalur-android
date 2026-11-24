#include <android/log.h>
#include "touch_spell_input.h"
#include "touch_run_pad.h"
#include "../veil_map_gesture.h"
#include <jni.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define HIDDEN __attribute__((visibility("hidden")))
#define JNI_API(name) JNIEXPORT jint JNICALL Java_org_openamalur_originalengine_VeilActivity_##name

static _Atomic uint64_t hud_update;
static _Atomic uint64_t title_update;
static _Atomic int title_cover_presented;
static _Atomic int options_visible;
static _Atomic bool script_navigation;
static _Atomic bool controller_active;
static _Atomic bool lock_splash, lock_game, lock_mouse;
static _Atomic bool enabled;
static _Atomic bool sprint_on_stick;
static bool touch_run, touch_interact;
static VeilRunSample run_sample;
HIDDEN atomic_bool veil_run_filter_active;
static _Atomic unsigned extra_context;
static _Atomic unsigned menu_request;
enum { TARGET_TOGGLE = 7, TARGET_TICK, TARGET_CANCEL };
static _Atomic bool target_toggle, target_active, target_cancel;
static _Atomic int target_pan;
static _Atomic uint64_t target_sample;
static uint64_t next_target_tick;  // Owned by the gameplay HUD.
static unsigned target_samples, target_centered;
static _Atomic bool mash_active;
static _Atomic unsigned mash_taps;
static _Atomic unsigned tutorial_mode;
static _Atomic bool tutorial_tap;
static _Atomic bool name_dialog;
static _Atomic bool save_import_request;
extern int rt_android_replace_name(const uint16_t *, int);
extern void rt_android_key(uint32_t, uint32_t, int, int);
static _Atomic uint64_t death_menu_update;
static _Atomic bool death_menu_tap;
static _Atomic bool item_radial;
static _Atomic bool map_open;
static _Atomic int map_zoom_steps;
static _Atomic bool map_waypoint, map_drag;
static _Atomic bool quantity_slider;
static _Atomic uint64_t aggression_offer, aggression_tap;
static _Atomic int hotbar_hud_mode;
static _Atomic uint64_t finisher_packet;
static _Atomic uint64_t finisher_update;
static _Atomic bool finisher_request;
static _Atomic bool camera_bounding;
static pthread_mutex_t camera_setting_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t camera_setting_once = PTHREAD_ONCE_INIT;
static char camera_setting_path[PATH_MAX];
static bool saved_camera_bounding;
static _Atomic unsigned ui_request;
static _Atomic unsigned viewport_width, viewport_height;
static unsigned packet;
static bool finisher_active;
static struct {
    uint16_t buttons;
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
} pad;
static pthread_mutex_t pad_mutex = PTHREAD_MUTEX_INITIALIZER;

HIDDEN void veil_tablet_wake_input(void);
HIDDEN int veil_tablet_gameplay_active(void);

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void cancel_touch_target(void)
{
    atomic_store(&target_toggle, false);
    if (atomic_exchange(&target_active, false))
        atomic_store(&target_cancel, true);
    atomic_store(&target_pan, 0);
}

static unsigned poll_touch_target(void)
{
    if (atomic_load(&target_active) && !veil_tablet_gameplay_active()) cancel_touch_target();
    if (atomic_exchange(&target_cancel, false)) return TARGET_CANCEL;
    if (atomic_exchange(&target_toggle, false)) return TARGET_TOGGLE;
    if (!atomic_load(&target_active)) return 0;
    uint64_t now = now_ms();
    if (now < next_target_tick) return 0;
    next_target_tick = now + 50;
    return TARGET_TICK;
}

static void load_camera_setting(void)
{
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (!root)
        return;
    int length = snprintf(camera_setting_path, sizeof camera_setting_path,
                          "%s/veil-camera-bounding", root);
    if (length < 0 || (size_t)length >= sizeof camera_setting_path) {
        camera_setting_path[0] = 0;
        return;
    }
    FILE *file = fopen(camera_setting_path, "r");
    if (file) {
        saved_camera_bounding = fgetc(file) == '1';
        fclose(file);
    }
}

static void save_camera_setting(bool value)
{
    pthread_once(&camera_setting_once, load_camera_setting);
    pthread_mutex_lock(&camera_setting_mutex);
    saved_camera_bounding = value;
    char temporary[PATH_MAX];
    int length = snprintf(temporary, sizeof temporary, "%s.tmp", camera_setting_path);
    if (camera_setting_path[0] && length > 0 && (size_t)length < sizeof temporary) {
        FILE *file = fopen(temporary, "w");
        if (file) {
            bool written = fputs(value ? "1\n" : "0\n", file) >= 0;
            if (fclose(file) == 0 && written && rename(temporary, camera_setting_path) == 0) {
                pthread_mutex_unlock(&camera_setting_mutex);
                return;
            }
            unlink(temporary);
        }
    }
    __android_log_print(ANDROID_LOG_WARN, "VeilCamera", "Could not save Camera Bounding");
    pthread_mutex_unlock(&camera_setting_mutex);
}

static bool read_camera_setting(void)
{
    pthread_once(&camera_setting_once, load_camera_setting);
    pthread_mutex_lock(&camera_setting_mutex);
    bool value = saved_camera_bounding;
    pthread_mutex_unlock(&camera_setting_mutex);
    return value;
}

HIDDEN void veil_tablet_viewport(unsigned width, unsigned height)
{
    atomic_store(&viewport_width, width);
    atomic_store(&viewport_height, height);
}

HIDDEN int veil_tablet_command(const char *section, const char *key, const char *value)
{
    if (!key || !value || strcasecmp(section, "DEFAULT"))
        return -1;
    if (!strcasecmp(key, "VeilLockSplash") || !strcasecmp(key, "VeilLockGame")) {
        atomic_store(!strcasecmp(key, "VeilLockGame") ? &lock_game : &lock_splash, value[0] == '1');
        return 1;
    }
    if (!strcasecmp(key, "VeilOptionsScriptInput")) {
        atomic_store(&script_navigation, value[0] == '1');
        return 1;
    }
    if (!strcasecmp(key, "VeilOptionsVisible")) {
        atomic_store(&options_visible, value[0] == '1');
        return 1;
    }
    if (!strcasecmp(key, "VeilTitle")) {
        static _Thread_local char previous;
        if (previous != value[0]) {
            previous = value[0];
            __android_log_print(ANDROID_LOG_INFO, "VeilUpdates", "Title screen %c", previous);
        }
        atomic_store(&title_update, value[0] == '1' ? now_ms() : 0);
        if (value[0] == '1') atomic_store(&quantity_slider, false);
        return 1;
    }
    if (!strcasecmp(key, "VeilTabletHud")) {
        static _Thread_local char last;
        if (last != value[0]) {
            last = value[0];
            __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Gameplay HUD %c", last);
        }
        if (value[0] != '1') {
            cancel_touch_target();
            atomic_store(&aggression_offer, 0);
            atomic_store(&aggression_tap, 0);
        }
        atomic_store(&hud_update, value[0] == '1' ? now_ms() : 0);
        if (value[0] != '1') veil_spell_cancel();
        return 1;
    }
    if (!strcasecmp(key, "VeilTouchTarget")) {
        bool active = *value && strcmp(value, "0");
        atomic_store(&target_active, active);
        atomic_store(&target_pan, 0);
        next_target_tick = 0;
        __android_log_print(ANDROID_LOG_INFO, "VeilTarget", "%s %s (%u samples, %u centered)",
                            active ? "Acquired" : "Released", value, active ? 0 : target_samples, active ? 0 : target_centered);
        target_samples = target_centered = 0;
        return 1;
    }
    if (!strcasecmp(key, "VeilTouchTargetX")) {
        if (atomic_load(&target_active)) {
            int offset = atoi(value);
            if (offset > 100) offset = 100;
            if (offset < -100) offset = -100;
            int magnitude = abs(offset);
            // A small central band avoids hunting when the enemy idles.
            int pan = magnitude <= 7 ? 0 : 8000 + 230 * (magnitude - 7);
            atomic_store(&target_pan, offset < 0 ? -pan : pan);
            atomic_store(&target_sample, now_ms());
            ++target_samples;
            if (!pan) ++target_centered;
        }
        return 1;
    }
    if (!strcasecmp(key, "VeilCameraBounding")) {
        save_camera_setting(value[0] == '1');
        return 1;
    }
    if (!strcasecmp(key, "VeilCameraBoundingLive")) {
        atomic_store(&camera_bounding, value[0] == '1');
        __android_log_print(ANDROID_LOG_INFO, "VeilCamera", "Camera Bounding %s", value);
        return 1;
    }
    if (!strcasecmp(key, "VeilFinisher")) {
        unsigned position = (unsigned)strtoul(value, NULL, 10);
        if (position > 0) {
            atomic_store(&finisher_packet, position);
            atomic_store(&finisher_update, now_ms());
        }
        return 1;
    }
    if (!strcasecmp(key, "VeilHotbarHudMode")) {
        atomic_store(&hotbar_hud_mode, value[0] == '2' ? 2 : value[0] == '1' ? 1 : 0);
        return 1;
    }
    if (!strcasecmp(key, "VeilCastHotbarSlot")) {
        veil_spell_accept_slot((unsigned)strtoul(value, NULL, 10));
        return 1;
    }
    if (!strcasecmp(key, "VeilTabletSlotSelected")) {
        __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Selected native ability slot %s", value);
        return 1;
    }
    if (!strcasecmp(key, "VeilTabletTutorial")) {
        unsigned mode = value[0] == '2' ? 2 : value[0] == '1';
        atomic_store(&tutorial_tap, false);
        atomic_store(&tutorial_mode, mode);
        __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Tutorial A %s", value);
        return 1;
    }
    if (!strcasecmp(key, "VeilNameDialog")) {
        bool open = value[0] == '1';
        if (atomic_exchange(&name_dialog, open) != open)
            __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Name dialog %d", open);
        return 1;
    }
    if (!strcasecmp(key, "VeilImportSaves")) {
        atomic_store(&save_import_request, value[0] == '1');
        return 1;
    }
    if (!strcasecmp(key, "VeilDeathMenu")) {
        bool active = value[0] == '1';
        atomic_store(&death_menu_update, active ? now_ms() : 0);
        if (!active) atomic_store(&death_menu_tap, false);
        return 1;
    }
    if (!strcasecmp(key, "VeilItemRadial")) {
        atomic_store(&item_radial, value[0] == '1');
        return 1;
    }
    if (!strcasecmp(key, "VeilQuantitySlider")) {
        bool open = value[0] == '1';
        atomic_store(&quantity_slider, open);
        if (open) {
            veil_spell_cancel();
            cancel_touch_target();
        }
        return 1;
    }
    if (!strcasecmp(key, "VeilMap")) {
        atomic_store(&map_open, value[0] == '1');
        atomic_store(&map_zoom_steps, 0);
        atomic_store(&map_waypoint, false);
        atomic_store(&map_drag, false);
        return 1;
    }
    if (!strcasecmp(key, "VeilTabletMash")) {
        __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Mash prompt %s", value);
        atomic_store(&mash_taps, 0);
        atomic_store(&mash_active, value[0] == '1');
        return 1;
    }
    if (!strcasecmp(key, "VeilTabletSprint")) {
        atomic_store(&sprint_on_stick, value[0] == '1');
        return 1;
    }
    if (!strcasecmp(key, "VeilTabletEditor")) {
        atomic_store(&ui_request, 1);
        return 1;
    }
    return -1;
}

HIDDEN int veil_tablet_read(const char *section, const char *key, char *value)
{
    if (strcasecmp(section, "DEFAULT"))
        return 0;
    unsigned result;
    if (!strcasecmp(key, "VeilTabletMenu")) {
        result = atomic_exchange(&menu_request, 0);
        if (result >= 1 && result <= 6) cancel_touch_target();
        else if (!result) result = poll_touch_target();
    } else if (!strcasecmp(key, "VeilTabletMashTap")) {
        unsigned pending = atomic_load(&mash_taps);
        result = 0;
        while (pending && !atomic_compare_exchange_weak(&mash_taps, &pending, pending - 1)) {}
        if (pending) {
            result = 1;
            __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Touch mash delivered");
        }
    } else if (!strcasecmp(key, "VeilTabletTutorialTap")) {
        result = atomic_exchange(&tutorial_tap, false);
    } else if (!strcasecmp(key, "VeilMapGesture")) {
        if (atomic_exchange(&map_waypoint, false)) result = MAP_WAYPOINT_COMMAND;
        else {
            int steps = atomic_exchange(&map_zoom_steps, 0);
            result = steps < 0 ? MAP_MAX_ZOOM_STEPS - steps : steps;
        }
    } else if (!strcasecmp(key, "VeilMapDrag")) {
        result = atomic_exchange(&map_drag, false);
    } else if (!strcasecmp(key, "VeilDeathMenuTap")) {
        result = atomic_exchange(&death_menu_tap, false);
    } else if (!strcasecmp(key, "VeilLockTouch")) {
        result = atomic_load(&lock_game) && atomic_load(&enabled) &&
                 !atomic_load(&controller_active) && !atomic_load(&lock_mouse);
    } else if (!strcasecmp(key, "VeilTabletInput")) {
        result = atomic_load(&controller_active) ? 2 :
                 atomic_load(&enabled) ? 1 : 0;

    } else if (!strcasecmp(key, "VeilCameraBounding")) {
        result = read_camera_setting();
    } else if (!strcasecmp(key, "VeilCameraBoundingLive")) {
        result = atomic_load(&camera_bounding);
    } else if (!strcasecmp(key, "VeilAggressionTouch")) {
        uint64_t now = now_ms();
        uint64_t previous = atomic_exchange(&aggression_offer, now);
        uint64_t tap = atomic_exchange(&aggression_tap, 0);
        result = previous && now - previous < 300 && tap && now - tap < 300 &&
                 atomic_load(&enabled) && !atomic_load(&controller_active) && veil_tablet_gameplay_active();
    } else if (!strcasecmp(key, "VeilTabletSlot")) {
        result = veil_tablet_gameplay_active() && !atomic_load(&controller_active)
            ? veil_spell_take_request() : 0;
    } else {
        return 0;
    }
    if (result >= 10) {
        value[0] = '1';
        value[1] = (char)('0' + result - 10);
        value[2] = 0;
    } else {
        value[0] = (char)('0' + result);
        value[1] = 0;
    }
    return 1;
}

JNI_API(mash)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    if (atomic_load(&tutorial_mode) == 1) {
        atomic_store(&tutorial_tap, true);
    } else if (atomic_load(&mash_active)) {
        atomic_fetch_add(&mash_taps, 1);
    }
    return 0;
}

JNI_API(mode)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    uint64_t now = now_ms();
    // The original name window handles its own OK, Cancel, and edit-field taps.
    if (atomic_load(&name_dialog) || atomic_load(&item_radial) ||
        atomic_load(&map_open) || atomic_load(&quantity_slider))
        return 0;
    if (atomic_exchange(&ui_request, 0))
        return 2;
    if (atomic_load(&controller_active))
        return 0;
    if (atomic_load(&enabled)) {
        unsigned tutorial = atomic_load(&tutorial_mode);
        if (tutorial) return tutorial == 2 ? 6 : 5;
    }
    if (atomic_load(&lock_game) || atomic_load(&lock_splash))
        return atomic_load(&enabled) && !atomic_load(&lock_mouse) ? (atomic_load(&lock_game) ? 7 : 8) : 0;
    if (atomic_load(&mash_active) && atomic_load(&enabled))
        return 4;
    if (now - atomic_load(&hud_update) >= 300)
        return 0;
    if (!atomic_load(&enabled)) return 3;
    return atomic_load(&target_active) ? 9 : 1;
}

JNI_API(nameDialogVisible)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    return atomic_load(&name_dialog);
}

JNI_API(takeSaveImportRequest)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    return atomic_exchange(&save_import_request, false);
}

JNI_API(submitName)(JNIEnv *env, jclass type, jstring name)
{
    (void)type;
    if (!name || !atomic_load(&name_dialog)) return 0;
    jsize length = (*env)->GetStringLength(env, name);
    if (length > 24) return 0;
    if (length) {
        const jchar *chars = (*env)->GetStringChars(env, name, NULL);
        if (!chars) return 0;
        int accepted = rt_android_replace_name((const uint16_t *)chars, length);
        (*env)->ReleaseStringChars(env, name, chars);
        if (!accepted) return 0;
    }
    return 1;
}

JNI_API(confirmNativeName)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    if (!atomic_load(&name_dialog)) return 0;
    rt_android_key(0x0d, 0x1c, 0, 1);
    rt_android_key(0x0d, 0x1c, 0, 0);
    return 1;
}

JNI_API(finisherPosition)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    return now_ms() - atomic_load(&finisher_update) < 240 ? (jint)atomic_load(&finisher_packet) : 0;
}

JNI_API(finisher)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    if (now_ms() - atomic_load(&finisher_update) >= 240)
        return 0;
    atomic_store(&finisher_request, true);
    __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Floating A pressed");
    return 1;
}

JNI_API(hotbarHudMode)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    return atomic_load(&hotbar_hud_mode);
}

JNI_API(aggressionOffered)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    uint64_t offered = atomic_load(&aggression_offer);
    return offered && now_ms() - offered < 300;
}

JNI_API(aspect)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    unsigned height = atomic_load(&viewport_height);
    return height ? atomic_load(&viewport_width) * 10000 / height : 17778;
}

HIDDEN int veil_title_is_visible(void)
{
    uint64_t updated = atomic_load(&title_update);
    return updated && now_ms() - updated < 350;
}

HIDDEN int veil_options_uses_legacy_navigation(void)
{
    return atomic_load(&options_visible) && !atomic_load(&script_navigation);
}

HIDDEN int veil_options_is_visible(void)
{
    return atomic_load(&options_visible);
}

HIDDEN void veil_title_presented(int full_screen)
{
    atomic_store(&title_cover_presented, full_screen);
}

HIDDEN int veil_title_cover_is_presented(void)
{
    return atomic_load(&title_cover_presented);
}

JNI_API(titleVisible)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    return veil_title_is_visible();
}

JNI_API(input)(JNIEnv *env, jclass type, jint on, jint buttons, jint lt, jint rt, jint cast,
               jint lx, jint ly, jint rx, jint ry)
{
    (void)env;
    (void)type;
    pthread_mutex_lock(&pad_mutex);
    touch_run = on && (buttons & VEIL_TOUCH_RUN);
    touch_interact = on && (buttons & VEIL_PAD_A);
    buttons &= ~VEIL_TOUCH_RUN;
    if (touch_run) buttons |= atomic_load(&sprint_on_stick) ? VEIL_PAD_LS : VEIL_PAD_A;
    if (!on) {
        run_sample = (VeilRunSample){0};
        atomic_store(&veil_run_filter_active, false);
        atomic_store(&extra_context, VEIL_NO_CONTEXT);
    }
    if (!on) cancel_touch_target();
    else if ((buttons & 0x80) && !(pad.buttons & 0x80) && veil_tablet_gameplay_active())
        atomic_store(&target_toggle, true);
    if (atomic_load(&enabled) != (on != 0) || pad.buttons != buttons ||
        pad.lt != lt || pad.rt != rt || pad.lx != lx || pad.ly != ly ||
        pad.rx != rx || pad.ry != ry) {
        pad.buttons = buttons;
        pad.lt = lt;
        pad.rt = rt;
        pad.lx = lx;
        pad.ly = ly;
        pad.rx = rx;
        pad.ry = ry;
        ++packet;
    }
    atomic_store(&enabled, on != 0);
    pthread_mutex_unlock(&pad_mutex);
    veil_spell_set_held(on && cast);
    return 0;
}

JNI_API(menu)(JNIEnv *env, jclass type, jint destination)
{
    (void)env;
    (void)type;
    if ((destination >= 1 && destination <= 6) || destination == 10)
        atomic_store(&menu_request, destination);
    else if (destination == 21) {
        uint64_t now = now_ms(), offered = atomic_load(&aggression_offer);
        if (offered && now - offered < 300 && veil_tablet_gameplay_active() &&
            atomic_load(&enabled) && !atomic_load(&controller_active))
            atomic_store(&aggression_tap, now);
    }
    else if (destination >= 11 && destination <= 20) {
        __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Touch ability slot %d", destination - 10);
        veil_spell_tap_slot(destination - 10);
    }
    return 0;
}

HIDDEN int veil_touch_spell_playable(void)
{
    return atomic_load(&enabled) && !atomic_load(&controller_active) &&
           veil_tablet_gameplay_active();
}

JNI_API(cancelHotbar)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    veil_spell_cancel();
    return 0;
}

HIDDEN int veil_tablet_poll_actions(void)
{
    static uint64_t release_at;
    uint64_t now = now_ms();
    pthread_mutex_lock(&pad_mutex);
    if (release_at && now >= release_at) {
        release_at = 0;
    }
    if (!release_at && atomic_exchange(&finisher_request, false)) {
        release_at = now + 90;
    }
    if (finisher_active != (release_at != 0)) {
        finisher_active = release_at != 0;
        ++packet;
    }
    pthread_mutex_unlock(&pad_mutex);
    return 1000;
}

HIDDEN void veil_tablet_map_gesture(int command)
{
    if (!atomic_load(&map_open)) return;
    if (command == MAP_WAYPOINT_COMMAND) atomic_store(&map_waypoint, true);
    else if (command == MAP_DRAG_COMMAND || command == 0) atomic_store(&map_drag, command != 0);
    else {
        int old = atomic_load(&map_zoom_steps), next;
        do {
            next = old + command;
            if (next > MAP_MAX_ZOOM_STEPS) next = MAP_MAX_ZOOM_STEPS;
            if (next < -MAP_MAX_ZOOM_STEPS) next = -MAP_MAX_ZOOM_STEPS;
        } while (!atomic_compare_exchange_weak(&map_zoom_steps, &old, next));
    }
}

HIDDEN int veil_tablet_map_open(void)
{
    return atomic_load(&map_open);
}

HIDDEN int veil_tablet_quantity_slider_open(void)
{
    return atomic_load(&quantity_slider);
}

HIDDEN int veil_tablet_death_menu_active(void)
{
    uint64_t updated = atomic_load(&death_menu_update);
    return updated && now_ms() - updated < 200;
}

HIDDEN void veil_tablet_death_menu_tap(void)
{
    if (veil_tablet_death_menu_active()) atomic_store(&death_menu_tap, true);
}

HIDDEN int veil_tablet_gameplay_active(void)
{
    return now_ms() - atomic_load(&hud_update) < 300 &&
           !atomic_load(&item_radial) && !atomic_load(&map_open) && !atomic_load(&quantity_slider) &&
           !atomic_load(&options_visible) && !atomic_load(&tutorial_mode) &&
           !atomic_load(&lock_splash) && !atomic_load(&lock_game);
}

HIDDEN void veil_tablet_touch_mode(void)
{
    atomic_store(&controller_active, false);
    atomic_store(&lock_mouse, false);
}

HIDDEN void veil_tablet_lock_mouse(void)
{
    atomic_store(&lock_mouse, true);
}

JNI_API(lockMouse)(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    veil_tablet_lock_mouse();
    return 0;
}

HIDDEN unsigned veil_take_touch_context(void)
{
    return atomic_exchange(&extra_context, VEIL_NO_CONTEXT);
}

HIDDEN uint32_t veil_stock_pad_get(uint32_t index, uint8_t state[16]);
HIDDEN uint32_t veil_trace_pad_get(uint32_t index, uint8_t state[16])
{
    uint32_t status = veil_stock_pad_get(index, state);
    if (index != 0)
        return status;
    uint16_t buttons = 0;
    int16_t sticks[4] = {0};
    if (!status) {
        memcpy(&buttons, state + 4, sizeof(buttons));
        memcpy(sticks, state + 8, sizeof(sticks));
        if (buttons || state[6] > 32 || state[7] > 32 ||
            sticks[0] < -4096 || sticks[0] > 4096 || sticks[1] < -4096 || sticks[1] > 4096 ||
            sticks[2] < -4096 || sticks[2] > 4096 || sticks[3] < -4096 || sticks[3] > 4096) {
            if (!atomic_exchange(&controller_active, true)) {
                cancel_touch_target();
                pthread_mutex_lock(&pad_mutex);
                run_sample = (VeilRunSample){0};
                atomic_store(&veil_run_filter_active, false);
                atomic_store(&extra_context, VEIL_NO_CONTEXT);
                pthread_mutex_unlock(&pad_mutex);
            }
            return status;
        }
        if (!atomic_load(&enabled) || atomic_load(&controller_active))
            return status;
    }
    // Present a neutral virtual pad even before the touch overlay starts.
    pthread_mutex_lock(&pad_mutex);
    VeilContextRequest context = veil_sample_run(&run_sample, touch_run, touch_interact,
                                                (pad.buttons & VEIL_PAD_LS) != 0, finisher_active);
    atomic_store(&veil_run_filter_active, run_sample.run_owns_face);
    if (context != VEIL_NO_CONTEXT) atomic_store(&extra_context, context);
    // Let an explicit camera drag take priority, then resume tracking when released.
    static int16_t previous_rx;
    int16_t rx = pad.rx;
    if (!rx && atomic_load(&target_active) && now_ms() - atomic_load(&target_sample) < 180)
        rx = (int16_t)atomic_load(&target_pan);
    if (rx != previous_rx) {
        previous_rx = rx;
        ++packet;
    }
    memcpy(state, &packet, sizeof(packet));
    memcpy(state + 4, &pad, sizeof(pad));
    memcpy(state + 12, &rx, sizeof(rx));
    if (finisher_active) state[5] |= 0x10;
    pthread_mutex_unlock(&pad_mutex);
    return 0;
}
