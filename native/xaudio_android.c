/* Original Win32 XAudio2.7 COM ABI backed by the native FAudio mixer. */
#include "rt.h"
#include "FAudio.h"
#include "FAPO.h"
#include "FAudioFX.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARG(n) rt_arg(ctx, (n) + 1)
#define RET(v) (ctx->eax = (uint32_t)(v))
#define PTR(n) (ARG(n) ? rt_ptr(ARG(n)) : NULL)
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))

enum { ENGINE = 1, VOICE, EFFECT };
static const FAudioGUID engine_class = {0x5a508685, 0xa254, 0x4fba, {0x9b,0x82,0x9a,0x24,0xb0,0x03,0x06,0xaf}};
static const FAudioGUID engine_iid = {0x8bcf1f58, 0x9fe7, 0x4583, {0x8a,0xc6,0xe2,0xad,0xc4,0x65,0xc8,0xbb}};
static const FAudioGUID effect_class = {0x6a93130e, 0x1d53, 0x41d1, {0xa9,0xcf,0xe7,0x58,0x80,0x0b,0xb1,0x79}};
static const FAudioGUID unknown_iid = {0, 0, 0, {0xc0,0,0,0,0,0,0,0x46}};
static uint32_t vtables[4];
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct EngineCallback {
    FAudioEngineCallback native;
    uint32_t guest;
    struct EngineCallback *next;
} EngineCallback;

typedef struct Voice {
    FAudioVoiceCallback native;
    FAudioVoice *voice;
    uint32_t callback, guest;
    struct Voice *next;
    struct Engine *owner;
} Voice;

typedef struct Engine {
    FAudio *audio;
    EngineCallback *callbacks;
    Voice *voices;
    pthread_mutex_t lock;
    int initialized;
} Engine;

int oa_faudio_start_output(FAudio *);
static uint32_t make_object(unsigned, void *);
static int guid_is(uint32_t p, const FAudioGUID *guid)
{
    return p && !memcmp(rt_ptr(p), guid, sizeof *guid);
}

static void *native_object(uint32_t guest, unsigned type)
{
    if (!guest || ld32(guest) != vtables[type]) return NULL;
    return (void *)(uintptr_t)ld64(guest + 8);
}

static Engine *engine(X86Ctx *ctx) { return native_object(rt_arg(ctx, 0), ENGINE); }
static Voice *voice(X86Ctx *ctx) { return native_object(rt_arg(ctx, 0), VOICE); }
static FAudioVoice *voice_at(uint32_t g)
{
    Voice *obj = native_object(g, VOICE);
    return obj ? obj->voice : NULL;
}

static void callback(uint32_t object, unsigned slot, unsigned count, uint32_t a, uint32_t b)
{
    RtThread *thread = rt_thread_self();
    if (!thread) {
        thread = rt_thread_new(1u << 20);
        rt_thread_bind(thread);
    }
    const uint32_t args[] = {object, a, b};
    X86Ctx saved = thread->ctx;
    rt_call(&thread->ctx, ld32(ld32(object) + slot * 4), count + 1, args);
    thread->ctx = saved;
}

static void processing_start(FAudioEngineCallback *p) { callback(((EngineCallback *)p)->guest, 0, 0, 0, 0); }
static void processing_end(FAudioEngineCallback *p) { callback(((EngineCallback *)p)->guest, 1, 0, 0, 0); }
static void processing_error(FAudioEngineCallback *p, uint32_t e) { callback(((EngineCallback *)p)->guest, 2, 1, e, 0); }
static void voice_pass_start(FAudioVoiceCallback *p, uint32_t n) { callback(((Voice *)p)->callback, 0, 1, n, 0); }
static void voice_pass_end(FAudioVoiceCallback *p) { callback(((Voice *)p)->callback, 1, 0, 0, 0); }
static void stream_end(FAudioVoiceCallback *p) { callback(((Voice *)p)->callback, 2, 0, 0, 0); }
static void buffer_start(FAudioVoiceCallback *p, void *c) { callback(((Voice *)p)->callback, 3, 1, (uintptr_t)c, 0); }
static void buffer_end(FAudioVoiceCallback *p, void *c) { callback(((Voice *)p)->callback, 4, 1, (uintptr_t)c, 0); }
static void loop_end(FAudioVoiceCallback *p, void *c) { callback(((Voice *)p)->callback, 5, 1, (uintptr_t)c, 0); }
static void voice_error(FAudioVoiceCallback *p, void *c, uint32_t e) { callback(((Voice *)p)->callback, 6, 2, (uintptr_t)c, e); }

