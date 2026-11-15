/* ARM-safe adaptation of work/v30-engine/proxy/oa_engine.c:oa_draw_packets
 * and its texture helpers, from koa.exe cs:15032.
 * Addresses below are 32-bit guest addresses, including temporary COM arrays.
 * The caller supplies an immediate graphics context and guest stack scratch. */
typedef struct HandDraw {
    X86Ctx *ctx;
    uint32_t holder;
    uint32_t scratch;
} HandDraw;

#define HCALL(slot, site, ...) hand_call(h, slot, site, (uint32_t[]){__VA_ARGS__}, \
                                       sizeof((uint32_t[]){__VA_ARGS__}) / 4)

static inline void hand_texture(HandDraw *h, uint32_t state, uint32_t slot,
                                uint32_t sampler, uint32_t texture)
{
    const uint32_t zero = h->scratch + 12;
    sts32(zero, 0);
    st32(state + 0x1c + 4 * slot, sampler);
    HCALL(10, 0, slot, 1, sampler ? sampler + 8 : zero);
    st32(state + 0x5c + 4 * slot, texture);
    HCALL(8, 0, slot, 1, texture ? texture + 0x18 : zero);
}

static void hand_textures(HandDraw *h, uint32_t state, uint32_t table,
                          uint32_t ids, uint32_t layout, uint32_t material)
{
    /* The indirect and direct binding record formats are both 12 bytes. */
    if (material) {
        const uint32_t count = ld16(layout + 2);
        const uint32_t bindings = ld32(material + 8);
        const uint32_t indices = ld32(layout + 12);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t id = ld32(ids + 4 * ld8(indices + i));
            const uint32_t b = bindings + 12 * i;
            hand_texture(h, state, ld16(b + 2), ld32(b + 8),
                         id ? ld32(table + 4 * (uint16_t)id) : 0);
        }
        return;
    }
    const uint32_t count = ld32(layout);
    const uint32_t bindings = ld32(layout + 8);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t b = bindings + 12 * i;
        const uint32_t id = ids ? ld32(ids + 4 * i) : ld16(b);
        hand_texture(h, state, ld16(b + 2), ld32(b + 8),
                     !ids || id ? ld32(table + 4 * (uint16_t)id) : 0);
    }
}

static inline uint32_t hand_map(HandDraw *h, uint32_t resource, unsigned mode, uint32_t site)
{
    const uint32_t mapped = h->scratch;
    const int32_t result = HCALL(14, site, resource, 0, mode, 0, mapped);
    return result < 0 ? 0 : lds32(mapped);
}

static inline uint32_t hand_cb_map(HandDraw *h, uint32_t owner, uint32_t site)
{
    const uint32_t buffer = ld32(owner);
    const uint32_t data = hand_map(h, buffer, 4, site);
    if (data)
        st32(h->holder + 4, ld32(owner));
    return data;
}

static inline void hand_cb_commit(HandDraw *h, uint32_t state, unsigned slot, int both)
{
    const uint32_t pending = h->holder + 4;
    if (both)
        st32(state + 0x15c + 4 * slot, 0);
    st32(state + 0x13c + 4 * slot, 0);
    HCALL(15, 0, ld32(pending), 0);
    HCALL(7, 0, slot, 1, pending);
    if (both)
        HCALL(16, 0, slot, 1, pending);
    st32(pending, 0);
}

static inline void hand_copy(uint32_t dst, uint32_t src, uint32_t size)
{
    if (size)
        memmove((void *)X86_PTR(dst), (const void *)X86_PTR(src), size);
}

static inline void hand_clear(uint32_t dst, uint32_t size)
{
    if (size)
        memset((void *)X86_PTR(dst), 0, size);
}

