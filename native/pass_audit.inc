#include <time.h>
#include <unistd.h>

/* A short diagnostic capture. Content hashes describe command inputs, not
   the pixels in resources that preceding GPU work can have changed. */
#define PASS_CAPTURE_FRAMES 8
#define PASS_SCOPE_MAX 128
#define PASS_DRAW_MAX 8192
#define PASS_RENDER_MAX 1024
static int pass_frames, pass_capture_done;
static FILE *pass_file;
static _Thread_local uint32_t pass_scope;
static uint32_t pass_scope_n, pass_draw_n, pass_render_n, pass_dropped;
static uint64_t pass_constants, pass_textures;
static struct PassScope {
    uint32_t site, parent, ecx, args[4];
    uint64_t start, cpu;
} pass_scopes[PASS_SCOPE_MAX];
static struct PassRender {
    uint32_t scope, color, depth, w, h, format, depth_format;
} pass_renders[PASS_RENDER_MAX];
static struct PassDraw {
    uint32_t scope, render, vs, ps, n, first;
    int32_t base;
    uint64_t geometry, state, constants, textures;
} pass_draws[PASS_DRAW_MAX];

static uint64_t audit_hash(uint64_t h, uint64_t value) {
    return (h ^ value) * UINT64_C(1099511628211);
}

static uint64_t audit_bytes(uint64_t h, const void *p, size_t n) {
    const uint8_t *bytes = p;
    while (n >= 8) {
        uint64_t word;
        memcpy(&word, bytes, 8);
        h = audit_hash(h, word);
        n -= 8;
        bytes += 8;
    }
    while (n--) h = audit_hash(h, *bytes++);
    return h;
}

static uint64_t pass_cpu_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + t.tv_nsec;
}

uint32_t oa_pass_push(uint32_t site, X86Ctx *ctx) {
    if (!pass_frames || pass_scope_n + 1 >= PASS_SCOPE_MAX) return 0;
    uint32_t id = ++pass_scope_n;
    struct PassScope *s = &pass_scopes[id];
    s->site = site;
    s->parent = pass_scope;
    s->ecx = ctx->ecx;
    for (uint32_t i = 0; i < 4; ++i) s->args[i] = lds32(ctx->esp + 4 + 4*i);
    s->cpu = 0;
    pass_scope = id;
    s->start = pass_cpu_ns();
    return id;
}

void oa_pass_pop(uint32_t id) {
    if (!id) return;
    struct PassScope *s = &pass_scopes[id];
    s->cpu = pass_cpu_ns() - s->start;
    pass_scope = s->parent;
}

static void pass_begin_render(oa_context *c, uint32_t w, uint32_t h) {
    if (!pass_frames) return;
    if (pass_render_n + 1 == PASS_RENDER_MAX) { ++pass_dropped; return; }
    const oa_view *rt = c->rtv[0], *ds = c->dsv;
    pass_renders[++pass_render_n] = (struct PassRender){
        pass_scope, rt ? rt->uid : 0, ds ? ds->uid : 0, w, h,
        rt ? (uint32_t)rt->format : 0, ds ? (uint32_t)ds->format : 0
    };
}

static void pass_record_draw(oa_context *c, int indexed, uint32_t n, uint32_t first, int32_t base) {
    if (!pass_frames) return;
    if (pass_draw_n == PASS_DRAW_MAX) { ++pass_dropped; return; }
    struct PassDraw *d = &pass_draws[pass_draw_n++];
    *d = (struct PassDraw){.scope = pass_scope, .render = pass_render_n,
                           .vs = c->vs->hash, .ps = c->ps ? c->ps->hash : 0,
                           .n = n, .first = first, .base = base,
                           .constants = pass_constants, .textures = pass_textures};
    uint64_t geom = audit_hash(n, first);
    geom = audit_hash(geom, (uint32_t)base);
    geom = audit_hash(geom, indexed);
    for (uint32_t i = 0; i < 4; ++i) {
        const oa_buffer *b = c->vb[i];
        geom = audit_hash(geom, (uint64_t)c->set_vb[i]);
        geom = audit_hash(geom, c->set_off[i]);
        geom = audit_hash(geom, c->set_stride[i]);
        if (b) geom = audit_hash(geom, b->audit_version);
    }
    if (indexed) {
        geom = audit_hash(geom, (uint64_t)c->set_ib);
        geom = audit_hash(geom, c->set_ib_off);
        geom = audit_hash(geom, c->set_ib_type);
        geom = audit_hash(geom, c->ib->audit_version);
    }
    d->geometry = geom;
    uint64_t state = audit_hash(0, (uintptr_t)c->last_pipeline);
    state = audit_bytes(state, &c->set_vp, sizeof(c->set_vp));
    state = audit_bytes(state, &c->set_scissor, sizeof(c->set_scissor));
    state = audit_bytes(state, &c->set_blend, sizeof(c->set_blend));
    d->state = audit_hash(state, c->set_stencil);
}

static void pass_audit_present(void) {
    if (pass_frames) {
        const unsigned long long frame = g_frame;
        fprintf(pass_file, "F\t%llu\t%u\t%u\t%u\t%u\n", frame, pass_scope_n, pass_render_n, pass_draw_n, pass_dropped);
        for (uint32_t i = 1; i <= pass_scope_n; ++i) {
            const struct PassScope *s = &pass_scopes[i];
            fprintf(pass_file, "S\t%llu\t%u\t%u\t%06x\t%llu\t%x\t%x\t%x\t%x\t%x\n",
                    frame, i, s->parent, s->site, (unsigned long long)s->cpu, s->ecx,
                    s->args[0], s->args[1], s->args[2], s->args[3]);
        }
        for (uint32_t i = 1; i <= pass_render_n; ++i) {
            const struct PassRender *r = &pass_renders[i];
            fprintf(pass_file, "R\t%llu\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",
                    frame, i, r->scope, r->color, r->depth, r->w, r->h, r->format, r->depth_format);
        }
        for (uint32_t i = 0; i < pass_draw_n; ++i) {
            const struct PassDraw *d = &pass_draws[i];
            fprintf(pass_file, "D\t%llu\t%u\t%u\t%08x\t%08x\t%u\t%u\t%d\t%016llx\t%016llx\t%016llx\t%016llx\n",
                    frame, d->scope, d->render, d->vs, d->ps, d->n, d->first, d->base,
                    (unsigned long long)d->geometry, (unsigned long long)d->state,
                    (unsigned long long)d->constants, (unsigned long long)d->textures);
        }
        pass_scope_n = pass_render_n = pass_draw_n = pass_dropped = 0;
        if (--pass_frames == 0) {
            fclose(pass_file);
            pass_capture_done = 1;
            FILE *done = fopen("pass-audit.done", "w");
            if (done) fclose(done);
        }
    } else if (!pass_capture_done && (g_frame & 15u) == 0 && access("pass-audit", F_OK) == 0) {
        pass_file = fopen("pass-audit.tsv", "w");
        if (pass_file) {
            setvbuf(pass_file, NULL, _IOFBF, 262144);
            pass_frames = PASS_CAPTURE_FRAMES;
        }
    }
}