static void addref(X86Ctx *ctx)
{
    uint32_t guest = rt_arg(ctx, 0);
    Engine *e = native_object(guest, ENGINE);
    FAPO *effect = native_object(guest, EFFECT);
    RET(e ? FAudio_AddRef(e->audio) : effect->AddRef(effect));
}

static void release(X86Ctx *ctx)
{
    uint32_t guest = rt_arg(ctx, 0);
    Engine *e = native_object(guest, ENGINE);
    FAPO *effect = native_object(guest, EFFECT);
    uint32_t refs = e ? FAudio_Release(e->audio) : effect->Release(effect);
    if (!refs) {
        if (e) {
            while (e->voices) {
                Voice *v = e->voices;
                e->voices = v->next;
                rt_heap_free(v->guest);
                free(v);
            }
            while (e->callbacks) {
                EngineCallback *cb = e->callbacks;
                e->callbacks = cb->next;
                free(cb);
            }
            pthread_mutex_destroy(&e->lock);
            free(e);
        }
        rt_heap_free(guest);
    }
    RET(refs);
}

static void query(X86Ctx *ctx)
{
    uint32_t guest = rt_arg(ctx, 0), iid = ARG(0), out = ARG(1);
    if (!out) { RET(0x80004003u); return; }
    if (guid_is(iid, &unknown_iid) || (engine(ctx) && guid_is(iid, &engine_iid))) {
        addref(ctx);
        st32(out, guest);
        RET(0);
    } else {
        st32(out, 0);
        RET(0x80004002u);
    }
}

static void get_count(X86Ctx *ctx) { RET(FAudio_GetDeviceCount(engine(ctx)->audio, PTR(0))); }
static void get_details(X86Ctx *ctx) { RET(FAudio_GetDeviceDetails(engine(ctx)->audio, ARG(0), PTR(1))); }
static void initialize(X86Ctx *ctx)
{
    Engine *e = engine(ctx);
    if (e->initialized) { RET(FAUDIO_E_INVALID_CALL); return; }
    uint32_t result = FAudio_Initialize(e->audio, ARG(0) & (FAUDIO_DEBUG_ENGINE | FAUDIO_1024_QUANTUM), FAUDIO_DEFAULT_PROCESSOR);
    if (!result) e->initialized = 1;
    rt_log("Android XAudio2.7 Initialize flags=%#x HRESULT=%#x", ARG(0), result);
    RET(result);
}
static void register_callback(X86Ctx *ctx)
{
    if (!ARG(0)) { RET(FAUDIO_E_INVALID_ARG); return; }
    Engine *e = engine(ctx);
    EngineCallback *cb = calloc(1, sizeof *cb);
    if (!cb) { RET(FAUDIO_E_OUT_OF_MEMORY); return; }
    cb->guest = ARG(0);
    cb->native = (FAudioEngineCallback){processing_error, processing_end, processing_start};
    pthread_mutex_lock(&e->lock);
    uint32_t result = FAudio_RegisterForCallbacks(e->audio, &cb->native);
    if (!result) { cb->next = e->callbacks; e->callbacks = cb; }
    else free(cb);
    pthread_mutex_unlock(&e->lock);
    RET(result);
}
static void unregister_callback(X86Ctx *ctx)
{
    Engine *e = engine(ctx);
    pthread_mutex_lock(&e->lock);
    for (EngineCallback **item = &e->callbacks; *item; item = &(*item)->next) {
        if ((*item)->guest == ARG(0)) {
            EngineCallback *cb = *item;
            FAudio_UnregisterForCallbacks(e->audio, &cb->native);
            *item = cb->next;
            free(cb);
            break;
        }
    }
    pthread_mutex_unlock(&e->lock);
}

enum { MAX_ROUTES = 64 };
typedef struct {
    FAudioVoiceSends sends;
    FAudioSendDescriptor routes[MAX_ROUTES];
    FAudioEffectChain effects;
    FAudioEffectDescriptor descriptors[MAX_ROUTES];
} Routes;

static int read_sends(Routes *r, uint32_t guest)
{
    if (!guest) return 1;
    uint32_t count = ld32(guest), list = ld32(guest + 4);
    if (count > MAX_ROUTES || (count && !list)) return 0;
    r->sends = (FAudioVoiceSends){count, r->routes};
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t p = list + 8 * i;
        FAudioVoice *out = voice_at(ld32(p + 4));
        if (!out) return 0;
        r->routes[i] = (FAudioSendDescriptor){ld32(p), out};
    }
    return 1;
}