static void hand_scatter(uint32_t dst, uint32_t src, uint32_t records, uint32_t layout)
{
    const uint32_t count = ld16(layout);
    const uint32_t offsets = ld32(layout + 8);
    const uint32_t entries = ld32(records);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entry = entries + 8 * i;
        hand_copy(dst + ld16(entry + 6), src + ld16(offsets + 2 * i), ld16(entry + 4));
    }
}

static inline void hand_vertices(HandDraw *h, uint32_t vb, uint32_t secondary,
                                 uint32_t stride0, uint32_t stride1, uint32_t offset)
{
    const uint32_t args = h->scratch + 16;
    sts32(args, vb);
    sts32(args + 4, secondary);
    sts32(args + 8, stride0);
    sts32(args + 12, stride1);
    sts32(args + 16, offset);
    sts32(args + 20, 0);
    HCALL(18, 0, 0, 2, args, args + 8, args + 16);
}

static void hand_geometry(HandDraw *h, uint32_t state, uint32_t tables,
                          uint32_t packet, int instanced)
{
    const uint32_t mesh = ld32(packet + 0x2c);
    const uint32_t ib = ld32(mesh + 0x14);
    uint32_t count = ld32(mesh + (ib ? 0x18 : 0x10));
    uint32_t ib_offset = 0, vb_offset = 0;
    const uint32_t strides = ld32(h->holder + 0x2c);
    const uint32_t stride0 = ld32(strides + 0xd8);
    if (instanced) {
        st32(state + 0x184, 5);
        const uint32_t mapped = hand_cb_map(h, ld32(tables + 0x18), 0xc96f4f);
        hand_copy(mapped, ld32(packet + 0x30), ld32(packet + 0x34) * 48);
        hand_cb_commit(h, state, 5, 0);
        const uint32_t range = ld32(mesh + 0x34);
        const uint32_t count_override = ld32(range + 12);
        if (count_override)
            count = count_override;
        ib_offset = ld32(range + 8) * 2;
        vb_offset = ld32(range + 0x14) * ld32(ld32(mesh + 8) + 0xd8);
    }
    hand_vertices(h, ld32(mesh + 0xc), ld32(ld32(tables + 0x20) + 4),
                  stride0, ld32(strides + 0xdc), vb_offset);
    HCALL(24, 0, ld32(mesh + 0x2c));
    if (ib) {
        HCALL(19, 0, ib, ld32(mesh + 0x28), ib_offset);
        HCALL(12, 0, count, 0, 0);
    } else {
        HCALL(13, 0, count, 0);
    }
}

static void hand_immediate(HandDraw *h, unsigned code, uint32_t stride, uint32_t count,
                           uint32_t src, int split)
{
    const uint32_t holder = h->holder;
    const uint32_t vertices = code == 4 ? (count / 4) * 6 : count;
    if (code == 4 && count * stride > 0x18000)
        return;
    st8(holder + 0x34, code == 4);
    const uint32_t size = ld32(holder + 0x18);
    const uint32_t bytes = vertices * stride;
    if (bytes > size)
        return;
    uint32_t offset = ld32(holder + 0x14);
    const unsigned mode = offset + bytes < size ? 5 : 4;
    const uint32_t site = split ? (mode == 5 ? 0xc96974 : 0xc96996)
                                : (mode == 5 ? 0xc96c9b : 0xc96cbd);
    const uint32_t mapped = hand_map(h, ld32(holder + 0x10), mode, site);
    if (!mapped) {
        hand_log(h, 0x0173b764, h->ctx->eax);
        return;
    }
    if (mode == 4) {
        offset = 0;
        st32(holder + 0x14, 0);
    }
    const uint32_t dest = mapped + offset;
    st32(holder + 0x20, vertices);
    st32(holder + 0x1c, stride);
    st32(holder + 0x24, code == 4 ? 0 : code);
    st32(holder + 0x38, dest);
    if (code == 4) {
        const uint32_t temp = holder + 0x3c;
        hand_copy(temp, src, count * stride);
        static const uint8_t winding[] = {0, 1, 2, 0, 2, 3};
        for (uint32_t q = 0; q < count / 4; ++q)
            for (uint32_t i = 0; i < 6; ++i)
                hand_copy(dest + (q * 6 + i) * stride,
                          temp + (q * 4 + winding[i]) * stride, stride);
    } else {
        hand_copy(dest, src, count * stride);
    }
    HCALL(15, 0, ld32(holder + 0x10), 0);
    const uint32_t stride_arg = h->scratch + 16;
    sts32(stride_arg, stride);
    HCALL(18, 0, 0, 1, holder + 0x10, stride_arg, holder + 0x14);
    HCALL(19, 0, 0, 0x39, 0);
    HCALL(24, 0, ld32(hand_static(0x0175c6d8) + 4 * ld32(holder + 0x24)));
    HCALL(13, 0, vertices, 0);
    st32(holder + 0x14, offset + bytes);
    st32(holder + 0x1c, 0);
}

