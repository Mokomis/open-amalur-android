#include "waterfall_state.h"
#include "waterfall_composite.h"

enum { FALL_WIDTH = 256, FALL_HEIGHT = 320, FALL_FRAMES = 100 };

typedef struct {
    oa_tex scratch;
    VkDescriptorSet set;
} WaterfallFrame;

static WaterfallFrame waterfall_frames[FRAMES];
static oa_tex *waterfall_texture;
static VkPipeline waterfall_pipeline;
static VkPipelineLayout waterfall_layout;
static VkSampler waterfall_sampler;
static const uint8_t *waterfall_frames_data;
static unsigned waterfall_uploaded;

static int waterfall_init(VkFormat format)
{
    const uint32_t desc[11] = {FALL_WIDTH, FALL_HEIGHT, 1, 1, 77, 1, 0, 0, 8};
    if (create_texture(desc, NULL, &waterfall_texture, 0) != S_OK_ ||
        !hdr_texture_view(waterfall_texture))
        return 0;

    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
    };
    VkDescriptorSetLayout dsl;
    const VkDescriptorSetLayoutCreateInfo set = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = bindings,
    };
    if (pvkCreateDescriptorSetLayout(g_dev, &set, NULL, &dsl) != VK_SUCCESS) return 0;
    const VkPipelineLayoutCreateInfo pipeline_layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl,
    };
    if (pvkCreatePipelineLayout(g_dev, &pipeline_layout, NULL, &waterfall_layout) != VK_SUCCESS)
        return 0;
    const VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FRAMES * 2};
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = FRAMES, .poolSizeCount = 1, .pPoolSizes = &size,
    };
    VkDescriptorPool pool;
    if (pvkCreateDescriptorPool(g_dev, &pool_info, NULL, &pool) != VK_SUCCESS) return 0;
    const VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &dsl,
    };
    for (unsigned i = 0; i < FRAMES; ++i)
        if (pvkAllocateDescriptorSets(g_dev, &alloc, &waterfall_frames[i].set) != VK_SUCCESS)
            return 0;
    const VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (pvkCreateSampler(g_dev, &sampler, NULL, &waterfall_sampler) != VK_SUCCESS) return 0;
    VkShaderModule vertex = hdr_module(oa_hdr_vertex, sizeof(oa_hdr_vertex));
    VkShaderModule fragment = hdr_module(waterfall_fragment, sizeof(waterfall_fragment));
    if (vertex && fragment) waterfall_pipeline = fullscreen_pipeline(format, vertex, fragment, waterfall_layout);
    if (vertex) pvkDestroyShaderModule(g_dev, vertex, NULL);
    if (fragment) pvkDestroyShaderModule(g_dev, fragment, NULL);
    return waterfall_pipeline != VK_NULL_HANDLE;
}

