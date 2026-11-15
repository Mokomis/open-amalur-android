/* HDR10 presentation for the original renderer. The engine's own SDR render
   and HUD stay in place. A side pass preserves the highlight overflow that
   its four-input scene/bloom/effects merge discards before color grading. */
#include "oa_hdr_shaders.h"

typedef struct {
    oa_tex image;
    VkImageView view;
    VkDescriptorSet merge_set, output_set;
    uint64_t frame;
} OaHdrFrame;

static OaHdrFrame hdr_frames[FRAMES];
static VkDescriptorSetLayout hdr_set_layout;
static VkPipelineLayout hdr_layout;
static VkDescriptorPool hdr_pool;
static VkSampler hdr_sampler;
static VkPipeline hdr_merge_pipeline;
static VkPipeline hdr_output_pipelines[2];
static VkShaderModule hdr_vertex, hdr_merge_fragment, hdr_output_fragment;
static int hdr_colorspace, hdr_requested, hdr_active, hdr_init_result;
static PFN_vkSetHdrMetadataEXT hdr_set_metadata;
static uint32_t hdr_merge_logged[8];
static uint64_t hdr_capture_count;
static float hdr_paper_white = 203.0f;

static VkShaderModule hdr_module(const uint32_t *words, size_t size)
{
    VkShaderModule shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = words;
    if (pvkCreateShaderModule(g_dev, &info, NULL, &shader) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return shader;
}

static VkPipeline fullscreen_pipeline(VkFormat format, VkShaderModule vertex_module,
                                      VkShaderModule fragment, VkPipelineLayout layout)
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_module, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"},
    };
    const VkPipelineVertexInputStateCreateInfo vertex = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo sample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState attachment = {.colorWriteMask = 0xf};
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment,
    };
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = states,
    };
    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &sample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkResult result = pvkCreateGraphicsPipelines(g_dev, g_pcache, 1, &info, NULL, &pipeline);
    if (result != VK_SUCCESS) logf_("Fullscreen pipeline failed format=%d result=%d", format, result);
    return result == VK_SUCCESS ? pipeline : VK_NULL_HANDLE;
}

static VkPipeline hdr_pipeline(VkFormat format, VkShaderModule fragment)
{
    return fullscreen_pipeline(format, hdr_vertex, fragment, hdr_layout);
}

static int hdr_initialize(void)
{
    VkDescriptorSetLayoutBinding bindings[4];
    for (uint32_t i = 0; i < 4; ++i) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL
        };
    }
    const VkDescriptorSetLayoutCreateInfo set = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 4, .pBindings = bindings,
    };
    if (pvkCreateDescriptorSetLayout(g_dev, &set, NULL, &hdr_set_layout) != VK_SUCCESS) return 0;
    const VkPushConstantRange push = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 2 * sizeof(float)};
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &hdr_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
    };
    if (pvkCreatePipelineLayout(g_dev, &layout, NULL, &hdr_layout) != VK_SUCCESS) return 0;
    const VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FRAMES * 8};
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = FRAMES * 2, .poolSizeCount = 1, .pPoolSizes = &size,
    };
    if (pvkCreateDescriptorPool(g_dev, &pool, NULL, &hdr_pool) != VK_SUCCESS) return 0;
    const VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = hdr_pool, .descriptorSetCount = 1, .pSetLayouts = &hdr_set_layout,
    };
    for (unsigned i = 0; i < FRAMES; ++i) {
        if (pvkAllocateDescriptorSets(g_dev, &alloc, &hdr_frames[i].merge_set) != VK_SUCCESS ||
            pvkAllocateDescriptorSets(g_dev, &alloc, &hdr_frames[i].output_set) != VK_SUCCESS) return 0;
    }
    const VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (pvkCreateSampler(g_dev, &sampler, NULL, &hdr_sampler) != VK_SUCCESS) return 0;
    hdr_vertex = hdr_module(oa_hdr_vertex, sizeof(oa_hdr_vertex));
    hdr_merge_fragment = hdr_module(oa_hdr_merge_fragment, sizeof(oa_hdr_merge_fragment));
    hdr_output_fragment = hdr_module(oa_hdr_output_fragment, sizeof(oa_hdr_output_fragment));
    if (!hdr_vertex || !hdr_merge_fragment || !hdr_output_fragment) return 0;
    hdr_merge_pipeline = hdr_pipeline(VK_FORMAT_R16G16B16A16_SFLOAT, hdr_merge_fragment);
    return hdr_merge_pipeline != VK_NULL_HANDLE;
}

static VkPipeline hdr_output_pipeline(VkFormat format)
{
    if (!hdr_init_result) hdr_init_result = hdr_initialize() ? 1 : -1;
    if (hdr_init_result < 0) return VK_NULL_HANDLE;
    unsigned slot = format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    if (!hdr_output_pipelines[slot]) hdr_output_pipelines[slot] = hdr_pipeline(format, hdr_output_fragment);
    return hdr_output_pipelines[slot];
}

