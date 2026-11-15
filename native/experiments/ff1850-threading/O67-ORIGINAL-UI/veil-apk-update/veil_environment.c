#include "veil_environment.h"

#include <android/log.h>
#include <jni.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

enum { NIGHT_AMBIENCE = 1, MINUTES_PER_DAY = 1440, VOLUME_STEPS = 101 };
static _Atomic unsigned options = NIGHT_AMBIENCE;
static _Atomic unsigned outdoor_sample;
static _Atomic int last_period = -1;
static _Atomic uint64_t sample_at;
static _Atomic uint64_t next_sample;
static pthread_once_t settings_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t settings_lock = PTHREAD_MUTEX_INITIALIZER;
static char settings_path[PATH_MAX];

extern int veil_tablet_gameplay_active(void);

static uint64_t now_ms(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static void load_settings(void)
{
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (!root) return;
    int length = snprintf(settings_path, sizeof settings_path, "%s/veil-environment", root);
    if (length < 0 || (size_t)length >= sizeof settings_path) {
        settings_path[0] = 0;
        return;
    }
    FILE *file = fopen(settings_path, "r");
    if (file) {
        unsigned saved;
        if (fscanf(file, "%u", &saved) == 1) atomic_store(&options, saved);
        fclose(file);
    }
}

static int save_settings(unsigned requested)
{
    if (!settings_path[0]) return 0;
    char temporary[PATH_MAX + sizeof ".tmp"];
    snprintf(temporary, sizeof temporary, "%s.tmp", settings_path);
    FILE *file = fopen(temporary, "w");
    if (!file) return 0;
    int ok = fprintf(file, "%u\n", requested) > 0;
    ok = fclose(file) == 0 && ok;
    if (ok && rename(temporary, settings_path) == 0) return 1;
    unlink(temporary);
    return 0;
}

static unsigned setting(const char *key)
{
    return !strcasecmp(key, "VeilNightAmbience") ? NIGHT_AMBIENCE : 0;
}

int veil_environment_read(const char *section, const char *key, char value[8])
{
    if (!section || !key || strcasecmp(section, "DEFAULT")) return 0;
    unsigned option = setting(key);
    if (!option && strcasecmp(key, "VeilEnvironmentPoll")) return 0;
    pthread_once(&settings_once, load_settings);
    int enabled = (atomic_load(&options) & option) != 0;
    if (!option && atomic_load(&options)) {
        uint64_t now = now_ms();
        uint64_t due = atomic_load(&next_sample);
        enabled = now >= due && atomic_compare_exchange_strong(&next_sample, &due, now + 800);
    }
    value[0] = enabled ? '1' : '0';
    value[1] = 0;
    return 1;
}

int veil_environment_write(const char *section, const char *key, const char *value)
{
    if (!section || !key || strcasecmp(section, "DEFAULT")) return -1;
    if (!strcasecmp(key, "VeilOutdoorClock")) {
        if (!value) return 0;
        char *end;
        long sample = strtol(value, &end, 10);
        if (sample < 0 || sample >= (MINUTES_PER_DAY + 1) * VOLUME_STEPS || end == value || *end)
            return 0;
        atomic_store(&outdoor_sample, (unsigned)sample);
        atomic_store(&sample_at, now_ms());
        int minute = (int)sample / VOLUME_STEPS - 1;
        int period = minute < 0 ? 0 : (minute < 6 * 60 || minute >= 20 * 60 ? 2 : 1);
        if (atomic_exchange(&last_period, period) != period)
            __android_log_print(ANDROID_LOG_INFO, "VeilAmbience",
                                "Outdoor clock minute=%d", minute);
        return 1;
    }
    unsigned option = setting(key);
    if (!option) return -1;
    if (!value || (value[0] != '0' && value[0] != '1') || value[1]) return 0;
    pthread_once(&settings_once, load_settings);
    pthread_mutex_lock(&settings_lock);
    unsigned previous = atomic_load(&options);
    unsigned changed = value[0] == '1' ? previous | option : previous & ~option;
    int ok = previous == changed || save_settings(changed);
    if (ok) atomic_store(&options, changed);
    pthread_mutex_unlock(&settings_lock);
    return ok;
}

JNIEXPORT jint JNICALL Java_org_openamalur_originalengine_VeilAmbience_volume(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    if (!(atomic_load(&options) & NIGHT_AMBIENCE) || !veil_tablet_gameplay_active() ||
        now_ms() - atomic_load(&sample_at) >= 1800) return 0;
    unsigned sample = atomic_load(&outdoor_sample);
    if (sample < VOLUME_STEPS) return 0;
    unsigned minute = sample / VOLUME_STEPS - 1;
    return minute < 6 * 60 || minute >= 20 * 60 ? sample % VOLUME_STEPS : 0;
}
