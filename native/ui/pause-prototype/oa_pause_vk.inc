#include <android/asset_manager.h>
#include <math.h>
#include "oa_pause_spv.h"

#define OA_PAUSE_VERTEX_FLOATS (9u * 60000u)
#include "oa_ui_draw.inc"

extern AAsset *oa_pause_asset(void);
extern void oa_pause_renderer_ready(unsigned ready);
extern void oa_pause_frame(unsigned draws);
extern unsigned oa_pause_visible(void), oa_pause_focus(void);
extern void oa_pause_row_rect(unsigned row, int width, int height, float out[4]);

static oa_ui pause_ui;
static oa_tex *pause_textures[3];
static VkImageView pause_views[3];
static vk_buffer pause_vertices[FRAMES];
static VkPipeline pause_pipeline;
static VkPipelineLayout pause_layout;
static VkDescriptorSet pause_set;

static int pause_image(unsigned slot, uint32_t width, uint32_t height, uint32_t format,
                       const uint8_t *data, uint32_t pixel_bytes)
{
    const uint32_t desc[11] = {width, height, 1, 1, format, 1, 0, 0, 8};
    const oa_init init = {data, width * pixel_bytes, width * height * pixel_bytes};
    if (create_texture(desc, &init, &pause_textures[slot], 0) != S_OK_) return 0;
    pause_views[slot] = make_view(pause_textures[slot], VK_IMAGE_VIEW_TYPE_2D,
                                 pause_textures[slot]->vkfmt, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1, 0);
    return pause_views[slot] != VK_NULL_HANDLE;
}

static int pause_load_assets(void)
{
    AAsset *asset = oa_pause_asset();
    if (!asset) return 0;
    const uint8_t *data = AAsset_getBuffer(asset);
    size_t size = (size_t)AAsset_getLength64(asset);
    uint32_t atlas_w, atlas_h, field_w, field_h, title_w, title_h, count;
    uint8_t *title_rgba = NULL;
    int ok = 0;
    if (!data || size < 20 || memcmp(data, "OAUI0002", 8)) goto done;
    memcpy(&atlas_w, data+8, 4);
    memcpy(&atlas_h, data+12, 4);
    if (!atlas_w || !atlas_h || atlas_w > 4096 || atlas_h > 4096) goto done;
    const size_t atlas_bytes = (size_t)atlas_w * atlas_h * 4;
    size_t at = 16 + atlas_bytes;
    if (at + 4 > size) goto done;
    memcpy(&count, data+at, 4);
    at += 4;
    if (count != OA_SPR_COUNT || at + sizeof(pause_ui.sprites) + 8 > size) goto done;
    memcpy(pause_ui.sprites, data+at, sizeof(pause_ui.sprites));
    at += sizeof(pause_ui.sprites);
    memcpy(&field_w, data+at, 4);
    memcpy(&field_h, data+at+4, 4);
    at += 8;
    if (!field_w || !field_h || field_w > 4096 || field_h > 4096) goto done;
    const uint8_t *field = data+at;
    at += (size_t)field_w * field_h;
    if (at + sizeof(pause_ui.fonts) + 8 > size) goto done;
    memcpy(pause_ui.fonts, data+at, sizeof(pause_ui.fonts));
    at += sizeof(pause_ui.fonts);
    memcpy(&title_w, data+at, 4);
    memcpy(&title_h, data+at+4, 4);
    at += 8;
    if (!title_w || !title_h || title_w > 4096 || title_h > 4096 ||
        size - at != (size_t)title_w * title_h * 3) goto done;
    size_t pixels = (size_t)title_w * title_h;
    title_rgba = malloc(pixels * 4);
    if (!title_rgba) goto done;
    for (size_t i = 0; i < pixels; ++i) {
        memcpy(title_rgba+i*4, data+at+i*3, 3);
        title_rgba[i*4+3] = 255;
    }
    pause_ui.atlas_w = (float)atlas_w;
    pause_ui.atlas_h = (float)atlas_h;
    pause_ui.field_w = (float)field_w;
    pause_ui.field_h = (float)field_h;
    pause_ui.vertices = malloc(OA_PAUSE_VERTEX_FLOATS * sizeof(float));
    ok = pause_ui.vertices &&
        pause_image(0, atlas_w, atlas_h, 28, data+16, 4) &&
        pause_image(1, field_w, field_h, 61, field, 1) &&
        pause_image(2, title_w, title_h, 28, title_rgba, 4);
done:
    free(title_rgba);
    AAsset_close(asset);
    return ok;
}

