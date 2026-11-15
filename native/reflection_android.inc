/* Legacy D3DReflect ABI for the original game. Reflection reads the metadata
 * already present in compiled Shader Model 4 and 5 DXBC containers. */
#undef THIS
#undef THIS_
#define THIS INTERFACE *This
#define THIS_ INTERFACE *This,
#include <d3d11shader.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <strings.h>
#include "oa_dxbc.h"

static const GUID oa_reflect43 = {0x0a233719,0x3960,0x4578,{0x9d,0x7c,0x20,0x3b,0x8b,0x1d,0x9c,0xc1}};

typedef struct {
    const uint8_t *data;
    uint32_t bytes, count, offset, stride;
} ReflectionTable;

typedef struct Reflection Reflection;
typedef struct ReflectionView ReflectionView;
struct ReflectionView {
    ReflectionView *next;
    Reflection *shader;
    const uint8_t *record, *buffer;
    uint32_t offset;
    int kind;
};

struct Reflection {
    IUnknown identity;
    _Atomic uint32_t refs;
    ReflectionView *views;
    uint32_t blob;
    ReflectionTable resources, buffers, input, output, patch;
    const uint8_t *rdef;
    uint32_t rdef_size, variable_stride, creator, version, flags;
    uint32_t stats[37];
};

static uint32_t word(const void *bytes, uint32_t index)
{
    uint32_t value;
    memcpy(&value, (const uint8_t *)bytes + index * 4, 4);
    return value;
}

static const uint8_t *record_at(ReflectionTable table, uint32_t index)
{
    return index < table.count ? table.data + table.offset + index * table.stride : NULL;
}

static bool table_init(ReflectionTable *table, const uint8_t *data, uint32_t bytes,
                       uint32_t count, uint32_t offset, uint32_t stride)
{
    if (offset > bytes || count > (bytes - offset) / stride) return false;
    *table = (ReflectionTable){data, bytes, count, offset, stride};
    return true;
}

static const char *bounded_name(const uint8_t *data, uint32_t bytes, uint32_t offset)
{
    return offset < bytes && memchr(data + offset, 0, bytes - offset) ?
            (const char *)data + offset : NULL;
}

static uint32_t guest_name(Reflection *shader, const uint8_t *data, uint32_t bytes, uint32_t offset)
{
    const char *name = bounded_name(data, bytes, offset);
    return name && *name ? shader->blob + (uint32_t)(data - (uint8_t *)rt_ptr(shader->blob)) + offset : 0;
}

static const uint8_t *record_named(ReflectionTable table, uint32_t name)
{
    if (!name) return NULL;
    for (uint32_t i = 0; i < table.count; i++) {
        const uint8_t *record = record_at(table, i);
        const char *candidate = bounded_name(table.data, table.bytes, word(record, 0));
        if (candidate && !strcmp(candidate, rt_str(name))) return record;
    }
    return NULL;
}

static ReflectionTable variables(Reflection *shader, const uint8_t *buffer)
{
    ReflectionTable table = {0};
    if (buffer) table_init(&table, shader->rdef, shader->rdef_size,
                          word(buffer, 1), word(buffer, 2), shader->variable_stride);
    return table;
}

static ReflectionTable members(ReflectionView *view)
{
    ReflectionTable table = {0};
    if (view->record) table_init(&table, view->shader->rdef, view->shader->rdef_size,
                                word(view->record, 2) >> 16, word(view->record, 3), 12);
    return table;
}

static uint32_t child(Object *parent, Reflection *shader, int kind, const uint8_t *record,
                      const uint8_t *buffer, uint32_t offset)
{
    for (ReflectionView *view = shader->views; view; view = view->next) {
        if (view->kind == kind && view->record == record && view->buffer == buffer &&
                view->offset == offset) return wrap_dependent(view, kind, parent);
    }
    ReflectionView *view = malloc(sizeof(*view));
    if (!view) rt_fatal("could not allocate shader reflection view");
    *view = (ReflectionView){shader->views, shader, record, buffer, offset, kind};
    shader->views = view;
    return wrap_dependent(view, kind, parent);
}

