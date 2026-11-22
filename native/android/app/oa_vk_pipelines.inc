/* Part of oa_vk_fullscreen.c: pipelines. Included in place; not compiled on its own. */
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

static int create_program_layout(const oa_shader *vs, const oa_shader *ps,
                                 VkDescriptorSetLayout *dsl, VkPipelineLayout *layout) {
    VkDescriptorSetLayoutBinding b[128];
    uint32_t n = 0;
    VkDescriptorSetLayoutCreateInfo dci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VkPipelineLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    add_bindings(&vs->info, b, &n, VK_SHADER_STAGE_VERTEX_BIT);
    if (ps) add_bindings(&ps->info, b, &n, VK_SHADER_STAGE_FRAGMENT_BIT);
    dci.bindingCount = n;
    dci.pBindings = b;
    if (pvkCreateDescriptorSetLayout(g_dev, &dci, NULL, dsl) != VK_SUCCESS) return 0;
    lci.setLayoutCount = 1;
    lci.pSetLayouts = dsl;
    const VkPushConstantRange regions = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(oa_dxbc_render_regions),
    };
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &regions;
    if (pvkCreatePipelineLayout(g_dev, &lci, NULL, layout) != VK_SUCCESS) {
        pvkDestroyDescriptorSetLayout(g_dev, *dsl, NULL);
        *dsl = VK_NULL_HANDLE;
        return 0;
    }
    return 1;
}

static prog_entry *program_of(oa_shader *vs, oa_shader *ps) {
    uint32_t h = hash_bytes(&vs, sizeof(vs)) ^ hash_bytes(&ps, sizeof(ps)) * 31u, m = (1u << 13) - 1, i = h & m;
    while (g_progs[i].used) {
        if (g_progs[i].vs == vs && g_progs[i].ps == ps) return &g_progs[i];
        i = (i + 1) & m;
    }
    if (!create_program_layout(vs, ps, &g_progs[i].dsl, &g_progs[i].layout))
        rt_fatal("Vulkan program layout creation failed");
    g_progs[i].vs = vs;
    g_progs[i].ps = ps;
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

static VkPipeline build_pipeline(const pipe_key *k, VkPipelineLayout layout, VkPipelineCache cache) {
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
    if (pvkCreateGraphicsPipelines(g_dev, cache, 1, &ci, NULL, &p)) logf_("pipeline failed");
    return p;
}

#include "oa_shader_preparation.inc"

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
    g_pipes[i].p = get_prepared_pipeline(k, g_pipes[i].program->layout);
    c->selected_pipe = &g_pipes[i];
    g_pipes[i].used = 1;
    g_pipes_n++;
    return g_pipes[i].p;
}