static int read_effects(Routes *r, uint32_t guest)
{
    if (!guest) return 1;
    uint32_t count = ld32(guest), list = ld32(guest + 4);
    if (count > MAX_ROUTES || (count && !list)) return 0;
    r->effects = (FAudioEffectChain){count, r->descriptors};
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t p = list + 12 * i;
        FAPO *effect = native_object(ld32(p), EFFECT);
        if (!effect) return 0;
        r->descriptors[i] = (FAudioEffectDescriptor){effect, ld32(p + 4), ld32(p + 8)};
    }
    return 1;
}

static float as_float(uint32_t v)
{
    float result;
    memcpy(&result, &v, sizeof result);
    return result;
}

enum { MASTER, SUBMIX, SOURCE };
static void create_voice(X86Ctx *ctx, int kind)
{
    uint32_t out = ARG(0);
    if (!out) { RET(FAUDIO_E_INVALID_ARG); return; }
    st32(out, 0);
    Engine *e = engine(ctx);
    if (!e->initialized) { RET(FAUDIO_E_INVALID_CALL); return; }
    Routes routes;
    const uint32_t sends = kind == MASTER ? 0 : ARG(5);
    const uint32_t effects = kind == MASTER ? ARG(5) : ARG(6);
    if (!read_sends(&routes, sends) || !read_effects(&routes, effects)) { RET(FAUDIO_E_INVALID_ARG); return; }
    const FAudioVoiceSends *s = sends ? &routes.sends : NULL;
    const FAudioEffectChain *f = effects ? &routes.effects : NULL;
    Voice *v = calloc(1, sizeof *v);
    if (!v) { RET(FAUDIO_E_OUT_OF_MEMORY); return; }
    v->owner = e;
    uint32_t result;
    if (kind == MASTER) {
        result = FAudio_CreateMasteringVoice(e->audio, &v->voice, ARG(1), ARG(2), ARG(3), ARG(4), f);
        if (!result && !oa_faudio_start_output(e->audio)) {
            FAudioVoice_DestroyVoice(v->voice);
            result = FAUDIO_E_DEVICE_INVALIDATED;
        }
        rt_log("Android XAudio2.7 mastering voice HRESULT=%#x", result);
    } else if (kind == SUBMIX) {
        result = FAudio_CreateSubmixVoice(e->audio, &v->voice, ARG(1), ARG(2), ARG(3), ARG(4), s, f);
    } else {
        v->callback = ARG(4);
        v->native = (FAudioVoiceCallback){buffer_end, buffer_start, loop_end, stream_end, voice_error, voice_pass_end, voice_pass_start};
        result = FAudio_CreateSourceVoice(e->audio, &v->voice, PTR(1), ARG(2), as_float(ARG(3)),
                                          v->callback ? &v->native : NULL, s, f);
    }
    if (result) {
        if (kind != MASTER) rt_log("Android XAudio2.7 create voice kind=%d HRESULT=%#x", kind, result);
        free(v);
    } else {
        v->guest = make_object(VOICE, v);
        pthread_mutex_lock(&e->lock);
        v->next = e->voices;
        e->voices = v;
        pthread_mutex_unlock(&e->lock);
        st32(out, v->guest);
    }
    RET(result);
}

static void master(X86Ctx *ctx) { create_voice(ctx, MASTER); }
static void submix(X86Ctx *ctx) { create_voice(ctx, SUBMIX); }
static void source(X86Ctx *ctx) { create_voice(ctx, SOURCE); }
static void start_engine(X86Ctx *ctx) { RET(FAudio_StartEngine(engine(ctx)->audio)); }
static void stop_engine(X86Ctx *ctx) { FAudio_StopEngine(engine(ctx)->audio); }
static void commit(X86Ctx *ctx) { RET(FAudio_CommitOperationSet(engine(ctx)->audio, ARG(0))); }
static void get_perf(X86Ctx *ctx)
{
    FAudioPerformanceData perf;
    FAudio_GetPerformanceData(engine(ctx)->audio, &perf);
    memcpy(PTR(0), &perf, 64);
}
static void debug_config(X86Ctx *ctx) { FAudio_SetDebugConfiguration(engine(ctx)->audio, PTR(0), NULL); }

