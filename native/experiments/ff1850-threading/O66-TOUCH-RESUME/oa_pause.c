#include "oa_pause.h"

#include <android/asset_manager.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { ROW_RESUME, ROW_STOCK, ROW_COUNT };
enum { PAD_UP = 1, PAD_DOWN = 2, PAD_START = 16, PAD_A = 4096, PAD_B = 8192 };
enum { NO_SLOT = -1, VK_ESCAPE = 27, SCAN_ESCAPE = 1, RESUME_HOLD_MS = 120 };
static AAssetManager *assets;
static int enabled;
static _Atomic unsigned renderer_ready;
static _Atomic unsigned visible, focus_row, world_ready, opening;
static uint16_t last_buttons[4];
static int last_vertical[4], touch_row = -1;
static int last_slot = NO_SLOT;
static uint64_t release_at;
static int pulse_slot = NO_SLOT;
static int forwarded_pause_key;

extern void rt_android_pad(uint32_t, int, uint16_t, uint8_t, uint8_t,
                           int16_t, int16_t, int16_t, int16_t);
extern void rt_android_key(uint32_t vk, uint32_t scan, int extended, int down);

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

void oa_pause_configure(AAssetManager *manager, const char *root)
{
    char path[2048];
    assets = manager;
    if (!root || snprintf(path, sizeof path, "%s/prototype-pause", root) >= (int)sizeof path) return;
    FILE *file = fopen(path, "r");
    enabled = file && fgetc(file) == '1';
    if (file) fclose(file);
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur", "Prototype pause enabled=%d", enabled);
}

AAsset *oa_pause_asset(void)
{
    return assets ? AAssetManager_open(assets, "oa_prototype_ui.bin", AASSET_MODE_BUFFER) : NULL;
}

unsigned oa_pause_visible(void) { return atomic_load(&visible); }
void oa_pause_renderer_ready(unsigned ready) { atomic_store(&renderer_ready, ready); }
unsigned oa_pause_focus(void) { return atomic_load(&focus_row); }

/* Let Start run the native pause action first. The bridge pause screen
   submits 124 draws, while gameplay resumes submitting world geometry. */
void oa_pause_frame(unsigned draws)
{
    if (!enabled) return;
    static unsigned candidate_frames;
    atomic_store(&world_ready, draws > 500);
    unsigned pending = atomic_load(&opening);
    if (!pending) return;
    if (draws >= 100 && draws <= 170) {
        if (++candidate_frames == 2) {
            candidate_frames = 0;
            atomic_store(&opening, 0);
            atomic_store(&focus_row, ROW_RESUME);
            atomic_store(&visible, 1);
            __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                                "Prototype pause native frame draws=%u", draws);
        }
    } else {
        candidate_frames = 0;
    }
    if (pending == 1) {
        atomic_store(&opening, 0);
        candidate_frames = 0;
    } else if (atomic_load(&opening)) {
        atomic_fetch_sub(&opening, 1);
    }
}

void oa_pause_row_rect(unsigned row, int width, int height, float out[4])
{
    float scale = height / 840.0f;
    (void)width;
    out[0] = 48 * scale;
    out[1] = (122 + 68 * row) * scale;
    out[2] = 330 * scale;
    out[3] = (row == ROW_RESUME ? 64 : 56) * scale;
}

static void choose(unsigned row, int slot)
{
    atomic_store(&visible, 0);
    touch_row = -1;
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur", "Prototype pause action=%s input=%s",
                        row == ROW_RESUME ? "resume" : "original_menu",
                        slot == NO_SLOT ? "keyboard" : "controller");
    if (row != ROW_RESUME) return;
    pulse_slot = slot;
    if (slot == NO_SLOT)
        rt_android_key(VK_ESCAPE, SCAN_ESCAPE, 0, 1);
    else
        rt_android_pad((unsigned)slot, 1, PAD_B, 0, 0, 0, 0, 0, 0);
    release_at = monotonic_ms() + RESUME_HOLD_MS;
}

