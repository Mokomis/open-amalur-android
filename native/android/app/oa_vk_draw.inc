/* Part of oa_vk_fullscreen.c: draw calls. Included in place; not compiled on its own. */
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

static long g_n_draw, g_n_skip_vs, g_n_skip_rt, g_n_skip_pipe, g_n_skip_ib, g_n_skip_query, g_n_query_kept;

static void draw_descriptors(oa_context *c, desc_build *d, int eager, int uniforms_only) {
    d->nw = d->nimg = d->nbuf = d->nkey = 0;
    if (pass_frames) pass_constants = pass_textures = UINT64_C(14695981039346656037);
    /* Pixel bindings precede vertex bindings in the dynamic-offset array. */
    if (c->ps) write_stage(d, &c->ps->info, c->ps_cb, c->ps_srv, c->ps_smp, eager, uniforms_only, c->ps,
                            c->dsv && c->rtv[0] && !c->rtv[0]->tex->is_backbuffer);
    write_stage(d, &c->vs->info, c->vs_cb, c->vs_srv, c->vs_smp, eager, uniforms_only, c->vs, 0);
}

/* D3D11 discards depth (and stencil) writes through a read-only depth-stencil
   view. Mirror that with a twin of the depth state whose writes are disabled,
   so the pipeline never writes an image the pass may also be sampling. The twin
   is a distinct state, so it gets its own pipeline and prepared recipe. */
static oa_state *pipeline_depth_state(oa_context *c)
{
    static oa_state default_state[3];
    const uint32_t ro = c->dsv ? c->dsv->dsv_flags & (DSV_READ_ONLY_DEPTH | DSV_READ_ONLY_STENCIL) : 0;
    if (!ro) return c->depth;
    oa_state *twin = c->depth ? c->depth->read_only[ro - 1] : &default_state[ro - 1];
    if (twin && twin->o.kind == K_DEPTH) return twin; /* already built */
    if (c->depth && !twin) {
        twin = calloc(1, sizeof(*twin));
        if (!twin) return c->depth;
        c->depth->read_only[ro - 1] = twin;
    }
    if (c->depth) memcpy(twin->desc, c->depth->desc, sizeof(twin->desc));
    else {
        twin->desc[0] = 1; /* D3D11 default: depth test on, LESS, stencil off */
        twin->desc[2] = 2;
    }
    if (ro & DSV_READ_ONLY_DEPTH) twin->desc[1] = 0;            /* DepthWriteMask */
    if (ro & DSV_READ_ONLY_STENCIL) twin->desc[4] &= ~0xff00u;  /* StencilWriteMask */
    twin->o.kind = K_DEPTH;
    return twin;
}

static VkPipeline draw_pipeline(oa_context *c, int fast) {
    pipe_key k = {0};
    k.vs = c->vs; k.ps = c->ps; k.layout = c->layout;
    k.blend = c->blend; k.depth = pipeline_depth_state(c); k.raster = c->raster; k.topology = c->topology;
    for (uint32_t i = 0; i < 8; ++i)
        k.rt[i] = c->rtv[i] ? c->rtv[i]->format : VK_FORMAT_UNDEFINED;
    k.ds = c->dsv ? c->dsv->format : VK_FORMAT_UNDEFINED;
    VkPipeline p = get_pipeline(c, &k, fast);
    c->pipeline_inputs_dirty = !p;
    return p;
}

static void texture_regions(const oa_shader *shader, oa_view *const *views, uint32_t regions[2])
{
    regions[0] = regions[1] = 0;
    if (!shader) return;
    for (uint32_t mask = shader->info.texture_mask; mask; mask &= mask - 1) {
        unsigned slot = __builtin_ctz(mask);
        const oa_view *view = views[slot];
        if (view && view->view && !view->level && view->tex->region_level &&
            shader->info.texture_dim[slot] == 3)
            regions[view->tex->region_level - 1] |= 1u << slot;
    }
}

static void push_render_regions(oa_context *c, VkCommandBuffer cmd, VkPipelineLayout layout)
{
    if (render_regions_epoch && (c->region_bindings_epoch != render_regions_epoch ||
                                 c->region_material_epoch != c->material_epoch)) {
        texture_regions(c->vs, c->vs_srv, c->region_state.vertex_regions);
        texture_regions(c->ps, c->ps_srv, c->region_state.pixel_regions);
        c->region_bindings_epoch = render_regions_epoch;
        c->region_material_epoch = c->material_epoch;
    }
    if (!c->regions_pushed || memcmp(&c->pushed_regions, &c->region_state, sizeof(c->region_state))) {
        pvkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(c->region_state), &c->region_state);
        c->pushed_regions = c->region_state;
        c->regions_pushed = 1;
    }
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
    if (short_render_waits && c->rendering && !c->rendering_waits_for_vertex &&
        c->vs->info.texture_mask && vertex_reads_render_target(c))
        end_rendering(c);
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
        c->regions_pushed = 0;
        c->cache_valid = 1;
    }
    {
        const float sx = 1.0f / c->region_state.position_to_logical[0];
        const float sy = 1.0f / c->region_state.position_to_logical[1];
        VkViewport vp = c->vp;
        if (c->have_vp) {
            vp.x *= sx;
            vp.y *= sy;
            vp.width *= sx;
            vp.height *= sy;
            if (memcmp(&vp, &c->set_vp, sizeof(vp))) {
                pvkCmdSetViewport(cmd, 0, 1, &vp);
                c->set_vp = vp;
            }
        }
        VkRect2D r = {{0, 0}, c->region_extent};
        if (c->raster && c->raster->desc[7] && c->have_scissor) {
            if (sx == 1.0f && sy == 1.0f) {
                r = c->scissor;
            } else {
                r.offset.x = (int32_t)floorf(c->scissor.offset.x * sx);
                r.offset.y = (int32_t)floorf(c->scissor.offset.y * sy);
                r.extent.width = (uint32_t)ceilf((c->scissor.offset.x + (float)c->scissor.extent.width) * sx) - r.offset.x;
                r.extent.height = (uint32_t)ceilf((c->scissor.offset.y + (float)c->scissor.extent.height) * sy) - r.offset.y;
            }
        }
        if (memcmp(&r, &c->set_scissor, sizeof(r))) { pvkCmdSetScissor(cmd, 0, 1, &r); c->set_scissor = r; }
    }
    if (c->stencil_ref != c->set_stencil) { pvkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, c->stencil_ref); c->set_stencil = c->stencil_ref; }
    if (memcmp(c->blend_factor, c->set_blend, 16)) { pvkCmdSetBlendConstants(cmd, c->blend_factor); memcpy(c->set_blend, c->blend_factor, 16); }
    {
        VkBuffer bufs[4];
        VkDeviceSize offs[4], strides[4];
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
            if (bufs[i] != c->set_vb[i] || offs[i] != c->set_off[i] || strides[i] != c->set_stride[i]) changed = 1;
        }
        if (changed) {
            /* No sizes: VK_WHOLE_SIZE in pSizes needs maintenance5, which isn't enabled.
               Mesa drivers can treat it as a literal size and read past the buffer. */
            pvkCmdBindVertexBuffers2(cmd, 0, 4, bufs, offs, NULL, strides);
            g_c_vb++;
            memcpy(c->set_vb, bufs, sizeof(bufs));
            memcpy(c->set_off, offs, sizeof(offs));
            memcpy(c->set_stride, strides, sizeof(strides));
        }
    }
    const prog_entry *prog = fast ? c->selected_pipe->program : program_of(c->vs, c->ps);
    push_render_regions(c, cmd, prog->layout);
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