static ULONG STDMETHODCALLTYPE reflection_addref(IUnknown *object)
{
    return ++((Reflection *)object)->refs;
}

static ULONG STDMETHODCALLTYPE reflection_release(IUnknown *object)
{
    Reflection *shader = (Reflection *)object;
    uint32_t remaining = --shader->refs;
    if (!remaining) {
        for (ReflectionView *view = shader->views, *next; view; view = next) {
            next = view->next;
            free(view);
        }
        rt_heap_free(shader->blob);
        free(shader);
    }
    return remaining;
}

static HRESULT STDMETHODCALLTYPE reflection_query(IUnknown *object, REFIID iid, void **out)
{
    *out = NULL;
    if (!IsEqualGUID(iid, &oa_reflect43) && !IsEqualGUID(iid, &IID_IUnknown)) return E_NOINTERFACE;
    reflection_addref(object);
    *out = object;
    return S_OK;
}

static IUnknownVtbl reflection_identity = {reflection_query, reflection_addref, reflection_release};

static bool read_signature(ReflectionTable *table, const uint8_t *blob, uint32_t size,
                           const char tag[4], uint32_t stride)
{
    uint32_t bytes;
    const uint8_t *data = oa_dxbc_find_chunk(blob, size, tag, &bytes);
    if (!data) return true;
    return bytes >= 8 && table_init(table, data, bytes, word(data, 0), 8, stride);
}

static bool read_reflection(Reflection *shader, const uint8_t *blob, uint32_t size)
{
    uint32_t bytes;
    const uint8_t *code = oa_dxbc_find_chunk(blob, size, "SHDR", &bytes);
    if (!code) code = oa_dxbc_find_chunk(blob, size, "SHEX", &bytes);
    if (!code || bytes < 8) return false;
    shader->version = word(code, 0);

    const uint8_t *rdef = oa_dxbc_find_chunk(blob, size, "RDEF", &bytes);
    if (rdef) {
        if (bytes < 28) return false;
        uint32_t target = word(rdef, 4) & 0xffff;
        shader->rdef = rdef;
        shader->rdef_size = bytes;
        shader->variable_stride = target >= 0x500 ? 40 : 24;
        shader->flags = word(rdef, 5);
        shader->creator = guest_name(shader, rdef, bytes, word(rdef, 6));
        if (!table_init(&shader->buffers, rdef, bytes, word(rdef, 0), word(rdef, 1), 24) ||
            !table_init(&shader->resources, rdef, bytes, word(rdef, 2), word(rdef, 3),
                         target >= 0x501 ? 40 : 32)) return false;
    }
    const uint8_t *stats = oa_dxbc_find_chunk(blob, size, "STAT", &bytes);
    if (stats) memcpy(shader->stats, stats, bytes < sizeof(shader->stats) ? bytes : sizeof(shader->stats));

    return read_signature(&shader->input, blob, size, "ISGN", 24) &&
           read_signature(&shader->output, blob, size, "OSGN", 24) &&
           (!shader->output.data ? read_signature(&shader->output, blob, size, "OSG5", 28) : true) &&
           read_signature(&shader->patch, blob, size, "PCSG", 24);
}

static void compiler_reflect(X86Ctx *ctx)
{
    uint32_t source = rt_arg(ctx, 0), bytes = rt_arg(ctx, 1);
    uint32_t iid = rt_arg(ctx, 2), output = rt_arg(ctx, 3);
    if (!output) { RET(E_POINTER); return; }
    st32(output, 0);
    if (!source || bytes < 32 || bytes > 16 * 1024 * 1024 ||
        !iid || !IsEqualGUID(rt_ptr(iid), &oa_reflect43)) { RET(E_INVALIDARG); return; }

    Reflection *shader = calloc(1, sizeof(*shader));
    if (!shader) { RET(E_OUTOFMEMORY); return; }
    shader->identity.lpVtbl = &reflection_identity;
    shader->refs = 1;
    shader->blob = rt_heap_alloc(bytes, 0);
    memcpy(rt_ptr(shader->blob), rt_ptr(source), bytes);
    if (!read_reflection(shader, rt_ptr(shader->blob), bytes)) {
        reflection_release(&shader->identity);
        RET(E_INVALIDARG);
        return;
    }
    st32(output, wrap(shader, SREF));
    RET(S_OK);
}