static int pause_init(VkFormat format)
{
    if (!pause_load_assets()) return 0;
    VkDescriptorSetLayoutBinding bindings[3];
    VkDescriptorImageInfo images[3];
    const VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    VkSampler sampler;
    if (pvkCreateSampler(g_dev, &sampler_info, NULL, &sampler) != VK_SUCCESS) return 0;
    for (unsigned i = 0; i < 3; ++i) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL,
        };
        images[i] = (VkDescriptorImageInfo){sampler, pause_views[i], VK_IMAGE_LAYOUT_GENERAL};
    }
    VkDescriptorSetLayout set_layout;
    const VkDescriptorSetLayoutCreateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3, .pBindings = bindings,
    };
    if (pvkCreateDescriptorSetLayout(g_dev, &set_info, NULL, &set_layout) != VK_SUCCESS) return 0;
    const VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
        .poolSizeCount = 1, .pPoolSizes = &pool_size,
    };
    VkDescriptorPool pool;
    if (pvkCreateDescriptorPool(g_dev, &pool_info, NULL, &pool) != VK_SUCCESS) return 0;
    const VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool,
        .descriptorSetCount = 1, .pSetLayouts = &set_layout,
    };
    if (pvkAllocateDescriptorSets(g_dev, &allocate, &pause_set) != VK_SUCCESS) return 0;
    hdr_write_images(pause_set, images, 3);
    const VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(float)};
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
    };
    if (pvkCreatePipelineLayout(g_dev, &layout, NULL, &pause_layout) != VK_SUCCESS) return 0;
    for (unsigned i = 0; i < FRAMES; ++i)
        if (!make_buffer(&pause_vertices[i], 128 * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return 0;

    VkShaderModule vs = hdr_module(oa_pause_vertex, sizeof(oa_pause_vertex));
    VkShaderModule ps = hdr_module(oa_pause_fragment, sizeof(oa_pause_fragment));
    if (!vs || !ps) return 0;
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage=VK_SHADER_STAGE_VERTEX_BIT, .module=vs, .pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage=VK_SHADER_STAGE_FRAGMENT_BIT, .module=ps, .pName="main"},
    };
    const VkVertexInputBindingDescription binding = {0, 9 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 8},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}, {3, 0, VK_FORMAT_R32_SFLOAT, 32},
    };
    const VkPipelineVertexInputStateCreateInfo vertex = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 4, .pVertexAttributeDescriptions = attributes,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1,
    };
    const VkPipelineMultisampleStateCreateInfo samples = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState alpha = {
        .blendEnable = VK_TRUE, .colorWriteMask = 15,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &alpha,
    };
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = states,
    };
    const VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1, .pColorAttachmentFormats = &format,
    };
    const VkGraphicsPipelineCreateInfo pipeline = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rendering,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex,
        .pInputAssemblyState = &assembly, .pViewportState = &viewport,
        .pRasterizationState = &raster, .pMultisampleState = &samples,
        .pColorBlendState = &blend, .pDynamicState = &dynamic, .layout = pause_layout,
    };
    return pvkCreateGraphicsPipelines(g_dev, g_pcache, 1, &pipeline, NULL, &pause_pipeline) == VK_SUCCESS;
}

