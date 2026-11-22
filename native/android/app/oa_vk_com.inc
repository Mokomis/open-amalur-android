/* Part of oa_vk_fullscreen.c: COM object layouts. Included in place; not compiled on its own. */
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
    unsigned region_level;
    VkImageView hdr_view;
} oa_tex;

typedef struct {
    obj o;
    oa_tex *tex;
    VkImageView view;
    VkFormat format;
    uint32_t desc[12], w, h, level, uid;
    uint32_t dsv_flags; /* D3D11_DSV_READ_ONLY_DEPTH / _STENCIL for depth-stencil views */
} oa_view;
enum { DSV_READ_ONLY_DEPTH = 0x1, DSV_READ_ONLY_STENCIL = 0x2 };

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
    uint64_t recipe_id;
    uint64_t recipe_interface_id;
    uint32_t *recipe_code;
    uint32_t recipe_bytes;
    uint32_t hash;
    unsigned cart_particle;
    uint32_t af_textures[16];
} oa_shader;

typedef struct oa_state {
    obj o;
    uint32_t desc[20];
    VkSampler sampler;
    VkSampler af_sampler[2];
    /* Depth states only: the same state with depth and/or stencil writes
       disabled, built on first use with a read-only depth-stencil view. */
    struct oa_state *read_only[3];
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
    VkExtent2D region_extent;
    uint64_t region_bindings_epoch, region_material_epoch;
    oa_dxbc_render_regions region_state, pushed_regions;
    int regions_pushed;
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
    int rendering_waits_for_vertex;
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
    unsigned displayed_region_level;
    uint64_t region_ready_after;
    uint64_t region_step_time;
    OaResolutionTiming resolution_timing;
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

static uint32_t region_ceiling_width, region_ceiling_height;
static unsigned render_region_level;
static uint64_t render_regions_epoch;
static unsigned render_scaled_passes;
static int short_render_waits;
static int prepare_shader_variants, record_shader_variants;
static unsigned prepared_recipe_store_full;
static size_t captured_shader_bytes;

static int keep_shader_code(size_t bytes)
{
    if (!record_shader_variants || bytes > 128 * 1024 ||
        __atomic_load_n(&prepared_recipe_store_full, __ATOMIC_RELAXED))
        return 0;
    const size_t limit = 8 * 1024 * 1024;
    size_t used = __atomic_load_n(&captured_shader_bytes, __ATOMIC_RELAXED);
    while (bytes <= limit - used) {
        if (__atomic_compare_exchange_n(&captured_shader_bytes, &used, used + bytes, 1,
                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            return 1;
    }
    return 0;
}

static uint64_t shader_recipe_id(const void *bytes, size_t length)
{
    const uint8_t *data = bytes;
    uint64_t id = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; ++i)
        id = (id ^ data[i]) * UINT64_C(1099511628211);
    return id;
}

static FILE *open_renderer_file(const char *name, const char *mode)
{
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (!root || !*root) return NULL;
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/%s", root, name);
    return length > 0 && length < (int)sizeof(path) ? fopen(path, mode) : NULL;
}

static int renderer_option(const char *name, int fallback)
{
    FILE *file = open_renderer_file(name, "r");
    if (!file) return fallback;
    int value = fgetc(file);
    fclose(file);
    return value == '1' ? 1 : value == '0' ? 0 : fallback;
}

static void set_texture_region(oa_tex *texture, unsigned level)
{
    if (texture->region_level == level) return;
    texture->region_level = level;
    ++render_regions_epoch;
}

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