static void reflect_desc(X86Ctx *ctx)
{
    Reflection *r = self(ctx)->host;
    const uint32_t *s = r->stats;
    /* The 32-bit ABI has no padding and all fields after Creator are u32. */
    const uint32_t desc[] = {
        r->version, r->creator, r->flags, r->buffers.count, r->resources.count,
        r->input.count, r->output.count,
        s[0], s[1], s[10], s[2], s[3], s[14], s[15], s[16], s[17], s[18],
        s[4], s[5], s[6], s[7], s[8], s[9], s[11], s[12], s[13],
        s[24], s[25], s[23], r->patch.count, s[26], s[30], s[31], s[32], s[33],
        s[34], s[35], s[36],
    };
    if (!A(0)) { RET(E_FAIL); return; }
    memcpy(rt_ptr(A(0)), desc, sizeof(desc));
    RET(S_OK);
}

static void reflect_cb_index(X86Ctx *ctx)
{
    Object *p = self(ctx);
    Reflection *r = p->host;
    RET(child(p, r, CBREF, record_at(r->buffers, A(0)), NULL, 0));
}

static void reflect_cb_name(X86Ctx *ctx)
{
    Object *p = self(ctx);
    Reflection *r = p->host;
    RET(child(p, r, CBREF, record_named(r->buffers, A(0)), NULL, 0));
}

static void reflection_binding(X86Ctx *ctx, Reflection *r, const uint8_t *record)
{
    if (!record || !A(1)) { RET(E_INVALIDARG); return; }
    const uint32_t desc[] = {guest_name(r, r->rdef, r->rdef_size, word(record, 0)),
                            word(record, 1), word(record, 5), word(record, 6), word(record, 7),
                            word(record, 2), word(record, 3), word(record, 4)};
    memcpy(rt_ptr(A(1)), desc, sizeof(desc));
    RET(S_OK);
}

static void reflect_binding(X86Ctx *ctx)
{
    Reflection *r = self(ctx)->host;
    reflection_binding(ctx, r, record_at(r->resources, A(0)));
}

static void reflect_binding_name(X86Ctx *ctx)
{
    Reflection *r = self(ctx)->host;
    reflection_binding(ctx, r, record_named(r->resources, A(0)));
}

