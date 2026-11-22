/* Part of oa_vk_fullscreen.c: Vulkan entry points and loader. Included in place; not compiled on its own. */
/* ---- Vulkan entry points ---- */
#define VK_FUNCS(X) \
    X(vkDestroyInstance) X(vkDestroySurfaceKHR) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkEnumerateDeviceExtensionProperties) X(vkGetPhysicalDeviceFormatProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) OA_VK_SURFACE_ENTRY(X) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceProperties2)
#define VKD_FUNCS(X) \
    X(vkGetDeviceQueue) X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkGetImageMemoryRequirements2) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkCreatePipelineCache) X(vkGetPipelineCacheData) X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) X(vkFlushMappedMemoryRanges) X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkCreateFence) X(vkWaitForFences) \
    X(vkResetFences) X(vkGetFenceStatus) X(vkDestroyFence) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) \
    X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdCopyImage) X(vkCmdBlitImage) X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage) X(vkCmdBeginRendering) \
    X(vkCmdEndRendering) X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetStencilReference) X(vkCmdSetBlendConstants) X(vkCmdBindVertexBuffers2) X(vkCmdWriteTimestamp) \
    X(vkCmdBindIndexBuffer) X(vkCmdDraw) X(vkCmdDrawIndexed) X(vkCreateQueryPool) X(vkGetQueryPoolResults) X(vkCmdResetQueryPool) X(vkCmdBeginQuery) X(vkCmdEndQuery) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants)
#define X(n) static PFN_##n p##n;
VK_FUNCS(X)
VKD_FUNCS(X)
#undef X

static VkInstance g_instance;
static VkPhysicalDevice g_phys;
static VkDevice g_dev;
static float af_device_limit;
static unsigned af_requested, af_generation;
static unsigned af_active_draws, af_stock_mask;

static VkQueue g_queue;
static OaPresentOps g_present_ops;
static uint64_t g_present_submitted,g_present_completed,g_present_requests,g_present_deferred;
static uint32_t g_qfam;
static VkPhysicalDeviceMemoryProperties g_mem;
static VkPipelineCache g_pcache;
static VkDeviceSize g_ubo_align = 256;
static VkFormat g_d24s8 = VK_FORMAT_D24_UNORM_S8_UINT;
static PFN_vkMapMemory2KHR g_map_placed;
static int g_dynamic_map_mode;
static uint64_t g_dynamic_reserved;
enum { DYNAMIC_MAP_BUDGET = 4u << 20, DYNAMIC_MAP_MAX_BUFFER = 512u << 10 };
static VkDeviceSize g_map_alignment;

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags fallback) {
    uint32_t i;
    for (i = 0; i < g_mem.memoryTypeCount; i++)
        if ((bits >> i & 1) && (g_mem.memoryTypes[i].propertyFlags & want) == want) return i;
    for (i = 0; i < g_mem.memoryTypeCount; i++)
        if ((bits >> i & 1) && (g_mem.memoryTypes[i].propertyFlags & fallback) == fallback) return i;
    return 0;
}

typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    uint8_t *ptr;
    uint32_t guest, pool_slot;
    VkMemoryPropertyFlags memory_flags;
} vk_buffer;

