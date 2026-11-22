/* Part of oa_vk_fullscreen.c: Map/Unmap. Included in place; not compiled on its own. */
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
    if (t->region_level && !sub && !dx && !dy && w == t->w && h == t->h)
        set_texture_region(t, OA_RESOLUTION_FULL);
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
        if (!dsub && !ssub && !x && !y && !box && d->w == s->w && d->h == s->h)
            set_texture_region(d, s->region_level);
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
    if (!v->level) set_texture_region(v->tex, OA_RESOLUTION_FULL);
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
    if (!v->level) set_texture_region(v->tex, OA_RESOLUTION_FULL);
}
