#include <android/log.h>
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
static _Atomic bool controller_active;
static _Atomic bool enabled;
static _Atomic bool sprint_on_stick;
static _Atomic unsigned menu_request;
static _Atomic bool mash_active;
static _Atomic unsigned mash_taps;
static _Atomic unsigned tutorial_mode;
static _Atomic bool tutorial_tap;
static _Atomic bool name_dialog;
static _Atomic bool item_radial;
static _Atomic bool map_open;
static _Atomic unsigned touch_ability_slot;
static _Atomic bool touch_cast;
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

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
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
        return 1;
    }
    if (!strcasecmp(key, "VeilTabletHud")) {
        static _Thread_local char last;
        if (last != value[0]) {
            last = value[0];
            __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Gameplay HUD %c", last);
        }
        atomic_store(&hud_update, value[0] == '1' ? now_ms() : 0);
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
    if (!strcasecmp(key, "VeilItemRadial")) {
        atomic_store(&item_radial, value[0] == '1');
        return 1;
    }
    if (!strcasecmp(key, "VeilMap")) {
        atomic_store(&map_open, value[0] == '1');
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
    } else if (!strcasecmp(key, "VeilTabletInput")) {
        result = atomic_load(&controller_active) ? 2 :
                 atomic_load(&enabled) ? 1 : 0;

    } else if (!strcasecmp(key, "VeilCameraBounding")) {
        result = read_camera_setting();
    } else if (!strcasecmp(key, "VeilCameraBoundingLive")) {
        result = atomic_load(&camera_bounding);
    } else if (!strcasecmp(key, "VeilTabletSlot")) {
        result = atomic_exchange(&touch_ability_slot, 0);
    } else {
        return 0;
    }
    value[0] = result == 10 ? '1' : ('0' + result);
    value[1] = result == 10 ? '0' : 0;
    if (result == 10) value[2] = 0;
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
    if (atomic_load(&name_dialog) || atomic_load(&item_radial))
        return 0;
    if (atomic_exchange(&ui_request, 0))
        return 2;
    if (atomic_load(&controller_active))
        return 0;
    if (atomic_load(&enabled)) {
        unsigned tutorial = atomic_load(&tutorial_mode);
        if (tutorial) return tutorial == 2 ? 6 : 5;
    }
    if (atomic_load(&mash_active) && atomic_load(&enabled))
        return 4;
    if (now - atomic_load(&hud_update) >= 300)
        return 0;
    return atomic_load(&enabled) ? 1 : 3;
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
    if (buttons & 0x40) {
        buttons &= ~0x40;
        buttons |= atomic_load(&sprint_on_stick) ? 0x40 : 0x1000;
    }
    pthread_mutex_lock(&pad_mutex);
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
    bool casting = on && cast;
    if (atomic_exchange(&touch_cast, casting) != casting) veil_tablet_wake_input();
    return 0;
}

JNI_API(menu)(JNIEnv *env, jclass type, jint destination)
{
    (void)env;
    (void)type;
    if (destination >= 1 && destination <= 6)
        atomic_store(&menu_request, destination);
    else if (destination >= 11 && destination <= 20) {
        __android_log_print(ANDROID_LOG_INFO, "VeilTablet", "Touch ability slot %d", destination - 10);
        atomic_store(&touch_ability_slot, destination - 10);
    }
    return 0;
}

extern void rt_android_touch_cast(int down);

HIDDEN void veil_tablet_poll_actions(void)
{
    static uint64_t release_at;
    static bool casting;
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
    bool cast = atomic_load(&touch_cast) && !atomic_load(&controller_active) &&
                !atomic_load(&item_radial) && !atomic_load(&map_open) &&
                now - atomic_load(&hud_update) < 150;
    if (cast != casting) {
        rt_android_touch_cast(cast);
        casting = cast;
    }
}

HIDDEN int veil_tablet_map_open(void)
{
    return atomic_load(&map_open);
}

HIDDEN int veil_tablet_gameplay_active(void)
{
    return now_ms() - atomic_load(&hud_update) < 300 &&
           !atomic_load(&item_radial) && !atomic_load(&map_open) &&
           !atomic_load(&options_visible) && !atomic_load(&tutorial_mode);
}

HIDDEN void veil_tablet_touch_mode(void)
{
    atomic_store(&controller_active, false);
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
            atomic_store(&controller_active, true);
            return status;
        }
        if (!atomic_load(&enabled) || atomic_load(&controller_active))
            return status;
    }
    // Present a neutral virtual pad even before the touch overlay starts.
    pthread_mutex_lock(&pad_mutex);
    memcpy(state, &packet, sizeof(packet));
    memcpy(state + 4, &pad, sizeof(pad));
    if (finisher_active) state[5] |= 0x10;
    pthread_mutex_unlock(&pad_mutex);
    return 0;
}