/* The adapter admits only the retail primitive values and split sizes whose
 * chunks advance. The lift still handles unexpected packets and diagnostics. */
static void hand_streamed(HandDraw *h, uint32_t packet)
{
    static const uint8_t codes[] = {2, 0, 1, 0, 4, 3};
    const uint32_t stride = ld16(packet + 0x30);
    const uint32_t count = ld16(packet + 0x2e);
    const unsigned code = codes[ld16(packet + 0x2c) - 1];
    const uint32_t src = ld32(packet + 0x34);
    if (count * stride <= 0x10000) {
        hand_immediate(h, code, stride, count, src, 0);
        return;
    }
    const unsigned unit = code == 0 ? 3 : code == 2 ? 2 : code == 4 ? 4 : 1;
    const uint32_t limit = 0x10000 / stride;
    for (uint32_t done = 0; done < count;) {
        uint32_t n = count - done;
        if (n > limit)
            n = limit;
        n -= n % unit;
        if (!n)
            break;
        hand_immediate(h, code, stride, n, src + done * stride, 1);
        done += n;
    }
}

static inline void hand_raster(HandDraw *h, uint32_t state, uint32_t tables, uint32_t key)
{
    const uint32_t modes = ld32(tables);
    const uint32_t rs = ld32(modes + 0x34 + 4 * (key >> 30));
    const uint32_t bs = ld32(modes + 4 * ((key >> 24) & 63));
    const uint32_t ds = ld32(tables + 4);
    const uint32_t stencil = key & 255;
    if (ld32(state + 0x14) != rs) {
        HCALL(43, 0, ld32(rs + 8));
        st32(state + 0x14, rs);
    }
    if (ld32(state + 0x10) != bs) {
        const uint32_t factor = ld32(hand_static(0x013be4a0));
        const uint32_t arg = h->scratch + 16;
        for (unsigned i = 0; i < 4; ++i)
            sts32(arg + 4 * i, factor);
        HCALL(35, 0, ld32(bs + 8), arg, UINT32_MAX);
        st32(state + 0x10, bs);
    }
    if (ld32(state + 0x18) != ds || ld32(state + 0x180) != stencil) {
        HCALL(36, 0, ld32(ds + 8), stencil);
        st32(state + 0x18, ds);
        st32(state + 0x180, stencil);
    }
}

