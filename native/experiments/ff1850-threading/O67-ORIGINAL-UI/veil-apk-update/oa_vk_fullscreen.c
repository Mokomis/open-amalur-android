#include "title_cover.h"
#include "display_android.h"
/* oa_vk.c: the subset of Direct3D 11 and DXGI that koa.exe uses, implemented on Vulkan 1.3.
   Built as d3d11.dll. Anything not implemented logs its name once to oa_vk.log and returns E_NOTIMPL. */
#define WIN32_LEAN_AND_MEAN
#include "oa_android_win.h"
#include "rt.h"
#include <malloc.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#ifndef _WIN32
#include <android/native_window.h>
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#ifdef _WIN32
#include "windows/vulkan_surface.h"
typedef struct HWND__ ANativeWindow;
#define OA_VK_SURFACE_ENTRY(X) X(vkCreateWin32SurfaceKHR)
#define OA_SURFACE_EXTENSION "VK_KHR_win32_surface"
static void ANativeWindow_acquire(ANativeWindow *window) { (void)window; }
static void ANativeWindow_release(ANativeWindow *window) { (void)window; }
#else
#define OA_VK_SURFACE_ENTRY(X) X(vkCreateAndroidSurfaceKHR)
#define OA_SURFACE_EXTENSION "VK_KHR_android_surface"
#endif
#include "oa_present_lifecycle.h"
#include "oa_dxbc.h"

#define COM __stdcall
#define S_OK_ 0
#define E_NOTIMPL_ ((HRESULT)0x80004001L)
#define E_NOINTERFACE_ ((HRESULT)0x80004002L)
#define E_FAIL_ ((HRESULT)0x80004005L)
#define E_OOM_ ((HRESULT)0x8007000EL)
#define DXGI_NOT_FOUND ((HRESULT)0x887A0002L)
#define FRAMES 2
#define RING_SIZE (32u << 20)
#define UPLOAD_OWNERS 16384u

/* ---- logging ---- */
static FILE *g_log;
static char g_game_log[MAX_PATH];
static CRITICAL_SECTION g_lock;
static void logf_(const char *fmt, ...) {
    va_list a;
    if (!g_log) return;
    EnterCriticalSection(&g_lock);
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

/* ---- Vulkan entry points ---- */
#define VK_FUNCS(X) \
    X(vkDestroyInstance) X(vkDestroySurfaceKHR) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkEnumerateDeviceExtensionProperties) X(vkGetPhysicalDeviceFormatProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) OA_VK_SURFACE_ENTRY(X) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceProperties2)