static uint32_t signature_sysval(const char *name, uint32_t fallback)
{
    if (!name) return fallback;
    static const struct { const char *name; D3D_NAME value; } values[] = {
        {"SV_Target", D3D_NAME_TARGET}, {"SV_Depth", D3D_NAME_DEPTH},
        {"SV_Coverage", D3D_NAME_COVERAGE}, {"SV_DepthGreaterEqual", D3D_NAME_DEPTH_GREATER_EQUAL},
        {"SV_DepthLessEqual", D3D_NAME_DEPTH_LESS_EQUAL},
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        if (!strcasecmp(values[i].name, name)) return values[i].value;
    return fallback;
}

static void reflect_signature(X86Ctx *ctx, Reflection *r, ReflectionTable table)
{
    const uint8_t *record = record_at(table, A(0));
    if (!record || !A(1)) { RET(E_INVALIDARG); return; }
    uint32_t stream = table.stride == 28 ? word(record, 0) : 0;
    if (table.stride == 28) record += 4;
    uint32_t name_offset = word(record, 0);
    uint32_t sysval = signature_sysval(bounded_name(table.data, table.bytes, name_offset), word(record, 2));
    const uint32_t desc[] = {guest_name(r, table.data, table.bytes, name_offset),
                            word(record, 1), word(record, 4), sysval, word(record, 3),
                            word(record, 5) & 0xffff, stream};
    memcpy(rt_ptr(A(1)), desc, sizeof(desc));
    RET(S_OK);
}

static void reflect_input(X86Ctx *ctx) { Reflection *r = self(ctx)->host; reflect_signature(ctx, r, r->input); }
static void reflect_output(X86Ctx *ctx) { Reflection *r = self(ctx)->host; reflect_signature(ctx, r, r->output); }
static void reflect_patch(X86Ctx *ctx) { Reflection *r = self(ctx)->host; reflect_signature(ctx, r, r->patch); }

static void reflect_var_name(X86Ctx *ctx)
{
    Object *p = self(ctx);
    Reflection *r = p->host;
    for (uint32_t i = 0; i < r->buffers.count; i++) {
        const uint8_t *buffer = record_at(r->buffers, i);
        const uint8_t *record = record_named(variables(r, buffer), A(0));
        if (record) { RET(child(p, r, VARREF, record, buffer, 0)); return; }
    }
    RET(child(p, r, VARREF, NULL, NULL, 0));
}

static void cb_desc(X86Ctx *ctx)
{
    ReflectionView *v = self(ctx)->host;
    const uint8_t *r = v->record;
    if (!r || !A(0)) { RET(E_FAIL); return; }
    const uint32_t desc[] = {guest_name(v->shader, v->shader->rdef, v->shader->rdef_size, word(r, 0)),
                            word(r, 5), word(r, 1), word(r, 3), word(r, 4)};
    memcpy(rt_ptr(A(0)), desc, sizeof(desc));
    RET(S_OK);
}

static void cb_var_index(X86Ctx *ctx)
{
    Object *p = self(ctx);
    ReflectionView *v = p->host;
    const uint8_t *record = record_at(variables(v->shader, v->record), A(0));
    RET(child(p, v->shader, VARREF, record, v->record, 0));
}

static void cb_var_name(X86Ctx *ctx)
{
    Object *p = self(ctx);
    ReflectionView *v = p->host;
    const uint8_t *record = record_named(variables(v->shader, v->record), A(0));
    RET(child(p, v->shader, VARREF, record, v->record, 0));
}

static void var_desc(X86Ctx *ctx)
{
    ReflectionView *v = self(ctx)->host;
    Reflection *s = v->shader;
    const uint8_t *r = v->record;
    if (!r || !A(0)) { RET(E_FAIL); return; }
    uint32_t bytes = word(r, 2), initial = word(r, 5), data = 0;
    if (initial && initial <= s->rdef_size && bytes <= s->rdef_size - initial)
        data = s->blob + (uint32_t)(s->rdef - (uint8_t *)rt_ptr(s->blob)) + initial;
    bool sm5 = s->variable_stride == 40;
    const uint32_t desc[] = {guest_name(s, s->rdef, s->rdef_size, word(r, 0)),
                            word(r, 1), bytes, word(r, 3), data,
                            sm5 ? word(r, 6) : UINT32_MAX, sm5 ? word(r, 7) : 0,
                            sm5 ? word(r, 8) : UINT32_MAX, sm5 ? word(r, 9) : 0};
    memcpy(rt_ptr(A(0)), desc, sizeof(desc));
    RET(S_OK);
}

static const uint8_t *type_record(Reflection *s, uint32_t offset)
{
    uint32_t size = s->variable_stride == 40 ? 36 : 16;
    return offset <= s->rdef_size && size <= s->rdef_size - offset ? s->rdef + offset : NULL;
}

static void var_type(X86Ctx *ctx)
{
    Object *p = self(ctx);
    ReflectionView *v = p->host;
    const uint8_t *record = v->record ? type_record(v->shader, word(v->record, 4)) : NULL;
    RET(child(p, v->shader, TYPEREF, record, NULL, 0));
}

static void var_buffer(X86Ctx *ctx)
{
    Object *p = self(ctx);
    ReflectionView *v = p->host;
    RET(child(p, v->shader, CBREF, v->buffer, NULL, 0));
}

static void type_desc(X86Ctx *ctx)
{
    ReflectionView *v = self(ctx)->host;
    Reflection *s = v->shader;
    const uint8_t *r = v->record;
    if (!r || !A(0)) { RET(E_FAIL); return; }
    const uint32_t desc[] = {word(r, 0) & 0xffff, word(r, 0) >> 16,
                            word(r, 1) & 0xffff, word(r, 1) >> 16,
                            word(r, 2) & 0xffff, word(r, 2) >> 16, v->offset,
                            s->variable_stride == 40 ?
                                guest_name(s, s->rdef, s->rdef_size, word(r, 8)) : 0};
    memcpy(rt_ptr(A(0)), desc, sizeof(desc));
    RET(S_OK);
}

static uint32_t reflected_member(Object *p, Reflection *shader, const uint8_t *member)
{
    return child(p, shader, TYPEREF, member ? type_record(shader, word(member, 1)) : NULL,
                 NULL, member ? word(member, 2) : 0);
}

static void type_index(X86Ctx *ctx)
{
    Object *p = self(ctx);
    ReflectionView *v = p->host;
    RET(reflected_member(p, v->shader, record_at(members(v), A(0))));
}

static void type_name(X86Ctx *ctx)
{
    Object *p = self(ctx);
    ReflectionView *v = p->host;
    RET(reflected_member(p, v->shader, record_named(members(v), A(0))));
}

static void type_member_name(X86Ctx *ctx)
{
    ReflectionView *v = self(ctx)->host;
    ReflectionTable table = members(v);
    const uint8_t *member = record_at(table, A(0));
    RET(member ? guest_name(v->shader, table.data, table.bytes, word(member, 0)) : 0);
}

/* Canonicalize the guest-marshaled results for comparison with the actual x86
 * compiler oracle. Canary bytes catch accidental modern/64-bit tail writes. */
static FILE*reflection_proof;
static void proof_word(uint32_t x){fwrite(&x,4,1,reflection_proof);}
static void proof_name(uint32_t p){const char*s=p?rt_str(p):NULL;uint32_t n=s?(uint32_t)strlen(s):0xffffffff;proof_word(n);if(s)fwrite(s,1,n,reflection_proof);}
static void proof_words(uint32_t g,unsigned n){fwrite(rt_ptr(g),4,n,reflection_proof);}
static uint32_t proof_method(X86Ctx*ctx,uint32_t obj,unsigned slot,unsigned n,const uint32_t*params){uint32_t args[8];if(n>7)rt_fatal("proof argument bound");args[0]=obj;for(unsigned i=0;i<n;i++)args[i+1]=params[i];return rt_call(ctx,ld32(ld32(obj)+4*slot),n+1,args);}
static void proof_canary(uint32_t g,unsigned n){for(unsigned i=n;i<n+32;i++)if(ld8(g+i)!=0xa5)rt_fatal("reflection descriptor overwrote guest ABI boundary %u",n);}
#define PROOF_METHOD(O,I,M,N,...) proof_method(ctx,O,SLOT(I,M),N,(uint32_t[]){__VA_ARGS__})
static uint32_t proof_desc(X86Ctx*ctx,uint32_t obj,unsigned slot,unsigned size){uint32_t g=rt_heap_alloc(size+32,0);memset(rt_ptr(g),0xa5,size+32);uint32_t hr=proof_method(ctx,obj,slot,1,&g);proof_word(hr);if((int32_t)hr<0)rt_fatal("reflection proof GetDesc failed");proof_canary(g,size);return g;}
int oa_reflection_check(X86Ctx*ctx,const char*input){
 FILE*f=fopen(input,"rb");if(!f)return 10;fseek(f,0,SEEK_END);long bytes=ftell(f);rewind(f);if(bytes<1||bytes>16777216)rt_fatal("shader proof input outside bound");uint32_t blob=rt_heap_alloc(bytes,0);if(fread(rt_ptr(blob),1,bytes,f)!=(size_t)bytes)rt_fatal("shader input truncated");fclose(f);
 uint32_t iid=rt_heap_alloc(sizeof(oa_reflect43),0);memcpy(rt_ptr(iid),&oa_reflect43,sizeof(oa_reflect43));uint32_t out=rt_heap_alloc(4,1),args[]={blob,(uint32_t)bytes,iid,out};uint32_t hr=rt_call(ctx,rt_hle_thunk("d3dcompiler_43.dll","D3DReflect"),4,args);if((int32_t)hr<0)return 11;
 reflection_proof=fopen("reflection-bridge.bin","wb");if(!reflection_proof)return 12;uint32_t r=ld32(out),d=proof_desc(ctx,r,SLOT(ID3D11ShaderReflection,GetDesc),152);proof_word(ld32(d));proof_name(ld32(d+4));proof_words(d+8,36);
 unsigned buffers=ld32(d+12),resources=ld32(d+16),inputs=ld32(d+20),outputs=ld32(d+24);
 for(unsigned i=0;i<resources;i++){uint32_t g=rt_heap_alloc(64,0);memset(rt_ptr(g),0xa5,64);hr=PROOF_METHOD(r,ID3D11ShaderReflection,GetResourceBindingDesc,2,i,g);proof_word(hr);if((int32_t)hr<0)return 13;proof_canary(g,32);proof_name(ld32(g));proof_words(g+4,7);}
 for(unsigned mode=0;mode<2;mode++)for(unsigned i=0;i<(mode?outputs:inputs);i++){uint32_t g=rt_heap_alloc(60,0);memset(rt_ptr(g),0xa5,60);uint32_t p[]={i,g};hr=proof_method(ctx,r,mode?SLOT(ID3D11ShaderReflection,GetOutputParameterDesc):SLOT(ID3D11ShaderReflection,GetInputParameterDesc),2,p);proof_word(hr);if((int32_t)hr<0)return 14;proof_canary(g,28);proof_name(ld32(g));proof_words(g+4,4);proof_word(ld8(g+20));proof_word(ld8(g+21));proof_word(ld32(g+24));}
 for(unsigned i=0;i<buffers;i++){
  uint32_t cb=PROOF_METHOD(r,ID3D11ShaderReflection,GetConstantBufferByIndex,1,i);uint32_t b=proof_desc(ctx,cb,SLOT(ID3D11ShaderReflectionConstantBuffer,GetDesc),20);proof_name(ld32(b));proof_words(b+4,4);unsigned vars=ld32(b+8);
  for(unsigned j=0;j<vars;j++){uint32_t v=PROOF_METHOD(cb,ID3D11ShaderReflectionConstantBuffer,GetVariableByIndex,1,j);uint32_t x=proof_desc(ctx,v,SLOT(ID3D11ShaderReflectionVariable,GetDesc),36);proof_name(ld32(x));proof_words(x+4,3);uint32_t def=ld32(x+16),size=ld32(x+8);proof_word(def?size:0);if(def)fwrite(rt_ptr(def),1,size,reflection_proof);proof_words(x+20,4);uint32_t t=PROOF_METHOD(v,ID3D11ShaderReflectionVariable,GetType,0,0);uint32_t td=proof_desc(ctx,t,SLOT(ID3D11ShaderReflectionType,GetDesc),32);proof_words(td,7);proof_name(ld32(td+28));}
 }
 fclose(reflection_proof);PROOF_METHOD(r,IUnknown,Release,0,0);rt_log("reflection bridge proof complete: %u resources, %u buffers; canaries intact",resources,buffers);return 0;
}
