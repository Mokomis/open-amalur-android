/* Part of oa_vk_fullscreen.c: device context state. Included in place; not compiled on its own. */
/* ---- context: state ---- */
static void end_rendering(oa_context *c) {
    if (c->rendering) {
        pvkCmdEndRendering(g_cur->cmd);
        c->rendering = 0;
    }
}

static int scalable_attachment(const oa_view *view, uint32_t width, uint32_t height)
{
    if (!view) return 1;
    const oa_tex *texture = view->tex;
    if (texture->is_backbuffer || texture->mips != 1 || texture->layers != 1 ||
        texture->type3d || view->w != width || view->h != height ||
        width < 16 || height < 16 || !region_ceiling_width || !region_ceiling_height)
        return 0;

    /* Original scene buffers include rounded divisions for bloom and other passes. */
    for (uint32_t divisor = 1; divisor <= 16; divisor *= 2) {
        if (width >= region_ceiling_width / divisor &&
            width <= (region_ceiling_width + divisor - 1) / divisor &&
            height >= region_ceiling_height / divisor &&
            height <= (region_ceiling_height + divisor - 1) / divisor)
            return 1;
    }
    return 0;
}

static void set_render_regions(oa_context *c, uint32_t width, uint32_t height)
{
    unsigned level = render_region_level;
    if (level && !scalable_attachment(c->dsv, width, height))
        level = OA_RESOLUTION_FULL;
    for (unsigned i = 0; level && i < 8; ++i)
        if (!scalable_attachment(c->rtv[i], width, height))
            level = OA_RESOLUTION_FULL;

    c->region_extent.width = oa_resolution_dimension(width, level);
    c->region_extent.height = oa_resolution_dimension(height, level);
    c->region_state.position_to_logical[0] = width ? (float)width / c->region_extent.width : 1.0f;
    c->region_state.position_to_logical[1] = height ? (float)height / c->region_extent.height : 1.0f;
    if (!level && !render_regions_epoch) return;
    for (unsigned i = 0; i < 8; ++i)
        if (c->rtv[i]) set_texture_region(c->rtv[i]->tex, level);
    if (c->dsv) set_texture_region(c->dsv->tex, level);
    if (level) ++render_scaled_passes;
}

static int vertex_reads_render_target(const oa_context *c)
{
    for (uint32_t mask = c->vs->info.texture_mask; mask; mask &= mask - 1) {
        const oa_view *view = c->vs_srv[__builtin_ctz(mask)];
        if (view && (view->tex->desc[8] & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)))
            return 1;
    }
    return 0;
}

static void wait_for_render_targets(oa_context *c)
{
    if (!short_render_waits) {
        barrier_all(g_cur->cmd);
        return;
    }
    /* Render and transfer writes can feed later passes. Their fragment and
       attachment stages still wait; vertex processing only waits when it
       samples a resource that an earlier pass could have produced. */
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };
    VkPipelineStageFlags destination = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    c->rendering_waits_for_vertex = vertex_reads_render_target(c);
    if (c->rendering_waits_for_vertex) destination |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    pvkCmdPipelineBarrier(g_cur->cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, destination,
                          0, 1, &barrier, 0, NULL, 0, NULL);
}

static void begin_rendering(oa_context *c) {
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    VkRenderingAttachmentInfo col[8], dep = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    uint32_t i, n = 0, w = 0, h = 0;
    if (c->rendering) return;
    wait_for_render_targets(c);
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
    set_render_regions(c, w, h);
    ri.renderArea.extent = c->region_extent;
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