#define VKD_FUNCS(X) \
    X(vkGetDeviceQueue) X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkCreateGraphicsPipelines) X(vkCreatePipelineCache) X(vkGetPipelineCacheData) X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) X(vkFlushMappedMemoryRanges) X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkCreateFence) X(vkWaitForFences) \
    X(vkResetFences) X(vkGetFenceStatus) X(vkDestroyFence) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) \
    X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdCopyImage) X(vkCmdBlitImage) X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage) X(vkCmdBeginRendering) \
    X(vkCmdEndRendering) X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetStencilReference) X(vkCmdSetBlendConstants) X(vkCmdBindVertexBuffers2) \
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
                                    VkBufferUsageFlags usage, int place) {
    VkBufferCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryRequirements r;
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ci.size = size;
    ci.usage = usage;
    if (pvkCreateBuffer(g_dev, &ci, NULL, &b->buf)) return 0;
    pvkGetBufferMemoryRequirements(g_dev, b->buf, &r);
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = mem_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (pvkAllocateMemory(g_dev, &ai, NULL, &b->mem)) {
        ai.memoryTypeIndex = mem_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
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
    return make_buffer_with_mapping(b, size, usage, 0);
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

static int dynamic_buffer_acquire(vk_buffer *buffer, uint32_t bytes, VkBufferUsageFlags usage)
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

    int result = make_buffer_with_mapping(buffer, bytes, usage, 2);
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

/* ---- COM objects ---- */
enum { K_DEVICE, K_CONTEXT, K_DXGI_DEVICE, K_ADAPTER, K_FACTORY, K_CHAIN, K_BUFFER, K_TEX2D, K_SRV, K_RTV, K_DSV, K_LAYOUT, K_VS, K_PS, K_BLEND, K_DEPTH, K_RASTER, K_SAMPLER, K_PREDICATE };

typedef struct {
    void **vtbl;
    volatile LONG ref;
    int kind;
} obj;

typedef struct {
    vk_buffer buffer;
    uint64_t last_used;
} oa_buffer_generation;

#define INLINE_BUFFER_GENERATIONS 8
typedef struct oa_buffer {
    obj o;
    uint32_t size, usage, bind, cpu, misc, stride;
    int ring;           /* dynamic constant buffer */
    uint8_t *shadow;
    uint32_t shadow_allocation,shadow_guest;
    VkBuffer ring_buf;
    uint32_t ring_off;
    uint64_t ring_frame;
    int dirty;
    struct oa_buffer **upload_owner;
    uint8_t *upload_data;
    uint32_t upload_written;
    int upload_mapped, shadow_mapped;
    uint64_t audit_version;
    oa_buffer_generation inline_gens[INLINE_BUFFER_GENERATIONS];
    oa_buffer_generation *gens;
    size_t ngens, gen_capacity, cur;
    UINT mapped_type;
} oa_buffer;

typedef struct {
    obj o;
    uint32_t desc[11];
    uint32_t w, h, depth, mips, layers, fmt, type3d;
    VkFormat vkfmt;
    VkImage image;
    VkDeviceMemory mem;
    int is_depth, has_stencil, block, bytes;
    uint8_t *cpu;       /* staging and dynamic textures */
    uint32_t cpu_size;
    int upload_mip;
    int is_backbuffer;
    VkImageView hdr_view;
} oa_tex;

typedef struct {
    obj o;
    oa_tex *tex;
    VkImageView view;
    VkFormat format;
    uint32_t desc[12], w, h, level, uid;
} oa_view;

typedef struct {
    uint32_t name_hash;
    char name[32];
    uint32_t index, fmt, slot, offset, cls, step;
} oa_elem;
typedef struct {
    obj o;
    int n;
    oa_elem e[32];
} oa_layout;

typedef struct {
    obj o;
    VkShaderModule module;
    oa_shader_info info;
    uint32_t hash;
    unsigned cart_particle;
    uint32_t af_textures[16];
} oa_shader;

typedef struct {
    obj o;
    uint32_t desc[20];
    VkSampler sampler;
    VkSampler af_sampler[2];
} oa_state;

/* An occlusion predicate. The result is read back on the CPU from a finished frame, so a hidden object costs no Vulkan work at all. */
typedef struct {
    obj o;
    int visible, acc;
    uint64_t tested_frame;
} oa_pred;

/* forward */
typedef struct oa_chain oa_chain;

typedef struct {
    obj o;
    int deferred;
    oa_buffer *vs_cb[14], *ps_cb[14];
    oa_view *vs_srv[32], *ps_srv[32];
    oa_state *vs_smp[16], *ps_smp[16];
    oa_shader *vs, *ps;
    oa_layout *layout;
    oa_state *blend, *depth, *raster;
    uint32_t stencil_ref;
    float blend_factor[4];
    uint32_t topology;
    oa_buffer *vb[16];
    uint32_t vb_stride[16], vb_off[16];
    oa_buffer *ib;
    uint32_t ib_fmt, ib_off;
    oa_view *rtv[8], *dsv;
    VkViewport vp;
    VkRect2D scissor;
    int have_vp, have_scissor;
    /* recording */
    int rendering, pipeline_valid;
    oa_pred *q_active, *pred;
    VkPipeline last_pipeline;
    const struct pipe_entry *selected_pipe;
    int pipeline_inputs_dirty;
    uint64_t material_epoch;
    uint64_t cached_material_epoch, cached_pool_epoch;
    unsigned cached_af_generation;

    const void *cached_program;
    VkDescriptorSet cached_material_set;
    uint32_t cached_uniform_count;
    VkDescriptorBufferInfo cached_uniforms[32];
    /* What the command buffer already has, so unchanged state costs no Vulkan call. Each call is expensive under x86 translation. */
    int cache_valid;
    VkViewport set_vp;
    VkRect2D set_scissor;
    uint32_t set_stencil;
    float set_blend[4];
    VkBuffer set_vb[4];
    VkDeviceSize set_off[4], set_stride[4];
    VkBuffer set_ib;
    uint32_t set_ib_off, set_ib_type;
    VkPipelineLayout set_layout;
    VkDescriptorSet set_desc;
    uint32_t set_ndyn, set_dyn[32];
} oa_context;

struct oa_chain {
    obj o;
    HWND hwnd;
    uint32_t w, h, fmt;
    oa_tex *back;
    VkSurfaceKHR surface;
    ANativeWindow *native_window;
    uint64_t window_generation;
    int surface_lost;
    OaPresentState present;
    int mailbox_requested, mode_logged, sync_logged;
    UINT original_sync, original_flags;
    VkPresentModeKHR present_mode;
    VkSurfaceTransformFlagBitsKHR configured_transform;
    uint64_t next_surface_check;
    unsigned suboptimal_reported;
    struct oa_chain *present_next;
    VkExtent2D extent;
    VkFormat swap_fmt;
};

static oa_chain *g_present_chains;
typedef struct oa_present_orphan {
    OaPresentGeneration *generations;
    VkSurfaceKHR surface;
    ANativeWindow *native_window;
    struct oa_present_orphan *next;
} oa_present_orphan;
static oa_present_orphan *g_present_orphans;
static unsigned g_present_orphan_count;
static uint64_t g_present_no_window;
enum { OA_MAX_RETAINED_WINDOWS=8 };
extern ANativeWindow *oa_android_present_lock(uint64_t *);
extern void oa_android_present_unlock(void);
static void present_chain_release(oa_chain *s);
static void present_gc_completed(uint64_t frame);
static void *g_vt[32];
static obj g_device_obj = {0};
static oa_context *g_ctx;

/* ---- generated stubs for methods not implemented ---- */
static const char *const DEVICE_NAMES[43] = {"QueryInterface", "AddRef", "Release", "CreateBuffer", "CreateTexture1D", "CreateTexture2D", "CreateTexture3D", "CreateShaderResourceView", "CreateUnorderedAccessView", "CreateRenderTargetView", "CreateDepthStencilView", "CreateInputLayout", "CreateVertexShader", "CreateGeometryShader", "CreateGeometryShaderWithStreamOutput", "CreatePixelShader", "CreateHullShader", "CreateDomainShader", "CreateComputeShader", "CreateClassLinkage", "CreateBlendState", "CreateDepthStencilState", "CreateRasterizerState", "CreateSamplerState", "CreateQuery", "CreatePredicate", "CreateCounter", "CreateDeferredContext", "OpenSharedResource", "CheckFormatSupport", "CheckMultisampleQualityLevels", "CheckCounterInfo", "CheckCounter", "CheckFeatureSupport", "GetPrivateData", "SetPrivateData", "SetPrivateDataInterface", "GetFeatureLevel", "GetCreationFlags", "GetDeviceRemovedReason", "GetImmediateContext", "SetExceptionMode", "GetExceptionMode"};
static const uint8_t DEVICE_ARGS[43] = {3,1,1,4,4,4,4,4,4,4,4,6,5,5,10,5,5,5,5,2,3,3,3,3,3,3,3,3,4,3,4,2,10,4,4,4,3,1,1,1,2,2,1};
static const uint8_t CONTEXT_ARGS[115] = {3,1,1,2,4,4,3,4,4,4,4,4,4,3,6,3,4,2,6,4,6,5,4,4,2,4,4,2,2,5,3,4,4,4,8,4,3,4,1,3,3,4,3,2,3,3,9,3,7,4,3,3,3,5,2,3,2,6,3,4,4,4,4,4,4,4,4,4,5,4,4,4,4,4,4,4,4,4,2,6,4,4,4,2,4,4,3,4,4,4,7,4,3,3,2,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,1,1,1,1,3};

typedef struct { char label[48]; } stub_info;
static stub_info g_stubs[512];
static volatile LONG g_stub_seen[512];
static int g_nstubs;
static uint8_t *g_stub_code;

static void __cdecl stub_hit(int i) {
    if (InterlockedExchange(&g_stub_seen[i], 1) == 0) logf_("NOT IMPLEMENTED %s", g_stubs[i].label);
}

static uintptr_t oa_missing_0(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[0].label);}
static uintptr_t oa_missing_1(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[1].label);}
static uintptr_t oa_missing_2(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[2].label);}
static uintptr_t oa_missing_3(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[3].label);}
static uintptr_t oa_missing_4(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[4].label);}
static uintptr_t oa_missing_5(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[5].label);}
static uintptr_t oa_missing_6(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[6].label);}
static uintptr_t oa_missing_7(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[7].label);}
static uintptr_t oa_missing_8(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[8].label);}
static uintptr_t oa_missing_9(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[9].label);}
static uintptr_t oa_missing_10(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[10].label);}
static uintptr_t oa_missing_11(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[11].label);}
static uintptr_t oa_missing_12(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[12].label);}
static uintptr_t oa_missing_13(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[13].label);}
static uintptr_t oa_missing_14(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[14].label);}
static uintptr_t oa_missing_15(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[15].label);}
static uintptr_t oa_missing_16(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[16].label);}
static uintptr_t oa_missing_17(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[17].label);}
static uintptr_t oa_missing_18(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[18].label);}
static uintptr_t oa_missing_19(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[19].label);}
static uintptr_t oa_missing_20(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[20].label);}
static uintptr_t oa_missing_21(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[21].label);}
static uintptr_t oa_missing_22(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[22].label);}
static uintptr_t oa_missing_23(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[23].label);}
static uintptr_t oa_missing_24(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[24].label);}
static uintptr_t oa_missing_25(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[25].label);}
static uintptr_t oa_missing_26(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[26].label);}
static uintptr_t oa_missing_27(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[27].label);}
static uintptr_t oa_missing_28(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[28].label);}
static uintptr_t oa_missing_29(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[29].label);}
static uintptr_t oa_missing_30(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[30].label);}
static uintptr_t oa_missing_31(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[31].label);}
static uintptr_t oa_missing_32(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[32].label);}
static uintptr_t oa_missing_33(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[33].label);}
static uintptr_t oa_missing_34(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[34].label);}
static uintptr_t oa_missing_35(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[35].label);}
static uintptr_t oa_missing_36(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[36].label);}
static uintptr_t oa_missing_37(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[37].label);}
static uintptr_t oa_missing_38(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[38].label);}
static uintptr_t oa_missing_39(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[39].label);}
static uintptr_t oa_missing_40(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[40].label);}
static uintptr_t oa_missing_41(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[41].label);}
static uintptr_t oa_missing_42(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[42].label);}
static uintptr_t oa_missing_43(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[43].label);}
static uintptr_t oa_missing_44(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[44].label);}
static uintptr_t oa_missing_45(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[45].label);}
static uintptr_t oa_missing_46(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[46].label);}
static uintptr_t oa_missing_47(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[47].label);}
static uintptr_t oa_missing_48(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[48].label);}
static uintptr_t oa_missing_49(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[49].label);}
static uintptr_t oa_missing_50(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[50].label);}
static uintptr_t oa_missing_51(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[51].label);}
static uintptr_t oa_missing_52(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[52].label);}
static uintptr_t oa_missing_53(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[53].label);}
static uintptr_t oa_missing_54(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[54].label);}
static uintptr_t oa_missing_55(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[55].label);}
static uintptr_t oa_missing_56(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[56].label);}
static uintptr_t oa_missing_57(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[57].label);}
static uintptr_t oa_missing_58(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[58].label);}
static uintptr_t oa_missing_59(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[59].label);}
static uintptr_t oa_missing_60(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[60].label);}
static uintptr_t oa_missing_61(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[61].label);}
static uintptr_t oa_missing_62(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[62].label);}
static uintptr_t oa_missing_63(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[63].label);}
static uintptr_t oa_missing_64(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[64].label);}
static uintptr_t oa_missing_65(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[65].label);}
static uintptr_t oa_missing_66(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[66].label);}
static uintptr_t oa_missing_67(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[67].label);}
static uintptr_t oa_missing_68(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[68].label);}
static uintptr_t oa_missing_69(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[69].label);}
static uintptr_t oa_missing_70(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[70].label);}
static uintptr_t oa_missing_71(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[71].label);}
static uintptr_t oa_missing_72(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[72].label);}
static uintptr_t oa_missing_73(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[73].label);}
static uintptr_t oa_missing_74(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[74].label);}
static uintptr_t oa_missing_75(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[75].label);}
static uintptr_t oa_missing_76(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[76].label);}
static uintptr_t oa_missing_77(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[77].label);}
static uintptr_t oa_missing_78(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[78].label);}
static uintptr_t oa_missing_79(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[79].label);}
static uintptr_t oa_missing_80(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[80].label);}
static uintptr_t oa_missing_81(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[81].label);}
static uintptr_t oa_missing_82(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[82].label);}
static uintptr_t oa_missing_83(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[83].label);}
static uintptr_t oa_missing_84(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[84].label);}
static uintptr_t oa_missing_85(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[85].label);}
static uintptr_t oa_missing_86(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[86].label);}
static uintptr_t oa_missing_87(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[87].label);}
static uintptr_t oa_missing_88(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[88].label);}
static uintptr_t oa_missing_89(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[89].label);}
static uintptr_t oa_missing_90(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[90].label);}
static uintptr_t oa_missing_91(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[91].label);}
static uintptr_t oa_missing_92(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[92].label);}
static uintptr_t oa_missing_93(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[93].label);}
static uintptr_t oa_missing_94(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[94].label);}
static uintptr_t oa_missing_95(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[95].label);}
static uintptr_t oa_missing_96(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[96].label);}
static uintptr_t oa_missing_97(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[97].label);}
static uintptr_t oa_missing_98(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[98].label);}
static uintptr_t oa_missing_99(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[99].label);}
static uintptr_t oa_missing_100(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[100].label);}
static uintptr_t oa_missing_101(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[101].label);}
static uintptr_t oa_missing_102(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[102].label);}
static uintptr_t oa_missing_103(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[103].label);}
static uintptr_t oa_missing_104(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[104].label);}
static uintptr_t oa_missing_105(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[105].label);}
static uintptr_t oa_missing_106(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[106].label);}
static uintptr_t oa_missing_107(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[107].label);}
static uintptr_t oa_missing_108(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[108].label);}
static uintptr_t oa_missing_109(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[109].label);}
static uintptr_t oa_missing_110(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[110].label);}
static uintptr_t oa_missing_111(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[111].label);}
static uintptr_t oa_missing_112(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[112].label);}
static uintptr_t oa_missing_113(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[113].label);}
static uintptr_t oa_missing_114(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[114].label);}
static uintptr_t oa_missing_115(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[115].label);}
static uintptr_t oa_missing_116(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[116].label);}
static uintptr_t oa_missing_117(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[117].label);}
static uintptr_t oa_missing_118(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[118].label);}
static uintptr_t oa_missing_119(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[119].label);}
static uintptr_t oa_missing_120(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[120].label);}
static uintptr_t oa_missing_121(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[121].label);}
static uintptr_t oa_missing_122(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[122].label);}
static uintptr_t oa_missing_123(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[123].label);}
static uintptr_t oa_missing_124(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[124].label);}
static uintptr_t oa_missing_125(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[125].label);}
static uintptr_t oa_missing_126(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[126].label);}
static uintptr_t oa_missing_127(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[127].label);}
static uintptr_t oa_missing_128(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[128].label);}
static uintptr_t oa_missing_129(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[129].label);}
static uintptr_t oa_missing_130(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[130].label);}
static uintptr_t oa_missing_131(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[131].label);}
static uintptr_t oa_missing_132(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[132].label);}
static uintptr_t oa_missing_133(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[133].label);}
static uintptr_t oa_missing_134(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[134].label);}
static uintptr_t oa_missing_135(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[135].label);}
static uintptr_t oa_missing_136(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[136].label);}
static uintptr_t oa_missing_137(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[137].label);}
static uintptr_t oa_missing_138(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[138].label);}
static uintptr_t oa_missing_139(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[139].label);}
static uintptr_t oa_missing_140(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[140].label);}
static uintptr_t oa_missing_141(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[141].label);}
static uintptr_t oa_missing_142(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[142].label);}
static uintptr_t oa_missing_143(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[143].label);}
static uintptr_t oa_missing_144(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[144].label);}
static uintptr_t oa_missing_145(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[145].label);}
static uintptr_t oa_missing_146(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[146].label);}
static uintptr_t oa_missing_147(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[147].label);}
static uintptr_t oa_missing_148(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[148].label);}
static uintptr_t oa_missing_149(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[149].label);}
static uintptr_t oa_missing_150(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[150].label);}
static uintptr_t oa_missing_151(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[151].label);}
static uintptr_t oa_missing_152(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[152].label);}
static uintptr_t oa_missing_153(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[153].label);}
static uintptr_t oa_missing_154(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[154].label);}
static uintptr_t oa_missing_155(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[155].label);}
static uintptr_t oa_missing_156(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[156].label);}
static uintptr_t oa_missing_157(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[157].label);}
static uintptr_t oa_missing_158(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[158].label);}
static uintptr_t oa_missing_159(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[159].label);}
static uintptr_t oa_missing_160(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[160].label);}
static uintptr_t oa_missing_161(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[161].label);}
static uintptr_t oa_missing_162(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[162].label);}
static uintptr_t oa_missing_163(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[163].label);}
static uintptr_t oa_missing_164(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[164].label);}
static uintptr_t oa_missing_165(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[165].label);}
static uintptr_t oa_missing_166(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[166].label);}
static uintptr_t oa_missing_167(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[167].label);}
static uintptr_t oa_missing_168(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[168].label);}
static uintptr_t oa_missing_169(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[169].label);}
static uintptr_t oa_missing_170(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[170].label);}
static uintptr_t oa_missing_171(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[171].label);}
static uintptr_t oa_missing_172(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[172].label);}
static uintptr_t oa_missing_173(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[173].label);}
static uintptr_t oa_missing_174(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[174].label);}
static uintptr_t oa_missing_175(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[175].label);}
static uintptr_t oa_missing_176(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[176].label);}
static uintptr_t oa_missing_177(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[177].label);}
static uintptr_t oa_missing_178(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[178].label);}
static uintptr_t oa_missing_179(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[179].label);}
static uintptr_t oa_missing_180(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[180].label);}
static uintptr_t oa_missing_181(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[181].label);}
static uintptr_t oa_missing_182(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[182].label);}
static uintptr_t oa_missing_183(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[183].label);}
static uintptr_t oa_missing_184(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[184].label);}
static uintptr_t oa_missing_185(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[185].label);}
static uintptr_t oa_missing_186(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[186].label);}
static uintptr_t oa_missing_187(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[187].label);}
static uintptr_t oa_missing_188(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[188].label);}
static uintptr_t oa_missing_189(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[189].label);}
static uintptr_t oa_missing_190(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[190].label);}
static uintptr_t oa_missing_191(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[191].label);}
static uintptr_t oa_missing_192(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[192].label);}
static uintptr_t oa_missing_193(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[193].label);}
static uintptr_t oa_missing_194(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[194].label);}
static uintptr_t oa_missing_195(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[195].label);}
static uintptr_t oa_missing_196(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[196].label);}
static uintptr_t oa_missing_197(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[197].label);}
static uintptr_t oa_missing_198(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[198].label);}
static uintptr_t oa_missing_199(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[199].label);}
static uintptr_t oa_missing_200(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[200].label);}
static uintptr_t oa_missing_201(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[201].label);}
static uintptr_t oa_missing_202(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[202].label);}
static uintptr_t oa_missing_203(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[203].label);}
static uintptr_t oa_missing_204(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[204].label);}
static uintptr_t oa_missing_205(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[205].label);}
static uintptr_t oa_missing_206(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[206].label);}
static uintptr_t oa_missing_207(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[207].label);}
static uintptr_t oa_missing_208(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[208].label);}
static uintptr_t oa_missing_209(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[209].label);}
static uintptr_t oa_missing_210(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[210].label);}
static uintptr_t oa_missing_211(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[211].label);}
static uintptr_t oa_missing_212(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[212].label);}
static uintptr_t oa_missing_213(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[213].label);}
static uintptr_t oa_missing_214(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[214].label);}
static uintptr_t oa_missing_215(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[215].label);}
static uintptr_t oa_missing_216(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[216].label);}
static uintptr_t oa_missing_217(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[217].label);}
static uintptr_t oa_missing_218(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[218].label);}
static uintptr_t oa_missing_219(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[219].label);}
static uintptr_t oa_missing_220(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[220].label);}
static uintptr_t oa_missing_221(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[221].label);}
static uintptr_t oa_missing_222(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[222].label);}
static uintptr_t oa_missing_223(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[223].label);}
static uintptr_t oa_missing_224(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[224].label);}
static uintptr_t oa_missing_225(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[225].label);}
static uintptr_t oa_missing_226(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[226].label);}
static uintptr_t oa_missing_227(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[227].label);}
static uintptr_t oa_missing_228(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[228].label);}
static uintptr_t oa_missing_229(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[229].label);}
static uintptr_t oa_missing_230(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[230].label);}
static uintptr_t oa_missing_231(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[231].label);}
static uintptr_t oa_missing_232(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[232].label);}
static uintptr_t oa_missing_233(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[233].label);}
static uintptr_t oa_missing_234(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[234].label);}
static uintptr_t oa_missing_235(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[235].label);}
static uintptr_t oa_missing_236(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[236].label);}
static uintptr_t oa_missing_237(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[237].label);}
static uintptr_t oa_missing_238(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[238].label);}
static uintptr_t oa_missing_239(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[239].label);}
static uintptr_t oa_missing_240(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[240].label);}
static uintptr_t oa_missing_241(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[241].label);}
static uintptr_t oa_missing_242(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[242].label);}
static uintptr_t oa_missing_243(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[243].label);}
static uintptr_t oa_missing_244(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[244].label);}
static uintptr_t oa_missing_245(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[245].label);}
static uintptr_t oa_missing_246(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[246].label);}
static uintptr_t oa_missing_247(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[247].label);}
static uintptr_t oa_missing_248(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[248].label);}
static uintptr_t oa_missing_249(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[249].label);}
static uintptr_t oa_missing_250(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[250].label);}
static uintptr_t oa_missing_251(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[251].label);}
static uintptr_t oa_missing_252(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[252].label);}
static uintptr_t oa_missing_253(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[253].label);}
static uintptr_t oa_missing_254(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[254].label);}
static uintptr_t oa_missing_255(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[255].label);}
static uintptr_t oa_missing_256(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[256].label);}
static uintptr_t oa_missing_257(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[257].label);}
static uintptr_t oa_missing_258(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[258].label);}
static uintptr_t oa_missing_259(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[259].label);}
static uintptr_t oa_missing_260(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[260].label);}
static uintptr_t oa_missing_261(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[261].label);}
static uintptr_t oa_missing_262(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[262].label);}
static uintptr_t oa_missing_263(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[263].label);}
static uintptr_t oa_missing_264(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[264].label);}
static uintptr_t oa_missing_265(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[265].label);}
static uintptr_t oa_missing_266(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[266].label);}
static uintptr_t oa_missing_267(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[267].label);}
static uintptr_t oa_missing_268(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[268].label);}
static uintptr_t oa_missing_269(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[269].label);}
static uintptr_t oa_missing_270(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[270].label);}
static uintptr_t oa_missing_271(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[271].label);}
static uintptr_t oa_missing_272(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[272].label);}
static uintptr_t oa_missing_273(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[273].label);}
static uintptr_t oa_missing_274(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[274].label);}
static uintptr_t oa_missing_275(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[275].label);}
static uintptr_t oa_missing_276(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[276].label);}
static uintptr_t oa_missing_277(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[277].label);}
static uintptr_t oa_missing_278(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[278].label);}
static uintptr_t oa_missing_279(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[279].label);}
static uintptr_t oa_missing_280(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[280].label);}
static uintptr_t oa_missing_281(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[281].label);}
static uintptr_t oa_missing_282(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[282].label);}
static uintptr_t oa_missing_283(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[283].label);}
static uintptr_t oa_missing_284(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[284].label);}
static uintptr_t oa_missing_285(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[285].label);}
static uintptr_t oa_missing_286(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[286].label);}
static uintptr_t oa_missing_287(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[287].label);}
static uintptr_t oa_missing_288(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[288].label);}
static uintptr_t oa_missing_289(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[289].label);}
static uintptr_t oa_missing_290(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[290].label);}
static uintptr_t oa_missing_291(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[291].label);}
static uintptr_t oa_missing_292(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[292].label);}
static uintptr_t oa_missing_293(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[293].label);}
static uintptr_t oa_missing_294(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[294].label);}
static uintptr_t oa_missing_295(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[295].label);}
static uintptr_t oa_missing_296(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[296].label);}
static uintptr_t oa_missing_297(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[297].label);}
static uintptr_t oa_missing_298(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[298].label);}
static uintptr_t oa_missing_299(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[299].label);}
static uintptr_t oa_missing_300(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[300].label);}
static uintptr_t oa_missing_301(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[301].label);}
static uintptr_t oa_missing_302(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[302].label);}
static uintptr_t oa_missing_303(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[303].label);}
static uintptr_t oa_missing_304(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[304].label);}
static uintptr_t oa_missing_305(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[305].label);}
static uintptr_t oa_missing_306(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[306].label);}
static uintptr_t oa_missing_307(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[307].label);}
static uintptr_t oa_missing_308(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[308].label);}
static uintptr_t oa_missing_309(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[309].label);}
static uintptr_t oa_missing_310(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[310].label);}
static uintptr_t oa_missing_311(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[311].label);}
static uintptr_t oa_missing_312(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[312].label);}
static uintptr_t oa_missing_313(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[313].label);}
static uintptr_t oa_missing_314(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[314].label);}
static uintptr_t oa_missing_315(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[315].label);}
static uintptr_t oa_missing_316(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[316].label);}
static uintptr_t oa_missing_317(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[317].label);}
static uintptr_t oa_missing_318(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[318].label);}
static uintptr_t oa_missing_319(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[319].label);}
static uintptr_t oa_missing_320(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[320].label);}
static uintptr_t oa_missing_321(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[321].label);}
static uintptr_t oa_missing_322(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[322].label);}
static uintptr_t oa_missing_323(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[323].label);}
static uintptr_t oa_missing_324(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[324].label);}
static uintptr_t oa_missing_325(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[325].label);}
static uintptr_t oa_missing_326(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[326].label);}
static uintptr_t oa_missing_327(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[327].label);}
static uintptr_t oa_missing_328(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[328].label);}
static uintptr_t oa_missing_329(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[329].label);}
static uintptr_t oa_missing_330(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[330].label);}
static uintptr_t oa_missing_331(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[331].label);}
static uintptr_t oa_missing_332(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[332].label);}
static uintptr_t oa_missing_333(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[333].label);}
static uintptr_t oa_missing_334(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[334].label);}
static uintptr_t oa_missing_335(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[335].label);}
static uintptr_t oa_missing_336(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[336].label);}
static uintptr_t oa_missing_337(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[337].label);}
static uintptr_t oa_missing_338(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[338].label);}
static uintptr_t oa_missing_339(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[339].label);}
static uintptr_t oa_missing_340(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[340].label);}
static uintptr_t oa_missing_341(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[341].label);}
static uintptr_t oa_missing_342(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[342].label);}
static uintptr_t oa_missing_343(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[343].label);}
static uintptr_t oa_missing_344(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[344].label);}
static uintptr_t oa_missing_345(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[345].label);}
static uintptr_t oa_missing_346(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[346].label);}
static uintptr_t oa_missing_347(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[347].label);}
static uintptr_t oa_missing_348(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[348].label);}
static uintptr_t oa_missing_349(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[349].label);}
static uintptr_t oa_missing_350(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[350].label);}
static uintptr_t oa_missing_351(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[351].label);}
static uintptr_t oa_missing_352(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[352].label);}
static uintptr_t oa_missing_353(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[353].label);}
static uintptr_t oa_missing_354(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[354].label);}
static uintptr_t oa_missing_355(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[355].label);}
static uintptr_t oa_missing_356(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[356].label);}
static uintptr_t oa_missing_357(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[357].label);}
static uintptr_t oa_missing_358(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[358].label);}
static uintptr_t oa_missing_359(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[359].label);}
static uintptr_t oa_missing_360(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[360].label);}
static uintptr_t oa_missing_361(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[361].label);}
static uintptr_t oa_missing_362(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[362].label);}
static uintptr_t oa_missing_363(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[363].label);}
static uintptr_t oa_missing_364(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[364].label);}
static uintptr_t oa_missing_365(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[365].label);}
static uintptr_t oa_missing_366(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[366].label);}
static uintptr_t oa_missing_367(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[367].label);}
static uintptr_t oa_missing_368(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[368].label);}
static uintptr_t oa_missing_369(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[369].label);}
static uintptr_t oa_missing_370(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[370].label);}
static uintptr_t oa_missing_371(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[371].label);}
static uintptr_t oa_missing_372(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[372].label);}
static uintptr_t oa_missing_373(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[373].label);}
static uintptr_t oa_missing_374(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[374].label);}
static uintptr_t oa_missing_375(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[375].label);}
static uintptr_t oa_missing_376(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[376].label);}
static uintptr_t oa_missing_377(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[377].label);}
static uintptr_t oa_missing_378(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[378].label);}
static uintptr_t oa_missing_379(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[379].label);}
static uintptr_t oa_missing_380(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[380].label);}
static uintptr_t oa_missing_381(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[381].label);}
static uintptr_t oa_missing_382(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[382].label);}
static uintptr_t oa_missing_383(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[383].label);}
static uintptr_t oa_missing_384(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[384].label);}
static uintptr_t oa_missing_385(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[385].label);}
static uintptr_t oa_missing_386(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[386].label);}
static uintptr_t oa_missing_387(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[387].label);}
static uintptr_t oa_missing_388(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[388].label);}
static uintptr_t oa_missing_389(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[389].label);}
static uintptr_t oa_missing_390(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[390].label);}
static uintptr_t oa_missing_391(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[391].label);}
static uintptr_t oa_missing_392(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[392].label);}
static uintptr_t oa_missing_393(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[393].label);}
static uintptr_t oa_missing_394(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[394].label);}
static uintptr_t oa_missing_395(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[395].label);}
static uintptr_t oa_missing_396(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[396].label);}
static uintptr_t oa_missing_397(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[397].label);}
static uintptr_t oa_missing_398(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[398].label);}
static uintptr_t oa_missing_399(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[399].label);}
static uintptr_t oa_missing_400(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[400].label);}
static uintptr_t oa_missing_401(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[401].label);}
static uintptr_t oa_missing_402(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[402].label);}
static uintptr_t oa_missing_403(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[403].label);}
static uintptr_t oa_missing_404(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[404].label);}
static uintptr_t oa_missing_405(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[405].label);}
static uintptr_t oa_missing_406(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[406].label);}
static uintptr_t oa_missing_407(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[407].label);}
static uintptr_t oa_missing_408(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[408].label);}
static uintptr_t oa_missing_409(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[409].label);}
static uintptr_t oa_missing_410(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[410].label);}
static uintptr_t oa_missing_411(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[411].label);}
static uintptr_t oa_missing_412(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[412].label);}
static uintptr_t oa_missing_413(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[413].label);}
static uintptr_t oa_missing_414(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[414].label);}
static uintptr_t oa_missing_415(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[415].label);}
static uintptr_t oa_missing_416(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[416].label);}
static uintptr_t oa_missing_417(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[417].label);}
static uintptr_t oa_missing_418(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[418].label);}
static uintptr_t oa_missing_419(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[419].label);}
static uintptr_t oa_missing_420(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[420].label);}
static uintptr_t oa_missing_421(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[421].label);}
static uintptr_t oa_missing_422(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[422].label);}
static uintptr_t oa_missing_423(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[423].label);}
static uintptr_t oa_missing_424(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[424].label);}
static uintptr_t oa_missing_425(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[425].label);}
static uintptr_t oa_missing_426(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[426].label);}
static uintptr_t oa_missing_427(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[427].label);}
static uintptr_t oa_missing_428(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[428].label);}
static uintptr_t oa_missing_429(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[429].label);}
static uintptr_t oa_missing_430(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[430].label);}
static uintptr_t oa_missing_431(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[431].label);}
static uintptr_t oa_missing_432(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[432].label);}
static uintptr_t oa_missing_433(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[433].label);}
static uintptr_t oa_missing_434(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[434].label);}
static uintptr_t oa_missing_435(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[435].label);}
static uintptr_t oa_missing_436(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[436].label);}
static uintptr_t oa_missing_437(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[437].label);}
static uintptr_t oa_missing_438(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[438].label);}
static uintptr_t oa_missing_439(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[439].label);}
static uintptr_t oa_missing_440(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[440].label);}
static uintptr_t oa_missing_441(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[441].label);}
static uintptr_t oa_missing_442(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[442].label);}
static uintptr_t oa_missing_443(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[443].label);}
static uintptr_t oa_missing_444(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[444].label);}
static uintptr_t oa_missing_445(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[445].label);}
static uintptr_t oa_missing_446(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[446].label);}
static uintptr_t oa_missing_447(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[447].label);}
static uintptr_t oa_missing_448(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[448].label);}
static uintptr_t oa_missing_449(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[449].label);}
static uintptr_t oa_missing_450(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[450].label);}
static uintptr_t oa_missing_451(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[451].label);}
static uintptr_t oa_missing_452(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[452].label);}
static uintptr_t oa_missing_453(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[453].label);}
static uintptr_t oa_missing_454(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[454].label);}
static uintptr_t oa_missing_455(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[455].label);}
static uintptr_t oa_missing_456(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[456].label);}
static uintptr_t oa_missing_457(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[457].label);}
static uintptr_t oa_missing_458(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[458].label);}
static uintptr_t oa_missing_459(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[459].label);}
static uintptr_t oa_missing_460(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[460].label);}
static uintptr_t oa_missing_461(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[461].label);}
static uintptr_t oa_missing_462(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[462].label);}
static uintptr_t oa_missing_463(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[463].label);}
static uintptr_t oa_missing_464(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[464].label);}
static uintptr_t oa_missing_465(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[465].label);}
static uintptr_t oa_missing_466(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[466].label);}
static uintptr_t oa_missing_467(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[467].label);}
static uintptr_t oa_missing_468(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[468].label);}
static uintptr_t oa_missing_469(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[469].label);}
static uintptr_t oa_missing_470(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[470].label);}
static uintptr_t oa_missing_471(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[471].label);}
static uintptr_t oa_missing_472(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[472].label);}
static uintptr_t oa_missing_473(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[473].label);}
static uintptr_t oa_missing_474(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[474].label);}
static uintptr_t oa_missing_475(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[475].label);}
static uintptr_t oa_missing_476(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[476].label);}
static uintptr_t oa_missing_477(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[477].label);}
static uintptr_t oa_missing_478(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[478].label);}
static uintptr_t oa_missing_479(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[479].label);}
static uintptr_t oa_missing_480(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[480].label);}
static uintptr_t oa_missing_481(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[481].label);}
static uintptr_t oa_missing_482(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[482].label);}
static uintptr_t oa_missing_483(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[483].label);}
static uintptr_t oa_missing_484(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[484].label);}
static uintptr_t oa_missing_485(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[485].label);}
static uintptr_t oa_missing_486(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[486].label);}
static uintptr_t oa_missing_487(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[487].label);}
static uintptr_t oa_missing_488(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[488].label);}
static uintptr_t oa_missing_489(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[489].label);}
static uintptr_t oa_missing_490(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[490].label);}
static uintptr_t oa_missing_491(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[491].label);}
static uintptr_t oa_missing_492(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[492].label);}
static uintptr_t oa_missing_493(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[493].label);}
static uintptr_t oa_missing_494(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[494].label);}
static uintptr_t oa_missing_495(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[495].label);}
static uintptr_t oa_missing_496(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[496].label);}
static uintptr_t oa_missing_497(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[497].label);}
static uintptr_t oa_missing_498(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[498].label);}
static uintptr_t oa_missing_499(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[499].label);}
static uintptr_t oa_missing_500(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[500].label);}
static uintptr_t oa_missing_501(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[501].label);}
static uintptr_t oa_missing_502(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[502].label);}
static uintptr_t oa_missing_503(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[503].label);}
static uintptr_t oa_missing_504(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[504].label);}
static uintptr_t oa_missing_505(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[505].label);}
static uintptr_t oa_missing_506(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[506].label);}
static uintptr_t oa_missing_507(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[507].label);}
static uintptr_t oa_missing_508(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[508].label);}
static uintptr_t oa_missing_509(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[509].label);}
static uintptr_t oa_missing_510(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[510].label);}
static uintptr_t oa_missing_511(void*self,...){(void)self;rt_fatal("native Vulkan backend missing %s",g_stubs[511].label);}
static void*oa_missing_table[512]={(void*)oa_missing_0,(void*)oa_missing_1,(void*)oa_missing_2,(void*)oa_missing_3,(void*)oa_missing_4,(void*)oa_missing_5,(void*)oa_missing_6,(void*)oa_missing_7,(void*)oa_missing_8,(void*)oa_missing_9,(void*)oa_missing_10,(void*)oa_missing_11,(void*)oa_missing_12,(void*)oa_missing_13,(void*)oa_missing_14,(void*)oa_missing_15,(void*)oa_missing_16,(void*)oa_missing_17,(void*)oa_missing_18,(void*)oa_missing_19,(void*)oa_missing_20,(void*)oa_missing_21,(void*)oa_missing_22,(void*)oa_missing_23,(void*)oa_missing_24,(void*)oa_missing_25,(void*)oa_missing_26,(void*)oa_missing_27,(void*)oa_missing_28,(void*)oa_missing_29,(void*)oa_missing_30,(void*)oa_missing_31,(void*)oa_missing_32,(void*)oa_missing_33,(void*)oa_missing_34,(void*)oa_missing_35,(void*)oa_missing_36,(void*)oa_missing_37,(void*)oa_missing_38,(void*)oa_missing_39,(void*)oa_missing_40,(void*)oa_missing_41,(void*)oa_missing_42,(void*)oa_missing_43,(void*)oa_missing_44,(void*)oa_missing_45,(void*)oa_missing_46,(void*)oa_missing_47,(void*)oa_missing_48,(void*)oa_missing_49,(void*)oa_missing_50,(void*)oa_missing_51,(void*)oa_missing_52,(void*)oa_missing_53,(void*)oa_missing_54,(void*)oa_missing_55,(void*)oa_missing_56,(void*)oa_missing_57,(void*)oa_missing_58,(void*)oa_missing_59,(void*)oa_missing_60,(void*)oa_missing_61,(void*)oa_missing_62,(void*)oa_missing_63,(void*)oa_missing_64,(void*)oa_missing_65,(void*)oa_missing_66,(void*)oa_missing_67,(void*)oa_missing_68,(void*)oa_missing_69,(void*)oa_missing_70,(void*)oa_missing_71,(void*)oa_missing_72,(void*)oa_missing_73,(void*)oa_missing_74,(void*)oa_missing_75,(void*)oa_missing_76,(void*)oa_missing_77,(void*)oa_missing_78,(void*)oa_missing_79,(void*)oa_missing_80,(void*)oa_missing_81,(void*)oa_missing_82,(void*)oa_missing_83,(void*)oa_missing_84,(void*)oa_missing_85,(void*)oa_missing_86,(void*)oa_missing_87,(void*)oa_missing_88,(void*)oa_missing_89,(void*)oa_missing_90,(void*)oa_missing_91,(void*)oa_missing_92,(void*)oa_missing_93,(void*)oa_missing_94,(void*)oa_missing_95,(void*)oa_missing_96,(void*)oa_missing_97,(void*)oa_missing_98,(void*)oa_missing_99,(void*)oa_missing_100,(void*)oa_missing_101,(void*)oa_missing_102,(void*)oa_missing_103,(void*)oa_missing_104,(void*)oa_missing_105,(void*)oa_missing_106,(void*)oa_missing_107,(void*)oa_missing_108,(void*)oa_missing_109,(void*)oa_missing_110,(void*)oa_missing_111,(void*)oa_missing_112,(void*)oa_missing_113,(void*)oa_missing_114,(void*)oa_missing_115,(void*)oa_missing_116,(void*)oa_missing_117,(void*)oa_missing_118,(void*)oa_missing_119,(void*)oa_missing_120,(void*)oa_missing_121,(void*)oa_missing_122,(void*)oa_missing_123,(void*)oa_missing_124,(void*)oa_missing_125,(void*)oa_missing_126,(void*)oa_missing_127,(void*)oa_missing_128,(void*)oa_missing_129,(void*)oa_missing_130,(void*)oa_missing_131,(void*)oa_missing_132,(void*)oa_missing_133,(void*)oa_missing_134,(void*)oa_missing_135,(void*)oa_missing_136,(void*)oa_missing_137,(void*)oa_missing_138,(void*)oa_missing_139,(void*)oa_missing_140,(void*)oa_missing_141,(void*)oa_missing_142,(void*)oa_missing_143,(void*)oa_missing_144,(void*)oa_missing_145,(void*)oa_missing_146,(void*)oa_missing_147,(void*)oa_missing_148,(void*)oa_missing_149,(void*)oa_missing_150,(void*)oa_missing_151,(void*)oa_missing_152,(void*)oa_missing_153,(void*)oa_missing_154,(void*)oa_missing_155,(void*)oa_missing_156,(void*)oa_missing_157,(void*)oa_missing_158,(void*)oa_missing_159,(void*)oa_missing_160,(void*)oa_missing_161,(void*)oa_missing_162,(void*)oa_missing_163,(void*)oa_missing_164,(void*)oa_missing_165,(void*)oa_missing_166,(void*)oa_missing_167,(void*)oa_missing_168,(void*)oa_missing_169,(void*)oa_missing_170,(void*)oa_missing_171,(void*)oa_missing_172,(void*)oa_missing_173,(void*)oa_missing_174,(void*)oa_missing_175,(void*)oa_missing_176,(void*)oa_missing_177,(void*)oa_missing_178,(void*)oa_missing_179,(void*)oa_missing_180,(void*)oa_missing_181,(void*)oa_missing_182,(void*)oa_missing_183,(void*)oa_missing_184,(void*)oa_missing_185,(void*)oa_missing_186,(void*)oa_missing_187,(void*)oa_missing_188,(void*)oa_missing_189,(void*)oa_missing_190,(void*)oa_missing_191,(void*)oa_missing_192,(void*)oa_missing_193,(void*)oa_missing_194,(void*)oa_missing_195,(void*)oa_missing_196,(void*)oa_missing_197,(void*)oa_missing_198,(void*)oa_missing_199,(void*)oa_missing_200,(void*)oa_missing_201,(void*)oa_missing_202,(void*)oa_missing_203,(void*)oa_missing_204,(void*)oa_missing_205,(void*)oa_missing_206,(void*)oa_missing_207,(void*)oa_missing_208,(void*)oa_missing_209,(void*)oa_missing_210,(void*)oa_missing_211,(void*)oa_missing_212,(void*)oa_missing_213,(void*)oa_missing_214,(void*)oa_missing_215,(void*)oa_missing_216,(void*)oa_missing_217,(void*)oa_missing_218,(void*)oa_missing_219,(void*)oa_missing_220,(void*)oa_missing_221,(void*)oa_missing_222,(void*)oa_missing_223,(void*)oa_missing_224,(void*)oa_missing_225,(void*)oa_missing_226,(void*)oa_missing_227,(void*)oa_missing_228,(void*)oa_missing_229,(void*)oa_missing_230,(void*)oa_missing_231,(void*)oa_missing_232,(void*)oa_missing_233,(void*)oa_missing_234,(void*)oa_missing_235,(void*)oa_missing_236,(void*)oa_missing_237,(void*)oa_missing_238,(void*)oa_missing_239,(void*)oa_missing_240,(void*)oa_missing_241,(void*)oa_missing_242,(void*)oa_missing_243,(void*)oa_missing_244,(void*)oa_missing_245,(void*)oa_missing_246,(void*)oa_missing_247,(void*)oa_missing_248,(void*)oa_missing_249,(void*)oa_missing_250,(void*)oa_missing_251,(void*)oa_missing_252,(void*)oa_missing_253,(void*)oa_missing_254,(void*)oa_missing_255,(void*)oa_missing_256,(void*)oa_missing_257,(void*)oa_missing_258,(void*)oa_missing_259,(void*)oa_missing_260,(void*)oa_missing_261,(void*)oa_missing_262,(void*)oa_missing_263,(void*)oa_missing_264,(void*)oa_missing_265,(void*)oa_missing_266,(void*)oa_missing_267,(void*)oa_missing_268,(void*)oa_missing_269,(void*)oa_missing_270,(void*)oa_missing_271,(void*)oa_missing_272,(void*)oa_missing_273,(void*)oa_missing_274,(void*)oa_missing_275,(void*)oa_missing_276,(void*)oa_missing_277,(void*)oa_missing_278,(void*)oa_missing_279,(void*)oa_missing_280,(void*)oa_missing_281,(void*)oa_missing_282,(void*)oa_missing_283,(void*)oa_missing_284,(void*)oa_missing_285,(void*)oa_missing_286,(void*)oa_missing_287,(void*)oa_missing_288,(void*)oa_missing_289,(void*)oa_missing_290,(void*)oa_missing_291,(void*)oa_missing_292,(void*)oa_missing_293,(void*)oa_missing_294,(void*)oa_missing_295,(void*)oa_missing_296,(void*)oa_missing_297,(void*)oa_missing_298,(void*)oa_missing_299,(void*)oa_missing_300,(void*)oa_missing_301,(void*)oa_missing_302,(void*)oa_missing_303,(void*)oa_missing_304,(void*)oa_missing_305,(void*)oa_missing_306,(void*)oa_missing_307,(void*)oa_missing_308,(void*)oa_missing_309,(void*)oa_missing_310,(void*)oa_missing_311,(void*)oa_missing_312,(void*)oa_missing_313,(void*)oa_missing_314,(void*)oa_missing_315,(void*)oa_missing_316,(void*)oa_missing_317,(void*)oa_missing_318,(void*)oa_missing_319,(void*)oa_missing_320,(void*)oa_missing_321,(void*)oa_missing_322,(void*)oa_missing_323,(void*)oa_missing_324,(void*)oa_missing_325,(void*)oa_missing_326,(void*)oa_missing_327,(void*)oa_missing_328,(void*)oa_missing_329,(void*)oa_missing_330,(void*)oa_missing_331,(void*)oa_missing_332,(void*)oa_missing_333,(void*)oa_missing_334,(void*)oa_missing_335,(void*)oa_missing_336,(void*)oa_missing_337,(void*)oa_missing_338,(void*)oa_missing_339,(void*)oa_missing_340,(void*)oa_missing_341,(void*)oa_missing_342,(void*)oa_missing_343,(void*)oa_missing_344,(void*)oa_missing_345,(void*)oa_missing_346,(void*)oa_missing_347,(void*)oa_missing_348,(void*)oa_missing_349,(void*)oa_missing_350,(void*)oa_missing_351,(void*)oa_missing_352,(void*)oa_missing_353,(void*)oa_missing_354,(void*)oa_missing_355,(void*)oa_missing_356,(void*)oa_missing_357,(void*)oa_missing_358,(void*)oa_missing_359,(void*)oa_missing_360,(void*)oa_missing_361,(void*)oa_missing_362,(void*)oa_missing_363,(void*)oa_missing_364,(void*)oa_missing_365,(void*)oa_missing_366,(void*)oa_missing_367,(void*)oa_missing_368,(void*)oa_missing_369,(void*)oa_missing_370,(void*)oa_missing_371,(void*)oa_missing_372,(void*)oa_missing_373,(void*)oa_missing_374,(void*)oa_missing_375,(void*)oa_missing_376,(void*)oa_missing_377,(void*)oa_missing_378,(void*)oa_missing_379,(void*)oa_missing_380,(void*)oa_missing_381,(void*)oa_missing_382,(void*)oa_missing_383,(void*)oa_missing_384,(void*)oa_missing_385,(void*)oa_missing_386,(void*)oa_missing_387,(void*)oa_missing_388,(void*)oa_missing_389,(void*)oa_missing_390,(void*)oa_missing_391,(void*)oa_missing_392,(void*)oa_missing_393,(void*)oa_missing_394,(void*)oa_missing_395,(void*)oa_missing_396,(void*)oa_missing_397,(void*)oa_missing_398,(void*)oa_missing_399,(void*)oa_missing_400,(void*)oa_missing_401,(void*)oa_missing_402,(void*)oa_missing_403,(void*)oa_missing_404,(void*)oa_missing_405,(void*)oa_missing_406,(void*)oa_missing_407,(void*)oa_missing_408,(void*)oa_missing_409,(void*)oa_missing_410,(void*)oa_missing_411,(void*)oa_missing_412,(void*)oa_missing_413,(void*)oa_missing_414,(void*)oa_missing_415,(void*)oa_missing_416,(void*)oa_missing_417,(void*)oa_missing_418,(void*)oa_missing_419,(void*)oa_missing_420,(void*)oa_missing_421,(void*)oa_missing_422,(void*)oa_missing_423,(void*)oa_missing_424,(void*)oa_missing_425,(void*)oa_missing_426,(void*)oa_missing_427,(void*)oa_missing_428,(void*)oa_missing_429,(void*)oa_missing_430,(void*)oa_missing_431,(void*)oa_missing_432,(void*)oa_missing_433,(void*)oa_missing_434,(void*)oa_missing_435,(void*)oa_missing_436,(void*)oa_missing_437,(void*)oa_missing_438,(void*)oa_missing_439,(void*)oa_missing_440,(void*)oa_missing_441,(void*)oa_missing_442,(void*)oa_missing_443,(void*)oa_missing_444,(void*)oa_missing_445,(void*)oa_missing_446,(void*)oa_missing_447,(void*)oa_missing_448,(void*)oa_missing_449,(void*)oa_missing_450,(void*)oa_missing_451,(void*)oa_missing_452,(void*)oa_missing_453,(void*)oa_missing_454,(void*)oa_missing_455,(void*)oa_missing_456,(void*)oa_missing_457,(void*)oa_missing_458,(void*)oa_missing_459,(void*)oa_missing_460,(void*)oa_missing_461,(void*)oa_missing_462,(void*)oa_missing_463,(void*)oa_missing_464,(void*)oa_missing_465,(void*)oa_missing_466,(void*)oa_missing_467,(void*)oa_missing_468,(void*)oa_missing_469,(void*)oa_missing_470,(void*)oa_missing_471,(void*)oa_missing_472,(void*)oa_missing_473,(void*)oa_missing_474,(void*)oa_missing_475,(void*)oa_missing_476,(void*)oa_missing_477,(void*)oa_missing_478,(void*)oa_missing_479,(void*)oa_missing_480,(void*)oa_missing_481,(void*)oa_missing_482,(void*)oa_missing_483,(void*)oa_missing_484,(void*)oa_missing_485,(void*)oa_missing_486,(void*)oa_missing_487,(void*)oa_missing_488,(void*)oa_missing_489,(void*)oa_missing_490,(void*)oa_missing_491,(void*)oa_missing_492,(void*)oa_missing_493,(void*)oa_missing_494,(void*)oa_missing_495,(void*)oa_missing_496,(void*)oa_missing_497,(void*)oa_missing_498,(void*)oa_missing_499,(void*)oa_missing_500,(void*)oa_missing_501,(void*)oa_missing_502,(void*)oa_missing_503,(void*)oa_missing_504,(void*)oa_missing_505,(void*)oa_missing_506,(void*)oa_missing_507,(void*)oa_missing_508,(void*)oa_missing_509,(void*)oa_missing_510,(void*)oa_missing_511};
static void *make_stub(const char*label,int args){(void)args;int i=g_nstubs++;if(i>=512)rt_fatal("native Vulkan method table bound");snprintf(g_stubs[i].label,sizeof(g_stubs[i].label),"%s",label);return oa_missing_table[i];}
static uintptr_t oa_unknown_native_slot(void*self,...){(void)self;rt_fatal("native Vulkan extended interface method not implemented");}
static void **make_vtable(const char *iface, const char *const *names, const uint8_t *args, int n) {
    void **vt = calloc(128, sizeof(void *));
    for(int j=0;j<128;j++)vt[j]=(void*)oa_unknown_native_slot;
    int i;
    for (i = 0; i < n; i++) {
        char label[48];
        if (names) snprintf(label, sizeof(label), "%s::%s", iface, names[i]);
        else snprintf(label, sizeof(label), "%s::slot%d", iface, i);
        vt[i] = make_stub(label, args[i]);
    }
    return vt;
}