static int waterfall_capture(WaterfallFrame *frame, oa_tex *back, VkRect2D rect, VkCommandBuffer cmd)
{
    oa_tex *scratch = &frame->scratch;
    if (scratch->w != rect.extent.width || scratch->h != rect.extent.height ||
        scratch->vkfmt != back->vkfmt || !scratch->hdr_view) {
        if (scratch->hdr_view) pvkDestroyImageView(g_dev, scratch->hdr_view, NULL);
        if (scratch->image) pvkDestroyImage(g_dev, scratch->image, NULL);
        if (scratch->mem) pvkFreeMemory(g_dev, scratch->mem, NULL);
        *scratch = (oa_tex){
            .w = rect.extent.width, .h = rect.extent.height, .depth = 1, .mips = 1,
            .layers = 1, .vkfmt = back->vkfmt,
        };
        if (!make_image(scratch, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                        0, VK_IMAGE_TYPE_2D) || !hdr_texture_view(scratch))
            return 0;
        image_layout(cmd, scratch->image, VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        VkDescriptorImageInfo bindings[] = {
            {waterfall_sampler, scratch->hdr_view, VK_IMAGE_LAYOUT_GENERAL},
            {waterfall_sampler, waterfall_texture->hdr_view, VK_IMAGE_LAYOUT_GENERAL},
        };
        hdr_write_images(frame->set, bindings, 2);
    }
    const VkImageSubresourceLayers layer = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    const VkImageCopy copy = {
        .srcSubresource = layer,
        .srcOffset = {rect.offset.x, rect.offset.y, 0},
        .dstSubresource = layer,
        .extent = {rect.extent.width, rect.extent.height, 1},
    };
    barrier_all(cmd);
    pvkCmdCopyImage(cmd, back->image, VK_IMAGE_LAYOUT_GENERAL,
                    scratch->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    return 1;
}

static void oa_waterfall_render(oa_chain *chain, oa_frame *frame, oa_context *context)
{
    VeilWaterfallRect portrait;
    const unsigned index = veil_waterfall_frame(&portrait);
    static int portrait_open;
    if (!index) {
        portrait_open = 0;
        return;
    }
    if (!chain->back || hdr_active || !chain->w || !chain->h) return;
    if (!portrait_open) {
        // Refresh the clip when a portrait opens, including after a title-menu import.
        waterfall_frames_data = veil_waterfall_load(FALL_WIDTH * FALL_HEIGHT * FALL_FRAMES);
        waterfall_uploaded = 0;
        portrait_open = 1;
    }
    if (!waterfall_frames_data) return;

    static int initialized;
    oa_tex *back = chain->back;
    if (!initialized) {
        initialized = waterfall_init(back->vkfmt) ? 1 : -1;
        logf_("Veil waterfall init=%d", initialized);
    }
    if (initialized < 0 || !hdr_texture_view(back)) return;

    // The ledger has a fixed portrait margin. Other portrait windows report
    // their live rectangle in the game's 720-unit layout coordinates.
    const float scale = chain->h / 720.0f;
    const float x0 = portrait.width ? portrait.left : 721;
    const float y0 = portrait.width ? portrait.top : 106;
    const float x1 = portrait.width ? portrait.left + portrait.width : chain->w / scale - 53;
    const float y1 = portrait.width ? portrait.top + portrait.height : 585;
    const uint32_t left = (uint32_t)fmaxf(0, ceilf(x0 * scale));
    const uint32_t top = (uint32_t)fmaxf(0, ceilf(y0 * scale));
    const float right = fminf(chain->w, floorf(x1 * scale));
    const float bottom = fminf(chain->h, floorf(y1 * scale));
    if (right <= left || bottom <= top) return;
    const VkRect2D rect = {
        {left, top}, {(uint32_t)right - left, (uint32_t)bottom - top},
    };
    VkCommandBuffer cmd = frame->cmd;
    WaterfallFrame *slot = &waterfall_frames[frame - g_f];
    if (!waterfall_capture(slot, back, rect, cmd)) return;
    if (index != waterfall_uploaded) {
        upload_region(waterfall_texture, 0,
                      waterfall_frames_data + (index - 1) * FALL_WIDTH * FALL_HEIGHT,
                      FALL_WIDTH * 4, 0, 0, FALL_WIDTH, FALL_HEIGHT, context);
        waterfall_uploaded = index;
    }
    const VkRenderingAttachmentInfo target = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = back->hdr_view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = rect, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &target,
    };
    const VkViewport viewport = {
        rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height, 0, 1,
    };
    barrier_all(cmd);
    pvkCmdBeginRendering(cmd, &rendering);
    pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, waterfall_pipeline);
    pvkCmdSetViewport(cmd, 0, 1, &viewport);
    pvkCmdSetScissor(cmd, 0, 1, &rect);
    pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, waterfall_layout, 0, 1, &slot->set, 0, NULL);
    pvkCmdDraw(cmd, 3, 1, 0, 0);
    pvkCmdEndRendering(cmd);
}
