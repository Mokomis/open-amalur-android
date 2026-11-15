/* Use the existing graphics adapters directly for the qualified draw-list caller.
 * They consume the same guest arguments as the vtable thunks. */
static inline uint32_t hand_static(uint32_t address)
{
    return address;
}

typedef struct HandDraw HandDraw;
static uint32_t hand_call(HandDraw *, unsigned, uint32_t, const uint32_t *, unsigned);
static void hand_log(HandDraw *, uint32_t, uint32_t);

#include "handwritten_draw.inc"

static uint32_t hand_call(HandDraw *h, unsigned slot, uint32_t site,
                          const uint32_t *args, unsigned count)
{
    X86Ctx *ctx = h->ctx;
    const uint32_t sp = h->scratch - 32;
    sts32(sp, site);
    sts32(sp + 4, ld32(h->holder));
    for (unsigned i = 0; i < count; ++i)
        sts32(sp + 8 + 4 * i, args[i]);
    ctx->esp = sp;
    tables[GFX_CONTEXT].methods[slot].fn(ctx);
    return ctx->eax;
}

static void hand_log(HandDraw *h, uint32_t format, uint32_t value)
{
    const uint32_t args[] = {format, value};
    h->ctx->esp = h->scratch - 48;
    rt_call(h->ctx, 0x01050e90, 2, args);
}

static uint64_t handwritten_lists, handwritten_packets, handwritten_fallback;

void oa_handwritten_draw_report(void)
{
    rt_log("HANDDRAW lists=%llu packets=%llu fallback=%llu",
           (unsigned long long)__atomic_load_n(&handwritten_lists, __ATOMIC_RELAXED),
           (unsigned long long)__atomic_load_n(&handwritten_packets, __ATOMIC_RELAXED),
           (unsigned long long)__atomic_load_n(&handwritten_fallback, __ATOMIC_RELAXED));
}

int oa_try_handwritten_draw(X86Ctx *ctx)
{
    const uint32_t sp = ctx->esp;
    const uint32_t begin = rt_arg(ctx, 0), end = rt_arg(ctx, 1);
    const uint32_t resources = rt_arg(ctx, 2), state = rt_arg(ctx, 3);
    const int queries = (uint8_t)rt_arg(ctx, 4) != 0;
    if (begin == end) {
        ctx->eax = end;
        ctx->ecx = ld32(0x0197713c);
        ctx->esp = sp + 4;
        return 1;
    }
    const uint32_t holder = ld32(state);
    const uint32_t device_context = ld32(holder);
    if (end < begin || ((end - begin) & 7) || end - begin > 8u * 65536u ||
        !device_context || ld32(device_context) != tables[GFX_CONTEXT].vtable)
        goto original;

    /* Preserve the original diagnostic/exception path for unusual primitives.
     * Reject the list before any writes, so fallback replays no commands. */
    for (uint32_t cur = begin; cur != end; cur += 8) {
        const uint32_t packet = ld32(cur + 4);
        if (ld32(packet) != 2)
            continue;
        const unsigned primitive = ld16(packet + 0x2c);
        const unsigned count = ld16(packet + 0x2e);
        const unsigned stride = ld16(packet + 0x30);
        if (!stride || primitive < 1 || primitive > 6 || primitive == 4)
            goto original;
        const unsigned unit = primitive == 1 ? 2 : primitive == 2 ? 3 : primitive == 5 ? 4 : 1;
        if (count * stride > 0x10000 && (count % unit || 0x10000 / stride < unit))
            goto original;
    }

    const uint32_t edi = ctx->edi;
    HandDraw h = {ctx, holder, sp - 128};
    hand_draw_packets(&h, begin, end, resources, state, queries);
    ctx->edi = edi;
    ctx->eax = end;
    ctx->ecx = ld32(0x0197713c);
    ctx->esp = sp + 4;
    __atomic_fetch_add(&handwritten_lists, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&handwritten_packets, (end - begin) / 8, __ATOMIC_RELAXED);
    return 1;

original:
    __atomic_fetch_add(&handwritten_fallback, 1, __ATOMIC_RELAXED);
    return 0;
}
