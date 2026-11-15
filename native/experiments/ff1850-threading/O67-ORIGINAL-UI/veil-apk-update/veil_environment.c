#include "veil_environment.h"

#include <android/log.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

enum { NIGHT_AMBIENCE = 1, DAY_AMBIENCE = 2, AMBIENCE_MASK = NIGHT_AMBIENCE | DAY_AMBIENCE };
enum { MINUTES_PER_DAY = 1440, VOLUME_STEPS = 101 };
enum { INDOORS, DAY, NIGHT };
static _Atomic unsigned options = AMBIENCE_MASK;
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
        if (fscanf(file, "%u", &saved) == 1) atomic_store(&options, saved & AMBIENCE_MASK);
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
    if (!strcasecmp(key, "VeilNightAmbience")) return NIGHT_AMBIENCE;
    if (!strcasecmp(key, "VeilDayAmbience")) return DAY_AMBIENCE;
    return 0;
}

static int outdoor_period(int minute)
{
    if (minute < 0) return INDOORS;
    return minute < 6 * 60 || minute >= 20 * 60 ? NIGHT : DAY;
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
        int period = outdoor_period(minute);
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

static unsigned current_sample(void)
{
    if (!veil_tablet_gameplay_active() ||
        now_ms() - atomic_load(&sample_at) >= 1800) return 0;
    return atomic_load(&outdoor_sample);
}

enum { CUE_RATE = 24000, CUES_PER_PERIOD = 3 };
static struct {
    const char *name;
    int16_t *samples;
    uint32_t count;
} cues[] = {
    { .name = "day-thrush" }, { .name = "day-flicker" }, { .name = "day-grasshopper" },
    { .name = "night-crickets" }, { .name = "night-owl" }, { .name = "night-treefrog" },
};
static uint32_t randomness = 1;

void veil_ambience_load(AAssetManager *assets)
{
    randomness = (uint32_t)now_ms() | 1;
    for (unsigned i = 0; i < sizeof cues / sizeof cues[0]; ++i) {
        if (cues[i].samples) continue;
        char path[64];
        snprintf(path, sizeof path, "ambience/%s.pcm", cues[i].name);
        AAsset *asset = AAssetManager_open(assets, path, AASSET_MODE_BUFFER);
        if (!asset) continue;
        const off_t bytes = AAsset_getLength(asset);
        const void *data = AAsset_getBuffer(asset);
        if (data && bytes >= 2 && !(bytes % 2) && bytes <= CUE_RATE * 2 * 20) {
            cues[i].samples = malloc(bytes);
            if (cues[i].samples) {
                memcpy(cues[i].samples, data, bytes);
                cues[i].count = bytes / 2;
            }
        }
        AAsset_close(asset);
    }
}

static uint32_t ambience_random(void)
{
    randomness ^= randomness << 13;
    randomness ^= randomness >> 17;
    randomness ^= randomness << 5;
    return randomness;
}

void veil_ambience_mix(float *output, uint32_t frames, uint32_t rate, int foreground)
{
    static int active_period, previous = -1, cue = -1;
    static uint64_t wait_frames;
    static double position;
    static float pan, envelope;
    unsigned sample = foreground ? current_sample() : 0;
    unsigned volume = sample % VOLUME_STEPS;
    int period = outdoor_period((int)(sample / VOLUME_STEPS) - 1);
    unsigned option = period == DAY ? DAY_AMBIENCE : NIGHT_AMBIENCE;
    if (!volume || !(atomic_load(&options) & option)) period = INDOORS;
    if (period != active_period) {
        active_period = period;
        cue = -1;
        envelope = 0;
        wait_frames = (uint64_t)rate * (2500 + ambience_random() % 5000) / 1000;
    }
    if (period == INDOORS || !rate) return;
    if (cue < 0) {
        if (wait_frames > frames) {
            wait_frames -= frames;
            return;
        }
        wait_frames = (uint64_t)rate * (16 + ambience_random() % 28);
        int start = ambience_random() % CUES_PER_PERIOD;
        for (int offset = 0; offset < CUES_PER_PERIOD; ++offset) {
            int index = (period - 1) * CUES_PER_PERIOD + (start + offset) % CUES_PER_PERIOD;
            if (index == previous || !cues[index].count) continue;
            cue = previous = index;
            position = 0;
            pan = (int)(ambience_random() % 41) * .01f - .20f;
            __android_log_print(ANDROID_LOG_INFO, "VeilAmbience", "Mixing %s at effects volume %u",
                                cues[cue].name, volume);
            break;
        }
        if (cue < 0) {
            previous = -1;
            return;
        }
    }

    const double step = (double)CUE_RATE / rate;
    const float gain = volume * (.0014f / 32768);
    const float attack = fminf(1, 200.0f / rate), release = fminf(1, 5.0f / rate);
    const float side[] = {1 - fmaxf(0, pan), 1 + fminf(0, pan)};
    for (uint32_t i = 0; i < frames; ++i, position += step) {
        uint32_t index = (uint32_t)position;
        if (index + 1 >= cues[cue].count) {
            cue = -1;
            break;
        }
        float *stereo = output + 2 * i;
        float peak = fmaxf(fabsf(stereo[0]), fabsf(stereo[1]));
        envelope += (peak - envelope) * (peak > envelope ? attack : release);
        float duck = 1 - .92f * fminf(1, fmaxf(0, (envelope - .067f) / .232f));
        const int16_t *pcm = cues[cue].samples;
        float sound = pcm[index] + (float)(position - index) * (pcm[index + 1] - pcm[index]);
        float wildlife = sound * gain * duck;
        for (unsigned channel = 0; channel < 2; ++channel) {
            float headroom = fmaxf(0, 1 - fabsf(stereo[channel]));
            stereo[channel] += fmaxf(-headroom, fminf(headroom, wildlife * side[channel]));
        }
    }
}