#define V (voice(ctx)->voice)
static void voice_details(X86Ctx *ctx)
{
    FAudioVoiceDetails data;
    FAudioVoice_GetVoiceDetails(V, &data);
    st32(ARG(0), data.CreationFlags);
    st32(ARG(0) + 4, data.InputChannels);
    st32(ARG(0) + 8, data.InputSampleRate);
}
static void set_sends(X86Ctx *ctx)
{
    Routes r;
    RET(read_sends(&r, ARG(0)) ? FAudioVoice_SetOutputVoices(V, ARG(0) ? &r.sends : NULL) : FAUDIO_E_INVALID_ARG);
}
static void set_effects(X86Ctx *ctx)
{
    Routes r;
    RET(read_effects(&r, ARG(0)) ? FAudioVoice_SetEffectChain(V, ARG(0) ? &r.effects : NULL) : FAUDIO_E_INVALID_ARG);
}
static void enable_effect(X86Ctx *ctx) { RET(FAudioVoice_EnableEffect(V, ARG(0), ARG(1))); }
static void disable_effect(X86Ctx *ctx) { RET(FAudioVoice_DisableEffect(V, ARG(0), ARG(1))); }
static void get_effect(X86Ctx *ctx) { FAudioVoice_GetEffectState(V, ARG(0), PTR(1)); }
static void set_effect_parameters(X86Ctx *ctx) { RET(FAudioVoice_SetEffectParameters(V, ARG(0), PTR(1), ARG(2), ARG(3))); }
static void get_effect_parameters(X86Ctx *ctx) { RET(FAudioVoice_GetEffectParameters(V, ARG(0), PTR(1), ARG(2))); }
static void set_filter(X86Ctx *ctx) { RET(FAudioVoice_SetFilterParameters(V, PTR(0), ARG(1))); }
static void get_filter(X86Ctx *ctx) { FAudioVoice_GetFilterParameters(V, PTR(0)); }
static void set_output_filter(X86Ctx *ctx) { RET(FAudioVoice_SetOutputFilterParameters(V, voice_at(ARG(0)), PTR(1), ARG(2))); }
static void get_output_filter(X86Ctx *ctx) { FAudioVoice_GetOutputFilterParameters(V, voice_at(ARG(0)), PTR(1)); }
static void set_volume(X86Ctx *ctx) { RET(FAudioVoice_SetVolume(V, as_float(ARG(0)), ARG(1))); }
static void get_volume(X86Ctx *ctx) { FAudioVoice_GetVolume(V, PTR(0)); }
static void set_channels(X86Ctx *ctx) { RET(FAudioVoice_SetChannelVolumes(V, ARG(0), PTR(1), ARG(2))); }
static void get_channels(X86Ctx *ctx) { FAudioVoice_GetChannelVolumes(V, ARG(0), PTR(1)); }
static void set_matrix(X86Ctx *ctx) { RET(FAudioVoice_SetOutputMatrix(V, voice_at(ARG(0)), ARG(1), ARG(2), PTR(3), ARG(4))); }
static void get_matrix(X86Ctx *ctx) { FAudioVoice_GetOutputMatrix(V, voice_at(ARG(0)), ARG(1), ARG(2), PTR(3)); }
static void destroy_voice(X86Ctx *ctx)
{
    Voice *v = voice(ctx);
    uint32_t result = FAudioVoice_DestroyVoiceSafeEXT(v->voice);
    if (result) { rt_log("Android XAudio2.7 DestroyVoice HRESULT=%#x", result); return; }
    Engine *e = v->owner;
    pthread_mutex_lock(&e->lock);
    for (Voice **item = &e->voices; *item; item = &(*item)->next) {
        if (*item == v) { *item = v->next; break; }
    }
    pthread_mutex_unlock(&e->lock);
    rt_heap_free(v->guest);
    free(v);
}
static void start_voice(X86Ctx *ctx) { RET(FAudioSourceVoice_Start(V, ARG(0), ARG(1))); }
static void stop_voice(X86Ctx *ctx) { RET(FAudioSourceVoice_Stop(V, ARG(0), ARG(1))); }
static void submit(X86Ctx *ctx)
{
    uint32_t p = ARG(0), w = ARG(1);
    if (!p) { RET(FAUDIO_E_INVALID_ARG); return; }
    uint32_t data = ld32(p + 8);
    FAudioBuffer buffer = {ld32(p), ld32(p + 4), data ? rt_ptr(data) : NULL,
        ld32(p + 12), ld32(p + 16), ld32(p + 20), ld32(p + 24), ld32(p + 28),
        (void *)(uintptr_t)ld32(p + 32)};
    FAudioBufferWMA wma;
    if (w) wma = (FAudioBufferWMA){rt_ptr(ld32(w)), ld32(w + 4)};
    RET(FAudioSourceVoice_SubmitSourceBuffer(V, &buffer, w ? &wma : NULL));
}
static void flush(X86Ctx *ctx) { RET(FAudioSourceVoice_FlushSourceBuffers(V)); }
static void discontinuity(X86Ctx *ctx) { RET(FAudioSourceVoice_Discontinuity(V)); }
static void exit_loop(X86Ctx *ctx) { RET(FAudioSourceVoice_ExitLoop(V, ARG(0))); }
static void get_state(X86Ctx *ctx)
{
    FAudioVoiceState state;
    FAudioSourceVoice_GetState(V, &state, 0);
    st32(ARG(0), (uintptr_t)state.pCurrentBufferContext);
    st32(ARG(0) + 4, state.BuffersQueued);
    st64(ARG(0) + 8, state.SamplesPlayed);
}
static void set_ratio(X86Ctx *ctx) { RET(FAudioSourceVoice_SetFrequencyRatio(V, as_float(ARG(0)), ARG(1))); }
static void get_ratio(X86Ctx *ctx) { FAudioSourceVoice_GetFrequencyRatio(V, PTR(0)); }
static void set_rate(X86Ctx *ctx) { RET(FAudioSourceVoice_SetSourceSampleRate(V, ARG(0))); }
#undef V

