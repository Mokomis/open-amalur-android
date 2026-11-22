#include "title_cover.h"
#include "display_android.h"
/* oa_vk.c: the subset of Direct3D 11 and DXGI that koa.exe uses, implemented on Vulkan 1.3.
   Built as d3d11.dll. Anything not implemented logs its name once to oa_vk.log and returns E_NOTIMPL. */
#define WIN32_LEAN_AND_MEAN
#include "oa_android_win.h"
#include "rt.h"
#include <android/native_window.h>
#include <malloc.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include "oa_present_lifecycle.h"
#include "oa_dynamic_resolution.h"
#include "veil_native_scaling.h"
#include "oa_dxbc.h"

#define COM __stdcall
#define S_OK_ 0
#define E_NOTIMPL_ ((HRESULT)0x80004001L)
#define E_NOINTERFACE_ ((HRESULT)0x80004002L)
#define E_FAIL_ ((HRESULT)0x80004005L)
#define E_OOM_ ((HRESULT)0x8007000EL)
#define DXGI_NOT_FOUND ((HRESULT)0x887A0002L)
#define FRAMES 2
#define RING_SIZE (32u << 20)
#define UPLOAD_OWNERS 16384u

/* ---- logging ---- */
static FILE *g_log;
static CRITICAL_SECTION g_lock;
static void logf_(const char *fmt, ...) {
    va_list a;
    if (!g_log) return;
    EnterCriticalSection(&g_lock);
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

#include "oa_vk_entry_points.inc"

#include "oa_vk_com.inc"

#include "oa_vk_stubs.inc"

/* ---- IUnknown ---- */
static ULONG COM o_addref(obj *o) { return (ULONG)InterlockedIncrement(&o->ref); }

/* Objects are parked for a few frames before their Vulkan handles go away; the GPU may still be using them. */
typedef struct gone { struct gone *next; obj *o; uint64_t frame; } gone;
static gone *g_gone;
static uint64_t g_frame = 1, g_done_frame;
#include "pass_audit.inc"
extern int oa_fast_pass_batches;
static uint64_t batch_pipeline_reused, batch_descriptor_reused, batch_verified, batch_queries_omitted;

void oa_batch_report(void) {
    rt_log("PASS counters pipelines=%llu descriptors=%llu checked=%llu queries=%llu",
           (unsigned long long)batch_pipeline_reused, (unsigned long long)batch_descriptor_reused,
           (unsigned long long)batch_verified, (unsigned long long)batch_queries_omitted);
}
static uint64_t upload_draw_copies, upload_draw_bytes;
static uint64_t upload_skipped_copies, upload_skipped_bytes, upload_preserved_bytes;

static void forget_upload(oa_buffer *b) {
    if (b->upload_owner) {
        *b->upload_owner = NULL;
        b->upload_owner = NULL;
        b->upload_data = NULL;
    }
    b->upload_written = 0;
}

static void restore_upload_shadow(oa_buffer *b) {
    if (!b->upload_data) return;
    if (b->upload_mapped) rt_fatal("constant upload still mapped when shadow needed");
    memcpy(b->shadow, b->upload_data, b->size);
    upload_preserved_bytes += b->size;
    forget_upload(b);
}

void oa_upload_report(void) {
    rt_log("UPLOAD counters copied=%llu bytes=%llu avoided=%llu bytes=%llu preserved=%llu",
           (unsigned long long)upload_draw_copies, (unsigned long long)upload_draw_bytes,
           (unsigned long long)upload_skipped_copies, (unsigned long long)upload_skipped_bytes,
           (unsigned long long)upload_preserved_bytes);
}

static ULONG COM o_release(obj *o) {
    LONG r = InterlockedDecrement(&o->ref);
    if (r == 0) {
        gone *g = malloc(sizeof(gone));
        EnterCriticalSection(&g_lock);
        g->o = o;
        g->frame = g_frame;
        g->next = g_gone;
        g_gone = g;
        LeaveCriticalSection(&g_lock);
    }
    return (ULONG)r;
}

static void destroy_obj(obj *o) {
    switch (o->kind) {
    case K_BUFFER: {
        oa_buffer *b = (oa_buffer *)o;
        forget_upload(b);
        for (size_t generation = 0; generation < b->ngens; generation++) {
            vk_buffer *gen = &b->gens[generation].buffer;
            if (gen->pool_slot) {
                dynamic_buffer_return(gen);
            } else {
                pvkDestroyBuffer(g_dev, gen->buf, NULL);
                pvkFreeMemory(g_dev, gen->mem, NULL);
            }
        }
        if (b->gens != b->inline_gens) free(b->gens);
        if(b->shadow_allocation)rt_heap_free(b->shadow_allocation);else _aligned_free(b->shadow);
        break;
    }
    case K_TEX2D: {
        oa_tex *t = (oa_tex *)o;
        if (t->hdr_view) pvkDestroyImageView(g_dev, t->hdr_view, NULL);
        if (t->image) pvkDestroyImage(g_dev, t->image, NULL);
        if (t->mem) pvkFreeMemory(g_dev, t->mem, NULL);
        _aligned_free(t->cpu);
        break;
    }
    case K_SRV: case K_RTV: case K_DSV: {
        oa_view *v = (oa_view *)o;
        if (v->view) pvkDestroyImageView(g_dev, v->view, NULL);
        if (v->tex) o_release(&v->tex->o);
        break;
    }
    case K_SAMPLER: {
        oa_state *s = (oa_state *)o;
        for (unsigned i = 0; i < 2; ++i)
            if (s->af_sampler[i]) pvkDestroySampler(g_dev, s->af_sampler[i], NULL);
        break;
    }
    case K_VS: case K_PS: {
        oa_shader *shader = (oa_shader *)o;
        if (shader->recipe_code) {
            free(shader->recipe_code);
            __atomic_fetch_sub(&captured_shader_bytes, shader->recipe_bytes, __ATOMIC_RELAXED);
        }
        break;
    }
    case K_CHAIN:
        present_chain_release((oa_chain *)o);
        break;
    default: break;
    }
    free(o);
}

static void collect_gone(void) {
    gone *keep = NULL, *g, *n, *dead = NULL;
    EnterCriticalSection(&g_lock);
    for (g = g_gone; g; g = n) {
        n = g->next;
        if (g->frame + FRAMES + 1 < g_frame && g->frame <= g_done_frame) { g->next = dead; dead = g; }
        else { g->next = keep; keep = g; }
    }
    g_gone = keep;
    LeaveCriticalSection(&g_lock);
    for (g = dead; g; g = n) {
        n = g->next;
        destroy_obj(g->o);
        free(g);
    }
}

static const GUID IID_IUnknown_ = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_D3D11Device = {0xdb6f6ddb, 0xac77, 0x4e88, {0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40}};
static const GUID IID_DXGIDevice = {0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};
static obj *g_dxgi_device, *g_adapter, *g_factory;

static HRESULT COM o_qi(obj *o, const GUID *iid, void **out) {
    if (!memcmp(iid, &IID_IUnknown_, sizeof(GUID))) {
        o_addref(o);
        *out = o;
        return S_OK_;
    }
    if (o->kind == K_DEVICE && !memcmp(iid, &IID_D3D11Device, sizeof(GUID))) {
        o_addref(o);
        *out = o;
        return S_OK_;
    }
    if (o->kind == K_DEVICE && !memcmp(iid, &IID_DXGIDevice, sizeof(GUID))) {
        o_addref(g_dxgi_device);
        *out = g_dxgi_device;
        return S_OK_;
    }
    if (o->kind == K_TEX2D || o->kind == K_BUFFER || o->kind == K_CHAIN || o->kind == K_FACTORY || o->kind == K_ADAPTER || o->kind == K_DXGI_DEVICE) {
        /* Same object for any derived interface of the object itself. */
        o_addref(o);
        *out = o;
        return S_OK_;
    }
    logf_("QueryInterface kind %d unknown %08lx-%04x-%04x", o->kind, iid->Data1, iid->Data2, iid->Data3);
    *out = NULL;
    return E_NOINTERFACE_;
}

static obj *new_obj(size_t size, int kind) {
    obj *o = calloc(1, size);
    o->vtbl = g_vt[kind];
    o->ref = 1;
    o->kind = kind;
    return o;
}

#include "oa_vk_formats.inc"

#include "oa_vk_frames.inc"

#include "oa_vk_resources.inc"

#include "oa_vk_pipelines.inc"

#include "oa_vk_state.inc"

#include "oa_vk_map.inc"

#include "oa_vk_draw.inc"

#include "oa_vk_dxgi.inc"

/* ---- resource getters ---- */
static void COM t_get_desc(oa_tex *t, uint32_t *d) { memcpy(d, t->desc, (t->type3d ? 9 : 11) * 4); if (!t->type3d) d[2] = t->mips; else d[3] = t->mips; }
static void COM b_get_desc(oa_buffer *b, uint32_t *d) { d[0] = b->size; d[1] = b->usage; d[2] = b->bind; d[3] = b->cpu; d[4] = b->misc; d[5] = b->stride; }
static void COM r_get_type(obj *o, UINT *t) { *t = o->kind == K_BUFFER ? 1 : (((oa_tex *)o)->type3d ? 4 : 3); }
static void COM v_get_resource(oa_view *v, obj **out) { o_addref(&v->tex->o); *out = &v->tex->o; }
static void COM child_get_device(obj *o, obj **out) { (void)o; o_addref(&g_device_obj); *out = &g_device_obj; }

#include "oa_vk_setup.inc"
