#include "veil_dialogue_touch.h"

#include <android/input.h>
#include <android/log.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t visible_at;
static int tracking, x_axis, y_axis, released;

static uint64_t now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int axis(int delta)
{
    int value = delta * 3;
    return value > 100 ? 100 : value < -100 ? -100 : value;
}

int veil_dialogue_touch(int action, int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return 0;
    // The native dialogue artwork uses a 1280 by 720 coordinate space.
    int dx = x * 1280 / width - 640;
    int dy = y * 720 / height - 609;
    pthread_mutex_lock(&lock);
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        tracking = visible_at && now_ms() - visible_at < 3000 &&
                   dx >= -51 && dx <= 51 && dy >= -51 && dy <= 51 &&
                   dx * dx + dy * dy <= 51 * 51;
        if (tracking) {
            released = 0;
            __android_log_print(ANDROID_LOG_INFO, "VeilDialogue", "Circle drag started");
        }
    }
    int handled = tracking;
    if (tracking) {
        x_axis = axis(dx);
        y_axis = axis(dy);
        if (action == AMOTION_EVENT_ACTION_CANCEL) {
            tracking = released = 0;
        } else if (action == AMOTION_EVENT_ACTION_UP) {
            tracking = 0;
            __android_log_print(ANDROID_LOG_INFO, "VeilDialogue", "Circle release x=%d y=%d", x_axis, y_axis);
            released = 2;
        }
    }
    pthread_mutex_unlock(&lock);
    return handled;
}

int veil_dialogue_command(const char *section, const char *key, const char *value)
{
    if (!key || !value || strcasecmp(section, "DEFAULT") || strcasecmp(key, "VeilDialogueCircle"))
        return -1;
    pthread_mutex_lock(&lock);
    visible_at = value[0] == '1' ? now_ms() : 0;
    if (!visible_at) released = 0;
    pthread_mutex_unlock(&lock);
    return 1;
}

int veil_dialogue_read(const char *section, const char *key, char *value, size_t length)
{
    if (strcasecmp(section, "DEFAULT")) return 0;
    const int gesture = !strcasecmp(key, "VeilDialogueGesture");
    const int confirm = !strcasecmp(key, "VeilDialogueConfirm");
    if (!gesture && !confirm) return 0;
    pthread_mutex_lock(&lock);
    unsigned result = 0;
    if (visible_at && now_ms() - visible_at < 3000) {
        if (gesture && (tracking || released == 2)) {
            result = 2 + (y_axis + 100) * 201 + x_axis + 100;
            if (released == 2) released = 1;
        } else if (confirm && released == 1) {
            result = 1;
            released = 0;
            __android_log_print(ANDROID_LOG_INFO, "VeilDialogue", "Circle confirm delivered");
        }
    }
    pthread_mutex_unlock(&lock);
    snprintf(value, length, "%u", result);
    return 1;
}