static void hdr_write_images(VkDescriptorSet set, VkDescriptorImageInfo *images, unsigned count)
{
    VkWriteDescriptorSet writes[4];
    for (unsigned i = 0; i < count; ++i) {
        writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set, .dstBinding = i, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &images[i],
        };
    }
    pvkUpdateDescriptorSets(g_dev, count, writes, 0, NULL);
}

static void hdr_draw(VkCommandBuffer cmd, VkPipeline pipeline, VkDescriptorSet set,
                     VkImageView target, VkImageLayout image_layout, VkExtent2D extent,
                     VkRect2D rect, float parameter, float paper_white)
{
    const VkRenderingAttachmentInfo color = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = target, .imageLayout = image_layout,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.color.float32 = {0, 0, 0, 1},
    };
    const VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea.extent = extent, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color,
    };
    const VkViewport viewport = {
        rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height, 0.0f, 1.0f,
    };
    pvkCmdBeginRendering(cmd, &rendering);
    pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    pvkCmdSetViewport(cmd, 0, 1, &viewport);
    pvkCmdSetScissor(cmd, 0, 1, &rect);
    pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, hdr_layout, 0, 1, &set, 0, NULL);
    const float push[] = {parameter, paper_white};
    pvkCmdPushConstants(cmd, hdr_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
    pvkCmdDraw(cmd, 3, 1, 0, 0);
    pvkCmdEndRendering(cmd);
}

static VkImageView hdr_texture_view(oa_tex *texture)
{
    if (!texture->hdr_view)
        texture->hdr_view = make_view(texture, VK_IMAGE_VIEW_TYPE_2D, texture->vkfmt, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1, 0);
    return texture->hdr_view;
}

static int hdr_allocate_highlights(OaHdrFrame *frame, VkExtent2D extent)
{
    oa_tex *image = &frame->image;
    if (image->w == extent.width && image->h == extent.height && frame->view) return 1;
    if (frame->view) pvkDestroyImageView(g_dev, frame->view, NULL);
    if (image->image) pvkDestroyImage(g_dev, image->image, NULL);
    if (image->mem) pvkFreeMemory(g_dev, image->mem, NULL);
    frame->view = VK_NULL_HANDLE;
    *image = (oa_tex){
        .w = extent.width, .h = extent.height, .depth = 1, .mips = 1, .layers = 1,
        .vkfmt = VK_FORMAT_R16G16B16A16_SFLOAT,
    };
    if (!make_image(image, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, VK_IMAGE_TYPE_2D))
        return 0;
    frame->view = hdr_texture_view(image);
    if (frame->view) logf_("HDR highlight buffer %ux%u format=RGBA16F", extent.width, extent.height);
    return frame->view != VK_NULL_HANDLE;
}