static void hand_draw_packets(HandDraw *h, uint32_t begin, uint32_t end,
                              uint32_t tables, uint32_t state, int queries)
{
    const uint32_t scene = ld32(hand_static(0x019fdfb4));
    uint32_t last_material = 0, last_key = UINT32_MAX;
    for (uint32_t cur = begin; cur != end; cur += 8) {
        const uint32_t packet = ld32(cur + 4);
        const uint32_t material = ld32(packet + 8);
        const uint32_t meta = material ? ld32(material + 0x10) : 0;
        if (!meta)
            continue;
        h->ctx->edi = packet; /* Original Map fast path reads the packet register. */
        const uint32_t bindings = ld32(material + 0x14);
        if (material != last_material) {
            const uint32_t vs = ld32(material + 8), ps = ld32(material + 0xc);
            HCALL(11, 0, vs ? ld32(vs + 8) : 0, 0, 0);
            if (vs) {
                HCALL(17, 0, ld32(meta + 0xc));
                st32(h->holder + 0x2c, ld32(meta + 8));
            }
            HCALL(9, 0, ps ? ld32(ps) : 0, 0, 0);
            st32(state + 0x9c, material + 8);
            st32(state + 0xc, meta);
            last_material = material;
            if (bindings)
                hand_textures(h, state, ld32(tables + 8) - 0x58, 0, bindings + 0xc, 0);
        }
        const uint32_t key = ld32(packet + 4);
        if (key != last_key) {
            hand_raster(h, state, tables, key);
            last_key = key;
        }
        const uint32_t obj0 = ld32(packet + 0x10);
        if (obj0) {
            const uint32_t layout = ld32(packet + 0xc);
            const uint32_t data = hand_cb_map(h, ld32(tables + 0x14), 0xc96440);
            hand_scatter(data, ld32(obj0 + 4), bindings + 0x30, layout);
            hand_cb_commit(h, state, 0, 1);
            hand_textures(h, state, ld32(scene + 0x254), ld32(obj0 + 8), layout, bindings + 0x3c);
        }
        const uint32_t obj3 = ld32(packet + 0x18);
        if (obj3) {
            const uint32_t data = hand_cb_map(h, ld32(tables + 0xc), 0xc9650b);
            hand_copy(data, ld32(obj3 + 4), (uint32_t)ld16(obj3) * 16);
            hand_cb_commit(h, state, 3, 1);
            hand_textures(h, state, ld32(scene + 0x254), ld32(obj3 + 8),
                          ld32(packet + 0x14), bindings + 0x24);
        } else if (!bindings || (ld8(bindings + 0x48) & 4)) {
            const uint32_t data = hand_cb_map(h, ld32(tables + 0xc), 0xc965dd);
            hand_clear(data, 0x90);
            hand_cb_commit(h, state, 3, 1);
        }
        const uint32_t obj4 = ld32(packet + 0x20);
        if (obj4) {
            const uint32_t data = hand_cb_map(h, ld32(tables + 0x10), 0xc96677);
            hand_copy(data, ld32(obj4 + 4), (uint32_t)ld16(obj4) * 16);
            hand_cb_commit(h, state, 4, 1);
            const uint32_t layout = ld32(packet + 0x1c);
            if (layout)
                hand_textures(h, state, ld32(scene + 0x254), ld32(obj4 + 8), layout, 0);
        }
        const uint32_t obj5 = ld32(packet + 0x24);
        if (obj5) {
            const uint32_t data = hand_cb_map(h, ld32(tables + 0x1c), 0xc96736);
            const uint32_t size = (uint32_t)ld16(obj5) * 16;
            if (ld8(hand_static(0x01966179)))
                hand_copy(data, ld32(obj5 + 4), size);
            else
                hand_clear(data, size);
            hand_cb_commit(h, state, 5, 1);
        }
        const uint32_t pred = ld32(packet + 0x28);
        if (pred && ld8(hand_static(0x0196617a))) {
            if (queries)
                HCALL(27, 0, ld32(pred));
            else
                HCALL(30, 0, ld32(pred), 0);
        }
        const uint32_t kind = ld32(packet);
        if (kind == 2)
            hand_streamed(h, packet);
        else if (kind <= 1)
            hand_geometry(h, state, tables, packet, kind == 1);
        if (pred && ld8(hand_static(0x0196617a))) {
            if (queries)
                HCALL(28, 0, ld32(pred));
            else
                HCALL(30, 0, 0, 0);
        }
    }
}

#undef HCALL
