#include "veil_native_scaling.h"
#ifdef _WIN32
#include "rt.h"
#endif

#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifndef _WIN32
static const char setting_key[] = "VeilNativeHardwareScaling";
static const char active_key[] = "VeilNativeHardwareScalingActive";
static char settings_path[PATH_MAX];
#endif
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char resolution_path[PATH_MAX];
#ifdef _WIN32
static char window_mode_path[PATH_MAX];
static const char *const window_modes[] = {"windowed", "borderless", "fullscreen"};
static int requested_window_mode, active_window_mode;

int veil_windows_mode(const char *root)
{
    pthread_mutex_lock(&lock);
    if (!window_mode_path[0] && root) {
        int length = snprintf(window_mode_path, sizeof window_mode_path, "%s/window-mode", root);
        if (length < 0 || (size_t)length >= sizeof window_mode_path) window_mode_path[0] = 0;
        else {
            FILE *file = fopen(window_mode_path, "r");
            char value[32], extra;
            if (file) {
                if (fscanf(file, "%31s %c", value, &extra) == 1)
                    for (int i = 0; i < 3; ++i)
                        if (!strcmp(value, window_modes[i])) requested_window_mode = i;
                fclose(file);
            }
        }
        active_window_mode = requested_window_mode;
    }
    int mode = active_window_mode;
    pthread_mutex_unlock(&lock);
    return mode;
}
#endif
static int display_width = 1920;
static int display_height = 1080;
typedef struct { int width, height; } Resolution;
static Resolution fallback_resolutions[3] = {{1920, 1080}, {1920, 1080}, {960, 540}};
static Resolution *resolutions = fallback_resolutions;
static int resolution_count;
static int native_resolution;
static int requested_resolution;
static int active_resolution;

// A launch owns one immutable list: indices cannot change underneath an open
// options menu. Persist dimensions, not OS mode IDs or enumeration order.
static int add_resolution(int width, int height)
{
    if (width <= 0 || height <= 0) return -1;
#ifndef _WIN32
    if (height > width) { int swap = width; width = height; height = swap; }
#endif
    for (int i = 0; i < resolution_count; ++i)
        if (resolutions[i].width == width && resolutions[i].height == height) return i;
    resolutions[resolution_count] = (Resolution){width, height};
    return resolution_count++;
}

void veil_display_resolutions(const int *sizes, int count, int width, int height)
{
    pthread_mutex_lock(&lock);
    if (!resolution_count) {
        if (!sizes || count < 0) count = 0;
        Resolution *items = calloc((size_t)count + 3, sizeof *items);
        if (items) resolutions = items;
        else count = 0;
        add_resolution(1920, 1080);
        native_resolution = add_resolution(width > 0 ? width : 1920, height > 0 ? height : 1080);
        add_resolution(960, 540);
        for (int i = 0; i < count; ++i) add_resolution(sizes[2*i], sizes[2*i+1]);
    }
    pthread_mutex_unlock(&lock);
}

static int resolution_index(int width, int height)
{
    for (int i = 0; i < resolution_count; ++i)
        if (resolutions[i].width == width && resolutions[i].height == height) return i;
    return 0;
}
#ifndef _WIN32
static int initialized;
static const int percentages[] = {0, 25, 50, 75, 100};
enum { DEFAULT_PRESET = 4, PRESET_COUNT = sizeof percentages / sizeof percentages[0] };
static int requested = DEFAULT_PRESET;
static int active = DEFAULT_PRESET;

int veil_native_scaling_percent(const char *root)
{
    pthread_mutex_lock(&lock);
    if (!initialized && root) {
        int size = snprintf(settings_path, sizeof settings_path, "%s/veil-native-scaling", root);
        if (size < 0 || (size_t)size >= sizeof settings_path) {
            settings_path[0] = '\0';
        } else {
            FILE *file = fopen(settings_path, "r");
            if (file) {
                int option;
                char extra;
                if (fscanf(file, "%d %c", &option, &extra) == 1) {
                    if (option == 1)
                        option = 100;
                    for (int i = 0; i < PRESET_COUNT; ++i) {
                        if (percentages[i] == option) {
                            requested = i;
                            break;
                        }
                    }
                }
                fclose(file);
            }
        }
        active = requested;
        initialized = 1;
    }
    int percent = percentages[active];
    pthread_mutex_unlock(&lock);
    return percent;
}

#endif

int veil_render_resolution(const char *root, int window_width, int window_height,
                           int *width, int *height)
{
    veil_display_resolutions(NULL, 0, window_width, window_height);
    pthread_mutex_lock(&lock);
    display_width = window_width;
    display_height = window_height;
    if (!resolution_path[0] && root) {
        int size = snprintf(resolution_path, sizeof resolution_path, "%s/render-scale", root);
        if (size < 0 || (size_t)size >= sizeof resolution_path) {
            resolution_path[0] = '\0';
        } else {
            FILE *file = fopen(resolution_path, "r");
            if (file) {
                char mode[64], extra;
                int width, height;
                if (fscanf(file, "%63s", mode) == 1) {
                    if (!strcmp(mode, "native")) requested_resolution = native_resolution;
                    else if (!strcmp(mode, "50")) requested_resolution = resolution_index(960, 540);
                    else if (sscanf(mode, "%dx%d%c", &width, &height, &extra) == 2)
                        requested_resolution = resolution_index(width, height);
                    // Legacy "100" and unavailable sizes use the safe 1920x1080 default.
                }
                fclose(file);
            }
        }
        active_resolution = requested_resolution;
    }
    int native = active_resolution == native_resolution;
    *width = resolutions[active_resolution].width;
    *height = resolutions[active_resolution].height;
    pthread_mutex_unlock(&lock);
    return native;
}