static int make_buffer_with_mapping(vk_buffer *b, VkDeviceSize size,
                                    VkBufferUsageFlags usage, int place, int prefer_cached) {
    VkBufferCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryRequirements r;
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ci.size = size;
    ci.usage = usage;
    if (pvkCreateBuffer(g_dev, &ci, NULL, &b->buf)) return 0;
    pvkGetBufferMemoryRequirements(g_dev, b->buf, &r);
    ai.allocationSize = r.size;
    const VkMemoryPropertyFlags coherent = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkMemoryPropertyFlags cached = coherent | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    VkResult allocated = VK_ERROR_FEATURE_NOT_PRESENT;
    if (prefer_cached) {
        ai.memoryTypeIndex = mem_type(r.memoryTypeBits, cached, coherent);
        if ((g_mem.memoryTypes[ai.memoryTypeIndex].propertyFlags & cached) == cached)
            allocated = pvkAllocateMemory(g_dev, &ai, NULL, &b->mem);
    }
    if (allocated != VK_SUCCESS) {
        ai.memoryTypeIndex = mem_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | coherent, coherent);
        allocated = pvkAllocateMemory(g_dev, &ai, NULL, &b->mem);
    }
    if (allocated != VK_SUCCESS) {
        ai.memoryTypeIndex = mem_type(r.memoryTypeBits, coherent, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (pvkAllocateMemory(g_dev, &ai, NULL, &b->mem)) {
            pvkDestroyBuffer(g_dev,b->buf,NULL);
            memset(b,0,sizeof *b);
            return 0;
        }
    }
    if (pvkBindBufferMemory(g_dev, b->buf, b->mem, 0))
        goto failed;
    VkMemoryPropertyFlags flags = g_mem.memoryTypes[ai.memoryTypeIndex].propertyFlags;
    b->memory_flags = flags;
    const int dynamic_place = place == 2;
    const VkDeviceSize placement_span = r.size + g_map_alignment;
    int budget = 1;
    if (dynamic_place && placement_span <= DYNAMIC_MAP_BUDGET) {
        uint64_t old = __atomic_fetch_add(&g_dynamic_reserved, placement_span, __ATOMIC_RELAXED);
        budget = old + placement_span <= DYNAMIC_MAP_BUDGET;
        if (!budget) __atomic_fetch_sub(&g_dynamic_reserved, placement_span, __ATOMIC_RELAXED);
    } else if (dynamic_place) {
        budget = 0;
    }
    if (place && budget && g_map_placed && g_map_alignment &&
        r.size + g_map_alignment <= UINT32_MAX &&
        (flags & (VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
        (VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        uint32_t region = rt_vm_reserve(0, (uint32_t)(r.size + g_map_alignment));
        if (region) {
            uintptr_t address = (uintptr_t)rt_ptr(region);
            address += (g_map_alignment - address % g_map_alignment) % g_map_alignment;
            VkMemoryMapPlacedInfoEXT placed = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT,
                .pPlacedAddress = (void*)address
            };
            VkMemoryMapInfoKHR map = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR,
                .pNext = &placed,
                .flags = VK_MEMORY_MAP_PLACED_BIT_EXT,
                .memory = b->mem,
                .size = VK_WHOLE_SIZE
            };
            VkResult result = g_map_placed(g_dev, &map, (void**)&b->ptr);
            if (result == VK_SUCCESS) {
                if ((uintptr_t)b->ptr != address) rt_fatal("Vulkan placed mapping returned a different address");
                b->guest = (uint32_t)(address - (uintptr_t)rt_ptr(0));
                if (!rt_vm_commit(b->guest, (uint32_t)r.size)) rt_fatal("record Vulkan guest mapping");
                if (!dynamic_place)
                    logf_("constant upload ring placed: guest %#x, type %u flags %#x", b->guest, ai.memoryTypeIndex, flags);
                return 1;
            }
            rt_vm_release(region);
            logf_("constant upload placed map unavailable: %d", result);
        }
    }
    if (dynamic_place && budget)
        __atomic_fetch_sub(&g_dynamic_reserved, placement_span, __ATOMIC_RELAXED);
    if (!pvkMapMemory(g_dev, b->mem, 0, VK_WHOLE_SIZE, 0, (void**)&b->ptr))
        return 1;
failed:
    pvkDestroyBuffer(g_dev, b->buf, NULL);
    pvkFreeMemory(g_dev, b->mem, NULL);
    memset(b, 0, sizeof(*b));
    return 0;
}

static int make_buffer(vk_buffer *b, VkDeviceSize size, VkBufferUsageFlags usage) {
    return make_buffer_with_mapping(b, size, usage, 0, 0);
}

/* The driver can drop a reserved address on vkFreeMemory. Keep placed
 * vertex/index allocations alive, with a bounded pool tied to g_dev. */
enum { DYNAMIC_BUFFER_SLOTS = 16 };
typedef struct {
    vk_buffer buffer;
    uint32_t size;
    int available;
} DynamicBufferSlot;
static DynamicBufferSlot dynamic_buffers[DYNAMIC_BUFFER_SLOTS];
static pthread_mutex_t dynamic_buffer_lock = PTHREAD_MUTEX_INITIALIZER;

static int dynamic_buffer_acquire(vk_buffer *buffer, uint32_t bytes, VkBufferUsageFlags usage, int prefer_cached)
{
    unsigned slot = DYNAMIC_BUFFER_SLOTS;
    pthread_mutex_lock(&dynamic_buffer_lock);
    for (unsigned i = 0; i < DYNAMIC_BUFFER_SLOTS; ++i) {
        DynamicBufferSlot *entry = &dynamic_buffers[i];
        if (entry->available && entry->size == bytes) {
            entry->available = 0;
            *buffer = entry->buffer;
            pthread_mutex_unlock(&dynamic_buffer_lock);
            memset(buffer->ptr, 0, bytes);
            return 1;
        }
        if (!entry->size && slot == DYNAMIC_BUFFER_SLOTS) slot = i;
    }
    if (slot != DYNAMIC_BUFFER_SLOTS) dynamic_buffers[slot].size = bytes;
    pthread_mutex_unlock(&dynamic_buffer_lock);
    if (slot == DYNAMIC_BUFFER_SLOTS) return make_buffer(buffer, bytes, usage);

    int result = make_buffer_with_mapping(buffer, bytes, usage, 2, prefer_cached);
    pthread_mutex_lock(&dynamic_buffer_lock);
    if (result && buffer->guest) {
        buffer->pool_slot = slot + 1;
        dynamic_buffers[slot].buffer = *buffer;
    } else {
        dynamic_buffers[slot].size = 0;
    }
    pthread_mutex_unlock(&dynamic_buffer_lock);
    return result;
}

static void dynamic_buffer_return(const vk_buffer *buffer)
{
    pthread_mutex_lock(&dynamic_buffer_lock);
    DynamicBufferSlot *slot = &dynamic_buffers[buffer->pool_slot - 1];
    if (slot->available || slot->buffer.buf != buffer->buf)
        rt_fatal("invalid placed dynamic buffer release");
    slot->available = 1;
    pthread_mutex_unlock(&dynamic_buffer_lock);
}

/* Mapped memory must be 16-byte aligned: the engine writes it with aligned SSE moves. */
static void *zalloc16(size_t n) {
    void *p = _aligned_malloc(n ? n : 16, 16);
    if (p) memset(p, 0, n);
    return p;
}