static void hdr_capture_merge(oa_context *context)
{
    oa_view *target = context->rtv[0];
    oa_view *lut = context->ps_srv[0];
    oa_view *scene = context->ps_srv[1];
    oa_view *effects = context->ps_srv[2];
    oa_view *bloom = context->ps_srv[3];
    oa_buffer *buffer = context->ps_cb[0];
    if (context != g_ctx || !context->ps || context->ps->info.texture_mask != 15 ||
        context->ps->info.cb_mask != 1 || !target || context->rtv[1] || !lut ||
        lut->tex->w != 256 || lut->tex->h != 16 || !scene || !effects || !bloom ||
        !scene->view || !effects->view || !bloom->view || !buffer || buffer->size < 36)
        return;
    if (target->w < 640 || scene->tex->w != target->w || scene->tex->h != target->h ||
        effects->tex->w != target->w || effects->tex->h != target->h ||
        bloom->tex->w != (target->w + 2) / 4 || bloom->tex->h != (target->h + 2) / 4)
        return;

    const void *data = buffer->ring ? (buffer->upload_data ? buffer->upload_data : buffer->shadow) :
        (buffer->ngens ? buffer->gens[buffer->cur].ptr : NULL);
    float values[9];
    if (!data) return;
    memcpy(values, data, sizeof(values));
    if (!(values[0] == 0.05859375f && values[1] == 0.9375f &&
          values[2] == 0.001953125f && values[3] == 0.03125f &&
          values[6] == 16.0f && values[7] == 0.0625f && values[8] >= 0.0f && values[8] <= 4.0f))
        return;

    uint32_t hash = context->ps->hash;
    for (unsigned i = 0; i < sizeof(hdr_merge_logged) / sizeof(hdr_merge_logged[0]); ++i) {
        if (hdr_merge_logged[i] == hash) break;
        if (!hdr_merge_logged[i]) {
            hdr_merge_logged[i] = hash;
            logf_("HDR native merge DXBC fnv1a=%08x scene=%ux%u effects=%ux%u bloom=%ux%u strength=%.5f",
                  hash, target->w, target->h, effects->tex->w, effects->tex->h,
                  bloom->tex->w, bloom->tex->h, (double)values[8]);
            break;
        }
    }
    if (!hdr_active) return;
    OaHdrFrame *frame = &hdr_frames[g_cur - g_f];
    if (frame->frame == g_frame) return;
    VkExtent2D extent = {(target->w + 1) / 2, (target->h + 1) / 2};
    if (!hdr_allocate_highlights(frame, extent)) return;

    VkImageView graded = hdr_texture_view(target->tex);
    if (!graded) return;
    VkDescriptorImageInfo images[] = {
        {hdr_sampler, scene->view, VK_IMAGE_LAYOUT_GENERAL},
        {hdr_sampler, effects->view, VK_IMAGE_LAYOUT_GENERAL},
        {hdr_sampler, bloom->view, VK_IMAGE_LAYOUT_GENERAL},
        {hdr_sampler, graded, VK_IMAGE_LAYOUT_GENERAL},
    };
    hdr_write_images(frame->merge_set, images, 4);
    end_rendering(context);
    barrier_all(g_cur->cmd);
    image_layout(g_cur->cmd, frame->image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    hdr_draw(g_cur->cmd, hdr_merge_pipeline, frame->merge_set, frame->view, VK_IMAGE_LAYOUT_GENERAL,
             extent, (VkRect2D){{0, 0}, extent}, values[8], 0.0f);
    context->pipeline_valid = 0;
    context->cache_valid = 0;
    frame->frame = g_frame;
    if (++hdr_capture_count == 1 || hdr_capture_count % 600 == 0)
        logf_("HDR native highlights captured=%llu frame=%llu strength=%.5f",
              (unsigned long long)hdr_capture_count, (unsigned long long)g_frame, (double)values[8]);
}

static void hdr_poll(oa_chain *chain)
{
    static uint64_t next_poll;
    if (g_frame < next_poll) return;
    next_poll = g_frame + 30;
    const char *root = getenv("OA_OFFLINE_ROOT");
    char path[4096];
    int enabled = 0;
    if (root && snprintf(path, sizeof(path), "%s/hdr-mode", root) < (int)sizeof(path)) {
        FILE *file = fopen(path, "r");
        if (file) {
            enabled = fgetc(file) == '1';
            fclose(file);
        }
    }
    if (enabled && root && snprintf(path, sizeof(path), "%s/hdr-paper-white", root) < (int)sizeof(path)) {
        unsigned requested_white = 203;
        FILE *file = fopen(path, "r");
        if (file) {
            unsigned value;
            if (fscanf(file, "%u", &value) == 1 && value >= 80 && value <= 800) requested_white = value;
            fclose(file);
        }
        if (hdr_paper_white != (float)requested_white || !hdr_requested)
            logf_("HDR paper white=%u nits output ceiling=1250 nits", requested_white);
        hdr_paper_white = (float)requested_white;
    }
    if (enabled == hdr_requested) return;
    hdr_requested = enabled;
    chain->present.recreate = 1;
    logf_("HDR requested=%d frame=%llu", enabled, (unsigned long long)g_frame);
}

static void hdr_metadata(VkSwapchainKHR swapchain)
{
    if (!hdr_set_metadata) return;
    const VkHdrMetadataEXT metadata = {
        .sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT,
        .displayPrimaryRed = {0.708f, 0.292f},
        .displayPrimaryGreen = {0.170f, 0.797f},
        .displayPrimaryBlue = {0.131f, 0.046f},
        .whitePoint = {0.3127f, 0.3290f},
        .maxLuminance = 1250.0f, .minLuminance = 0.001f,
        .maxContentLightLevel = 1250.0f,
    };
    hdr_set_metadata(g_dev, 1, &swapchain, &metadata);
}

static void hdr_render_present(oa_chain *chain, oa_frame *frame, uint32_t index)
{
    OaPresentGeneration *generation = chain->present.current;
    VkImage image = generation->images[index];
    VkImageView back = hdr_texture_view(chain->back);
    VkImageView target = generation->views[index];
    OaHdrFrame *highlights = &hdr_frames[frame - g_f];
    int have_highlights = highlights->frame == g_frame;
    VkDescriptorImageInfo inputs[] = {
        {hdr_sampler, back, VK_IMAGE_LAYOUT_GENERAL},
        {hdr_sampler, have_highlights ? highlights->view : back, VK_IMAGE_LAYOUT_GENERAL},
    };
    hdr_write_images(highlights->output_set, inputs, 2);
    OaViewport centered = oa_centered_viewport(chain->extent.width, chain->extent.height, chain->w, chain->h);
    VkRect2D viewport = {{centered.x, centered.y}, {(uint32_t)centered.width, (uint32_t)centered.height}};
    barrier_all(frame->cmd);
    image_layout(frame->cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    hdr_draw(frame->cmd, hdr_output_pipeline(chain->swap_fmt), highlights->output_set, target,
             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, chain->extent, viewport, (float)have_highlights, hdr_paper_white);
    image_layout(frame->cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