int veil_native_scaling_read(const char *section, const char *key, char *value)
{
    if (!section || !key || strcasecmp(section, "DEFAULT"))
        return 0;
    pthread_mutex_lock(&lock);
    int handled = 1;
    int mode_index;
    char extra;
#ifdef _WIN32
    if (!strcasecmp(key, "VeilWindowMode"))
        snprintf(value, 8, "%d", requested_window_mode);
    else if (!strcasecmp(key, "VeilWindowModeActive"))
        snprintf(value, 8, "%d", active_window_mode);
    else
#endif
    if (!strcasecmp(key, "VeilResolutionCount"))
        snprintf(value, 8, "%d", resolution_count);
    else if (sscanf(key, "VeilResolutionWidth%d%c", &mode_index, &extra) == 1 &&
             mode_index >= 0 && mode_index < resolution_count)
        snprintf(value, 8, "%d", resolutions[mode_index].width);
    else if (sscanf(key, "VeilResolutionHeight%d%c", &mode_index, &extra) == 1 &&
             mode_index >= 0 && mode_index < resolution_count)
        snprintf(value, 8, "%d", resolutions[mode_index].height);
#ifndef _WIN32
    else if (!strcasecmp(key, setting_key))
        snprintf(value, 8, "%d", requested);
    else if (!strcasecmp(key, active_key))
        snprintf(value, 8, "%d", active);
#endif
    else if (!strcasecmp(key, "VeilRenderResolution"))
        snprintf(value, 8, "%d", requested_resolution);
    else if (!strcasecmp(key, "VeilRenderResolutionActive"))
        snprintf(value, 8, "%d", active_resolution);
    else if (!strcasecmp(key, "VeilTitleContentLeft")) {
        int left = 178;
        int menu_width = 720 * display_width / display_height;
        // A native narrow backbuffer has no presentation crop to move the
        // title content left. Match the inset of the cropped widescreen title.
        if (active_resolution == native_resolution && menu_width < 1280)
            left -= (1280 - menu_width) * 3 / 8;
        snprintf(value, 8, "%d", left > 60 ? left : 60);
    } else if (!strcasecmp(key, "VeilOptionsWidth")) {
        int width = 720 * display_width / display_height;
        snprintf(value, 8, "%d", width >= 1080 && width < 1280 ? width : 1280);
    } else if (!strcasecmp(key, "VeilDisplayWidth"))
        snprintf(value, 8, "%d", display_width);
    else if (!strcasecmp(key, "VeilDisplayHeight"))
        snprintf(value, 8, "%d", display_height);
    else
        handled = 0;
    pthread_mutex_unlock(&lock);
    return handled;
}

// Called with lock held to serialize the preference update and its rename.
static int save_setting(const char *path, const char *value)
{
    if (!path[0])
        return 0;

    char temporary[PATH_MAX + sizeof ".tmp"];
    snprintf(temporary, sizeof temporary, "%s.tmp", path);
    FILE *file = fopen(temporary, "w");
    if (!file)
        return 0;

    int ok = fprintf(file, "%s\n", value) > 0;
    ok = fclose(file) == 0 && ok;
#ifdef _WIN32
    ok = ok && host_rename(temporary, path);
#else
    ok = ok && rename(temporary, path) == 0;
#endif
    if (!ok)
        unlink(temporary);
    return ok;
}

int veil_native_scaling_write(const char *section, const char *key, const char *value)
{
    if (!section || !key || strcasecmp(section, "DEFAULT"))
        return -1;
    int resolution = !strcasecmp(key, "VeilRenderResolution");
#ifdef _WIN32
    int window_mode = !strcasecmp(key, "VeilWindowMode");
    if (!resolution && !window_mode) return -1;
#else
    if (!resolution && strcasecmp(key, setting_key)) return -1;
#endif
    if (!value || !*value) return 0;
    char *end;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno || *end || parsed < 0 || parsed > INT_MAX) return 0;
    int preset = (int)parsed;

    pthread_mutex_lock(&lock);
    int count = resolution_count;
#ifdef _WIN32
    if (window_mode) count = 3;
#endif
#ifndef _WIN32
    if (!resolution) count = PRESET_COUNT;
#endif
    if (preset >= count) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    int ok = 0;
#ifdef _WIN32
    if (window_mode) {
        ok = save_setting(window_mode_path, window_modes[preset]);
        if (ok) requested_window_mode = preset;
        pthread_mutex_unlock(&lock);
        return ok;
    }
#endif
    if (resolution) {
        char dimensions[64];
        snprintf(dimensions, sizeof dimensions, "%dx%d", resolutions[preset].width, resolutions[preset].height);
        ok = save_setting(resolution_path, dimensions);
        if (ok)
            requested_resolution = preset;
#ifndef _WIN32
    } else {
        char percent[4];
        snprintf(percent, sizeof percent, "%d", percentages[preset]);
        ok = save_setting(settings_path, percent);
        if (ok)
            requested = preset;
#endif
    }
    pthread_mutex_unlock(&lock);
    return ok;
}
