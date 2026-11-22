
#ifndef OA_PIPELINE_CACHE_H
#define OA_PIPELINE_CACHE_H
#include <pthread.h>
#include <unistd.h>

enum { OA_CACHE_LIMIT = 16 * 1024 * 1024 };
typedef struct {
    uint8_t magic[8];
    uint32_t size;
    uint32_t checksum;
} OaPipelineCacheFile;

static const uint8_t cache_magic[8] = {'O','A','P','C','A','C','H',1};
static char cache_path[2048];
static VkPhysicalDeviceProperties cache_properties;
static unsigned cache_generation, cache_previous_generation;
static unsigned cache_saved_generation, cache_worker_running;
static uint64_t cache_next_attempt;
static uint32_t cache_last_checksum, cache_last_size;

/* The app owns this cache and the Vulkan device for the process lifetime. */
static uint32_t cache_checksum(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static int cache_blob_valid(const void *data, size_t size)
{
    VkPipelineCacheHeaderVersionOne header;
    _Static_assert(sizeof header == 32, "Vulkan cache header layout");
    if (size <= sizeof header || size > OA_CACHE_LIMIT) return 0;
    memcpy(&header, data, sizeof header);
    return header.headerSize == sizeof header &&
           header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header.vendorID == cache_properties.vendorID &&
           header.deviceID == cache_properties.deviceID &&
           memcmp(header.pipelineCacheUUID, cache_properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

static void *cache_read(size_t *size)
{
    FILE *file = fopen(cache_path, "rb");
    if (!file) return NULL;
    OaPipelineCacheFile header;
    void *data = NULL;
    if (fread(&header, sizeof header, 1, file) == 1 &&
        memcmp(header.magic, cache_magic, sizeof cache_magic) == 0 &&
        header.size > 32 && header.size <= OA_CACHE_LIMIT) {
        data = malloc(header.size);
        if (data && fread(data, 1, header.size, file) == header.size &&
            fgetc(file) == EOF && !ferror(file) &&
            cache_checksum(data, header.size) == header.checksum &&
            cache_blob_valid(data, header.size)) {
            *size = header.size;
            cache_last_size = header.size;
            cache_last_checksum = header.checksum;
        } else {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    return data;
}

static int cache_store(const void *data, size_t size, uint32_t checksum)
{
    OaPipelineCacheFile header = { .size = (uint32_t)size, .checksum = checksum };
    memcpy(header.magic, cache_magic, sizeof cache_magic);
    char temp[2080];
    snprintf(temp, sizeof temp, "%s.tmp", cache_path);
    FILE *file = fopen(temp, "wb");
    if (!file) return 0;
    int written = fwrite(&header, sizeof header, 1, file) == 1 &&
                  fwrite(data, 1, size, file) == size;
    int closed = fclose(file) == 0;
    if (written && closed && rename(temp, cache_path) == 0) return 1;
    unlink(temp);
    return 0;
}

static void pipeline_cache_init(void)
{
    VkPipelineCacheCreateInfo info = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (root && *root) {
        int len = snprintf(cache_path, sizeof cache_path, "%s/vulkan-pipeline-cache-v1.bin", root);
        if (len <= 0 || (size_t)len >= sizeof cache_path) cache_path[0] = '\0';
        pvkGetPhysicalDeviceProperties(g_phys, &cache_properties);
    }
    size_t size = 0;
    void *data = cache_path[0] ? cache_read(&size) : NULL;
    info.initialDataSize = size;
    info.pInitialData = data;
    VkResult result = pvkCreatePipelineCache(g_dev, &info, NULL, &g_pcache);
    free(data);
    rt_log("PIPELINE_CACHE loaded=%zu result=%d", size, result);
    if (result == VK_SUCCESS || !size) return;

    cache_last_size = 0;
    cache_last_checksum = 0;
    info.initialDataSize = 0;
    info.pInitialData = NULL;
    g_pcache = VK_NULL_HANDLE;
    pvkCreatePipelineCache(g_dev, &info, NULL, &g_pcache);
}

static void *cache_save_worker(void *arg)
{
    unsigned generation = (unsigned)(uintptr_t)arg;
    size_t size = 0;
    VkResult result = pvkGetPipelineCacheData(g_dev, g_pcache, &size, NULL);
    if (result == VK_SUCCESS && size > 32 && size <= OA_CACHE_LIMIT) {
        void *data = malloc(size);
        if (data) {
            result = pvkGetPipelineCacheData(g_dev, g_pcache, &size, data);
            if (result == VK_SUCCESS && cache_blob_valid(data, size)) {
                uint32_t checksum = cache_checksum(data, size);
                if (cache_last_size == size && cache_last_checksum == checksum) {
                    __atomic_store_n(&cache_saved_generation, generation, __ATOMIC_RELEASE);
                } else if (cache_store(data, size, checksum)) {
                    cache_last_size = (uint32_t)size;
                    cache_last_checksum = checksum;
                    __atomic_store_n(&cache_saved_generation, generation, __ATOMIC_RELEASE);
                    rt_log("PIPELINE_CACHE stored=%zu", size);
                }
            }
            free(data);
        }
    } else if (result == VK_SUCCESS) {
        __atomic_store_n(&cache_saved_generation, generation, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&cache_worker_running, 0, __ATOMIC_RELEASE);
    return NULL;
}

static void pipeline_cache_note_creation(void)
{
    ++cache_generation;
}

static void pipeline_cache_maybe_save(uint64_t frame)
{
    unsigned previous = cache_previous_generation;
    cache_previous_generation = cache_generation;
    if (!g_pcache || !cache_path[0] || frame < cache_next_attempt || previous != cache_generation ||
        __atomic_load_n(&cache_saved_generation, __ATOMIC_ACQUIRE) == cache_generation ||
        __atomic_exchange_n(&cache_worker_running, 1, __ATOMIC_ACQUIRE)) return;

    cache_next_attempt = frame + 600;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) == 0) {
        pthread_t thread;
        int result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (!result) result = pthread_create(&thread, &attr, cache_save_worker, (void *)(uintptr_t)cache_generation);
        pthread_attr_destroy(&attr);
        if (!result) return;
    }
    __atomic_store_n(&cache_worker_running, 0, __ATOMIC_RELEASE);
}
#endif