/* ---- IUnknown ---- */
static ULONG COM o_addref(obj *o) { return (ULONG)InterlockedIncrement(&o->ref); }

/* Objects are parked for a few frames before their Vulkan handles go away; the GPU may still be using them. */
typedef struct gone { struct gone *next; obj *o; uint64_t frame; } gone;
static gone *g_gone;
static uint64_t g_frame = 1, g_done_frame;
#include "pass_audit.inc"
extern int oa_fast_pass_batches;
static uint64_t batch_pipeline_reused, batch_descriptor_reused, batch_verified, batch_queries_omitted;

void oa_batch_report(void) {
    rt_log("PASS counters pipelines=%llu descriptors=%llu checked=%llu queries=%llu",
           (unsigned long long)batch_pipeline_reused, (unsigned long long)batch_descriptor_reused,
           (unsigned long long)batch_verified, (unsigned long long)batch_queries_omitted);
}
static uint64_t upload_draw_copies, upload_draw_bytes;
static uint64_t upload_skipped_copies, upload_skipped_bytes, upload_preserved_bytes;

static void forget_upload(oa_buffer *b) {
    if (b->upload_owner) {
        *b->upload_owner = NULL;
        b->upload_owner = NULL;
        b->upload_data = NULL;
    }
    b->upload_written = 0;
}

static void restore_upload_shadow(oa_buffer *b) {
    if (!b->upload_data) return;
    if (b->upload_mapped) rt_fatal("constant upload still mapped when shadow needed");
    memcpy(b->shadow, b->upload_data, b->size);
    upload_preserved_bytes += b->size;
    forget_upload(b);
}

void oa_upload_report(void) {
    rt_log("UPLOAD counters copied=%llu bytes=%llu avoided=%llu bytes=%llu preserved=%llu",
           (unsigned long long)upload_draw_copies, (unsigned long long)upload_draw_bytes,
           (unsigned long long)upload_skipped_copies, (unsigned long long)upload_skipped_bytes,
           (unsigned long long)upload_preserved_bytes);
}

static ULONG COM o_release(obj *o) {
    LONG r = InterlockedDecrement(&o->ref);
    if (r == 0) {
        gone *g = malloc(sizeof(gone));
        EnterCriticalSection(&g_lock);
        g->o = o;
        g->frame = g_frame;
        g->next = g_gone;
        g_gone = g;
        LeaveCriticalSection(&g_lock);
    }
    return (ULONG)r;
}

static void destroy_obj(obj *o) {
    switch (o->kind) {
    case K_BUFFER: {
        oa_buffer *b = (oa_buffer *)o;
        forget_upload(b);
        for (size_t generation = 0; generation < b->ngens; generation++) {
            vk_buffer *gen = &b->gens[generation].buffer;
            if (gen->pool_slot) {
                dynamic_buffer_return(gen);
            } else {
                pvkDestroyBuffer(g_dev, gen->buf, NULL);
                pvkFreeMemory(g_dev, gen->mem, NULL);
            }
        }
        if (b->gens != b->inline_gens) free(b->gens);
        if(b->shadow_allocation)rt_heap_free(b->shadow_allocation);else _aligned_free(b->shadow);
        break;
    }
    case K_TEX2D: {
        oa_tex *t = (oa_tex *)o;
        if (t->hdr_view) pvkDestroyImageView(g_dev, t->hdr_view, NULL);
        if (t->image) pvkDestroyImage(g_dev, t->image, NULL);
        if (t->mem) pvkFreeMemory(g_dev, t->mem, NULL);
        _aligned_free(t->cpu);
        break;
    }
    case K_SRV: case K_RTV: case K_DSV: {
        oa_view *v = (oa_view *)o;
        if (v->view) pvkDestroyImageView(g_dev, v->view, NULL);
        if (v->tex) o_release(&v->tex->o);
        break;
    }
    case K_SAMPLER: {
        oa_state *s = (oa_state *)o;
        for (unsigned i = 0; i < 2; ++i)
            if (s->af_sampler[i]) pvkDestroySampler(g_dev, s->af_sampler[i], NULL);
        break;
    }
    case K_CHAIN:
        present_chain_release((oa_chain *)o);
        break;
    default: break;
    }
    free(o);
}

static void collect_gone(void) {
    gone *keep = NULL, *g, *n, *dead = NULL;
    EnterCriticalSection(&g_lock);
    for (g = g_gone; g; g = n) {
        n = g->next;
        if (g->frame + FRAMES + 1 < g_frame && g->frame <= g_done_frame) { g->next = dead; dead = g; }
        else { g->next = keep; keep = g; }
    }
    g_gone = keep;
    LeaveCriticalSection(&g_lock);
    for (g = dead; g; g = n) {
        n = g->next;
        destroy_obj(g->o);
        free(g);
    }
}

static const GUID IID_IUnknown_ = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_D3D11Device = {0xdb6f6ddb, 0xac77, 0x4e88, {0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40}};
static const GUID IID_DXGIDevice = {0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};
static obj *g_dxgi_device, *g_adapter, *g_factory;

static HRESULT COM o_qi(obj *o, const GUID *iid, void **out) {
    if (!memcmp(iid, &IID_IUnknown_, sizeof(GUID))) {
        o_addref(o);
        *out = o;
        return S_OK_;
    }
    if (o->kind == K_DEVICE && !memcmp(iid, &IID_D3D11Device, sizeof(GUID))) {
        o_addref(o);
        *out = o;
        return S_OK_;
    }
    if (o->kind == K_DEVICE && !memcmp(iid, &IID_DXGIDevice, sizeof(GUID))) {
        o_addref(g_dxgi_device);
        *out = g_dxgi_device;
        return S_OK_;
    }
    if (o->kind == K_TEX2D || o->kind == K_BUFFER || o->kind == K_CHAIN || o->kind == K_FACTORY || o->kind == K_ADAPTER || o->kind == K_DXGI_DEVICE) {
        /* Same object for any derived interface of the object itself. */
        o_addref(o);
        *out = o;
        return S_OK_;
    }
    logf_("QueryInterface kind %d unknown %08lx-%04x-%04x", o->kind, iid->Data1, iid->Data2, iid->Data3);
    *out = NULL;
    return E_NOINTERFACE_;
}

static obj *new_obj(size_t size, int kind) {
    obj *o = calloc(1, size);
    o->vtbl = g_vt[kind];
    o->ref = 1;
    o->kind = kind;
    return o;
}

/* ---- formats ---- */
typedef struct { VkFormat vk; int block, bytes; } fmt_info;

static fmt_info dx_format(uint32_t f) {
    fmt_info r = {VK_FORMAT_UNDEFINED, 1, 4};
#define F(D, V, B, Y) case D: r.vk = V; r.block = B; r.bytes = Y; break
    switch (f) {
    F(2, VK_FORMAT_R32G32B32A32_SFLOAT, 1, 16);
    F(6, VK_FORMAT_R32G32B32_SFLOAT, 1, 12);
    F(9, VK_FORMAT_R16G16B16A16_SFLOAT, 1, 8);
    F(10, VK_FORMAT_R16G16B16A16_SFLOAT, 1, 8);
    F(11, VK_FORMAT_R16G16B16A16_UNORM, 1, 8);
    F(12, VK_FORMAT_R16G16B16A16_UINT, 1, 8);
    F(13, VK_FORMAT_R16G16B16A16_SNORM, 1, 8);
    F(14, VK_FORMAT_R16G16B16A16_SINT, 1, 8);
    F(16, VK_FORMAT_R32G32_SFLOAT, 1, 8);
    F(24, VK_FORMAT_A2B10G10R10_UNORM_PACK32, 1, 4);
    F(26, VK_FORMAT_B10G11R11_UFLOAT_PACK32, 1, 4);
    F(27, VK_FORMAT_R8G8B8A8_UNORM, 1, 4);
    F(28, VK_FORMAT_R8G8B8A8_UNORM, 1, 4);
    F(29, VK_FORMAT_R8G8B8A8_SRGB, 1, 4);
    F(30, VK_FORMAT_R8G8B8A8_UINT, 1, 4);
    F(31, VK_FORMAT_R8G8B8A8_SNORM, 1, 4);
    F(32, VK_FORMAT_R8G8B8A8_SINT, 1, 4);
    F(34, VK_FORMAT_R16G16_SFLOAT, 1, 4);
    F(35, VK_FORMAT_R16G16_UNORM, 1, 4);
    F(36, VK_FORMAT_R16G16_UINT, 1, 4);
    F(37, VK_FORMAT_R16G16_SNORM, 1, 4);
    F(38, VK_FORMAT_R16G16_SINT, 1, 4);
    F(39, VK_FORMAT_R32_SFLOAT, 1, 4);
    F(40, VK_FORMAT_D32_SFLOAT, 1, 4);
    F(41, VK_FORMAT_R32_SFLOAT, 1, 4);
    F(44, g_d24s8, 1, 4);
    F(45, g_d24s8, 1, 4);
    F(46, g_d24s8, 1, 4);
    F(49, VK_FORMAT_R8G8_UNORM, 1, 2);
    F(53, VK_FORMAT_R16_UNORM, 1, 2);
    F(54, VK_FORMAT_R16_SFLOAT, 1, 2);
    F(55, VK_FORMAT_D16_UNORM, 1, 2);
    F(56, VK_FORMAT_R16_UNORM, 1, 2);
    F(57, VK_FORMAT_R16_UINT, 1, 2);
    F(61, VK_FORMAT_R8_UNORM, 1, 1);
    F(65, VK_FORMAT_R8_UNORM, 1, 1);
    F(71, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 4, 8);
    F(72, VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 4, 8);
    F(74, VK_FORMAT_BC2_UNORM_BLOCK, 4, 16);
    F(75, VK_FORMAT_BC2_SRGB_BLOCK, 4, 16);
    F(77, VK_FORMAT_BC3_UNORM_BLOCK, 4, 16);
    F(78, VK_FORMAT_BC3_SRGB_BLOCK, 4, 16);
    F(80, VK_FORMAT_BC4_UNORM_BLOCK, 4, 8);
    F(83, VK_FORMAT_BC5_UNORM_BLOCK, 4, 16);
    F(87, VK_FORMAT_B8G8R8A8_UNORM, 1, 4);
    F(88, VK_FORMAT_B8G8R8A8_UNORM, 1, 4);
    F(91, VK_FORMAT_B8G8R8A8_SRGB, 1, 4);
    default: logf_("unknown DXGI format %u", f); break;
    }
#undef F
    return r;
}

