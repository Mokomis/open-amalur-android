#include "veil_native_scaling.h"
#include "rt.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char resolution_path[PATH_MAX];
static int display_width = 1920;
static int display_height = 1080;
enum { FULL_HD, NATIVE_DISPLAY, HALF_HD };
static const char *const resolution_modes[] = {"100", "native", "50"};
static int requested_resolution = FULL_HD;
static int active_resolution = FULL_HD;
#ifndef _WIN32
static const char setting_key[] = "VeilNativeHardwareScaling";
static const char active_key[] = "VeilNativeHardwareScalingActive";
static char settings_path[PATH_MAX];
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
                char mode[16];
                if (fscanf(file, "%15s", mode) == 1) {
                    for (int i = FULL_HD; i <= HALF_HD; ++i) {
                        if (!strcmp(mode, resolution_modes[i])) {
                            requested_resolution = i;
                            break;
                        }
                    }
                }
                fclose(file);
            }
        }
        active_resolution = requested_resolution;
    }
    int native = active_resolution == NATIVE_DISPLAY;
    *width = native ? display_width : active_resolution == HALF_HD ? 960 : 1920;
    *height = native ? display_height : active_resolution == HALF_HD ? 540 : 1080;
    pthread_mutex_unlock(&lock);
    return native;
}

int veil_native_scaling_read(const char *section, const char *key, char *value)
{
    if (!section || !key || strcasecmp(section, "DEFAULT"))
        return 0;
    pthread_mutex_lock(&lock);
    int handled = 1;
#ifndef _WIN32
    if (!strcasecmp(key, setting_key))
        snprintf(value, 8, "%d", requested);
    else if (!strcasecmp(key, active_key))
        snprintf(value, 8, "%d", active);
    else
#endif
    if (!strcasecmp(key, "VeilRenderResolution"))
        snprintf(value, 8, "%d", requested_resolution);
    else if (!strcasecmp(key, "VeilRenderResolutionActive"))
        snprintf(value, 8, "%d", active_resolution);
    else if (!strcasecmp(key, "VeilTitleContentLeft")) {
        int left = 178;
        int menu_width = 720 * display_width / display_height;
        // A native narrow backbuffer has no presentation crop to move the
        // title content left. Match the inset of the cropped widescreen title.
        if (active_resolution == NATIVE_DISPLAY && menu_width < 1280)
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
    ok = ok && host_rename(temporary, path);
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
    if (!resolution)
        return -1;
    int last_preset = HALF_HD;
#else
    if (!resolution && strcasecmp(key, setting_key))
        return -1;
    int last_preset = resolution ? HALF_HD : PRESET_COUNT - 1;
#endif
    if (!value || value[0] < '0' || value[0] > '0' + last_preset || value[1])
        return 0;
    int preset = value[0] - '0';

    pthread_mutex_lock(&lock);
    int ok;
    if (resolution) {
        ok = save_setting(resolution_path, resolution_modes[preset]);
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
