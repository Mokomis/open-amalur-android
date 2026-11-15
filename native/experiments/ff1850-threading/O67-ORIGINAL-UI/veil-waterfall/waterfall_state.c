#include "waterfall_state.h"

#include <android/log.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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

static const uint8_t *load_local_waterfall(const char *root, const char *name, size_t length)
{
    char path[2048];
    int size = snprintf(path, sizeof(path), "%s/%s", root, name);
    if (size < 0 || (size_t)size >= sizeof(path)) return NULL;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return NULL;
    struct stat info;
    void *mapping = MAP_FAILED;
    if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == (off_t)length)
        mapping = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) return NULL;
    __android_log_print(ANDROID_LOG_INFO, "VeilWaterfall", "Loaded local waterfall clip");
    return mapping;
}

const uint8_t *veil_waterfall_load(size_t length)
{
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (root) {
        const uint8_t *local = load_local_waterfall(root, "veil-waterfall.bc3", length);
        if (!local) local = load_local_waterfall(root, "imported-install/veil-waterfall.bc3", length);
        if (local) return local;
    }

    // A locally assembled development APK may still put its private clip in assets.
    AAssetManager *manager = atomic_load(&assets);
    AAsset *asset = manager ? AAssetManager_open(manager, "veil-waterfall.bc3", AASSET_MODE_BUFFER) : NULL;
    if (!asset) return NULL;
    const uint8_t *bytes = AAsset_getLength64(asset) == (off64_t)length ? AAsset_getBuffer(asset) : NULL;
    if (!bytes) AAsset_close(asset);
    // A successful mapping belongs to the renderer for the lifetime of the process.
    return bytes;
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