void oa_pause_poll(void)
{
    if (!release_at || monotonic_ms() < release_at) return;
    if (pulse_slot == NO_SLOT)
        rt_android_key(VK_ESCAPE, SCAN_ESCAPE, 0, 0);
    else
        rt_android_pad((unsigned)pulse_slot, 1, 0, 0, 0, 0, 0, 0, 0);
    release_at = 0;
    pulse_slot = NO_SLOT;
}

int oa_pause_pad(unsigned slot, int connected, uint16_t buttons, int16_t x, int16_t y)
{
    (void)x;
    if (!enabled || slot >= 4) return 0;
    uint16_t pressed = buttons & ~last_buttons[slot];
    last_buttons[slot] = buttons;
    if (connected && (pressed & (PAD_START | PAD_A | PAD_B))) last_slot = (int)slot;
    int vertical = y > 18000 ? -1 : y < -18000 ? 1 : 0;
    int step = (pressed & PAD_UP) || (pressed & PAD_DOWN) || (vertical && !last_vertical[slot]);
    last_vertical[slot] = vertical;
    if (!connected) {
        if (last_slot == (int)slot) last_slot = NO_SLOT;
        if (pulse_slot == (int)slot) {
            release_at = 0;
            pulse_slot = NO_SLOT;
        }
        return 0;
    }
    if (release_at && pulse_slot == (int)slot) return 1;
    if (!oa_pause_visible()) {
        if (pressed & PAD_START && atomic_load(&world_ready)) atomic_store(&opening, 90);
        return 0;
    }
    if (!atomic_load(&renderer_ready)) return 0;
    if (step) atomic_store(&focus_row, 1 - oa_pause_focus());
    if (pressed & (PAD_B | PAD_START)) choose(ROW_RESUME, (int)slot);
    else if (pressed & PAD_A) choose(oa_pause_focus(), (int)slot);
    return 1;
}

int oa_pause_input(const AInputEvent *event, float x, float y, int width, int height)
{
    if (!enabled) return 0;
    int active = oa_pause_visible() && atomic_load(&renderer_ready);
    int type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int key = AKeyEvent_getKeyCode(event);
        if ((AInputEvent_getSource(event) & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) return 0;
        int action = AKeyEvent_getAction(event);
        int initial_down = action == AKEY_EVENT_ACTION_DOWN && !AKeyEvent_getRepeatCount(event);
        if (key == AKEYCODE_ESCAPE) {
            /* Let the original game open the menu, as with controller Start. */
            if (initial_down && !active && !release_at && atomic_load(&world_ready)) {
                forwarded_pause_key = 1;
                atomic_store(&opening, 90);
            }
            if (forwarded_pause_key) {
                if (action == AKEY_EVENT_ACTION_UP) forwarded_pause_key = 0;
                return 0;
            }
            if (release_at && pulse_slot == NO_SLOT) return 1;
        }
        if (!active) return 0;
        if (initial_down) {
            if (key == AKEYCODE_ESCAPE || key == AKEYCODE_BACK) choose(ROW_RESUME, NO_SLOT);
            if (key == AKEYCODE_DPAD_UP || key == AKEYCODE_DPAD_DOWN)
                atomic_store(&focus_row, 1 - oa_pause_focus());
            if (key == AKEYCODE_ENTER || key == AKEYCODE_SPACE) choose(oa_pause_focus(), NO_SLOT);
        }
        return 1;
    }
    if (!active || type != AINPUT_EVENT_TYPE_MOTION) return 0;
    int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    int hit = -1;
    for (unsigned row = 0; row < ROW_COUNT; ++row) {
        float r[4];
        oa_pause_row_rect(row, width, height, r);
        if (x >= r[0] && x < r[0]+r[2] && y >= r[1] && y < r[1]+r[3]) hit = (int)row;
    }
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        touch_row = hit;
        if (hit >= 0) atomic_store(&focus_row, (unsigned)hit);
    } else if (action == AMOTION_EVENT_ACTION_UP) {
        if (hit >= 0 && hit == touch_row) choose((unsigned)hit, last_slot);
        touch_row = -1;
    } else if (action == AMOTION_EVENT_ACTION_CANCEL) touch_row = -1;
    return 1;
}

int oa_pause_poll_timeout(void)
{
    return release_at ? 20 : 1000;
}
