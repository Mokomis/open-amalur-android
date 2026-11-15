#include "waterfall_state.h"

#include <android/log.h>
#include <stdatomic.h>
#include <stdint.h>
#include <strings.h>
#include <time.h>

static _Atomic(AAssetManager *) assets;
static _Atomic uint64_t last_seen;

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
    if (!key || !value || strcasecmp(section, "DEFAULT") || strcasecmp(key, "VeilWaterfall"))
        return -1;
    const uint64_t now = monotonic_ms();
    if (now - atomic_load(&last_seen) > 200)
        __android_log_print(ANDROID_LOG_INFO, "VeilWaterfall", "Character portrait active");
    atomic_store(&last_seen, now);
    return 1;
}

unsigned veil_waterfall_frame(void)
{
    const uint64_t now = monotonic_ms();
    if (now - atomic_load(&last_seen) > 100)
        return 0;
    return (unsigned)((now / 50) % 100) + 1;
}