static void pause_widgets(oa_ui *ui, int width, int height)
{
    const float s = height / 840.0f;
    const float paper[4] = {.937f,.902f,.824f,1};
    const float dim[4] = {.84f,.81f,.73f,1};
    const float gold[4] = {.902f,.812f,.58f,1};
    const float line[4] = {.788f,.655f,.396f,.75f};
    const float left[4] = {.024f,.031f,.035f,.90f};
    const float mid[4] = {.024f,.031f,.035f,.60f};
    const float right[4] = {.024f,.031f,.035f,.30f};
    const float glow[4] = {.788f,.655f,.396f,.30f};
    const float clear[4] = {.788f,.655f,.396f,0};
    oa_ui_title(ui, 0, 0, width, height);
    oa_ui_fade(ui, 0, 0, width*.4f, height, left, mid);
    oa_ui_fade(ui, width*.4f, 0, width*.6f, height, mid, right);
    oa_ui_rect(ui, 0, 0, width, 84*s, left);
    oa_ui_rect(ui, 0, 84*s, width, 1, line);
    oa_ui_text(ui, OA_FONT_HEAD, 48*s, 27*s, 26*s, OA_ALIGN_LEFT, paper, "Paused");
    static const char *const labels[] = {"Resume", "Game Menu"};
    const unsigned focus = oa_pause_focus();
    for (unsigned i = 0; i < 2; ++i) {
        float row[4];
        oa_pause_row_rect(i, width, height, row);
        if (focus == i) {
            oa_ui_fade(ui, row[0], row[1], row[2], row[3], glow, clear);
            oa_ui_rect(ui, row[0], row[1], 2*s, row[3], gold);
        }
        oa_ui_text(ui, OA_FONT_HEAD, row[0]+20*s, row[1]+(row[3]-27*s)*.5f,
                   (i == 0 ? 22 : 18)*s, OA_ALIGN_LEFT, focus == i ? paper : dim, labels[i]);
    }
}

static void oa_pause_render(oa_chain *chain, oa_frame *frame)
{
    static int initialized;
    static long last_draws;
    long draws = g_n_draw - last_draws;
    last_draws = g_n_draw;
    oa_pause_frame((unsigned)draws);
    if (!oa_pause_visible() || !chain->back || !chain->w || !chain->h || hdr_active) return;
    if (!initialized) {
        initialized = pause_init(chain->back->vkfmt) ? 1 : -1;
        logf_("PROTOTYPE_PAUSE Vulkan init=%d", initialized);
        oa_pause_renderer_ready(initialized > 0);
    }
    if (initialized < 0) return;
    VkImageView view = hdr_texture_view(chain->back);
    if (!view) return;
    pause_ui.count = 0;
    pause_widgets(&pause_ui, chain->w, chain->h);
    vk_buffer *vertices = &pause_vertices[frame-g_f];
    const size_t bytes = pause_ui.count * sizeof(float);
    if (bytes > 128 * 1024) return;
    memcpy(vertices->ptr, pause_ui.vertices, bytes);
    if (!(vertices->memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        const VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = vertices->mem, .size = VK_WHOLE_SIZE,
        };
        if (pvkFlushMappedMemoryRanges(g_dev, 1, &range) != VK_SUCCESS) return;
    }
    VkCommandBuffer cmd = frame->cmd;
    const VkRenderingAttachmentInfo color = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea.extent = {chain->w, chain->h},
        .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color,
    };
    const VkViewport viewport = {0, 0, chain->w, chain->h, 0, 1};
    const VkRect2D scissor = {{0,0},{chain->w,chain->h}};
    const float size[] = {chain->w, chain->h};
    const VkDeviceSize offset = 0;
    barrier_all(cmd);
    pvkCmdBeginRendering(cmd, &rendering);
    pvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pause_pipeline);
    pvkCmdSetViewport(cmd, 0, 1, &viewport);
    pvkCmdSetScissor(cmd, 0, 1, &scissor);
    pvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pause_layout, 0, 1, &pause_set, 0, NULL);
    pvkCmdBindVertexBuffers2(cmd, 0, 1, &vertices->buf, &offset, NULL, NULL);
    pvkCmdPushConstants(cmd, pause_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(size), size);
    pvkCmdDraw(cmd, pause_ui.count/9, 1, 0, 0);
    pvkCmdEndRendering(cmd);
}
