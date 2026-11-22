/* Part of oa_vk_fullscreen.c: device resource creation. Included in place; not compiled on its own. */
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
    int prefer_cached = 0;
#ifdef _WIN32
    /* The guest Map bridge reads the current vertex/index contents to find
       changed bytes on NO_OVERWRITE. Reads from write-combined VRAM can stall
       for milliseconds even though the GPU has finished the previous frame. */
    prefer_cached = b->usage == 2 && (b->bind & 3) && (b->cpu & 0x10000u);
#endif
    oa_buffer_generation gen = {0};
    if (!(place ? dynamic_buffer_acquire(&gen.buffer, b->size, u, prefer_cached) :
                  make_buffer_with_mapping(&gen.buffer, b->size, u, 0, prefer_cached))) return 0;
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

static int make_texture_image(oa_tex *t, VkImageUsageFlags usage, VkImageCreateFlags flags,
                              VkImageType type) {
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
    return pvkBindImageMemory(g_dev, t->image, t->mem, 0) == VK_SUCCESS;
}

static int make_image(oa_tex *t, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageType type)
{
    return make_texture_image(t, usage, flags, type);
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
    if (usage_d3d != 3 && !make_texture_image(t, usage, flags, is3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D)) {
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
    v->level = base;
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
    /* D3D11_DEPTH_STENCIL_VIEW_DESC: Format, ViewDimension, Flags, MipSlice. */
    v->dsv_flags = desc ? desc[2] & (DSV_READ_ONLY_DEPTH | DSV_READ_ONLY_STENCIL) : 0;
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
    if (!oa_dxbc_translate_regions(code, size, &s->info, &spv, &words, err, sizeof(err))) {
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
    if (prepare_shader_variants || record_shader_variants) {
        s->recipe_id = shader_recipe_id(spv, ci.codeSize);
        s->recipe_interface_id = shader_recipe_id(&s->info, sizeof(s->info));
    }
    if (keep_shader_code(ci.codeSize)) {
        s->recipe_code = spv;
        s->recipe_bytes = (uint32_t)ci.codeSize;
    } else {
        free(spv);
    }
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