/* Report once per format anything this driver cannot do with it. Using an unsupported format is undefined, and can differ by GPU. */
static uint8_t g_fmt_seen[256];
static void check_format(uint32_t dx, int use_vertex, int use_rt, uint32_t w, uint32_t h, uint32_t mips) {
    VkFormatProperties p;
    fmt_info fi;
    uint8_t bit = use_vertex ? 1 : use_rt ? 2 : 4;
    if (dx >= 256 || (g_fmt_seen[dx] & bit)) return;
    g_fmt_seen[dx] |= bit;
    fi = dx_format(dx);
    pvkGetPhysicalDeviceFormatProperties(g_phys, fi.vk, &p);
    logf_("format dxgi %u vk %d use %s first %ux%u mips %u: buffer %x optimal %x%s%s%s", dx, fi.vk, use_vertex ? "vertex" : use_rt ? "target" : "texture", w, h, mips, p.bufferFeatures, p.optimalTilingFeatures,
          use_vertex && !(p.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) ? " NO-VERTEX" : "",
          !use_vertex && !(p.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? " NO-SAMPLE" : !use_vertex && !(p.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ? " NO-LINEAR" : "",
          use_rt && !(p.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) ? " NO-BLEND" : "");
}

static uint32_t mip_dim(uint32_t d, uint32_t m) { d >>= m; return d ? d : 1; }

/* ---- frames, ring and one-shot commands ---- */
typedef struct {
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired;
    vk_buffer rings[8];
    int nrings, ring_cur;
    uint32_t ring_off;
    uint32_t upload_owner_count;
    oa_buffer *upload_owners[UPLOAD_OWNERS];
    uint64_t frame;
    int submitted;
    VkQueryPool qpool;
    uint32_t qused, qreset;
    oa_pred **qlist;
} oa_frame;
static oa_frame g_f[FRAMES];
static oa_frame *g_cur;
static void texture_upload_collect(void);
static VkCommandPool g_upload_pool;
static VkCommandBuffer g_upload_cmd;
static VkFence g_upload_fence;
static int g_ring_overflow;

static void *ring_alloc(uint32_t size, VkBuffer *buf, uint32_t *off) {
    oa_frame *f = g_cur;
    uint32_t a = (uint32_t)g_ubo_align, o = (f->ring_off + a - 1) & ~(a - 1);
    if (size > RING_SIZE) { logf_("ring request of %u bytes is too large", size); size = RING_SIZE; }
    if (o + size > RING_SIZE) {
        f->ring_cur++;
        if (f->ring_cur == f->nrings) {
            if (f->nrings < 8 && make_buffer(&f->rings[f->nrings], RING_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
                f->nrings++;
                logf_("ring grew to %d chunks", f->nrings);
            } else {
                if (!g_ring_overflow) { g_ring_overflow = 1; logf_("ring overflow"); }
                f->ring_cur = 0;
            }
        }
        o = 0;
    }
    f->ring_off = o + size;
    *buf = f->rings[f->ring_cur].buf;
    *off = o;
    return f->rings[f->ring_cur].ptr + o;
}

static void desc_retire_pools(void);
#define QUERY_MAX 32768u
static long g_n_culled, g_n_tests, g_n_test_skipped;
static int g_cull = 1;

static void read_queries(oa_frame *f) {
    static uint32_t r[QUERY_MAX];
    uint32_t i, n = f->qused;
    f->qreset = n ? n : f->qreset;
    f->qused = 0;
    if (!n) return;
    if (pvkGetQueryPoolResults(g_dev, f->qpool, 0, n, n * 4, r, 4, 0)) return;
    for (i = 0; i < n; i++) f->qlist[i]->acc = 0;
    for (i = 0; i < n; i++) f->qlist[i]->acc |= r[i] != 0;
    for (i = 0; i < n; i++) {
        f->qlist[i]->visible = f->qlist[i]->acc;
        f->qlist[i]->tested_frame = f->frame;
    }
}

static void frame_begin(void) {
    oa_frame *f = &g_f[g_frame % FRAMES];
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (f->submitted) {
        VkResult completed=pvkWaitForFences(g_dev, 1, &f->fence, VK_TRUE, UINT64_MAX);
        if(completed!=VK_SUCCESS)rt_fatal("Vulkan frame fence wait failed: %d",completed);
        ++g_present_completed;
        present_gc_completed(f->frame);
        if(pvkResetFences(g_dev, 1, &f->fence)!=VK_SUCCESS)rt_fatal("Vulkan frame fence reset failed");
        f->submitted = 0;
        g_done_frame = f->frame;
        read_queries(f);
    }
    /* Once the frame fence has signalled, save only the still-current
       resource versions before this ring is recycled. */
    for (uint32_t i = 0; i < f->upload_owner_count; ++i)
        if (f->upload_owners[i]) restore_upload_shadow(f->upload_owners[i]);
    f->upload_owner_count = 0;
    texture_upload_collect();
    collect_gone();
    desc_retire_pools();
    pvkResetCommandPool(g_dev, f->pool, 0);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pvkBeginCommandBuffer(f->cmd, &bi);
    if (f->qreset) {
        pvkCmdResetQueryPool(f->cmd, f->qpool, 0, f->qreset);
        f->qreset = 0;
    }
    f->ring_off = 0;
    f->ring_cur = 0;
    f->frame = g_frame;
    g_cur = f;
}

#include "oa_texture_upload_queue.h"

static void barrier_all(VkCommandBuffer c) {
    VkMemoryBarrier m = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    m.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    m.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    pvkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &m, 0, NULL, 0, NULL);
}

static void image_layout(VkCommandBuffer c, VkImage i, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = i;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    pvkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

static int64_t g_t_upload, g_uploads;
static void upload_begin(void) {
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    pvkResetCommandPool(g_dev, g_upload_pool, 0);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pvkBeginCommandBuffer(g_upload_cmd, &bi);
}

/* The queue is shared with the render thread, so submits go through g_lock. */
static void upload_submit(void) {
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    pvkEndCommandBuffer(g_upload_cmd);
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_upload_cmd;
    pvkQueueSubmit(g_queue, 1, &si, g_upload_fence);
    pvkWaitForFences(g_dev, 1, &g_upload_fence, VK_TRUE, UINT64_MAX);
    pvkResetFences(g_dev, 1, &g_upload_fence);
}

/* ---- device: resources ---- */
static int buffer_add_gen(oa_buffer *b) {
    VkBufferUsageFlags u = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (b->ngens == b->gen_capacity) {
        if (b->gen_capacity > SIZE_MAX / sizeof(*b->gens) / 2) return 0;
        size_t capacity = b->gen_capacity * 2;
        oa_buffer_generation *gens;
        if (b->gens == b->inline_gens) {
            gens = malloc(capacity * sizeof(*gens));
            if (gens) memcpy(gens, b->inline_gens, sizeof(b->inline_gens));
        } else {
            gens = realloc(b->gens, capacity * sizeof(*gens));
        }
        if (!gens) return 0;
        b->gens = gens;
        b->gen_capacity = capacity;
    }
    const int place = g_dynamic_map_mode && b->size && b->size <= DYNAMIC_MAP_MAX_BUFFER &&
        b->usage == 2 && (b->bind & 3) && (b->cpu & 0x10000u);
    oa_buffer_generation gen = {0};
    if (!(place ? dynamic_buffer_acquire(&gen.buffer, b->size, u) :
                  make_buffer(&gen.buffer, b->size, u))) return 0;
    b->gens[b->ngens++] = gen;
    if (b->ngens == INLINE_BUFFER_GENERATIONS + 1)
        logf_("Vulkan expanded buffer generations: %u-byte buffer", b->size);
    return 1;
}

typedef struct { const void *mem; uint32_t row, slice; } oa_init;

static HRESULT COM d_create_buffer(obj *d, const uint32_t *desc, const oa_init *init, oa_buffer **out) {
    oa_buffer *b;
    (void)d;
    if (!out) return S_OK_;
    b = (oa_buffer *)new_obj(sizeof(oa_buffer), K_BUFFER);
    b->gens = b->inline_gens;
    b->gen_capacity = INLINE_BUFFER_GENERATIONS;
    b->size = desc[0]; b->usage = desc[1]; b->bind = desc[2]; b->cpu = desc[3]; b->misc = desc[4]; b->stride = desc[5];
    if (b->bind & 4) {
        b->ring = 1;
        /* CPU-only constant shadow already gets copied into the GPU frame ring.
           Place it in guest-addressable memory to avoid a second shadow/copy. */
        if(b->size>0x0fffffe0u)rt_fatal("constant buffer shadow bound");
        b->shadow_allocation=rt_heap_alloc(b->size+15,1);
        if(!b->shadow_allocation)rt_fatal("guest constant shadow allocation");
        b->shadow_guest=(b->shadow_allocation+15u)&~15u;
        b->shadow=rt_ptr(b->shadow_guest);
        if (init) memcpy(b->shadow, init->mem, b->size);
    } else {
        if (!buffer_add_gen(b)) { logf_("buffer %u failed", b->size); free(b); return E_OOM_; }
        if (init) memcpy(b->gens[0].buffer.ptr, init->mem, b->size);
    }
    *out = b;
    return S_OK_;
}

static VkImageAspectFlags tex_aspect(const oa_tex *t) {
    return t->is_depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | (t->has_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)) : VK_IMAGE_ASPECT_COLOR_BIT;
}

static int make_image(oa_tex *t, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageType type) {
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryRequirements r;
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ci.flags = flags;
    ci.imageType = type;
    ci.format = t->vkfmt;
    ci.extent.width = t->w;
    ci.extent.height = t->h;
    ci.extent.depth = t->depth;
    ci.mipLevels = t->mips;
    ci.arrayLayers = t->layers;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (pvkCreateImage(g_dev, &ci, NULL, &t->image)) return 0;
    pvkGetImageMemoryRequirements(g_dev, t->image, &r);
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = mem_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (pvkAllocateMemory(g_dev, &ai, NULL, &t->mem)) return 0;
    pvkBindImageMemory(g_dev, t->image, t->mem, 0);
    return 1;
}

static uint32_t rows_of(const oa_tex *t, uint32_t h) { return t->block == 4 ? (h + 3) / 4 : h; }
static uint32_t pitch_of(const oa_tex *t, uint32_t w) { return (t->block == 4 ? (w + 3) / 4 : w) * (uint32_t)t->bytes; }

static uint32_t cpu_mip_offset(const oa_tex *t, uint32_t mip) {
    uint32_t off = 0, m;
    for (m = 0; m < mip; m++) off += pitch_of(t, mip_dim(t->w, m)) * rows_of(t, mip_dim(t->h, m)) * mip_dim(t->depth, m);
    return off;
}

static uint64_t initial_texture_bytes(const oa_tex *t)
{
    uint64_t layer_bytes=0;
    for (uint32_t mip=0; mip<t->mips; ++mip)
        layer_bytes+=(uint64_t)pitch_of(t,mip_dim(t->w,mip)) *
            rows_of(t,mip_dim(t->h,mip))*mip_dim(t->depth,mip);
    return layer_bytes*t->layers;
}

static void record_initial_texture(VkCommandBuffer command, const oa_tex *t,
                                   const oa_init *init, const vk_buffer *staging)
{
    VkImageAspectFlags aspect=tex_aspect(t);
    image_layout(command,t->image,aspect,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    if (init) {
        uint32_t offset=0;
        for (uint32_t layer=0, sub=0; layer<t->layers; ++layer) {
            for (uint32_t mip=0; mip<t->mips; ++mip, ++sub) {
                uint32_t w=mip_dim(t->w,mip),h=mip_dim(t->h,mip),depth=mip_dim(t->depth,mip);
                uint32_t rows=rows_of(t,h),pitch=pitch_of(t,w);
                uint64_t plane=(uint64_t)pitch*rows;
                const uint8_t *source=init[sub].mem;
                uint8_t *dest=staging->ptr+offset;
                if (texture_upload_mode && init[sub].row == pitch && (depth == 1 || init[sub].slice == plane)) {
                    memcpy(dest,source,(size_t)(plane*depth));
                } else {
                    for (uint32_t z=0; z<depth; ++z)
                        for (uint32_t y=0; y<rows; ++y)
                            memcpy(dest+z*plane+(uint64_t)y*pitch,
                                   source+(uint64_t)z*init[sub].slice+(uint64_t)y*init[sub].row,pitch);
                }
                VkBufferImageCopy copy={0};
                copy.bufferOffset=offset;
                copy.imageSubresource.aspectMask=t->is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.mipLevel=mip;
                copy.imageSubresource.baseArrayLayer=layer;
                copy.imageSubresource.layerCount=1;
                copy.imageExtent=(VkExtent3D){w,h,depth};
                pvkCmdCopyBufferToImage(command,staging->buf,t->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
                offset+=(uint32_t)(plane*depth);
            }
        }
        if (!(staging->memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory=staging->mem;
            range.size=VK_WHOLE_SIZE;
            if (pvkFlushMappedMemoryRanges(g_dev,1,&range) != VK_SUCCESS)
                rt_fatal("flush initial texture staging buffer failed");
        }
    }
    barrier_all(command);
}

static HRESULT create_texture(const uint32_t *desc, const oa_init *init, oa_tex **out, int is3d) {
    /* 2D desc: W H Mips Array Fmt Samples Quality Usage Bind Cpu Misc. 3D desc: W H D Mips Fmt Usage Bind Cpu Misc. */
    oa_tex *t = (oa_tex *)new_obj(sizeof(oa_tex), K_TEX2D);
    uint32_t usage_d3d, bind, misc, cpu;
    fmt_info fi;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImageCreateFlags flags = 0;
    t->w = desc[0];
    t->h = desc[1];
    if (is3d) {
        t->depth = desc[2]; t->mips = desc[3]; t->layers = 1; t->fmt = desc[4]; usage_d3d = desc[5]; bind = desc[6]; cpu = desc[7]; misc = desc[8];
    } else {
        t->depth = 1; t->mips = desc[2]; t->layers = desc[3]; t->fmt = desc[4]; usage_d3d = desc[7]; bind = desc[8]; cpu = desc[9]; misc = desc[10];
    }
    (void)cpu;
    t->type3d = (uint32_t)is3d;
    memcpy(t->desc, desc, (is3d ? 9 : 11) * 4);
    if (!t->mips) {
        uint32_t m = t->w > t->h ? t->w : t->h;
        t->mips = 1;
        while (m > 1) { m >>= 1; t->mips++; }
    }
    if (bind & 0x40) { /* depth stencil */
        t->is_depth = 1;
        if (t->fmt == 53 || t->fmt == 55) { t->vkfmt = VK_FORMAT_D16_UNORM; t->bytes = 2; t->block = 1; }
        else if (t->fmt == 39 || t->fmt == 40) { t->vkfmt = VK_FORMAT_D32_SFLOAT; t->bytes = 4; t->block = 1; }
        else { t->vkfmt = g_d24s8; t->bytes = 4; t->block = 1; t->has_stencil = 1; }
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    } else {
        fi = dx_format(t->fmt);
        t->vkfmt = fi.vk; t->block = fi.block; t->bytes = fi.bytes;
        check_format(t->fmt, 0, (bind & 0x20) != 0, t->w, t->h, t->mips);
        flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        if (bind & 0x20) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (misc & 4) flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    /* Anything the CPU maps keeps its contents in plain memory; staging textures have no image at all. */
    if (usage_d3d == 2 || usage_d3d == 3) {
        t->cpu_size = cpu_mip_offset(t, t->mips) * t->layers;
        t->cpu = zalloc16(t->cpu_size);
    }
    if (usage_d3d != 3 && !make_image(t, usage, flags, is3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D)) {
        logf_("image %ux%u fmt %u failed", t->w, t->h, t->fmt);
        goto failed;
    }
    if (t->image) {
        uint64_t bytes=init ? initial_texture_bytes(t) : 0;
        if (init && (!bytes || bytes > UINT32_MAX)) goto failed;
        LARGE_INTEGER u0, u1;
        QueryPerformanceCounter(&u0);
        int queued=0;
        if (texture_upload_mode && init && !t->is_depth && bytes <= OA_UPLOAD_LIMIT) {
            pthread_mutex_lock(&texture_upload_lock);
            OaTextureUpload *slot=texture_upload_acquire((uint32_t)bytes);
            if (slot) {
                record_initial_texture(slot->command,t,init,&slot->staging);
                texture_upload_queue(slot,&t->o);
                queued=1;
            }
            pthread_mutex_unlock(&texture_upload_lock);
        }
        if (!queued) {
            vk_buffer staging={0};
            EnterCriticalSection(&g_lock);
            if (init) {
                int ok=make_buffer(&staging,bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
                if (!ok) {
                    LeaveCriticalSection(&g_lock);
                    goto failed;
                }
            }
            upload_begin();
            record_initial_texture(g_upload_cmd,t,init,&staging);
            upload_submit();
            LeaveCriticalSection(&g_lock);
            if (staging.buf) {
                pvkDestroyBuffer(g_dev,staging.buf,NULL);
                pvkFreeMemory(g_dev,staging.mem,NULL);
            }
        }
        if (init)
            __atomic_fetch_add(queued ? &texture_upload_calls : &texture_upload_fallbacks,1,__ATOMIC_RELAXED);
        QueryPerformanceCounter(&u1);
        __atomic_fetch_add(&g_t_upload,u1.QuadPart-u0.QuadPart,__ATOMIC_RELAXED);
        __atomic_fetch_add(&g_uploads,1,__ATOMIC_RELAXED);
    }
    *out = t;
    return S_OK_;
failed:
    if (t->image) pvkDestroyImage(g_dev,t->image,NULL);
    if (t->mem) pvkFreeMemory(g_dev,t->mem,NULL);
    _aligned_free(t->cpu);
    free(t);
    return E_OOM_;
}

static HRESULT COM d_create_tex2d(obj *d, const uint32_t *desc, const oa_init *init, oa_tex **out) {
    (void)d;
    if (!out) return S_OK_;
    return create_texture(desc, init, out, 0);
}
static HRESULT COM d_create_tex3d(obj *d, const uint32_t *desc, const oa_init *init, oa_tex **out) {
    (void)d;
    if (!out) return S_OK_;
    return create_texture(desc, init, out, 1);
}

static VkImageView make_view(oa_tex *t, VkImageViewType type, VkFormat f, VkImageAspectFlags aspect, uint32_t base_mip, uint32_t mips, uint32_t base_layer, uint32_t layers, int alpha_only) {
    VkImageViewCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkImageView v = VK_NULL_HANDLE;
    ci.image = t->image;
    ci.viewType = type;
    ci.format = f;
    if (alpha_only) {
        ci.components.r = ci.components.g = ci.components.b = VK_COMPONENT_SWIZZLE_ZERO;
        ci.components.a = VK_COMPONENT_SWIZZLE_R;
    }
    ci.subresourceRange.aspectMask = aspect;
    ci.subresourceRange.baseMipLevel = base_mip;
    ci.subresourceRange.levelCount = mips;
    ci.subresourceRange.baseArrayLayer = base_layer;
    ci.subresourceRange.layerCount = layers;
    if (pvkCreateImageView(g_dev, &ci, NULL, &v)) logf_("view failed fmt %d", f);
    return v;
}

static HRESULT COM d_create_srv(obj *d, oa_tex *t, const uint32_t *desc, oa_view **out) {
    oa_view *v;
    uint32_t fmt = desc ? desc[0] : t->fmt, dim = desc ? desc[1] : (t->type3d ? 8 : 4), base = 0, mips = VK_REMAINING_MIP_LEVELS, bl = 0, ll = t->layers;
    VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
    VkFormat f;
    (void)d;
    if (t->o.kind != K_TEX2D) { logf_("SRV of a buffer is not implemented"); return E_NOTIMPL_; }
    if (!out) return S_OK_;
    /* D3D11_SRV_DIMENSION: 2=1D 3=1DARRAY 4=2D 5=2DARRAY 8=3D 9=CUBE */
    if (desc) {
        if (dim == 4 || dim == 8 || dim == 9) { base = desc[2]; mips = desc[3]; }
        else if (dim == 5) { base = desc[2]; mips = desc[3]; bl = desc[4]; ll = desc[5]; }
        else { logf_("SRV dimension %u", dim); return E_NOTIMPL_; }
    } else if (t->layers == 6) dim = 9;
    if (dim == 9) { type = VK_IMAGE_VIEW_TYPE_CUBE; ll = 6; }
    else if (dim == 8) type = VK_IMAGE_VIEW_TYPE_3D;
    else if (dim == 5) type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    else ll = 1;
    if (mips == 0xffffffffu) mips = VK_REMAINING_MIP_LEVELS;
    v = (oa_view *)new_obj(sizeof(oa_view), K_SRV);
    v->tex = t;
    o_addref(&t->o);
    f = t->is_depth ? t->vkfmt : (fmt ? dx_format(fmt).vk : t->vkfmt);
    if (t->block == 4) f = t->vkfmt == dx_format(fmt).vk ? f : f;
    v->format = f;
    {
        static uint32_t next_uid;
        v->uid = ++next_uid;
    }
    if (t->image) v->view = make_view(t, type, f, t->is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, base, mips, bl, ll, fmt == 65);
    *out = v;
    return S_OK_;
}

static HRESULT COM d_create_rtv(obj *d, oa_tex *t, const uint32_t *desc, oa_view **out) {
    oa_view *v;
    uint32_t fmt = desc && desc[0] ? desc[0] : t->fmt, level = desc ? desc[2] : 0;
    (void)d;
    if (!out) return S_OK_;
    v = (oa_view *)new_obj(sizeof(oa_view), K_RTV);
    v->tex = t;
    o_addref(&t->o);
    v->format = dx_format(fmt).vk;
    v->level = level;
    v->w = mip_dim(t->w, level);
    v->h = mip_dim(t->h, level);
    v->view = make_view(t, VK_IMAGE_VIEW_TYPE_2D, v->format, VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1, 0);
    *out = v;
    return S_OK_;
}

static HRESULT COM d_create_dsv(obj *d, oa_tex *t, const uint32_t *desc, oa_view **out) {
    oa_view *v;
    uint32_t level = desc ? desc[3] : 0;
    (void)d;
    if (!out) return S_OK_;
    v = (oa_view *)new_obj(sizeof(oa_view), K_DSV);
    v->tex = t;
    o_addref(&t->o);
    v->format = t->vkfmt;
    v->level = level;
    v->w = mip_dim(t->w, level);
    v->h = mip_dim(t->h, level);
    v->view = make_view(t, VK_IMAGE_VIEW_TYPE_2D, t->vkfmt, tex_aspect(t), level, 1, 0, 1, 0);
    *out = v;
    return S_OK_;
}

static HRESULT COM d_create_layout(obj *d, const D3D11_INPUT_ELEMENT_DESC *e, UINT n, const void *code, SIZE_T size, oa_layout **out) {
    oa_layout *l;
    UINT i;
    uint32_t next[16] = {0};
    (void)d; (void)code; (void)size;
    if (!out) return S_OK_;
    l = (oa_layout *)new_obj(sizeof(oa_layout), K_LAYOUT);
    l->n = (int)n;
    for (i = 0; i < n && i < 32; i++) {
        const D3D11_INPUT_ELEMENT_DESC *x = &e[i];
        oa_elem *o = &l->e[i];
        snprintf(o->name, sizeof(o->name), "%s", x->SemanticName);
        o->index = x->SemanticIndex; o->fmt = x->Format; o->slot = x->InputSlot;
        o->offset = x->AlignedByteOffset; o->cls = x->InputSlotClass; o->step = x->InstanceDataStepRate;
        if (o->offset == 0xffffffffu) o->offset = next[o->slot & 15];
        next[o->slot & 15] = o->offset + (uint32_t)dx_format(o->fmt).bytes;
        check_format(o->fmt, 1, 0, 0, 0, 0);
    }
    *out = l;
    return S_OK_;
}

static uint32_t hash_bytes(const void *p, size_t n);

static void af_get_texture_pairs(oa_shader *shader, const uint32_t *spv, size_t words);

static int intro_cart_camera;
static unsigned intro_cart_particle_draws[2];

__attribute__((visibility("hidden"))) void veil_intro_cart_camera(int active)
{
    if (__atomic_exchange_n(&intro_cart_camera, active, __ATOMIC_RELAXED) == active)
        return;
    unsigned depth = __atomic_exchange_n(&intro_cart_particle_draws[0], 0, __ATOMIC_RELAXED);
    unsigned sprite = __atomic_exchange_n(&intro_cart_particle_draws[1], 0, __ATOMIC_RELAXED);
    if (active)
        logf_("Opening cart particle guard active");
    else
        logf_("Opening cart particle guard inactive, %u depth and %u sprite particle draws suppressed",
              depth, sprite);
}

static HRESULT create_shader(const void *code, SIZE_T size, oa_shader **out, int kind) {
    oa_shader *s = (oa_shader *)new_obj(sizeof(oa_shader), kind);
    uint32_t *spv = NULL;
    size_t words = 0;
    char err[200];
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    if (!oa_dxbc_translate(code, size, &s->info, &spv, &words, err, sizeof(err))) {
        logf_("shader translate failed: %s", err);
        free(s);
        return E_FAIL_;
    }
    if (kind == K_PS && s->info.sampler_mask && s->info.texture_mask)
        af_get_texture_pairs(s, spv, words);
    if (kind == K_PS && s->info.texture_mask == 15 && s->info.cb_mask == 1)
        s->hash = hash_bytes(code, size);
    if (kind == K_PS && (size == 2728 || size == 1684)) {
        uint32_t shader = hash_bytes(code, size);
        if (size == 2728 && shader == 0x32e7f79fu) s->cart_particle = 1;
        if (size == 1684 && shader == 0xf587466eu) s->cart_particle = 2;
    }
    ci.codeSize = words * 4;
    ci.pCode = spv;
    if (pvkCreateShaderModule(g_dev, &ci, NULL, &s->module)) { logf_("shader module failed"); free(spv); free(s); return E_FAIL_; }
    free(spv);
    *out = s;
    return S_OK_;
}
static HRESULT COM d_create_vs(obj *d, const void *code, SIZE_T size, void *link, oa_shader **out) { (void)d; (void)link; if (!out) return S_OK_; return create_shader(code, size, out, K_VS); }
static HRESULT COM d_create_ps(obj *d, const void *code, SIZE_T size, void *link, oa_shader **out) { (void)d; (void)link; if (!out) return S_OK_; return create_shader(code, size, out, K_PS); }

static HRESULT create_state(const void *desc, size_t words, oa_state **out, int kind) {
    oa_state *s;
    if (!out) return S_OK_;
    s = (oa_state *)new_obj(sizeof(oa_state), kind);
    memcpy(s->desc, desc, words * 4);
    *out = s;
    return S_OK_;
}
/* The blend description is 2+8*8 words; only the first target is kept unless independent blend is on, so it is stored in full on the side. */
typedef struct { oa_state s; uint32_t rt[64]; } oa_blend;
static HRESULT COM d_create_blend(obj *d, const uint32_t *desc, oa_state **out) {
    oa_blend *b;
    (void)d;
    if (!out) return S_OK_;
    b = (oa_blend *)new_obj(sizeof(oa_blend), K_BLEND);
    b->s.desc[0] = desc[0];
    b->s.desc[1] = desc[1];
    memcpy(b->rt, desc + 2, 64 * 4);
    *out = &b->s;
    return S_OK_;
}
static HRESULT COM d_create_depth(obj *d, const void *desc, oa_state **out) { (void)d; return create_state(desc, 13, out, K_DEPTH); }
static HRESULT COM d_create_raster(obj *d, const void *desc, oa_state **out) { (void)d; return create_state(desc, 10, out, K_RASTER); }

static VkSamplerAddressMode addr_mode(uint32_t m) {
    switch (m) {
    case 1: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case 2: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case 3: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case 4: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    }
}

static HRESULT COM d_create_sampler(obj *d, const uint32_t *x, oa_state **out) {
    oa_state *s;
    VkSamplerCreateInfo ci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    uint32_t f = x[0];
    const float *fl = (const float *)x;
    (void)d;
    if (!out) return S_OK_;
    s = (oa_state *)new_obj(sizeof(oa_state), K_SAMPLER);
    memcpy(s->desc, x, 13 * 4);
    ci.magFilter = (f >> 2 & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.minFilter = (f >> 4 & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.mipmapMode = (f & 1) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (f & 0x40) { ci.magFilter = ci.minFilter = VK_FILTER_LINEAR; ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; ci.anisotropyEnable = VK_TRUE; ci.maxAnisotropy = (float)x[5]; }
    ci.addressModeU = addr_mode(x[1]);
    ci.addressModeV = addr_mode(x[2]);
    ci.addressModeW = addr_mode(x[3]);
    ci.mipLodBias = fl[4];
    if (f & 0x80) { ci.compareEnable = VK_TRUE; ci.compareOp = (VkCompareOp)(x[6] - 1); }
    ci.borderColor = (fl[10] > 0.5f) ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : (fl[7] > 0.5f ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
    ci.minLod = fl[11];
    ci.maxLod = fl[12];
    if (pvkCreateSampler(g_dev, &ci, NULL, &s->sampler)) logf_("sampler failed");
    /* Material samplers can be selected at draw time. Leave comparison,
       point and single-mip filters on their native sampler. */
    if (!ci.compareEnable && ci.minFilter == VK_FILTER_LINEAR &&
        ci.magFilter == VK_FILTER_LINEAR && ci.maxLod > ci.minLod &&
        (f & 0x180u) == 0) {
        ci.anisotropyEnable = VK_TRUE;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        for (unsigned i = 0; i < 2; ++i) {
            ci.maxAnisotropy = (float)(8u << i);
            if (ci.maxAnisotropy <= af_device_limit &&
                pvkCreateSampler(g_dev, &ci, NULL, &s->af_sampler[i]) != VK_SUCCESS)
                logf_("AF sampler creation failed %.0f", (double)ci.maxAnisotropy);
        }
    }
    *out = s;
    return S_OK_;
}

static HRESULT COM d_create_predicate(obj *d, const void *desc, oa_pred **out) {
    (void)d; (void)desc;
    if (out) {
        *out = (oa_pred *)new_obj(sizeof(oa_pred), K_PREDICATE);
        (*out)->visible = 1;
    }
    return S_OK_;
}
static HRESULT COM d_check_format(obj *d, UINT fmt, UINT *out) { (void)d; (void)fmt; *out = 0x000FFFFF; return S_OK_; }
static HRESULT COM d_check_msaa(obj *d, UINT fmt, UINT count, UINT *out) { (void)d; (void)fmt; *out = count == 1 ? 1 : 0; return S_OK_; }
static HRESULT COM d_get_private(obj *d, const GUID *g, UINT *size, void *data) { (void)d; (void)g; (void)data; if (size) *size = 0; return DXGI_NOT_FOUND; }
static HRESULT COM d_set_private(obj *d, const GUID *g, UINT size, const void *data) { (void)d; (void)g; (void)size; (void)data; return S_OK_; }
static UINT COM d_feature_level(obj *d) { (void)d; return 0xb000; }
static UINT COM d_flags(obj *d) { (void)d; return 0; }
static HRESULT COM d_removed(obj *d) { (void)d; return S_OK_; }
static void COM d_immediate(obj *d, oa_context **out) { (void)d; o_addref(&g_ctx->o); *out = g_ctx; }
static HRESULT COM d_deferred(obj *d, UINT flags, oa_context **out) {
    oa_context *c;
    (void)d; (void)flags;
    if (!out) return S_OK_;
    c = (oa_context *)new_obj(sizeof(oa_context), K_CONTEXT);
    c->deferred = 1;
    *out = c;
    return S_OK_;
}

/* ---- pipelines ---- */
typedef struct {
    oa_shader *vs, *ps;
    oa_layout *layout;
    oa_state *blend, *depth, *raster;
    uint32_t topology;
    VkFormat rt[8], ds;
} pipe_key;

typedef struct { oa_shader *vs, *ps; VkPipelineLayout layout; VkDescriptorSetLayout dsl; int used; } prog_entry;
typedef struct pipe_entry { pipe_key k; VkPipeline p; const prog_entry *program; int used; } pipe_entry;
static pipe_entry *g_pipes;
static uint32_t g_pipes_cap = 1u << 14, g_pipes_n;

static prog_entry g_progs[1u << 13];

static uint32_t hash_bytes(const void *p, size_t n) {
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

static void add_bindings(const oa_shader_info *i, VkDescriptorSetLayoutBinding *b, uint32_t *n, VkShaderStageFlags stage) {
    uint32_t s;
    for (s = 0; s < 16; s++)
        if (i->cb_mask >> s & 1) {
            VkDescriptorSetLayoutBinding *x = &b[(*n)++];
            x->binding = i->binding_base + OA_BIND_CB + s; x->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; x->descriptorCount = 1; x->stageFlags = stage; x->pImmutableSamplers = NULL;
        }
    for (s = 0; s < 16; s++)
        if (i->sampler_mask >> s & 1) {
            VkDescriptorSetLayoutBinding *x = &b[(*n)++];
            x->binding = i->binding_base + OA_BIND_SAMPLER + s; x->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; x->descriptorCount = 1; x->stageFlags = stage; x->pImmutableSamplers = NULL;
        }
    for (s = 0; s < 32; s++)
        if (i->texture_mask >> s & 1) {
            VkDescriptorSetLayoutBinding *x = &b[(*n)++];
            x->binding = i->binding_base + OA_BIND_TEXTURE + s; x->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; x->descriptorCount = 1; x->stageFlags = stage; x->pImmutableSamplers = NULL;
        }
}

static prog_entry *program_of(oa_shader *vs, oa_shader *ps) {
    uint32_t h = hash_bytes(&vs, sizeof(vs)) ^ hash_bytes(&ps, sizeof(ps)) * 31u, m = (1u << 13) - 1, i = h & m;
    VkDescriptorSetLayoutBinding b[128];
    uint32_t n = 0;
    VkDescriptorSetLayoutCreateInfo dci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VkPipelineLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkDescriptorSetLayout dsl;
    while (g_progs[i].used) {
        if (g_progs[i].vs == vs && g_progs[i].ps == ps) return &g_progs[i];
        i = (i + 1) & m;
    }
    add_bindings(&vs->info, b, &n, VK_SHADER_STAGE_VERTEX_BIT);
    if (ps) add_bindings(&ps->info, b, &n, VK_SHADER_STAGE_FRAGMENT_BIT);
    dci.bindingCount = n;
    dci.pBindings = b;
    if (pvkCreateDescriptorSetLayout(g_dev, &dci, NULL, &dsl)) logf_("set layout failed, %u bindings", n);
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &dsl;
    pvkCreatePipelineLayout(g_dev, &lci, NULL, &g_progs[i].layout);
    g_progs[i].vs = vs;
    g_progs[i].ps = ps;
    g_progs[i].dsl = dsl;
    g_progs[i].used = 1;
    return &g_progs[i];
}

static VkBlendFactor blend_factor(uint32_t b) {
    static const VkBlendFactor T[20] = {VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR, VK_BLEND_FACTOR_SRC1_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR, VK_BLEND_FACTOR_SRC1_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA};
    return b < 20 ? T[b] : VK_BLEND_FACTOR_ONE;
}
static VkBlendOp blend_op(uint32_t o) {
    switch (o) { case 2: return VK_BLEND_OP_SUBTRACT; case 3: return VK_BLEND_OP_REVERSE_SUBTRACT; case 4: return VK_BLEND_OP_MIN; case 5: return VK_BLEND_OP_MAX; default: return VK_BLEND_OP_ADD; }
}
static VkStencilOp stencil_op(uint32_t o) {
    static const VkStencilOp T[9] = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_INVERT, VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_DECREMENT_AND_WRAP};
    return o < 9 ? T[o] : VK_STENCIL_OP_KEEP;
}

static void pipeline_cache_note_creation(void);

static VkPipeline build_pipeline(const pipe_key *k, VkPipelineLayout layout) {
    VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    VkPipelineShaderStageCreateInfo st[2];
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState att[8];
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    static const VkDynamicState DYN[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};
    VkVertexInputBindingDescription vb[16];
    VkVertexInputAttributeDescription va[32];
    uint32_t nb = 0, na = 0, i, j, nrt = 0, seen = 0;
    VkPipeline p = VK_NULL_HANDLE;

    memset(st, 0, sizeof(st));
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = k->vs->module;
    st[0].pName = "main";
    if (k->ps) {
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        st[1].module = k->ps->module;
        st[1].pName = "main";
    }
    /* Vertex attributes: the layout's elements matched to the shader's inputs by semantic. */
    for (i = 0; k->layout && i < (uint32_t)k->vs->info.input_count; i++) {
        const oa_sig_element *in = &k->vs->info.inputs[i];
        int found = 0;
        for (j = 0; j < (uint32_t)k->layout->n; j++) {
            const oa_elem *e = &k->layout->e[j];
            if (e->index == in->semantic_index && !_stricmp(e->name, in->name)) {
                va[na].location = in->reg;
                va[na].binding = e->slot;
                va[na].format = dx_format(e->fmt).vk;
                va[na].offset = e->offset;
                na++;
                if (!(seen >> e->slot & 1)) {
                    seen |= 1u << e->slot;
                    vb[nb].binding = e->slot;
                    vb[nb].stride = 0;
                    vb[nb].inputRate = e->cls ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
                    nb++;
                }
                found = 1;
                break;
            }
        }
        if (!found && in->sysval == 0) logf_("vertex shader input %s%u has no layout element", in->name, in->semantic_index);
    }
    vi.vertexBindingDescriptionCount = nb;
    vi.pVertexBindingDescriptions = vb;
    vi.vertexAttributeDescriptionCount = na;
    vi.pVertexAttributeDescriptions = va;
    switch (k->topology) {
    case 1: ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
    case 2: ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
    case 3: ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; ia.primitiveRestartEnable = VK_TRUE; break;
    case 5: ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; ia.primitiveRestartEnable = VK_TRUE; break;
    default: ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
    }
    vp.viewportCount = vp.scissorCount = 1;
    /* Raster: Fill Cull FrontCCW DepthBias BiasClamp Slope DepthClip Scissor MS AA */
    rs.polygonMode = (k->raster && k->raster->desc[0] == 2) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode = !k->raster ? VK_CULL_MODE_BACK_BIT : k->raster->desc[1] == 1 ? VK_CULL_MODE_NONE : k->raster->desc[1] == 2 ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
    rs.frontFace = (k->raster && k->raster->desc[2]) ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    if (k->raster) {
        int32_t bias = (int32_t)k->raster->desc[3];
        float clamp_, slope;
        memcpy(&clamp_, &k->raster->desc[4], 4);
        memcpy(&slope, &k->raster->desc[5], 4);
        rs.depthBiasEnable = bias != 0 || slope != 0.0f;
        rs.depthBiasConstantFactor = (float)bias;
        rs.depthBiasClamp = clamp_;
        rs.depthBiasSlopeFactor = slope;
        rs.depthClampEnable = !k->raster->desc[6];
    }
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    /* Depth: Enable Write Func StencilEnable RW(bytes) Front(4) Back(4) */
    if (k->depth) {
        const uint32_t *d = k->depth->desc;
        ds.depthTestEnable = d[0] != 0;
        ds.depthWriteEnable = d[0] != 0 && d[1] != 0;
        ds.depthCompareOp = d[2] ? (VkCompareOp)(d[2] - 1) : VK_COMPARE_OP_ALWAYS;
        ds.stencilTestEnable = d[3] != 0;
        ds.front.failOp = stencil_op(d[5]); ds.front.depthFailOp = stencil_op(d[6]); ds.front.passOp = stencil_op(d[7]);
        ds.front.compareOp = d[8] ? (VkCompareOp)(d[8] - 1) : VK_COMPARE_OP_ALWAYS;
        ds.front.compareMask = d[4] & 0xff; ds.front.writeMask = (d[4] >> 8) & 0xff;
        ds.back.failOp = stencil_op(d[9]); ds.back.depthFailOp = stencil_op(d[10]); ds.back.passOp = stencil_op(d[11]);
        ds.back.compareOp = d[12] ? (VkCompareOp)(d[12] - 1) : VK_COMPARE_OP_ALWAYS;
        ds.back.compareMask = ds.front.compareMask; ds.back.writeMask = ds.front.writeMask;
    } else {
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
    }
    /* Blend: AlphaToCoverage Independent then 8 targets of Enable Src Dst Op SrcA DstA OpA Mask */
    memset(att, 0, sizeof(att));
    for (i = 0; i < 8; i++) {
        if (k->rt[i]) nrt = i + 1;
    }
    for (i = 0; i < nrt; i++) {
        const uint32_t *r = NULL;
        if (k->blend) {
            const oa_blend *bl = (const oa_blend *)k->blend;
            r = bl->rt + (bl->s.desc[1] ? i : 0) * 8;
        }
        att[i].colorWriteMask = r ? (r[7] & 0xf) : 0xf;
        if (r && r[0]) {
            att[i].blendEnable = VK_TRUE;
            att[i].srcColorBlendFactor = blend_factor(r[1]);
            att[i].dstColorBlendFactor = blend_factor(r[2]);
            att[i].colorBlendOp = blend_op(r[3]);
            att[i].srcAlphaBlendFactor = blend_factor(r[4]);
            att[i].dstAlphaBlendFactor = blend_factor(r[5]);
            att[i].alphaBlendOp = blend_op(r[6]);
        }
    }
    cb.attachmentCount = nrt;
    cb.pAttachments = att;
    dy.dynamicStateCount = 5;
    dy.pDynamicStates = DYN;
    ri.colorAttachmentCount = nrt;
    ri.pColorAttachmentFormats = k->rt;
    ri.depthAttachmentFormat = k->ds;
    if (k->ds == g_d24s8) ri.stencilAttachmentFormat = k->ds;
    ci.pNext = &ri;
    ci.stageCount = k->ps ? 2 : 1;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = layout;
    if (pvkCreateGraphicsPipelines(g_dev, g_pcache, 1, &ci, NULL, &p)) logf_("pipeline failed");
    else pipeline_cache_note_creation();
    return p;
}

extern int oa_fast_draw_preparation;

static VkPipeline get_pipeline(oa_context *c, const pipe_key *k, int fast) {
    if (fast && c->selected_pipe && !memcmp(&c->selected_pipe->k, k, sizeof(*k))) {
        static int seen;
        if (!seen) { seen = 1; logf_("draw pipeline exact-key hit"); }
        return c->selected_pipe->p;
    }
    uint32_t m = g_pipes_cap - 1, i = hash_bytes(k, sizeof(*k)) & m;
    while (g_pipes[i].used) {
        if (!memcmp(&g_pipes[i].k, k, sizeof(*k))) {
            c->selected_pipe = &g_pipes[i];
            return g_pipes[i].p;
        }
        i = (i + 1) & m;
    }
    if (g_pipes_n + 1 >= g_pipes_cap) { logf_("pipeline table full"); return VK_NULL_HANDLE; }
    g_pipes[i].k = *k;
    g_pipes[i].program = program_of(k->vs, k->ps);
    g_pipes[i].p = build_pipeline(k, g_pipes[i].program->layout);
    c->selected_pipe = &g_pipes[i];
    g_pipes[i].used = 1;
    g_pipes_n++;
    return g_pipes[i].p;
}

/* ---- context: state ---- */
static void end_rendering(oa_context *c) {
    if (c->rendering) {
        pvkCmdEndRendering(g_cur->cmd);
        c->rendering = 0;
    }
}

static void begin_rendering(oa_context *c) {
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    VkRenderingAttachmentInfo col[8], dep = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    uint32_t i, n = 0, w = 0, h = 0;
    if (c->rendering) return;
    barrier_all(g_cur->cmd);
    memset(col, 0, sizeof(col));
    for (i = 0; i < 8; i++) {
        col[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        col[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        col[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        col[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (c->rtv[i]) {
            col[i].imageView = c->rtv[i]->view;
            n = i + 1;
            w = c->rtv[i]->w;
            h = c->rtv[i]->h;
        }
    }
    if (c->dsv) { w = c->dsv->w; h = c->dsv->h; }
    ri.renderArea.extent.width = w;
    ri.renderArea.extent.height = h;
    ri.layerCount = 1;
    ri.colorAttachmentCount = n;
    ri.pColorAttachments = col;
    if (c->dsv) {
        dep.imageView = c->dsv->view;
        dep.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        dep.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        dep.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ri.pDepthAttachment = &dep;
        if (c->dsv->tex->has_stencil) ri.pStencilAttachment = &dep;
    }
    pass_begin_render(c, w, h);
    pvkCmdBeginRendering(g_cur->cmd, &ri);
    c->rendering = 1;
    c->pipeline_valid = 0;
}

static void clear_refs(void **p, int n) { memset(p, 0, (size_t)n * sizeof(void *)); }

static void COM c_set_vs_cb(oa_context *c, UINT s, UINT n, oa_buffer *const *b) { UINT i; for (i = 0; i < n && s + i < 14; i++) c->vs_cb[s + i] = b ? b[i] : NULL; }
static void COM c_set_ps_cb(oa_context *c, UINT s, UINT n, oa_buffer *const *b) { UINT i; for (i = 0; i < n && s + i < 14; i++) c->ps_cb[s + i] = b ? b[i] : NULL; }
static void COM c_set_vs_srv(oa_context *c, UINT s, UINT n, oa_view *const *v) {
    const uint32_t used = c->vs ? c->vs->info.texture_mask : 0;
    for (UINT i = 0; i < n && s + i < 32; ++i) {
        const UINT slot = s + i;
        oa_view *next = v ? v[i] : NULL;
        if (next != c->vs_srv[slot] && ((used >> slot) & 1u))
            ++c->material_epoch;
        c->vs_srv[slot] = next;
    }
}
static void COM c_set_ps_srv(oa_context *c, UINT s, UINT n, oa_view *const *v) {
    const uint32_t used = c->ps ? c->ps->info.texture_mask : 0;
    for (UINT i = 0; i < n && s + i < 32; ++i) {
        const UINT slot = s + i;
        oa_view *next = v ? v[i] : NULL;
        if (next != c->ps_srv[slot] && ((used >> slot) & 1u))
            ++c->material_epoch;
        c->ps_srv[slot] = next;
    }
}
static void COM c_set_vs_smp(oa_context *c, UINT s, UINT n, oa_state *const *v) {
    const uint32_t used = c->vs ? c->vs->info.sampler_mask : 0;
    for (UINT i = 0; i < n && s + i < 16; ++i) {
        const UINT slot = s + i;
        oa_state *next = v ? v[i] : NULL;
        if (next != c->vs_smp[slot] && ((used >> slot) & 1u))
            ++c->material_epoch;
        c->vs_smp[slot] = next;
    }
}
static void COM c_set_ps_smp(oa_context *c, UINT s, UINT n, oa_state *const *v) {
    const uint32_t used = c->ps ? c->ps->info.sampler_mask : 0;
    for (UINT i = 0; i < n && s + i < 16; ++i) {
        const UINT slot = s + i;
        oa_state *next = v ? v[i] : NULL;
        if (next != c->ps_smp[slot] && ((used >> slot) & 1u))
            ++c->material_epoch;
        c->ps_smp[slot] = next;
    }
}
static void COM c_set_vs(oa_context *c, oa_shader *s, void *i, UINT n) { (void)i; (void)n; if (c->vs != s) { ++c->material_epoch; c->pipeline_inputs_dirty = 1; } c->vs = s; }
static void COM c_set_ps(oa_context *c, oa_shader *s, void *i, UINT n) { (void)i; (void)n; if (c->ps != s) { ++c->material_epoch; c->pipeline_inputs_dirty = 1; } c->ps = s; }
static void COM c_set_layout(oa_context *c, oa_layout *l) { if (c->layout != l) c->pipeline_inputs_dirty = 1; c->layout = l; }
static void COM c_set_vb(oa_context *c, UINT s, UINT n, oa_buffer *const *b, const UINT *st, const UINT *of) {
    UINT i;
    for (i = 0; i < n && s + i < 16; i++) {
        c->vb[s + i] = b ? b[i] : NULL;
        c->vb_stride[s + i] = st ? st[i] : 0;
        c->vb_off[s + i] = of ? of[i] : 0;
    }
}
static void COM c_set_ib(oa_context *c, oa_buffer *b, UINT f, UINT o) { c->ib = b; c->ib_fmt = f; c->ib_off = o; }
static void COM c_set_topo(oa_context *c, UINT t) { if (c->topology != t) c->pipeline_inputs_dirty = 1; c->topology = t; }
static void COM c_set_blend(oa_context *c, oa_state *b, const float *f, UINT m) { (void)m; if (c->blend != b) c->pipeline_inputs_dirty = 1; c->blend = b; if (f) memcpy(c->blend_factor, f, 16); }
static void COM c_set_depth(oa_context *c, oa_state *d, UINT r) { if (c->depth != d) c->pipeline_inputs_dirty = 1; c->depth = d; c->stencil_ref = r; }
static void COM c_set_raster(oa_context *c, oa_state *r) { if (c->raster != r) c->pipeline_inputs_dirty = 1; c->raster = r; }
static void COM c_set_viewports(oa_context *c, UINT n, const float *v) {
    if (!n) return;
    c->vp.x = v[0];
    c->vp.y = v[1] + v[3];
    c->vp.width = v[2];
    c->vp.height = -v[3];
    c->vp.minDepth = v[4];
    c->vp.maxDepth = v[5];
    c->have_vp = 1;
}
static void COM c_set_scissors(oa_context *c, UINT n, const LONG *r) {
    if (!n) return;
    c->scissor.offset.x = r[0];
    c->scissor.offset.y = r[1];
    c->scissor.extent.width = (uint32_t)(r[2] - r[0]);
    c->scissor.extent.height = (uint32_t)(r[3] - r[1]);
    c->have_scissor = 1;
}
static void COM c_set_rts(oa_context *c, UINT n, oa_view *const *v, oa_view *d) {
    UINT i;
    int changed = c->dsv != d;
    for (i = 0; i < 8; i++) {
        oa_view *nv = (v && i < n) ? v[i] : NULL;
        if (c->rtv[i] != nv) changed = 1;
        c->rtv[i] = nv;
    }
    c->dsv = d;
    if (changed) {
        ++c->material_epoch;
        c->pipeline_inputs_dirty = 1;
        if (!c->deferred) end_rendering(c);
    }
}
static void COM c_clear_state(oa_context *c) {
    c->pipeline_inputs_dirty = 1;
    ++c->material_epoch;
    if (!c->deferred && g_cur) end_rendering(c);
    clear_refs((void **)c->vs_cb, 14); clear_refs((void **)c->ps_cb, 14);
    clear_refs((void **)c->vs_srv, 32); clear_refs((void **)c->ps_srv, 32);
    clear_refs((void **)c->vs_smp, 16); clear_refs((void **)c->ps_smp, 16);
    clear_refs((void **)c->vb, 16); clear_refs((void **)c->rtv, 8);
    c->vs = c->ps = NULL; c->layout = NULL; c->blend = c->depth = c->raster = NULL;
    c->ib = NULL; c->dsv = NULL; c->topology = 0; c->have_vp = c->have_scissor = 0;
    c->stencil_ref = 0;
    c->blend_factor[0] = c->blend_factor[1] = c->blend_factor[2] = c->blend_factor[3] = 1.0f;
}
static void COM c_flush(oa_context *c) { (void)c; }

/* Predication uses results from a finished frame, not this frame, so an object coming into view can appear a frame or two late. */
static void COM c_begin(oa_context *c, oa_pred *q) { c->q_active = q; }
static void COM c_end(oa_context *c, oa_pred *q) { (void)q; c->q_active = NULL; }
/* The engine passes FALSE: skip rendering when the predicate says nothing passed. */
static void COM c_set_predication(oa_context *c, oa_pred *q, BOOL v) { c->pred = v ? NULL : q; }

/* ---- map ---- */
typedef struct { void *p; UINT row, slice; } oa_mapped;

void oa_vk_restore_constant_shadow(void *resource) {
    obj *o = resource;
    if (o->kind == K_BUFFER) restore_upload_shadow((oa_buffer*)o);
}

int oa_vk_guest_discard_upload(void *context, void *resource, uint32_t written,
                               uint32_t *guest, uint32_t *bytes) {
    obj *o = resource;
    if (context != g_ctx || o->kind != K_BUFFER || !g_cur)
        return 0;
    oa_buffer *b = resource;
    oa_frame *f = g_cur;
    if (!b->ring || b->upload_mapped || b->shadow_mapped || written > b->size || written < 64 ||
        b->size - written > written || f->ring_cur || !f->rings[0].guest ||
        f->upload_owner_count == UPLOAD_OWNERS)
        return 0;
    const uint32_t alignment = (uint32_t)g_ubo_align;
    const uint32_t next = (f->ring_off + alignment - 1) & ~(alignment - 1);
    if (b->size > RING_SIZE - next)
        return 0;

    uint8_t *target = ring_alloc(b->size, &b->ring_buf, &b->ring_off);
    if (written < b->size) {
        /* Preserve the old shadow tail for games that discard only a prefix. */
        const uint8_t *previous = b->upload_data ? b->upload_data : b->shadow;
        memcpy(target + written, previous + written, b->size - written);
        upload_preserved_bytes += b->size - written;
    }
    forget_upload(b);
    b->upload_data = target;
    b->upload_owner = &f->upload_owners[f->upload_owner_count++];
    *b->upload_owner = b;
    b->upload_mapped = 1;
    b->upload_written = written;
    *guest = f->rings[0].guest + b->ring_off;
    *bytes = b->size;
    return 1;
}

int oa_vk_finish_discard_upload(void *context, void *resource) {
    obj *o = resource;
    if (context != g_ctx || o->kind != K_BUFFER)
        return 0;
    oa_buffer *b = resource;
    if (!b->upload_mapped)
        return 0;
    b->upload_mapped = 0;
    b->ring_frame = g_frame;
    b->dirty = 0;
    return 1;
}

int oa_vk_guest_constant_map(void*resource,const void*data,size_t bytes,uint32_t*guest){
 obj*o=resource;if(o->kind!=K_BUFFER)return 0;oa_buffer*b=(oa_buffer*)o;
 if(b->ring){
  if(data!=b->shadow||bytes>b->size||!b->shadow_guest)return 0;
  *guest=b->shadow_guest;return 1;
 }
 /* D3D11_MAP_WRITE_NO_OVERWRITE keeps the current GPU generation. */
 if(!g_dynamic_map_mode||__atomic_load_n(&b->mapped_type,__ATOMIC_RELAXED)!=5||bytes!=b->size)return 0;
 vk_buffer *gen=&b->gens[b->cur].buffer;
 if(!gen->pool_slot||data!=gen->ptr)return 0;
 *guest=gen->guest;
 static unsigned reported;
 if (!__atomic_load_n(&reported, __ATOMIC_RELAXED) &&
     !__atomic_exchange_n(&reported, 1, __ATOMIC_RELAXED))
    rt_log("DYNAMIC_MAP direct mapped bytes=%zu", bytes);
 return 1;
}
static HRESULT map_i(oa_context *c, obj *res, UINT sub, UINT type, UINT flags, oa_mapped *m) {
    (void)c; (void)flags;
    if (res->kind == K_BUFFER) {
        oa_buffer *b = (oa_buffer *)res;
        if (b->ring) {
            if (b->upload_mapped || b->shadow_mapped) return E_FAIL_;
            b->shadow_mapped = 1;
            m->p = b->shadow;
        } else {
            if (type == 4 && b->gens[b->cur].last_used > g_done_frame) {
                /* Discard: move to a copy the GPU is not reading. */
                size_t next = 0;
                while (next < b->ngens && b->gens[next].last_used > g_done_frame) next++;
                if (next == b->ngens && !buffer_add_gen(b)) {
                    logf_("Vulkan discard allocation failed: %u-byte buffer, %zu in-flight generations",
                          b->size, b->ngens);
                    return E_OOM_;
                }
                b->cur = next;
            }
            m->p = b->gens[b->cur].buffer.ptr;
            if (g_dynamic_map_mode) __atomic_store_n(&b->mapped_type,type,__ATOMIC_RELAXED);
        }
        m->row = m->slice = b->size;
        return S_OK_;
    }
    if (res->kind == K_TEX2D) {
        oa_tex *t = (oa_tex *)res;
        uint32_t mip = sub % t->mips, layer = sub / t->mips, w = mip_dim(t->w, mip), h = mip_dim(t->h, mip);
        if (!t->cpu) { logf_("Map of a texture with no CPU copy"); return E_FAIL_; }
        m->row = pitch_of(t, w);
        m->slice = m->row * rows_of(t, h);
        m->p = t->cpu + cpu_mip_offset(t, mip) + (t->cpu_size / t->layers) * layer;
        t->upload_mip = (int)sub;
        return S_OK_;
    }
    return E_FAIL_;
}

static void upload_region(oa_tex *t, uint32_t sub, const uint8_t *src, uint32_t row, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, oa_context *c) {
    uint32_t mip = sub % t->mips, layer = sub / t->mips, rows = rows_of(t, h), pitch = pitch_of(t, w), y;
    VkBuffer buf;
    uint32_t off;
    uint8_t *dst;
    VkBufferImageCopy r = {0};
    end_rendering(c);
    dst = ring_alloc(pitch * rows, &buf, &off);
    for (y = 0; y < rows; y++) memcpy(dst + (uint64_t)y * pitch, src + (uint64_t)y * row, pitch);
    r.bufferOffset = off;
    r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    r.imageSubresource.mipLevel = mip;
    r.imageSubresource.baseArrayLayer = layer;
    r.imageSubresource.layerCount = 1;
    r.imageOffset.x = (int32_t)dx;
    r.imageOffset.y = (int32_t)dy;
    r.imageExtent.width = w;
    r.imageExtent.height = h;
    r.imageExtent.depth = 1;
    barrier_all(g_cur->cmd);
    pvkCmdCopyBufferToImage(g_cur->cmd, buf, t->image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
    barrier_all(g_cur->cmd);
}

static void unmap_i(oa_context *c, obj *res, UINT sub) {
    if (res->kind == K_BUFFER) {
        oa_buffer *b = (oa_buffer *)res;
        if (b->ring) {
            b->shadow_mapped = 0;
            b->dirty = 1; /* uploaded on first use */
        } else {
            if (g_dynamic_map_mode) __atomic_store_n(&b->mapped_type,0,__ATOMIC_RELAXED);
            if (pass_frames) ++b->audit_version;
        }
        return;
    }
    if (res->kind == K_TEX2D) {
        oa_tex *t = (oa_tex *)res;
        if (t->image && t->cpu) {
            uint32_t mip = sub % t->mips, layer = sub / t->mips, w = mip_dim(t->w, mip), h = mip_dim(t->h, mip);
            upload_region(t, sub, t->cpu + cpu_mip_offset(t, mip) + (t->cpu_size / t->layers) * layer, pitch_of(t, w), 0, 0, w, h, c);
        }
    }
}

static void COM c_copy_subresource(oa_context *c, obj *dst, UINT dsub, UINT x, UINT y, UINT z, obj *src, UINT ssub, const uint32_t *box) {
    oa_tex *d = (oa_tex *)dst, *s = (oa_tex *)src;
    static int warned;
    (void)z;
    if (dst->kind != K_TEX2D || src->kind != K_TEX2D) { if (!warned++) logf_("CopySubresourceRegion on buffers is not implemented"); return; }
    if (d->image && s->cpu) {
        uint32_t mip = ssub % s->mips, layer = ssub / s->mips, sw = mip_dim(s->w, mip), sh = mip_dim(s->h, mip), bx = box ? box[0] : 0, by = box ? box[1] : 0;
        uint32_t w = box ? box[3] - box[0] : sw, h = box ? box[4] - box[1] : sh, row = pitch_of(s, sw);
        const uint8_t *p = s->cpu + cpu_mip_offset(s, mip) + (s->cpu_size / s->layers) * layer;
        p += (uint64_t)(s->block == 4 ? by / 4 : by) * row + (uint64_t)(s->block == 4 ? bx / 4 : bx) * (uint32_t)s->bytes;
        upload_region(d, dsub, p, row, x, y, w, h, c);
    } else if (d->image && s->image) {
        VkImageCopy r = {0};
        uint32_t smip = ssub % s->mips, dmip = dsub % d->mips;
        end_rendering(c);
        r.srcSubresource.aspectMask = r.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        r.srcSubresource.mipLevel = smip;
        r.srcSubresource.baseArrayLayer = ssub / s->mips;
        r.srcSubresource.layerCount = 1;
        r.dstSubresource.mipLevel = dmip;
        r.dstSubresource.baseArrayLayer = dsub / d->mips;
        r.dstSubresource.layerCount = 1;
        r.srcOffset.x = box ? (int32_t)box[0] : 0;
        r.srcOffset.y = box ? (int32_t)box[1] : 0;
        r.dstOffset.x = (int32_t)x;
        r.dstOffset.y = (int32_t)y;
        r.extent.width = box ? box[3] - box[0] : mip_dim(s->w, smip);
        r.extent.height = box ? box[4] - box[1] : mip_dim(s->h, smip);
        r.extent.depth = 1;
        barrier_all(g_cur->cmd);
        pvkCmdCopyImage(g_cur->cmd, s->image, VK_IMAGE_LAYOUT_GENERAL, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
        barrier_all(g_cur->cmd);
    } else if (!warned++) {
        logf_("CopySubresourceRegion readback to CPU is not implemented");
    }
}

static void COM c_clear_rtv(oa_context *c, oa_view *v, const float *col) {
    VkClearColorValue cv;
    VkImageSubresourceRange r = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!v || !v->tex->image) return;
    end_rendering(c);
    memcpy(cv.float32, col, 16);
    r.baseMipLevel = v->level;
    barrier_all(g_cur->cmd);
    pvkCmdClearColorImage(g_cur->cmd, v->tex->image, VK_IMAGE_LAYOUT_GENERAL, &cv, 1, &r);
}

static void COM c_clear_dsv(oa_context *c, oa_view *v, UINT flags, float depth, UINT8 stencil) {
    VkClearDepthStencilValue cv;
    VkImageSubresourceRange r = {0, 0, 1, 0, 1};
    if (!v) return;
    end_rendering(c);
    cv.depth = depth;
    cv.stencil = stencil;
    /* Vulkan clears through the image, so clear both aspects if the image has stencil and only one was asked for, unless that would destroy data. */
    r.aspectMask = ((flags & 1) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) | (((flags & 2) && v->tex->has_stencil) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    r.baseMipLevel = v->level;
    if (!r.aspectMask) return;
    barrier_all(g_cur->cmd);
    pvkCmdClearDepthStencilImage(g_cur->cmd, v->tex->image, VK_IMAGE_LAYOUT_GENERAL, &cv, 1, &r);
}

/* ---- draw ---- */
static oa_tex *g_dummy2d, *g_dummycube, *g_dummy3d;
static oa_view *g_null2d, *g_nullcube, *g_null3d;
static oa_state *g_null_sampler;
static vk_buffer g_zero_ubo;

static long g_n_null_srv, g_n_dim_bad, g_n_no_view, g_c_push, g_c_vb, g_c_pipe, g_c_ib;
static int64_t g_t_draw, g_t_map, g_t_unmap;
static struct { const void *shader; uint32_t slot; long n; } g_nulls[24];
static uint32_t g_null_seen[64];

typedef struct {
    VkWriteDescriptorSet w[128];
    VkDescriptorImageInfo img[96];
    VkDescriptorBufferInfo buf[32];
    uint32_t dyn[32];
    uint64_t key[192];
    uint32_t nw, nimg, nbuf, nkey;
} desc_build;

/* Descriptor sets, reused whenever a draw needs the same textures, samplers and buffers. Constant buffer positions are dynamic offsets. */
#define DESC_TABLE (1u << 16)
#define DESC_POOL_SETS 16384u
typedef struct { uint32_t hash, nkey; uint64_t *key; VkDescriptorSet set; } desc_entry;
static desc_entry *g_desc;
static uint32_t g_desc_n;
static uint64_t g_desc_pool_epoch;
static VkDescriptorPool g_pool;
static struct { VkDescriptorPool pool; uint64_t frame; } g_old_pools[8];
static long g_c_set_miss, g_c_pools;

static void desc_new_pool(void) {
    VkDescriptorPoolSize sz[3];
    VkDescriptorPoolCreateInfo ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    uint32_t i;
    if (g_pool) {
        for (i = 0; i < 8; i++)
            if (!g_old_pools[i].pool) { g_old_pools[i].pool = g_pool; g_old_pools[i].frame = g_frame; break; }
        if (i == 8) logf_("too many retired descriptor pools");
    }
    sz[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; sz[0].descriptorCount = DESC_POOL_SETS * 6;
    sz[1].type = VK_DESCRIPTOR_TYPE_SAMPLER; sz[1].descriptorCount = DESC_POOL_SETS * 12;
    sz[2].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; sz[2].descriptorCount = DESC_POOL_SETS * 12;
    ci.maxSets = DESC_POOL_SETS;
    ci.poolSizeCount = 3;
    ci.pPoolSizes = sz;
    if (pvkCreateDescriptorPool(g_dev, &ci, NULL, &g_pool)) logf_("descriptor pool failed");
    if (!g_desc) g_desc = calloc(DESC_TABLE, sizeof(desc_entry));
    for (i = 0; i < DESC_TABLE; i++) {
        free(g_desc[i].key);
        g_desc[i].key = NULL;
    }
    g_desc_n = 0;
    ++g_desc_pool_epoch;
    g_c_pools++;
}

static void desc_retire_pools(void) {
    uint32_t i;
    for (i = 0; i < 8; i++)
        if (g_old_pools[i].pool && g_old_pools[i].frame + FRAMES + 2 < g_frame && g_old_pools[i].frame <= g_done_frame) {
            pvkDestroyDescriptorPool(g_dev, g_old_pools[i].pool, NULL);
            g_old_pools[i].pool = VK_NULL_HANDLE;
        }
}

static void add_descriptor_write(desc_build *d, uint32_t binding,
                                 VkDescriptorType type, const VkDescriptorBufferInfo *buffer,
                                 const VkDescriptorImageInfo *image) {
    d->w[d->nw++] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = type,
        .pBufferInfo = buffer,
        .pImageInfo = image
    };
}

/* Keys and resource values are gathered in binding order. On a cache miss
   materialize the Vulkan update using that same snapshot. */
static void emit_stage_writes(desc_build *d, const oa_shader_info *i,
                              uint32_t *buffer, uint32_t *image) {
    for (uint32_t mask = i->cb_mask & 0xffffu; mask; mask &= mask - 1)
        add_descriptor_write(d, i->binding_base + OA_BIND_CB + __builtin_ctz(mask),
                             VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, &d->buf[(*buffer)++], NULL);
    for (uint32_t mask = i->sampler_mask & 0xffffu; mask; mask &= mask - 1)
        add_descriptor_write(d, i->binding_base + OA_BIND_SAMPLER + __builtin_ctz(mask),
                             VK_DESCRIPTOR_TYPE_SAMPLER, NULL, &d->img[(*image)++]);
    for (uint32_t mask = i->texture_mask; mask; mask &= mask - 1)
        add_descriptor_write(d, i->binding_base + OA_BIND_TEXTURE + __builtin_ctz(mask),
                             VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, NULL, &d->img[(*image)++]);
}

static VkDescriptorSet desc_get(const prog_entry *prog, desc_build *d, int lazy) {
    uint32_t h = 2166136261u, i, m = DESC_TABLE - 1, at;
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    VkDescriptorSet set = VK_NULL_HANDLE;
    d->key[d->nkey++] = (uint64_t)prog->dsl;
    for (i = 0; i < d->nkey; i++) h = (h ^ (uint32_t)d->key[i] ^ (uint32_t)(d->key[i] >> 32)) * 16777619u;
    at = h & m;
    while (g_desc[at].key) {
        if (g_desc[at].hash == h && g_desc[at].nkey == d->nkey && !memcmp(g_desc[at].key, d->key, d->nkey * 8)) {
            static int lazy_seen;
            if (lazy && !lazy_seen) { lazy_seen = 1; logf_("draw descriptor cache hit skipped writes"); }
            return g_desc[at].set;
        }
        at = (at + 1) & m;
    }
    if (g_desc_n >= DESC_POOL_SETS - 1) {
        desc_new_pool();
        at = h & m;
    }
    ai.descriptorPool = g_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &prog->dsl;
    if (pvkAllocateDescriptorSets(g_dev, &ai, &set)) {
        desc_new_pool();
        at = h & m;
        ai.descriptorPool = g_pool;
        if (pvkAllocateDescriptorSets(g_dev, &ai, &set)) { logf_("descriptor set allocation failed"); return VK_NULL_HANDLE; }
    }
    if (lazy) {
        uint32_t buffer = 0, image = 0;
        if (prog->ps) emit_stage_writes(d, &prog->ps->info, &buffer, &image);
        emit_stage_writes(d, &prog->vs->info, &buffer, &image);
    }
    for (i = 0; i < d->nw; i++) d->w[i].dstSet = set;
    if (d->nw) pvkUpdateDescriptorSets(g_dev, d->nw, d->w, 0, NULL);
    g_desc[at].hash = h;
    g_desc[at].nkey = d->nkey;
    g_desc[at].key = malloc(d->nkey * 8);
    memcpy(g_desc[at].key, d->key, d->nkey * 8);
    g_desc[at].set = set;
    g_desc_n++;
    g_c_set_miss++;
    return set;
}

#include "oa_af.inc"

static void write_stage(desc_build *d, const oa_shader_info *i, oa_buffer **cbs,
                    oa_view **srv, oa_state **smp, int eager, int uniforms_only, const oa_shader *shader, int af_material) {
    for (uint32_t mask = i->cb_mask & 0xffffu; mask; mask &= mask - 1) {
        uint32_t s = __builtin_ctz(mask);
        VkDescriptorBufferInfo *b = &d->buf[d->nbuf++];
        oa_buffer *x = s < 14 ? cbs[s] : NULL;
        if (eager)
            add_descriptor_write(d, i->binding_base + OA_BIND_CB + s,
                                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, b, NULL);
        if (x && x->ring) {
            if (x->ring_frame != g_frame || x->dirty) {
                void *p = ring_alloc(x->size, &x->ring_buf, &x->ring_off);
                /* The previous frame's slot is valid as a CPU source only.
                   Never reference it from this frame's GPU commands. */
                const uint8_t *source = x->upload_data ? x->upload_data : x->shadow;
                memcpy(p, source, x->size);
                ++upload_draw_copies;
                upload_draw_bytes += x->size;
                x->upload_written = 0;
                x->ring_frame = g_frame;
                x->dirty = 0;
            } else if (x->upload_written) {
                ++upload_skipped_copies;
                upload_skipped_bytes += x->upload_written;
                x->upload_written = 0;
            }
            b->buffer = x->ring_buf;
            b->offset = 0;
            b->range = x->size;
            d->dyn[d->nbuf - 1] = x->ring_off;
        } else if (x) {
            b->buffer = x->gens[x->cur].buffer.buf;
            b->offset = 0;
            b->range = x->size;
            d->dyn[d->nbuf - 1] = 0;
            x->gens[x->cur].last_used = g_frame;
        } else {
            b->buffer = g_zero_ubo.buf;
            b->offset = 0;
            b->range = 65536;
            d->dyn[d->nbuf - 1] = 0;
        }
        if (pass_frames) {
            pass_constants = audit_hash(pass_constants, x ? x->size : 0);
            if (x) {
                const void *data = x->ring ?
                    (x->upload_data ? x->upload_data : x->shadow) : x->gens[x->cur].buffer.ptr;
                pass_constants = audit_bytes(pass_constants, data, x->size);
            }
        }
        if (!uniforms_only) {
            d->key[d->nkey++] = (uint64_t)b->buffer;
            d->key[d->nkey++] = b->range;
        }
    }
    if (uniforms_only) return;
    for (uint32_t mask = i->sampler_mask & 0xffffu; mask; mask &= mask - 1) {
        uint32_t s = __builtin_ctz(mask);
        VkDescriptorImageInfo *g = &d->img[d->nimg++];
        VkSampler sampler = smp[s] ? smp[s]->sampler : g_null_sampler->sampler;
        if (af_material && smp[s] && smp[s]->af_sampler[0] && af_is_material(shader, s, srv)) {
            ++af_active_draws;
            unsigned stock = smp[s]->desc[0] & 0x40u ? smp[s]->desc[5] : 1;
            if (stock <= 16) af_stock_mask |= 1u << stock;
            if (af_requested && smp[s]->af_sampler[af_requested == 16])
                sampler = smp[s]->af_sampler[af_requested == 16];
        }
        *g = (VkDescriptorImageInfo){.sampler = sampler};
        if (eager)
            add_descriptor_write(d, i->binding_base + OA_BIND_SAMPLER + s,
                                 VK_DESCRIPTOR_TYPE_SAMPLER, NULL, g);
        if (pass_frames) pass_textures = audit_hash(pass_textures, (uint64_t)g->sampler);
        d->key[d->nkey++] = (uint64_t)g->sampler;
    }
    for (uint32_t mask = i->texture_mask; mask; mask &= mask - 1) {
        uint32_t s = __builtin_ctz(mask);
        VkDescriptorImageInfo *g = &d->img[d->nimg++];
        oa_view *v = srv[s];
        if (!v || !v->view) {
            int ni;
            if (v) g_n_no_view++;
            else g_n_null_srv++;
            for (ni = 0; ni < 24; ni++) {
                if (!g_nulls[ni].shader) {
                    g_nulls[ni].shader = i;
                    g_nulls[ni].slot = s;
                    logf_("texture slot unbound: %s shader %p t%u dim %d, view object %p", i->is_pixel ? "pixel" : "vertex", (const void *)i, s, i->texture_dim[s], (void *)v);
                }
                if (g_nulls[ni].shader == i && g_nulls[ni].slot == s) { g_nulls[ni].n++; break; }
            }
            v = i->texture_dim[s] == 6 ? g_nullcube : i->texture_dim[s] == 5 ? g_null3d : g_null2d;
        } else {
            int want = i->texture_dim[s], have = v->tex->type3d ? 5 : v->tex->layers == 6 ? 6 : 3;
            if (want != have) {
                uint32_t key = (uint32_t)want * 16u + (uint32_t)have, h;
                g_n_dim_bad++;
                for (h = 0; h < 64 && g_null_seen[h] && g_null_seen[h] != key; h++) {}
                if (h < 64 && !g_null_seen[h]) { g_null_seen[h] = key; logf_("texture dimension mismatch: shader wants %d, bound %ux%u layers %u", want, v->tex->w, v->tex->h, v->tex->layers); }
                v = want == 6 ? g_nullcube : want == 5 ? g_null3d : g_null2d;
            }
        }
        *g = (VkDescriptorImageInfo){.imageView = v->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        if (eager)
            add_descriptor_write(d, i->binding_base + OA_BIND_TEXTURE + s,
                                 VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, NULL, g);
        if (pass_frames) pass_textures = audit_hash(pass_textures, v->uid);
        d->key[d->nkey++] = v->uid;
    }
}

static long g_n_draw, g_n_skip_vs, g_n_skip_rt, g_n_skip_pipe, g_n_clear, g_n_skip_ib, g_n_skip_query, g_n_query_kept;

static void draw_descriptors(oa_context *c, desc_build *d, int eager, int uniforms_only) {
    d->nw = d->nimg = d->nbuf = d->nkey = 0;
    if (pass_frames) pass_constants = pass_textures = UINT64_C(14695981039346656037);
    /* Pixel bindings precede vertex bindings in the dynamic-offset array. */
    if (c->ps) write_stage(d, &c->ps->info, c->ps_cb, c->ps_srv, c->ps_smp, eager, uniforms_only, c->ps,
                            c->dsv && c->rtv[0] && !c->rtv[0]->tex->is_backbuffer);
    write_stage(d, &c->vs->info, c->vs_cb, c->vs_srv, c->vs_smp, eager, uniforms_only, c->vs, 0);
}

static VkPipeline draw_pipeline(oa_context *c, int fast) {
    pipe_key k = {0};
    k.vs = c->vs; k.ps = c->ps; k.layout = c->layout;
    k.blend = c->blend; k.depth = c->depth; k.raster = c->raster; k.topology = c->topology;
    for (uint32_t i = 0; i < 8; ++i)
        k.rt[i] = c->rtv[i] ? c->rtv[i]->format : VK_FORMAT_UNDEFINED;
    k.ds = c->dsv ? c->dsv->format : VK_FORMAT_UNDEFINED;
    VkPipeline p = get_pipeline(c, &k, fast);
    c->pipeline_inputs_dirty = !p;
    return p;
}

static int prepare_draw(oa_context *c) {
    VkCommandBuffer cmd;
    VkPipeline p;
    uint32_t i;
    static desc_build d;
    if (c->deferred) return 0;
    if (c->q_active) g_n_query_kept++;
    if (!c->vs) { g_n_skip_vs++; return 0; }
    if (!c->rtv[0] && !c->dsv) { g_n_skip_rt++; return 0; }
    begin_rendering(c);
    cmd = g_cur->cmd;
    const int fast = __atomic_load_n(&oa_fast_draw_preparation, __ATOMIC_RELAXED);
    const int batch = fast && __atomic_load_n(&oa_fast_pass_batches, __ATOMIC_RELAXED);
    if (batch && !c->pipeline_inputs_dirty && c->selected_pipe && c->selected_pipe->p) {
        p = c->selected_pipe->p;
        ++batch_pipeline_reused;
        if (pass_frames && draw_pipeline(c, fast) != p)
            rt_fatal("reused pipeline differs from full lookup");
    } else {
        p = draw_pipeline(c, fast);
    }
    if (!p) { g_n_skip_pipe++; return 0; }
    g_n_draw++;
    if (!c->pipeline_valid || p != c->last_pipeline) {
        pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
        g_c_pipe++;
        c->last_pipeline = p;
        c->pipeline_valid = 1;
    }
    if (!c->cache_valid) {
        memset(&c->set_vp, 0, sizeof(c->set_vp));
        memset(&c->set_scissor, 0, sizeof(c->set_scissor));
        c->set_stencil = 0xffffffffu;
        c->set_blend[0] = -1.0f;
        memset(c->set_vb, 0, sizeof(c->set_vb));
        c->set_ib = VK_NULL_HANDLE;
        c->set_layout = VK_NULL_HANDLE;
        c->set_desc = VK_NULL_HANDLE;
        c->cache_valid = 1;
    }
    if (c->have_vp && memcmp(&c->vp, &c->set_vp, sizeof(c->vp))) { pvkCmdSetViewport(cmd, 0, 1, &c->vp); c->set_vp = c->vp; }
    {
        VkRect2D r;
        if (c->raster && c->raster->desc[7] && c->have_scissor) r = c->scissor;
        else {
            r.offset.x = r.offset.y = 0;
            r.extent.width = c->rtv[0] ? c->rtv[0]->w : c->dsv->w;
            r.extent.height = c->rtv[0] ? c->rtv[0]->h : c->dsv->h;
        }
        if (memcmp(&r, &c->set_scissor, sizeof(r))) { pvkCmdSetScissor(cmd, 0, 1, &r); c->set_scissor = r; }
    }
    if (c->stencil_ref != c->set_stencil) { pvkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, c->stencil_ref); c->set_stencil = c->stencil_ref; }
    if (memcmp(c->blend_factor, c->set_blend, 16)) { pvkCmdSetBlendConstants(cmd, c->blend_factor); memcpy(c->set_blend, c->blend_factor, 16); }
    {
        VkBuffer bufs[4];
        VkDeviceSize offs[4], strides[4], sizes[4];
        int changed = 0;
        for (i = 0; i < 4; i++) {
            oa_buffer *b = c->vb[i];
            if (b && !b->ring) {
                bufs[i] = b->gens[b->cur].buffer.buf;
                offs[i] = c->vb_off[i];
                strides[i] = c->vb_stride[i];
                b->gens[b->cur].last_used = g_frame;
            } else {
                bufs[i] = g_zero_ubo.buf;
                offs[i] = 0;
                strides[i] = 0;
            }
            sizes[i] = VK_WHOLE_SIZE;
            if (bufs[i] != c->set_vb[i] || offs[i] != c->set_off[i] || strides[i] != c->set_stride[i]) changed = 1;
        }
        if (changed) {
            pvkCmdBindVertexBuffers2(cmd, 0, 4, bufs, offs, sizes, strides);
            g_c_vb++;
            memcpy(c->set_vb, bufs, sizeof(bufs));
            memcpy(c->set_off, offs, sizeof(offs));
            memcpy(c->set_stride, strides, sizeof(strides));
        }
    }
    const prog_entry *prog = fast ? c->selected_pipe->program : program_of(c->vs, c->ps);
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (batch && c->cached_material_set && c->cached_program == prog &&
        c->cached_material_epoch == c->material_epoch && c->cached_pool_epoch == g_desc_pool_epoch &&
        c->cached_af_generation == af_generation) {
        draw_descriptors(c, &d, 0, 1);
        if (d.nbuf == c->cached_uniform_count &&
            !memcmp(d.buf, c->cached_uniforms, d.nbuf * sizeof(d.buf[0]))) {
            set = c->cached_material_set;
            ++batch_descriptor_reused;
            if (pass_frames) {
                uint32_t offsets[32], count = d.nbuf;
                memcpy(offsets, d.dyn, count * sizeof(offsets[0]));
                draw_descriptors(c, &d, 0, 0);
                if (desc_get(prog, &d, 1) != set || d.nbuf != count ||
                    memcmp(offsets, d.dyn, count * sizeof(offsets[0])))
                    rt_fatal("reused descriptor differs from full lookup");
                ++batch_verified;
            }
        }
    }
    if (!set) {
        draw_descriptors(c, &d, !fast, 0);
        if (d.nbuf || d.nimg) {
            set = desc_get(prog, &d, fast);
            if (batch && set) {
                c->cached_program = prog;
                c->cached_material_epoch = c->material_epoch;
                c->cached_af_generation = af_generation;
                c->cached_pool_epoch = g_desc_pool_epoch;
                c->cached_material_set = set;
                c->cached_uniform_count = d.nbuf;
                memcpy(c->cached_uniforms, d.buf, d.nbuf * sizeof(d.buf[0]));
            }
        }
    }
    if (set) {
        if (set != c->set_desc || prog->layout != c->set_layout || d.nbuf != c->set_ndyn || memcmp(d.dyn, c->set_dyn, d.nbuf * 4)) {
            pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prog->layout, 0, 1, &set, d.nbuf, d.dyn);
            g_c_push++;
            c->set_desc = set;
            c->set_layout = prog->layout;
            c->set_ndyn = d.nbuf;
            memcpy(c->set_dyn, d.dyn, d.nbuf * 4);
        }
    }
    return 1;
}

static int pred_skips(const oa_context *c) {
    const oa_pred *p = c->pred;
    if (!p || !g_cull || p->visible || p->tested_frame + 8 < g_frame) return 0;
    g_n_culled++;
    return 1;
}

/* Hidden objects are tested every frame. Visible ones only every 4th, since each test costs two Vulkan calls. */
static int query_begin(oa_context *c) {
    oa_pred *p = c->q_active;
    oa_frame *f = g_cur;
    uint32_t i;
    if (!p) return 0;
    if (p->visible && ((g_frame + ((uintptr_t)p >> 6)) & 3)) { g_n_test_skipped++; return 0; }
    if (!g_cull && __atomic_load_n(&oa_fast_pass_batches, __ATOMIC_RELAXED)) {
        ++batch_queries_omitted;
        return 0;
    }
    if (f->qused >= QUERY_MAX) return 0;
    i = f->qused++;
    f->qlist[i] = p;
    pvkCmdBeginQuery(f->cmd, f->qpool, i, 0);
    g_n_tests++;
    return 1;
}
static void query_end(void) { pvkCmdEndQuery(g_cur->cmd, g_cur->qpool, g_cur->qused - 1); }

#include "oa_hdr_waterfall.inc"

static int skip_intro_cart_particle(const oa_context *c)
{
    // Confine the two opening cart particle materials to this workaround.
    if (!c->ps || !c->ps->cart_particle ||
        !__atomic_load_n(&intro_cart_camera, __ATOMIC_RELAXED))
        return 0;
    __atomic_fetch_add(&intro_cart_particle_draws[c->ps->cart_particle - 1], 1, __ATOMIC_RELAXED);
    return 1;
}

static void draw_i(oa_context *c, UINT n, UINT first) {
    int q;
    if (skip_intro_cart_particle(c) || pred_skips(c) || !prepare_draw(c)) return;
    q = query_begin(c);
    pass_record_draw(c, 0, n, first, 0);
    pvkCmdDraw(g_cur->cmd, n, 1, first, 0);
    if (q) query_end();
    if (c->ps && c->ps->hash) hdr_capture_merge(c);
}
static void draw_indexed_i(oa_context *c, UINT n, UINT first, INT base) {
    if (!c->ib || c->ib->ring) { g_n_skip_ib++; return; }
    if (skip_intro_cart_particle(c) || pred_skips(c) || !prepare_draw(c)) return;
    c->ib->gens[c->ib->cur].last_used = g_frame;
    {
        VkBuffer b = c->ib->gens[c->ib->cur].buffer.buf;
        uint32_t ty = c->ib_fmt == 42 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        if (b != c->set_ib || c->ib_off != c->set_ib_off || ty != c->set_ib_type) {
            pvkCmdBindIndexBuffer(g_cur->cmd, b, c->ib_off, (VkIndexType)ty);
            g_c_ib++;
            c->set_ib = b; c->set_ib_off = c->ib_off; c->set_ib_type = ty;
        }
    }
    {
        int q = query_begin(c);
        pass_record_draw(c, 1, n, first, base);
        pvkCmdDrawIndexed(g_cur->cmd, n, 1, first, base, 0);
        if (q) query_end();
        if (c->ps && c->ps->hash) hdr_capture_merge(c);
    }
}

static int g_sample;
#define TIMED(store, call) do { if (g_sample) { LARGE_INTEGER t0_, t1_; QueryPerformanceCounter(&t0_); call; QueryPerformanceCounter(&t1_); store += t1_.QuadPart - t0_.QuadPart; } else { call; } } while (0)
static void COM c_draw(oa_context *c, UINT n, UINT first) { TIMED(g_t_draw, draw_i(c, n, first)); }
static void COM c_draw_indexed(oa_context *c, UINT n, UINT first, INT base) { TIMED(g_t_draw, draw_indexed_i(c, n, first, base)); }
static HRESULT COM c_map(oa_context *c, obj *res, UINT sub, UINT type, UINT flags, oa_mapped *m) { HRESULT hr; TIMED(g_t_map, hr = map_i(c, res, sub, type, flags, m)); return hr; }
static void COM c_unmap(oa_context *c, obj *res, UINT sub) { TIMED(g_t_unmap, unmap_i(c, res, sub)); }

/* ---- DXGI ---- */
static HRESULT COM x_get_parent(obj *o, const GUID *iid, void **out) {
    obj *p = o->kind == K_DXGI_DEVICE ? g_adapter : g_factory;
    (void)iid;
    o_addref(p);
    *out = p;
    return S_OK_;
}
static HRESULT COM x_get_adapter(obj *o, void **out) { (void)o; o_addref(g_adapter); *out = g_adapter; return S_OK_; }
static HRESULT COM x_set_priority(obj *o, INT p) { (void)o; (void)p; return S_OK_; }

static HRESULT COM a_enum_outputs(obj *o, UINT i, void **out) { (void)o; (void)i; *out = NULL; return DXGI_NOT_FOUND; }
static HRESULT COM a_get_desc(obj*o,DXGI_ADAPTER_DESC*d){(void)o;VkPhysicalDeviceProperties p;pvkGetPhysicalDeviceProperties(g_phys,&p);memset(d,0,sizeof(*d));for(unsigned i=0;i<127&&p.deviceName[i];i++)d->Description[i]=(unsigned char)p.deviceName[i];d->VendorId=p.vendorID;d->DeviceId=p.deviceID;for(unsigned i=0;i<g_mem.memoryHeapCount;i++)if(g_mem.memoryHeaps[i].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)d->DedicatedVideoMemory+=g_mem.memoryHeaps[i].size;return S_OK_;}

static VkResult create_swapchain(oa_chain *s) {
    VkSurfaceCapabilitiesKHR cap;
    VkSwapchainCreateInfoKHR ci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    VkSurfaceFormatKHR fmts[128];
    uint32_t n = 128, i;
    VkBool32 sup = 0;
    VkResult query=pvkGetPhysicalDeviceSurfaceSupportKHR(g_phys,g_qfam,s->surface,&sup);
    if(query!=VK_SUCCESS)return query;
    if(!sup)return VK_ERROR_INITIALIZATION_FAILED;
    query=pvkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_phys,s->surface,&cap);
    if(query!=VK_SUCCESS)return query;
    VkResult formats=pvkGetPhysicalDeviceSurfaceFormatsKHR(g_phys,s->surface,&n,fmts);
    if(formats!=VK_SUCCESS && formats!=VK_INCOMPLETE)return formats;
    if(!n)return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if(!(cap.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))return VK_ERROR_FORMAT_NOT_SUPPORTED;
    uint32_t mode_count=0;
    VkPresentModeKHR *modes=NULL;
    VkResult mode_result=pvkGetPhysicalDeviceSurfacePresentModesKHR(g_phys,s->surface,&mode_count,NULL);
    if(mode_result==VK_SUCCESS && mode_count && mode_count<=1024){
        modes=calloc(mode_count,sizeof(*modes));
        if(modes)mode_result=pvkGetPhysicalDeviceSurfacePresentModesKHR(g_phys,s->surface,&mode_count,modes);
        else mode_result=VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    ci.presentMode=mode_result==VK_SUCCESS && modes?oa_present_mode(modes,mode_count,s->mailbox_requested):VK_PRESENT_MODE_FIFO_KHR;
    if(!s->mode_logged){
        logf_("PRESENT startup headroom_requested=%d modes_query=%d modes=%u; throughput is render-submit loop iterations; successful submits/requests counted separately; displayed FPS unmeasured",
              s->mailbox_requested,mode_result,mode_count);
        if(mode_result==VK_SUCCESS && modes)for(uint32_t j=0;j<mode_count;++j)logf_("PRESENT supported_mode=%d",modes[j]);
        s->mode_logged=1;
    }
    free(modes);
    uint32_t selected = 0;
    for (i = 0; i < n; ++i) {
        if (fmts[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
        selected = i;
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) break;
    }
    if (hdr_requested && hdr_colorspace && (cap.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        for (i = 0; i < n; ++i) {
            VkFormat format = fmts[i].format;
            if (fmts[i].colorSpace != VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                (format != VK_FORMAT_A2B10G10R10_UNORM_PACK32 && format != VK_FORMAT_A2R10G10B10_UNORM_PACK32))
                continue;
            if (!hdr_output_pipeline(format) || !hdr_texture_view(s->back)) continue;
            selected = i;
            break;
        }
    }
    s->swap_fmt = fmts[selected].format;
    ci.imageColorSpace = fmts[selected].colorSpace;
    int hdr_format = ci.imageColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT;
    if (hdr_requested && !hdr_format) {
        logf_("HDR unavailable on this surface: colorspace_extension=%d usage=%#x formats=%u", hdr_colorspace, cap.supportedUsageFlags, n);
        for (i = 0; i < n; ++i) logf_("HDR surface offer format=%d colorspace=%d", fmts[i].format, fmts[i].colorSpace);
    }
    s->extent = cap.currentExtent;
    if (s->extent.width == UINT32_MAX) {
        extern int oa_android_buffer_width(void), oa_android_buffer_height(void);
        uint32_t width = oa_android_buffer_width(), height = oa_android_buffer_height();
        s->extent.width = width < cap.minImageExtent.width ? cap.minImageExtent.width :
            width > cap.maxImageExtent.width ? cap.maxImageExtent.width : width;
        s->extent.height = height < cap.minImageExtent.height ? cap.minImageExtent.height :
            height > cap.maxImageExtent.height ? cap.maxImageExtent.height : height;
    }
    if(!s->extent.width || !s->extent.height)return VK_NOT_READY;
    ci.surface = s->surface;
    uint32_t desired=ci.presentMode==VK_PRESENT_MODE_MAILBOX_KHR?3:2;
    ci.minImageCount = cap.minImageCount < desired ? desired : cap.minImageCount;
    if (cap.maxImageCount && ci.minImageCount > cap.maxImageCount) ci.minImageCount = cap.maxImageCount;
    ci.imageFormat = s->swap_fmt;
    ci.imageExtent = s->extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = hdr_format ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    /* The original engine renders upright Android window coordinates; it does
       not pre-rotate pixels into the display's native portrait orientation. */
    ci.preTransform = (cap.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : cap.currentTransform;
    logf_("surface transform current=%#x supported=%#x chosen=%#x extent=%ux%u window=%ux%u",
        cap.currentTransform, cap.supportedTransforms, ci.preTransform,
        s->extent.width,s->extent.height,s->w,s->h);
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.clipped = VK_TRUE;
    if(s->present.current){
        EnterCriticalSection(&g_lock);
        VkResult idle=pvkQueueWaitIdle(g_queue);
        LeaveCriticalSection(&g_lock);
        if(idle!=VK_SUCCESS){logf_("PRESENT recreate queue idle failed %d",idle);return idle;}
    }
    VkResult result=oa_present_replace(&s->present,&g_present_ops,g_dev,&ci);
    if(result!=VK_SUCCESS && ci.presentMode==VK_PRESENT_MODE_MAILBOX_KHR &&
       oa_present_startup_retry_allowed(&s->present)){
        logf_("PRESENT MAILBOX creation failed %d; fallback FIFO",result);
        ci.presentMode=VK_PRESENT_MODE_FIFO_KHR;
        ci.minImageCount=cap.minImageCount<2?2:cap.minImageCount;
        if(cap.maxImageCount && ci.minImageCount>cap.maxImageCount)ci.minImageCount=cap.maxImageCount;
        result=oa_present_replace(&s->present,&g_present_ops,g_dev,&ci);
    }
    if(result!=VK_SUCCESS){logf_("PRESENT swapchain failed %d (retired-handle retry forbidden)",result);return result;}
    hdr_active = hdr_format;
    if (hdr_active) hdr_metadata(s->present.current->handle);
    logf_("HDR actual=%d requested=%d colorspace=%d format=%d engine=%ux%u surface=%ux%u frame=%llu",
          hdr_active, hdr_requested, ci.imageColorSpace, s->swap_fmt, s->w, s->h,
          s->extent.width, s->extent.height, (unsigned long long)g_frame);
    s->present_mode=ci.presentMode;
    s->configured_transform=ci.preTransform;
    logf_("PRESENT swapchain %ux%u format=%d mode=%d requested_images=%u actual_images=%u caps=%u..%u",
          s->extent.width,s->extent.height,s->swap_fmt,s->present_mode,ci.minImageCount,
          s->present.current->count,cap.minImageCount,cap.maxImageCount);
    return VK_SUCCESS;
}

static VkResult present_check_surface(oa_chain *s)
{
    if(!s->present.recheck || s->present.recreate || g_frame<s->next_surface_check)return VK_SUCCESS;
    s->next_surface_check=g_frame+120;
    s->present.recheck=0;
    VkSurfaceCapabilitiesKHR cap;
    VkResult r=pvkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_phys,s->surface,&cap);
    if(r!=VK_SUCCESS){logf_("PRESENT capability recheck result=%d",r);return r;}
    extern int oa_android_buffer_width(void),oa_android_buffer_height(void);
    VkExtent2D effective;
    VkSurfaceTransformFlagBitsKHR transform;
    int changed=oa_present_surface_changed(s->extent,s->configured_transform,
        s->present.current?s->present.current->count:0,&cap,
        oa_android_buffer_width(),oa_android_buffer_height(),&effective,&transform);
    if(changed)s->present.recreate=1;
    logf_("PRESENT SUBOPTIMAL recheck changed=%d extent=%ux%u->%ux%u chosen_transform=%#x->%#x surface_current=%#x",
        changed,s->extent.width,s->extent.height,effective.width,effective.height,
        s->configured_transform,transform,cap.currentTransform);
    return VK_SUCCESS;
}

static void present_gc_orphans(void)
{
    oa_present_orphan **at=&g_present_orphans;
    while(*at){
        oa_present_orphan *o=*at;
        OaPresentGeneration **g=&o->generations;
        while(*g){
            OaPresentGeneration *candidate=*g;
            if(oa_present_fences_done(candidate,&g_present_ops,g_dev)){
                *g=candidate->next;oa_present_free(&g_present_ops,g_dev,candidate);
            }else g=&candidate->next;
        }
        if(!o->generations){
            *at=o->next;pvkDestroySurfaceKHR(g_instance,o->surface,NULL);
            if(o->native_window)ANativeWindow_release(o->native_window);
            free(o);--g_present_orphan_count;
            logf_("WINDOW old presentation resources collected; retained=%u",g_present_orphan_count);
        }else at=&o->next;
    }
}
static void present_gc_completed(uint64_t frame)
{
    for(oa_chain *s=g_present_chains;s;s=s->present_next)
        oa_present_collect(&s->present,&g_present_ops,g_dev,frame);
    present_gc_orphans();
}
/* Caller has completed the GPU work using this chain. WSI completion is separate. */
static void present_detach_surface(oa_chain *s)
{
    OaPresentGeneration *held=oa_present_release(&s->present,&g_present_ops,g_dev);
    if(held){
        present_gc_orphans();
        if(g_present_orphan_count>=OA_MAX_RETAINED_WINDOWS)
            rt_fatal("Android window retirement limit reached (%u); restart app; WSI fences have not completed",g_present_orphan_count);
        oa_present_orphan *orphan=calloc(1,sizeof(*orphan));
        if(!orphan)rt_fatal("present retirement metadata allocation failed");
        orphan->generations=held;orphan->surface=s->surface;orphan->native_window=s->native_window;
        orphan->next=g_present_orphans;g_present_orphans=orphan;++g_present_orphan_count;
        logf_("WINDOW detached old generation; retained=%u maintenance=%d",g_present_orphan_count,g_present_ops.maintenance);
    }else{
        if(s->surface)pvkDestroySurfaceKHR(g_instance,s->surface,NULL);
        if(s->native_window)ANativeWindow_release(s->native_window);
    }
    s->surface=VK_NULL_HANDLE;s->native_window=NULL;
    memset(&s->present,0,sizeof(s->present));
}
static VkResult present_ensure_window(oa_chain *s,ANativeWindow *window,uint64_t generation)
{
    if(!window)return VK_NOT_READY;
    present_gc_orphans();
    /* A formerly detached window may return before its WSI fences finish.
       Do not create a second independent swapchain on that same native window. */
    for(oa_present_orphan *o=g_present_orphans;o;o=o->next)
        if(o->native_window==window)return VK_NOT_READY;
    if(s->surface_lost && s->window_generation==generation)return VK_NOT_READY;
    if(s->native_window==window && s->surface && !s->surface_lost){
        if(s->window_generation!=generation){
            s->window_generation=generation;s->present.recreate=1;
            logf_("WINDOW same owned window resumed generation=%llu; recreate using oldSwapchain",(unsigned long long)generation);
        }
        return VK_SUCCESS;
    }
    if(s->surface){
        EnterCriticalSection(&g_lock);
        VkResult idle=pvkQueueWaitIdle(g_queue);
        LeaveCriticalSection(&g_lock);
        if(idle!=VK_SUCCESS)return idle;
        present_detach_surface(s);
        present_gc_orphans();
        for(oa_present_orphan *o=g_present_orphans;o;o=o->next)
            if(o->native_window==window)return VK_NOT_READY;
    }
    ANativeWindow_acquire(window);
#ifdef _WIN32
    OaWin32SurfaceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandleW(NULL),
        .hwnd = (HWND)window,
    };
    VkResult result = pvkCreateWin32SurfaceKHR(g_instance, &info, NULL, &s->surface);
#else
    VkAndroidSurfaceCreateInfoKHR info={VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    info.window=window;
    VkResult result=pvkCreateAndroidSurfaceKHR(g_instance,&info,NULL,&s->surface);
#endif
    if(result!=VK_SUCCESS){ANativeWindow_release(window);return result;}
    s->native_window=window;s->window_generation=generation;s->surface_lost=0;
    s->present.recreate=1;s->next_surface_check=0;s->suboptimal_reported=0;
    logf_("WINDOW attached generation=%llu window=%p new_surface=%p",(unsigned long long)generation,(void*)window,(void*)s->surface);
    return VK_SUCCESS;
}
static void present_chain_release(oa_chain *s)
{
    oa_chain **at=&g_present_chains;
    while(*at && *at!=s)at=&(*at)->present_next;
    if(*at)*at=s->present_next;
    present_detach_surface(s);
    if(s->back)o_release(&s->back->o);
}

static void make_back(oa_chain *s) {
    uint32_t d[11] = {0};
    d[0] = s->w; d[1] = s->h; d[2] = 1; d[3] = 1; d[4] = 28; d[5] = 1; d[7] = 0; d[8] = 0x28;
    create_texture(d, NULL, &s->back, 0);
    s->back->is_backbuffer = 1;
    s->back->fmt = s->fmt;
    s->back->desc[4] = s->fmt;
}

static HRESULT COM f_create_chain(obj *f, obj *device, const DXGI_SWAP_CHAIN_DESC *desc, oa_chain **out) {
    oa_chain *s = (oa_chain *)new_obj(sizeof(oa_chain), K_CHAIN);
    (void)f; (void)device;
    extern int oa_present_headroom_mailbox;
    s->mailbox_requested=oa_present_headroom_mailbox;
    s->w=desc->BufferDesc.Width;s->h=desc->BufferDesc.Height;s->fmt=desc->BufferDesc.Format;s->hwnd=desc->OutputWindow;
    uint64_t generation;
    ANativeWindow *window=oa_android_present_lock(&generation);
    VkResult attached=present_ensure_window(s,window,generation);
    if(attached==VK_SUCCESS)attached=create_swapchain(s);
    oa_android_present_unlock();
    if(attached!=VK_SUCCESS && attached!=VK_NOT_READY){
        logf_("WINDOW initial attach failed %d",attached);
        present_chain_release(s);free(s);return E_FAIL_;
    }
    s->present_next=g_present_chains;g_present_chains=s;
    make_back(s);
    logf_("chain %ux%u fmt %u hwnd %p", s->w, s->h, s->fmt, (void *)s->hwnd);
    *out = s;
    return S_OK_;
}
static HRESULT COM f_window_assoc(obj *f, HWND h, UINT flags) { (void)f; (void)h; (void)flags; return S_OK_; }

static HRESULT COM s_get_buffer(oa_chain *s, UINT i, const GUID *iid, void **out) {
    (void)i; (void)iid;
    o_addref(&s->back->o);
    *out = s->back;
    return S_OK_;
}

static void pipeline_cache_maybe_save(uint64_t frame);
static uint64_t g_frames_shown;
static int64_t g_t_wait, g_t_acquire, g_t_submit;
void oa_perf_frame(uint64_t,unsigned,unsigned);
#include "oa_waterfall.inc"

static HRESULT COM s_present(oa_chain *s, UINT sync, UINT flags) {
    LARGE_INTEGER q0, q1, q2;
    oa_frame *f = g_cur;
    oa_context *c = g_ctx;
    uint32_t idx = UINT32_MAX;
    OaPresentAcquired acquired={VK_ERROR_OUT_OF_DATE_KHR,UINT32_MAX,0,0};
    VkResult r;
    VkImageBlit b = {0};
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if(!s->sync_logged){
        s->original_sync=sync;s->original_flags=flags;s->sync_logged=1;
        logf_("PRESENT original SyncInterval=%u flags=%#x; backend_mode=%d headroom=%d (simulation cadence not qualified)",
              sync,flags,s->present_mode,s->mailbox_requested);
    }
    end_rendering(c);
    QueryPerformanceCounter(&q0);
    hdr_poll(s);
    af_poll();
    oa_waterfall_render(s, f, c);
    uint64_t window_generation;
    ANativeWindow *window=oa_android_present_lock(&window_generation);
    r=present_ensure_window(s,window,window_generation);
    if(r!=VK_SUCCESS && r!=VK_NOT_READY)rt_fatal("Vulkan Android window attach failed: %d",r);
    int window_ready=r==VK_SUCCESS;
    if(!window)++g_present_no_window;
    if(window_ready){
        r=present_check_surface(s);
        if(r==VK_ERROR_SURFACE_LOST_KHR){
            s->surface_lost=1;window_ready=0;
            logf_("WINDOW surface lost during capability recheck; wait for next Android generation");
        }else if(r!=VK_SUCCESS)rt_fatal("Vulkan surface capability recheck failed: %d",r);
    }
    if(!window_ready)acquired.result=VK_NOT_READY;
    for(unsigned attempt=0;window_ready && attempt<2;++attempt){
        if(s->present.recreate || !s->present.current){
            VkResult created=create_swapchain(s);
            if(created!=VK_SUCCESS){
                acquired.result=created;
                acquired.index=UINT32_MAX;
                acquired.valid=0;
                break;
            }
        }
        acquired=oa_present_acquire(&s->present,&g_present_ops,g_dev,f->acquired);
        if(acquired.valid){
            if(acquired.result==VK_SUBOPTIMAL_KHR && !(s->suboptimal_reported&1)){
                s->suboptimal_reported|=1;
                logf_("PRESENT acquire result=%d SUBOPTIMAL usable; recheck capabilities, no unconditional recreation",acquired.result);
            }
            break;
        }
        if(acquired.result!=VK_ERROR_OUT_OF_DATE_KHR)break;
    }
    QueryPerformanceCounter(&q1);
    g_t_acquire += q1.QuadPart - q0.QuadPart;
    if(acquired.result==VK_ERROR_SURFACE_LOST_KHR){
        s->surface_lost=1;
        logf_("WINDOW acquire surface lost result=%d; wait for next Android generation",acquired.result);
    }
    if(!acquired.valid && acquired.result!=VK_ERROR_OUT_OF_DATE_KHR &&
       acquired.result!=VK_ERROR_SURFACE_LOST_KHR &&
       acquired.result!=VK_TIMEOUT && acquired.result!=VK_NOT_READY)
        rt_fatal("Vulkan acquire failed before blit/submission: %d",acquired.result);
    if(acquired.valid){
        idx=acquired.index;
    if (hdr_active) {
        veil_title_presented(0);
        hdr_render_present(s, f, idx);
    } else {
    barrier_all(f->cmd);
    image_layout(f->cmd, s->present.current->images[idx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    b.srcSubresource.aspectMask = b.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.srcSubresource.layerCount = b.dstSubresource.layerCount = 1;
    b.srcOffsets[1].x = (int32_t)s->w; b.srcOffsets[1].y = (int32_t)s->h; b.srcOffsets[1].z = 1;
    OaViewport viewport = oa_centered_viewport(s->extent.width, s->extent.height, s->w, s->h);
    OaViewport crop = oa_title_crop(s->extent.width, s->extent.height, s->w, s->h);
    int cover = VEIL_COVER_NONE;
    if (crop.width < (int)s->w) {
        if (veil_options_is_visible())
            cover = VEIL_COVER_OPTIONS;
        else if (veil_title_is_visible())
            cover = VEIL_COVER_TITLE;
    }
    if (cover) {
        int offset = cover == VEIL_COVER_TITLE ? crop.x : 0;
        b.srcOffsets[0].x = offset;
        b.srcOffsets[1].x = offset + crop.width;
        viewport = (OaViewport){.width = (int)s->extent.width, .height = (int)s->extent.height};
    }
    veil_title_presented(cover);
    if (viewport.width != (int32_t)s->extent.width || viewport.height != (int32_t)s->extent.height) {
        const VkClearColorValue black = { .float32 = {0, 0, 0, 1} };
        const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        pvkCmdClearColorImage(f->cmd, s->present.current->images[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
        VkImageMemoryBarrier clear = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s->present.current->images[idx],
            .subresourceRange = range,
        };
        pvkCmdPipelineBarrier(f->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, NULL, 0, NULL, 1, &clear);
    }
    b.dstOffsets[0] = (VkOffset3D){viewport.x, viewport.y, 0};
    b.dstOffsets[1] = (VkOffset3D){viewport.x + viewport.width, viewport.y + viewport.height, 1};
    pvkCmdBlitImage(f->cmd, s->back->image, VK_IMAGE_LAYOUT_GENERAL, s->present.current->images[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
    image_layout(f->cmd, s->present.current->images[idx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    }else{
        ++g_present_deferred;
        if(window_ready || (g_present_deferred%120)==1)logf_("PRESENT deferred acquire=%d window_ready=%d; offscreen submit only",acquired.result,window_ready);
    }
    if(pvkEndCommandBuffer(f->cmd)!=VK_SUCCESS)rt_fatal("Vulkan frame command buffer end failed");
    si.waitSemaphoreCount = acquired.valid?1:0;
    si.pWaitSemaphores = &f->acquired;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f->cmd;
    si.signalSemaphoreCount = acquired.valid?1:0;
    si.pSignalSemaphores = acquired.valid?&s->present.current->finished[idx]:NULL;
    QueryPerformanceCounter(&q1);
    EnterCriticalSection(&g_lock);
    r=pvkQueueSubmit(g_queue,1,&si,f->fence);
    if(r!=VK_SUCCESS){
        LeaveCriticalSection(&g_lock);
        rt_fatal("Vulkan queue submit failed; frame fence not armed: %d",r);
    }
    f->submitted=1;
    ++g_present_submitted;
    oa_present_submitted(&s->present,&acquired,g_frame);
    if(acquired.valid){
        pi.waitSemaphoreCount=1;
        pi.pWaitSemaphores=&s->present.current->finished[idx];
        pi.swapchainCount=1;
        pi.pSwapchains=&s->present.current->handle;
        pi.pImageIndices=&idx;
        VkSwapchainPresentFenceInfoEXT present_fence={VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
        if(acquired.present_fence){
            present_fence.swapchainCount=1;present_fence.pFences=&acquired.present_fence;
            pi.pNext=&present_fence;
            oa_present_fence_queued(s->present.current,acquired.present_fence);
        }
        r=pvkQueuePresentKHR(g_queue,&pi);
        oa_present_result(&s->present,idx,r);
        if(r==VK_SUBOPTIMAL_KHR && !(s->suboptimal_reported&2)){
            s->suboptimal_reported|=2;
            logf_("PRESENT present result=%d SUBOPTIMAL usable; recheck capabilities, no unconditional recreation",r);
        }
        if(r==VK_SUCCESS || r==VK_SUBOPTIMAL_KHR)++g_present_requests;
        else if(r==VK_ERROR_OUT_OF_DATE_KHR)
            logf_("PRESENT out of date after submit; recreate next frame");
        else if(r==VK_ERROR_SURFACE_LOST_KHR){
            s->surface_lost=1;
            logf_("WINDOW present surface lost result=%d; wait for next Android generation",r);
        }else{
            LeaveCriticalSection(&g_lock);
            rt_fatal("Vulkan present failed after valid submission: %d",r);
        }
    }
    LeaveCriticalSection(&g_lock);
    oa_android_present_unlock();
    if(!window_ready)usleep(16000); /* Avoid an uncapped render loop while the app has no surface. */
    QueryPerformanceCounter(&q2);
    g_t_submit += q2.QuadPart - q1.QuadPart;
    g_sample = (g_frames_shown & 7) == 0;
    if (g_frames_shown % 120 == 0) {
        pipeline_cache_maybe_save(g_frames_shown);
        if (g_frames_shown % 600 == 0) {
            rt_log("TEXTURE_UPLOAD_COUNTS queued=%llu sync=%llu staging_bytes=%llu",
                   (unsigned long long)__atomic_load_n(&texture_upload_calls,__ATOMIC_RELAXED),
                   (unsigned long long)__atomic_load_n(&texture_upload_fallbacks,__ATOMIC_RELAXED),
                   (unsigned long long)__atomic_load_n(&texture_upload_staging_bytes,__ATOMIC_RELAXED));
        }
        int want = GetFileAttributesA("Z:\\storage\\emulated\\0\\Download\\oa\\nocull") == INVALID_FILE_ATTRIBUTES && GetFileAttributesA("nocull") == INVALID_FILE_ATTRIBUTES;
        if (want != g_cull) { g_cull = want; logf_("culling %s at frame %llu", want ? "on" : "off", (unsigned long long)g_frames_shown); }
    }

    static int last_draw_preparation = -1;
    int draw_preparation = __atomic_load_n(&oa_fast_draw_preparation, __ATOMIC_RELAXED);
    if (draw_preparation != last_draw_preparation && g_n_draw) {
        logf_("draw preparation used %s", draw_preparation ? "cached pipeline and lazy descriptor writes" : "full pipeline lookup and eager descriptor writes");
        last_draw_preparation = draw_preparation;
    }
    if (++g_frames_shown % 600 == 1) {
        static DWORD last_tick;
        static long last_draws, last_skipq;
        DWORD now = GetTickCount();
        LARGE_INTEGER fq;
        double ms;
        QueryPerformanceFrequency(&fq);
        ms = 1000.0 / (double)fq.QuadPart / 600.0;
        logf_("frame %llu fps %.1f draws/f %ld skipq/f %ld keptq %ld pipes %u | ms/f gpuwait %.2f acquire %.2f submit %.2f | uploads %lld %.1f ms total | null srv %ld dim bad %ld", (unsigned long long)g_frames_shown, last_tick && now != last_tick ? 600000.0 / (now - last_tick) : 0.0, (g_n_draw - last_draws) / 600, (g_n_skip_query - last_skipq) / 600, g_n_query_kept, g_pipes_n, g_t_wait * ms, g_t_acquire * ms, g_t_submit * ms, (long long)__atomic_exchange_n(&g_uploads,0,__ATOMIC_RELAXED), (double)__atomic_exchange_n(&g_t_upload,0,__ATOMIC_RELAXED) * 1000.0 / (double)fq.QuadPart, g_n_null_srv, g_n_dim_bad);
        logf_("  ms/f draw calls %.2f map %.2f unmap %.2f | per frame bindset %ld newset %ld vb %ld pipe %ld ib %ld | sets %u pools %ld | unbound %ld no-view %ld", g_t_draw * ms * 8, g_t_map * ms * 8, g_t_unmap * ms * 8, g_c_push / 600, g_c_set_miss / 600, g_c_vb / 600, g_c_pipe / 600, g_c_ib / 600, g_desc_n, g_c_pools, g_n_null_srv, g_n_no_view);
        g_c_set_miss = 0;
        logf_("PRESENT counts submitted=%llu gpu_completed=%llu accepted_requests=%llu deferred=%llu no_window=%llu window_generation=%llu mode=%d displayed_fps=unmeasured",
              (unsigned long long)g_present_submitted,(unsigned long long)g_present_completed,
              (unsigned long long)g_present_requests,(unsigned long long)g_present_deferred,
              (unsigned long long)g_present_no_window,(unsigned long long)window_generation,s->present_mode);
        logf_("  culling %d: hidden skipped/f %ld tests/f %ld tests saved/f %ld", g_cull, g_n_culled / 600, g_n_tests / 600, g_n_test_skipped / 600);
        g_n_culled = g_n_tests = g_n_test_skipped = 0;
        g_t_draw = g_t_map = g_t_unmap = 0; g_c_push = g_c_vb = g_c_pipe = g_c_ib = 0;
        g_t_wait = g_t_acquire = g_t_submit = 0;
        last_draws = g_n_draw; last_skipq = g_n_skip_query;
        last_tick = now;
    }
    pass_audit_present();
    g_frame++;
    {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        frame_begin();
        QueryPerformanceCounter(&t1);
        g_t_wait += t1.QuadPart - t0.QuadPart;
    }
    c->pipeline_valid = 0;
    c->cache_valid = 0;
    oa_perf_frame((uint64_t)g_n_draw,s->w,s->h);
    return S_OK_;
}
static HRESULT COM s_get_desc(oa_chain*s,DXGI_SWAP_CHAIN_DESC*d){memset(d,0,sizeof(*d));d->BufferDesc.Width=s->w;d->BufferDesc.Height=s->h;d->BufferDesc.Format=s->fmt;d->SampleDesc.Count=1;d->BufferUsage=0x20;d->BufferCount=1;d->OutputWindow=s->hwnd;d->Windowed=TRUE;return S_OK_;}
static HRESULT COM s_get_output(oa_chain*s,void**out){(void)s;if(!out)return E_INVALIDARG;*out=NULL;return DXGI_NOT_FOUND;}
static HRESULT COM s_set_fullscreen(oa_chain *s, BOOL f, void *t) { (void)s; (void)f; (void)t; return S_OK_; }
static HRESULT COM s_get_fullscreen(oa_chain *s, BOOL *f, void **t) { (void)s; if (f) *f = FALSE; if (t) *t = NULL; return S_OK_; }
static HRESULT COM s_resize(oa_chain *s, UINT n, UINT w, UINT h, UINT fmt, UINT fl) {
    (void)n; (void)fl;
    if (w) s->w = w;
    if (h) s->h = h;
    if (fmt) s->fmt = fmt;
    logf_("ResizeBuffers %ux%u", s->w, s->h);
    end_rendering(g_ctx);
    o_release(&s->back->o);
    make_back(s);
    return S_OK_;
}
static HRESULT COM s_resize_target(oa_chain *s, const void *d) { (void)s; (void)d; return S_OK_; }

/* ---- resource getters ---- */
static void COM t_get_desc(oa_tex *t, uint32_t *d) { memcpy(d, t->desc, (t->type3d ? 9 : 11) * 4); if (!t->type3d) d[2] = t->mips; else d[3] = t->mips; }
static void COM b_get_desc(oa_buffer *b, uint32_t *d) { d[0] = b->size; d[1] = b->usage; d[2] = b->bind; d[3] = b->cpu; d[4] = b->misc; d[5] = b->stride; }
static void COM r_get_type(obj *o, UINT *t) { *t = o->kind == K_BUFFER ? 1 : (((oa_tex *)o)->type3d ? 4 : 3); }
static void COM v_get_resource(oa_view *v, obj **out) { o_addref(&v->tex->o); *out = &v->tex->o; }
static void COM child_get_device(obj *o, obj **out) { (void)o; o_addref(&g_device_obj); *out = &g_device_obj; }

/* ---- setup ---- */
static void init_vtables(void) {
    static const uint8_t CHILD[] = {3, 1, 1, 2, 4, 4, 3, 2, 2, 1, 2, 2, 2, 2, 2};
    static const uint8_t DXGI_DEV[] = {3, 1, 1, 4, 3, 4, 3, 2, 6, 4, 2, 2};
    static const uint8_t ADAPTER[] = {3, 1, 1, 4, 3, 4, 3, 3, 2, 3};
    static const uint8_t FACTORY[] = {3, 1, 1, 4, 3, 4, 3, 3, 3, 2, 4, 3};
    static const uint8_t CHAIN[] = {3, 1, 1, 4, 3, 4, 3, 3, 3, 4, 3, 3, 2, 6, 2, 2, 2, 2};
    void **v;
    int k;
    /* Every method fallback is native C; no executable guest-code allocation. */
#define COMMON(v) (v)[0] = (void *)o_qi; (v)[1] = (void *)o_addref; (v)[2] = (void *)o_release
    v = make_vtable("Device", DEVICE_NAMES, DEVICE_ARGS, 43);
    COMMON(v);
    v[3] = (void *)d_create_buffer; v[5] = (void *)d_create_tex2d; v[6] = (void *)d_create_tex3d; v[7] = (void *)d_create_srv; v[9] = (void *)d_create_rtv; v[10] = (void *)d_create_dsv;
    v[11] = (void *)d_create_layout; v[12] = (void *)d_create_vs; v[15] = (void *)d_create_ps; v[20] = (void *)d_create_blend; v[21] = (void *)d_create_depth; v[22] = (void *)d_create_raster;
    v[23] = (void *)d_create_sampler; v[25] = (void *)d_create_predicate; v[24] = (void *)d_create_predicate; v[27] = (void *)d_deferred; v[29] = (void *)d_check_format; v[30] = (void *)d_check_msaa;
    v[34] = (void *)d_get_private; v[35] = (void *)d_set_private; v[37] = (void *)d_feature_level; v[38] = (void *)d_flags; v[39] = (void *)d_removed; v[40] = (void *)d_immediate;
    g_vt[K_DEVICE] = v;
    v = make_vtable("Context", NULL, CONTEXT_ARGS, 115);
    COMMON(v);
    v[3] = (void *)child_get_device; v[7] = (void *)c_set_vs_cb; v[8] = (void *)c_set_ps_srv; v[9] = (void *)c_set_ps; v[10] = (void *)c_set_ps_smp; v[11] = (void *)c_set_vs; v[12] = (void *)c_draw_indexed; v[13] = (void *)c_draw;
    v[14] = (void *)c_map; v[15] = (void *)c_unmap; v[16] = (void *)c_set_ps_cb; v[17] = (void *)c_set_layout; v[18] = (void *)c_set_vb; v[19] = (void *)c_set_ib; v[24] = (void *)c_set_topo; v[25] = (void *)c_set_vs_srv; v[26] = (void *)c_set_vs_smp;
    v[27] = (void *)c_begin; v[28] = (void *)c_end; v[30] = (void *)c_set_predication; v[33] = (void *)c_set_rts; v[35] = (void *)c_set_blend; v[36] = (void *)c_set_depth; v[43] = (void *)c_set_raster; v[44] = (void *)c_set_viewports; v[45] = (void *)c_set_scissors;
    v[46] = (void *)c_copy_subresource; v[50] = (void *)c_clear_rtv; v[53] = (void *)c_clear_dsv; v[110] = (void *)c_clear_state; v[111] = (void *)c_flush;
    g_vt[K_CONTEXT] = v;
    v = make_vtable("DXGIDevice", NULL, DXGI_DEV, 12);
    COMMON(v);
    v[6] = (void *)x_get_parent; v[7] = (void *)x_get_adapter; v[10] = (void *)x_set_priority;
    g_vt[K_DXGI_DEVICE] = v;
    v = make_vtable("Adapter", NULL, ADAPTER, 10);
    COMMON(v);
    v[6] = (void *)x_get_parent; v[7] = (void *)a_enum_outputs; v[8] = (void *)a_get_desc;
    g_vt[K_ADAPTER] = v;
    v = make_vtable("Factory", NULL, FACTORY, 12);
    COMMON(v);
    v[6] = (void *)x_get_parent; v[8] = (void *)f_window_assoc; v[10] = (void *)f_create_chain;
    g_vt[K_FACTORY] = v;
    v = make_vtable("SwapChain", NULL, CHAIN, 18);
    COMMON(v);
    v[6] = (void *)x_get_parent; v[7] = (void *)child_get_device; v[8] = (void *)s_present; v[9] = (void *)s_get_buffer; v[10] = (void *)s_set_fullscreen; v[11] = (void *)s_get_fullscreen; v[12] = (void *)s_get_desc; v[13] = (void *)s_resize; v[14] = (void *)s_resize_target; v[15]=(void*)s_get_output;
    g_vt[K_CHAIN] = v;
    /* Resources, views, shaders, states: DeviceChild is 7 slots, then a few getters. */
    for (k = K_BUFFER; k <= K_PREDICATE; k++) {
        static const char *const NAMES[] = {"Buffer", "Texture", "SRV", "RTV", "DSV", "InputLayout", "VS", "PS", "Blend", "DepthState", "Raster", "Sampler", "Predicate"};
        v = make_vtable(NAMES[k - K_BUFFER], NULL, CHILD, 15);
        COMMON(v);
        v[3] = (void *)child_get_device;
        v[4] = (void *)d_get_private;
        v[5] = (void *)d_set_private;
        if (k == K_BUFFER || k == K_TEX2D) { v[7] = (void *)r_get_type; v[10] = k == K_BUFFER ? (void *)b_get_desc : (void *)t_get_desc; }
        if (k == K_SRV || k == K_RTV || k == K_DSV) v[7] = (void *)v_get_resource;
        g_vt[k] = v;
    }
}

#include "oa_pipeline_cache.h"

static int init_vulkan(void) {
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkCreateInstance create;
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    const char *IEXT[5] = {"VK_KHR_surface", OA_SURFACE_EXTENSION};
    const char *device_extensions[5] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceMapMemoryPlacedFeaturesEXT placed = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT
    };
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance={
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT
    };
    int maintenance_enabled=0;
    VkPhysicalDevice devs[8];
    uint32_t n = 8, i;
    VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    float prio = 1.0f;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceProperties prop;
    VkQueueFamilyProperties qf[16];
    VkFormatProperties fp;
    if (!lib) { logf_("vulkan-1.dll not found"); return 0; }
    gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
    create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    app.apiVersion = VK_API_VERSION_1_3;
    app.pApplicationName = "Opem Amalur";
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = IEXT;
    PFN_vkEnumerateInstanceExtensionProperties instance_extensions=(PFN_vkEnumerateInstanceExtensionProperties)gipa(NULL,"vkEnumerateInstanceExtensionProperties");
    int instance_maintenance=0,instance_cap2=0;
    uint32_t instance_count=0;
    if(instance_extensions && instance_extensions(NULL,&instance_count,NULL)==VK_SUCCESS && instance_count){
        VkExtensionProperties *available=calloc(instance_count,sizeof(*available));
        if(available && instance_extensions(NULL,&instance_count,available)==VK_SUCCESS){
            for(uint32_t j=0;j<instance_count;++j){
                hdr_colorspace |= !strcmp(available[j].extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
                instance_maintenance|=!strcmp(available[j].extensionName,VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
                instance_cap2|=!strcmp(available[j].extensionName,VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
            }
        }
        free(available);
    }
    if(instance_maintenance && instance_cap2) {
        IEXT[ici.enabledExtensionCount++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
        IEXT[ici.enabledExtensionCount++] = VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
    }
    if(hdr_colorspace) IEXT[ici.enabledExtensionCount++] = VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;

    if (create(&ici, NULL, &g_instance)) { logf_("vkCreateInstance failed"); return 0; }
#define X(f) p##f = (PFN_##f)gipa(g_instance, #f);
    VK_FUNCS(X)
#undef X
    pvkEnumeratePhysicalDevices(g_instance, &n, devs);
    g_phys = devs[0];
    for (i = 0; i < n; i++) {
        pvkGetPhysicalDeviceProperties(devs[i], &prop);
        if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { g_phys = devs[i]; break; }
    }
    pvkGetPhysicalDeviceProperties(g_phys, &prop);
    logf_("GPU %s api %u.%u", prop.deviceName, VK_API_VERSION_MAJOR(prop.apiVersion), VK_API_VERSION_MINOR(prop.apiVersion));
    g_ubo_align = prop.limits.minUniformBufferOffsetAlignment;
    af_device_limit = prop.limits.maxSamplerAnisotropy;
    logf_("AF device sampler limit=%.0f", (double)af_device_limit);
    pvkGetPhysicalDeviceMemoryProperties(g_phys, &g_mem);
    n = 16;
    pvkGetPhysicalDeviceQueueFamilyProperties(g_phys, &n, qf);
    for (i = 0; i < n; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_qfam = i; break; }
    pvkGetPhysicalDeviceFormatProperties(g_phys, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) g_d24s8 = VK_FORMAT_D32_SFLOAT_S8_UINT;
    qi.queueFamilyIndex = g_qfam;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_FALSE;
    f2.pNext = &f13;
    f2.features.textureCompressionBC = VK_TRUE;
    f2.features.samplerAnisotropy = VK_TRUE;
    f2.features.depthClamp = VK_TRUE;
    f2.features.depthBiasClamp = VK_TRUE;
    f2.features.independentBlend = VK_TRUE;
    f2.features.dualSrcBlend = VK_TRUE;
    f2.features.fillModeNonSolid = VK_TRUE;
    f2.features.robustBufferAccess = VK_TRUE;
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qi;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = device_extensions;
    uint32_t extension_count = 0;
    int has_map2 = 0, has_placed = 0, has_maintenance=0, has_metadata=0;
    if (!pvkEnumerateDeviceExtensionProperties(g_phys, NULL, &extension_count, NULL) && extension_count) {
        VkExtensionProperties *extensions = calloc(extension_count, sizeof(*extensions));
        if (extensions && !pvkEnumerateDeviceExtensionProperties(g_phys, NULL, &extension_count, extensions)) {
            for (uint32_t j = 0; j < extension_count; ++j) {
                has_metadata |= !strcmp(extensions[j].extensionName, VK_EXT_HDR_METADATA_EXTENSION_NAME);
                has_maintenance |= !strcmp(extensions[j].extensionName,VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
                has_map2 |= !strcmp(extensions[j].extensionName, VK_KHR_MAP_MEMORY_2_EXTENSION_NAME);
                has_placed |= !strcmp(extensions[j].extensionName, VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME);
            }
        }
        free(extensions);
    }
    if (has_map2 && has_placed) {
        VkPhysicalDeviceFeatures2 features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &placed
        };
        pvkGetPhysicalDeviceFeatures2(g_phys, &features);
        if (placed.memoryMapPlaced) {
            VkPhysicalDeviceMapMemoryPlacedPropertiesEXT limits = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_PROPERTIES_EXT
            };
            VkPhysicalDeviceProperties2 properties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &limits
            };
            pvkGetPhysicalDeviceProperties2(g_phys, &properties);
            g_map_alignment = limits.minPlacedMemoryMapAlignment;
            f13.pNext = &placed;
            device_extensions[1] = VK_KHR_MAP_MEMORY_2_EXTENSION_NAME;
            device_extensions[2] = VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME;
            dci.enabledExtensionCount = 3;
        }
    }
    if(has_maintenance && (instance_maintenance && instance_cap2)){
        VkPhysicalDeviceFeatures2 features={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext=&maintenance;
        pvkGetPhysicalDeviceFeatures2(g_phys,&features);
        if(maintenance.swapchainMaintenance1){
            maintenance.pNext=f13.pNext;f13.pNext=&maintenance;
            device_extensions[dci.enabledExtensionCount++]=VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
            maintenance_enabled=1;
        }
    }
    if (hdr_colorspace && has_metadata)
        device_extensions[dci.enabledExtensionCount++] = VK_EXT_HDR_METADATA_EXTENSION_NAME;
    logf_("WINDOW maintenance extension=%d feature=%u instance_dependencies=%d enabled=%d",
          has_maintenance,maintenance.swapchainMaintenance1,(instance_maintenance && instance_cap2),maintenance_enabled);
    {
        VkResult r = pvkCreateDevice(g_phys, &dci, NULL, &g_dev);
        if (r && dci.enabledExtensionCount > 1) {
            logf_("Vulkan optional device features failed %d, retrying standard device", r);
            f13.pNext = NULL;
            dci.enabledExtensionCount = 1;
            g_map_alignment = 0;maintenance_enabled=0;has_metadata=0;
            r = pvkCreateDevice(g_phys, &dci, NULL, &g_dev);
        }
        if (r) { logf_("vkCreateDevice failed %d", r); return 0; }
    }
#define X(f) p##f = (PFN_##f)pvkGetDeviceProcAddr(g_dev, #f);
    VKD_FUNCS(X)
#undef X
    if (hdr_colorspace && has_metadata)
        hdr_set_metadata = (PFN_vkSetHdrMetadataEXT)pvkGetDeviceProcAddr(g_dev, "vkSetHdrMetadataEXT");
    if (g_map_alignment)
        g_map_placed = (PFN_vkMapMemory2KHR)pvkGetDeviceProcAddr(g_dev, "vkMapMemory2KHR");
    g_dynamic_map_mode = g_map_placed != NULL;
    texture_upload_mode=1;
    rt_log("TEXTURE_UPLOAD_QUEUE mode=%d slots=%u max_bytes=%u",texture_upload_mode,OA_UPLOAD_SLOTS,OA_UPLOAD_LIMIT);
    g_present_ops=(OaPresentOps){
        pvkCreateSwapchainKHR,pvkDestroySwapchainKHR,pvkGetSwapchainImagesKHR,
        pvkCreateSemaphore,pvkDestroySemaphore,pvkAcquireNextImageKHR,
        pvkCreateFence,pvkDestroyFence,pvkGetFenceStatus,pvkResetFences,pvkWaitForFences,maintenance_enabled,
        pvkCreateImageView,pvkDestroyImageView
    };
    logf_("WINDOW present_fence_cleanup active=%d fallback_max_retained_windows=%d",
          maintenance_enabled,OA_MAX_RETAINED_WINDOWS);
    pvkGetDeviceQueue(g_dev, g_qfam, 0, &g_queue);
    {
        VkCommandPoolCreateInfo cp = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        VkCommandBufferAllocateInfo ca = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        VkFenceCreateInfo fc = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkSemaphoreCreateInfo sc = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        pipeline_cache_init();
        cp.queueFamilyIndex = g_qfam;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        for (i = 0; i < FRAMES; i++) {
            pvkCreateCommandPool(g_dev, &cp, NULL, &g_f[i].pool);
            ca.commandPool = g_f[i].pool;
            pvkAllocateCommandBuffers(g_dev, &ca, &g_f[i].cmd);
            pvkCreateFence(g_dev, &fc, NULL, &g_f[i].fence);
            pvkCreateSemaphore(g_dev, &sc, NULL, &g_f[i].acquired);
            {
                VkQueryPoolCreateInfo qc = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                qc.queryType = VK_QUERY_TYPE_OCCLUSION;
                qc.queryCount = QUERY_MAX;
                if (pvkCreateQueryPool(g_dev, &qc, NULL, &g_f[i].qpool)) { logf_("query pool failed"); return 0; }
                g_f[i].qlist = calloc(QUERY_MAX, sizeof(oa_pred *));
                g_f[i].qreset = QUERY_MAX;
            }
            if (!make_buffer_with_mapping(&g_f[i].rings[0], RING_SIZE,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1)) {
                logf_("ring failed");
                return 0;
            }
            g_f[i].nrings = 1;
        }
        pvkCreateCommandPool(g_dev, &cp, NULL, &g_upload_pool);
        ca.commandPool = g_upload_pool;
        pvkAllocateCommandBuffers(g_dev, &ca, &g_upload_cmd);
        pvkCreateFence(g_dev, &fc, NULL, &g_upload_fence);
    }
    if (!make_buffer(&g_zero_ubo, 65536, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return 0;
    memset(g_zero_ubo.ptr, 0, 65536);
    g_pipes = calloc(g_pipes_cap, sizeof(pipe_entry));
    desc_new_pool();
    return 1;
}

static void init_dummies(void) {
    uint32_t d2[11] = {1, 1, 1, 1, 28, 1, 0, 0, 8, 0, 0}, dc[11] = {1, 1, 1, 6, 28, 1, 0, 0, 8, 0, 4}, d3[9] = {1, 1, 1, 1, 28, 0, 8, 0, 0};
    uint32_t smp[13] = {0x15, 3, 3, 3, 0, 1, 1, 0, 0, 0, 0, 0, 0x7f7fffff};
    create_texture(d2, NULL, &g_dummy2d, 0);
    create_texture(dc, NULL, &g_dummycube, 0);
    create_texture(d3, NULL, &g_dummy3d, 1);
    d_create_srv(NULL, g_dummy2d, NULL, &g_null2d);
    d_create_srv(NULL, g_dummycube, NULL, &g_nullcube);
    d_create_srv(NULL, g_dummy3d, NULL, &g_null3d);
    d_create_sampler(NULL, smp, &g_null_sampler);
}

/* If Present stops, log where every thread is: instruction pointer plus code addresses found on its stack. */
static HRESULT oa_vk_create_device(void *adapter, int type, HMODULE sw, UINT flags, const void *levels, UINT n, UINT sdk, obj **device, UINT *level, oa_context **ctx) {
    static int ready;
    (void)adapter; (void)type; (void)sw; (void)levels; (void)n; (void)sdk;
    logf_("D3D11CreateDevice flags=%x", flags);
    if (!ready) {
        init_vtables();
        if (!init_vulkan()) return E_FAIL_;
        g_device_obj.vtbl = g_vt[K_DEVICE];
        g_device_obj.ref = 1;
        g_device_obj.kind = K_DEVICE;
        g_dxgi_device = new_obj(sizeof(obj), K_DXGI_DEVICE);
        g_adapter = new_obj(sizeof(obj), K_ADAPTER);
        g_factory = new_obj(sizeof(obj), K_FACTORY);
        g_ctx = (oa_context *)new_obj(sizeof(oa_context), K_CONTEXT);
        init_dummies();
        frame_begin();
        ready = 1;

    }
    if (device) { o_addref(&g_device_obj); *device = &g_device_obj; }
    if (ctx) { o_addref(&g_ctx->o); *ctx = g_ctx; }
    if (level) *level = 0xb000;
    return S_OK_;
}

void oa_vk_android_initialize_log(const char*path){InitializeCriticalSection(&g_lock);g_log=fopen(path,"w");}
HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter*a,D3D_DRIVER_TYPE type,HMODULE sw,UINT flags,const D3D_FEATURE_LEVEL*levels,UINT n,UINT sdk,ID3D11Device**device,D3D_FEATURE_LEVEL*level,ID3D11DeviceContext**context){return oa_vk_create_device(a,type,sw,flags,levels,n,sdk,(obj**)device,(UINT*)level,(oa_context**)context);}