#define M(fn,n) {"oa-xaudio27", #fn, fn, (n) + 1}
static const HleDef engine_slots[] = {
    M(query,2), M(addref,0), M(release,0), M(get_count,1), M(get_details,2), M(initialize,2),
    M(register_callback,1), M(unregister_callback,1), M(source,7), M(submix,7), M(master,6),
    M(start_engine,0), M(stop_engine,0), M(commit,1), M(get_perf,1), M(debug_config,2),
};
static const HleDef voice_slots[] = {
    M(voice_details,1), M(set_sends,1), M(set_effects,1), M(enable_effect,2), M(disable_effect,2),
    M(get_effect,2), M(set_effect_parameters,4), M(get_effect_parameters,3), M(set_filter,2), M(get_filter,1),
    M(set_output_filter,3), M(get_output_filter,2), M(set_volume,2), M(get_volume,1),
    M(set_channels,3), M(get_channels,2), M(set_matrix,5), M(get_matrix,4), M(destroy_voice,0),
    M(start_voice,2), M(stop_voice,2), M(submit,2), M(flush,0), M(discontinuity,0), M(exit_loop,1),
    M(get_state,1), M(set_ratio,2), M(get_ratio,1), M(set_rate,1),
};
#undef M

static void init_table(unsigned type, const HleDef *slots, unsigned count)
{
    uint32_t table = rt_heap_alloc(4 * count, 1);
    for (unsigned i = 0; i < count; ++i) st32(table + 4 * i, rt_hle_thunk(slots[i].dll, slots[i].name));
    vtables[type] = table;
}

static uint32_t make_object(unsigned type, void *native)
{
    pthread_mutex_lock(&table_lock);
    if (!vtables[ENGINE]) {
        init_table(ENGINE, engine_slots, ARRAYSIZE(engine_slots));
        init_table(VOICE, voice_slots, ARRAYSIZE(voice_slots));
        init_table(EFFECT, engine_slots, 3);
    }
    uint32_t vtable = vtables[type];
    pthread_mutex_unlock(&table_lock);
    uint32_t object = rt_heap_alloc(16, 1);
    st32(object, vtable);
    st64(object + 8, (uintptr_t)native);
    return object;
}

int oa_audio_co_create(X86Ctx *ctx)
{
    uint32_t cls = rt_arg(ctx, 0), iid = rt_arg(ctx, 3), out = rt_arg(ctx, 4);
    int audio = guid_is(cls, &engine_class);
    if (!audio && !guid_is(cls, &effect_class)) return 0;
    if (!out) { RET(0x80004003u); return 1; }
    st32(out, 0);
    if (rt_arg(ctx, 1)) { RET(0x80040110u); return 1; }
    if (!guid_is(iid, &unknown_iid) && !(audio && guid_is(iid, &engine_iid))) {
        RET(0x80004002u);
        return 1;
    }
    if (audio) {
        Engine *e = calloc(1, sizeof *e);
        if (!e) { RET(FAUDIO_E_OUT_OF_MEMORY); return 1; }
        uint32_t result = FAudioCOMConstructEXT(&e->audio, 7);
        if (result) free(e);
        else {
            pthread_mutex_init(&e->lock, NULL);
            st32(out, make_object(ENGINE, e));
        }
        rt_log("Android XAudio2.7 COM creation HRESULT=%#x", result);
        RET(result);
    } else {
        FAPO *effect = NULL;
        uint32_t result = FAudioCreateReverb(&effect, 0);
        if (!result) st32(out, make_object(EFFECT, effect));
        RET(result);
    }
    return 1;
}

void oa_audio_register(void)
{
    rt_hle_register(engine_slots, ARRAYSIZE(engine_slots));
    rt_hle_register(voice_slots, ARRAYSIZE(voice_slots));
}
