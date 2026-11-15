#include "waterfall_state.h"

#include <android/log.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static _Atomic(AAssetManager *) assets;
static pthread_mutex_t portrait_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t last_seen;
static VeilWaterfallRect portrait;

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

void veil_waterfall_assets(AAssetManager *manager)
{
    atomic_store(&assets, manager);
}

AAsset *veil_waterfall_open(void)
{
    AAssetManager *manager = atomic_load(&assets);
    return manager ? AAssetManager_open(manager, "veil-waterfall.bc3", AASSET_MODE_BUFFER) : NULL;
}

int veil_waterfall_command(const char *section, const char *key, const char *value)
{
    if (!section || !key || !value || strcasecmp(section, "DEFAULT") || strcasecmp(key, "VeilWaterfall"))
        return -1;
    if (!strcmp(value, "0")) {
        pthread_mutex_lock(&portrait_lock);
        last_seen = 0;
        pthread_mutex_unlock(&portrait_lock);
        return 1;
    }
    VeilWaterfallRect next = {0};
    if (strcmp(value, "1")) {
        if (sscanf(value, "rect,%f,%f,%f,%f", &next.left, &next.top, &next.width, &next.height) != 4 ||
            !(fabsf(next.left) <= 4096 && fabsf(next.top) <= 4096 &&
              next.width > 0 && next.width <= 4096 && next.height > 0 && next.height <= 4096))
            return 1;
    }
    const uint64_t now = monotonic_ms();
    pthread_mutex_lock(&portrait_lock);
    if (now - last_seen > 200)
        __android_log_print(ANDROID_LOG_INFO, "VeilWaterfall", "Character portrait active");
    if (memcmp(&portrait, &next, sizeof(next)) != 0)
        __android_log_print(ANDROID_LOG_INFO, "VeilWaterfall", "Portrait rect %.1f,%.1f %.1fx%.1f",
                            next.left, next.top, next.width, next.height);
    portrait = next;
    last_seen = now;
    pthread_mutex_unlock(&portrait_lock);
    return 1;
}

unsigned veil_waterfall_frame(VeilWaterfallRect *rect)
{
    pthread_mutex_lock(&portrait_lock);
    const uint64_t now = monotonic_ms();
    const unsigned frame = now - last_seen <= 100 ? (unsigned)((now / 50) % 100) + 1 : 0;
    *rect = portrait;
    pthread_mutex_unlock(&portrait_lock);
    return frame;
}
