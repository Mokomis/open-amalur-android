/* Correct the original world-color merge before HUD and menu composition. */

extern int oa_android_width(void), oa_android_height(void);

typedef struct {
    oa_tex image;
    VkImageView view;
    VkDescriptorSet set;
} OaNightFrame;

static OaNightFrame night_frames[FRAMES];
static VkDescriptorSetLayout night_set_layout;
static VkPipelineLayout night_layout;
static VkDescriptorPool night_pool;
static VkSampler night_sampler;
static VkShaderModule night_vertex, night_fragment;
static struct {
    VkFormat format;
    VkPipeline pipeline;
} night_pipelines[4];
static int night_init_result;
static uint64_t night_graded_frame = UINT64_MAX;

static int night_initialize(void)
{
    const VkDescriptorSetLayoutBinding binding = {
        0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL
    };
    const VkDescriptorSetLayoutCreateInfo set = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding,
    };
    if (pvkCreateDescriptorSetLayout(g_dev, &set, NULL, &night_set_layout) != VK_SUCCESS) return 0;
    const VkPushConstantRange push = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 2 * sizeof(float)};
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &night_set_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
    };
    if (pvkCreatePipelineLayout(g_dev, &layout, NULL, &night_layout) != VK_SUCCESS) return 0;
    const VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FRAMES};
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = FRAMES, .poolSizeCount = 1, .pPoolSizes = &size,
    };
    if (pvkCreateDescriptorPool(g_dev, &pool, NULL, &night_pool) != VK_SUCCESS) return 0;
    const VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = night_pool, .descriptorSetCount = 1, .pSetLayouts = &night_set_layout,
    };
    for (unsigned i = 0; i < FRAMES; ++i) {
        if (pvkAllocateDescriptorSets(g_dev, &alloc, &night_frames[i].set) != VK_SUCCESS) return 0;
    }
    const VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (pvkCreateSampler(g_dev, &sampler, NULL, &night_sampler) != VK_SUCCESS) return 0;
    night_vertex = hdr_module(oa_hdr_vertex, sizeof(oa_hdr_vertex));
    night_fragment = hdr_module(oa_night_grade_fragment, sizeof(oa_night_grade_fragment));
    return night_vertex && night_fragment;
}

static VkPipeline night_pipeline(VkFormat format)
{
    if (!night_init_result) night_init_result = night_initialize() ? 1 : -1;
    if (night_init_result < 0) return VK_NULL_HANDLE;
    for (unsigned i = 0; i < sizeof night_pipelines / sizeof night_pipelines[0]; ++i) {
        if (night_pipelines[i].format == format) return night_pipelines[i].pipeline;
        if (night_pipelines[i].format == VK_FORMAT_UNDEFINED) {
            night_pipelines[i].format = format;
            night_pipelines[i].pipeline = fullscreen_pipeline(format, night_vertex, night_fragment,
                                                              night_layout);
            return night_pipelines[i].pipeline;
        }
    }
    return VK_NULL_HANDLE;
}

static int night_copy_target(OaNightFrame *frame, VkExtent2D extent, VkFormat format)
{
    oa_tex *image = &frame->image;
    if (frame->view && image->w == extent.width && image->h == extent.height &&
        image->vkfmt == format) return 1;
    if (frame->view) pvkDestroyImageView(g_dev, frame->view, NULL);
    if (image->image) pvkDestroyImage(g_dev, image->image, NULL);
    if (image->mem) pvkFreeMemory(g_dev, image->mem, NULL);
    frame->view = VK_NULL_HANDLE;
    *image = (oa_tex){
        .w = extent.width, .h = extent.height, .depth = 1, .mips = 1, .layers = 1, .vkfmt = format,
    };
    if (!make_image(image, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0,
                    VK_IMAGE_TYPE_2D)) return 0;
    frame->view = make_view(image, VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_COLOR_BIT,
                            0, 1, 0, 1, 0);
    if (!frame->view) return 0;
    VkDescriptorImageInfo info = {night_sampler, frame->view, VK_IMAGE_LAYOUT_GENERAL};
    hdr_write_images(frame->set, &info, 1);
    return 1;
}

static void night_grade(oa_context *context, oa_view *target)
{
    if (night_graded_frame == g_frame) return;
    const float strength = veil_night_darkness();
    if (strength == 0.0f || target->format == VK_FORMAT_UNDEFINED ||
        target->level || target->tex->layers != 1 ||
        target->w != (unsigned)oa_android_width() || target->h != (unsigned)oa_android_height())
        return;
    VkPipeline pipeline = night_pipeline(target->format);
    if (!pipeline) return;
    VkExtent2D extent = {target->w, target->h};
    OaNightFrame *frame = &night_frames[g_cur - g_f];
    if (!night_copy_target(frame, extent, target->format)) return;

    VkCommandBuffer cmd = g_cur->cmd;
    end_rendering(context);
    barrier_all(cmd);
    image_layout(cmd, frame->image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    const VkImageCopy region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .extent = {target->w, target->h, 1},
    };
    pvkCmdCopyImage(cmd, target->tex->image, VK_IMAGE_LAYOUT_GENERAL,
                    frame->image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    barrier_all(cmd);
    draw_fullscreen(cmd, pipeline, night_layout, frame->set, target->view,
                    VK_IMAGE_LAYOUT_GENERAL, extent, (VkRect2D){{0, 0}, extent}, strength, 0.0f);
    barrier_all(cmd);
    context->pipeline_valid = 0;
    context->cache_valid = 0;
    static int logged;
    if (!logged) {
        logf_("VEIL_NIGHT graded world %ux%u format=%d strength=%.3f",
              extent.width, extent.height, target->format, (double)strength);
        logged = 1;
    }
    night_graded_frame = g_frame;
}
