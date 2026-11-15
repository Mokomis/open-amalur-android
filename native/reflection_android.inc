/* Native compiler-43 reflection. Keep the legacy v43 IID and signature layout.
 * Child reflection interfaces do not derive from IUnknown. */
#undef THIS
#undef THIS_
#define THIS INTERFACE *This
#define THIS_ INTERFACE *This,
#include <d3d11shader.h>
#define ID3D11ShaderReflectionConstantBuffer_GetDesc(p,...) ((p)->lpVtbl->GetDesc((p), ##__VA_ARGS__))
#define ID3D11ShaderReflectionConstantBuffer_GetVariableByIndex(p,...) ((p)->lpVtbl->GetVariableByIndex((p), ##__VA_ARGS__))
#define ID3D11ShaderReflectionConstantBuffer_GetVariableByName(p,...) ((p)->lpVtbl->GetVariableByName((p), ##__VA_ARGS__))
#define ID3D11ShaderReflectionType_GetDesc(p,...) ((p)->lpVtbl->GetDesc((p), ##__VA_ARGS__))
#define ID3D11ShaderReflectionType_GetMemberTypeByIndex(p,...) ((p)->lpVtbl->GetMemberTypeByIndex((p), ##__VA_ARGS__))
#define ID3D11ShaderReflectionType_GetMemberTypeByName(p,...) ((p)->lpVtbl->GetMemberTypeByName((p), ##__VA_ARGS__))
#define ID3D11ShaderReflectionType_GetMemberTypeName(p,...) ((p)->lpVtbl->GetMemberTypeName((p), ##__VA_ARGS__))
#define ID3D11ShaderReflectionVariable_GetBuffer(p,...) ((p)->lpVtbl->GetBuffer((p), ##__VA_ARGS__))
#define ID3D11ShaderReflectionVariable_GetDesc(p,...) ((p)->lpVtbl->GetDesc((p), ##__VA_ARGS__))
#define ID3D11ShaderReflectionVariable_GetType(p,...) ((p)->lpVtbl->GetType((p), ##__VA_ARGS__))
#define ID3D11ShaderReflection_GetConstantBufferByIndex(p,...) ((p)->lpVtbl->GetConstantBufferByIndex((p), ##__VA_ARGS__))
#define ID3D11ShaderReflection_GetConstantBufferByName(p,...) ((p)->lpVtbl->GetConstantBufferByName((p), ##__VA_ARGS__))
#define ID3D11ShaderReflection_GetDesc(p,...) ((p)->lpVtbl->GetDesc((p), ##__VA_ARGS__))
#define ID3D11ShaderReflection_GetResourceBindingDesc(p,...) ((p)->lpVtbl->GetResourceBindingDesc((p), ##__VA_ARGS__))
#define ID3D11ShaderReflection_GetResourceBindingDescByName(p,...) ((p)->lpVtbl->GetResourceBindingDescByName((p), ##__VA_ARGS__))
#define ID3D11ShaderReflection_GetVariableByName(p,...) ((p)->lpVtbl->GetVariableByName((p), ##__VA_ARGS__))
#define ID3D11ShaderReflection_GetInputParameterDesc(p,...) ((p)->lpVtbl->GetInputParameterDesc((p), ##__VA_ARGS__))
#define ID3D11ShaderReflection_GetOutputParameterDesc(p,...) ((p)->lpVtbl->GetOutputParameterDesc((p), ##__VA_ARGS__))
#define ID3D11ShaderReflection_GetPatchConstantParameterDesc(p,...) ((p)->lpVtbl->GetPatchConstantParameterDesc((p), ##__VA_ARGS__))

static const GUID oa_reflect43={0x0a233719,0x3960,0x4578,{0x9d,0x7c,0x20,0x3b,0x8b,0x1d,0x9c,0xc1}};
static uint32_t copy_bytes(const void*p,size_t n){if(!p)return 0;uint32_t g=rt_heap_alloc(n?n:1,0);memcpy(rt_ptr(g),p,n);return g;}
static uint32_t copy_name(const char*p){return p?copy_bytes(p,strlen(p)+1):0;}
static void reflect_desc(X86Ctx*ctx){D3D11_SHADER_DESC d={0};HRESULT hr=ID3D11ShaderReflection_GetDesc((ID3D11ShaderReflection*)self(ctx)->host,&d);if(SUCCEEDED(hr)){uint32_t g=A(0);st32(g,d.Version);st32(g+4,copy_name(d.Creator));memcpy(rt_ptr(g+8),&d.Flags,144);}RET(hr);}
static void reflect_cb_index(X86Ctx*ctx){RET(wrap_dependent(ID3D11ShaderReflection_GetConstantBufferByIndex((ID3D11ShaderReflection*)self(ctx)->host,A(0)),CBREF,self(ctx)));}
static void reflect_cb_name(X86Ctx*ctx){RET(wrap_dependent(ID3D11ShaderReflection_GetConstantBufferByName((ID3D11ShaderReflection*)self(ctx)->host,rt_str(A(0))),CBREF,self(ctx)));}
static void binding_write(uint32_t g,const D3D11_SHADER_INPUT_BIND_DESC*d){st32(g,copy_name(d->Name));memcpy(rt_ptr(g+4),&d->Type,28);}
static void reflect_binding(X86Ctx*ctx){D3D11_SHADER_INPUT_BIND_DESC d={0};HRESULT hr=ID3D11ShaderReflection_GetResourceBindingDesc((ID3D11ShaderReflection*)self(ctx)->host,A(0),&d);if(SUCCEEDED(hr))binding_write(A(1),&d);RET(hr);}
static void reflect_binding_name(X86Ctx*ctx){D3D11_SHADER_INPUT_BIND_DESC d={0};HRESULT hr=ID3D11ShaderReflection_GetResourceBindingDescByName((ID3D11ShaderReflection*)self(ctx)->host,rt_str(A(0)),&d);if(SUCCEEDED(hr))binding_write(A(1),&d);RET(hr);}
static void signature_write(uint32_t g,const D3D11_SIGNATURE_PARAMETER_DESC*d){/* v43 ends at Stream, 28 guest bytes; never write modern MinPrecision. */st32(g,copy_name(d->SemanticName));memcpy(rt_ptr(g+4),&d->SemanticIndex,24);}
#define SIGNATURE(F,M) static void F(X86Ctx*ctx){D3D11_SIGNATURE_PARAMETER_DESC d={0};HRESULT hr=ID3D11ShaderReflection_##M((ID3D11ShaderReflection*)self(ctx)->host,A(0),&d);if(SUCCEEDED(hr))signature_write(A(1),&d);RET(hr);}
SIGNATURE(reflect_input,GetInputParameterDesc)
SIGNATURE(reflect_output,GetOutputParameterDesc)
SIGNATURE(reflect_patch,GetPatchConstantParameterDesc)
static void reflect_var_name(X86Ctx*ctx){RET(wrap_dependent(ID3D11ShaderReflection_GetVariableByName((ID3D11ShaderReflection*)self(ctx)->host,rt_str(A(0))),VARREF,self(ctx)));}
static void cb_desc(X86Ctx*ctx){D3D11_SHADER_BUFFER_DESC d={0};HRESULT hr=ID3D11ShaderReflectionConstantBuffer_GetDesc((ID3D11ShaderReflectionConstantBuffer*)self(ctx)->host,&d);if(SUCCEEDED(hr)){st32(A(0),copy_name(d.Name));memcpy(rt_ptr(A(0)+4),&d.Type,16);}RET(hr);}
static void cb_var_index(X86Ctx*ctx){RET(wrap_dependent(ID3D11ShaderReflectionConstantBuffer_GetVariableByIndex((ID3D11ShaderReflectionConstantBuffer*)self(ctx)->host,A(0)),VARREF,self(ctx)));}
static void cb_var_name(X86Ctx*ctx){RET(wrap_dependent(ID3D11ShaderReflectionConstantBuffer_GetVariableByName((ID3D11ShaderReflectionConstantBuffer*)self(ctx)->host,rt_str(A(0))),VARREF,self(ctx)));}
static void var_desc(X86Ctx*ctx){D3D11_SHADER_VARIABLE_DESC d={0};HRESULT hr=ID3D11ShaderReflectionVariable_GetDesc((ID3D11ShaderReflectionVariable*)self(ctx)->host,&d);if(SUCCEEDED(hr)){uint32_t g=A(0);st32(g,copy_name(d.Name));memcpy(rt_ptr(g+4),&d.StartOffset,12);st32(g+16,copy_bytes(d.DefaultValue,d.Size));memcpy(rt_ptr(g+20),&d.StartTexture,16);}RET(hr);}
static void var_type(X86Ctx*ctx){RET(wrap_dependent(ID3D11ShaderReflectionVariable_GetType((ID3D11ShaderReflectionVariable*)self(ctx)->host),TYPEREF,self(ctx)));}
static void var_buffer(X86Ctx*ctx){RET(wrap_dependent(ID3D11ShaderReflectionVariable_GetBuffer((ID3D11ShaderReflectionVariable*)self(ctx)->host),CBREF,self(ctx)));}
static void type_desc(X86Ctx*ctx){D3D11_SHADER_TYPE_DESC d={0};HRESULT hr=ID3D11ShaderReflectionType_GetDesc((ID3D11ShaderReflectionType*)self(ctx)->host,&d);if(SUCCEEDED(hr)){memcpy(rt_ptr(A(0)),&d.Class,28);st32(A(0)+28,copy_name(d.Name));}RET(hr);}
static void type_index(X86Ctx*ctx){RET(wrap_dependent(ID3D11ShaderReflectionType_GetMemberTypeByIndex((ID3D11ShaderReflectionType*)self(ctx)->host,A(0)),TYPEREF,self(ctx)));}
static void type_name(X86Ctx*ctx){RET(wrap_dependent(ID3D11ShaderReflectionType_GetMemberTypeByName((ID3D11ShaderReflectionType*)self(ctx)->host,rt_str(A(0))),TYPEREF,self(ctx)));}
static void type_member_name(X86Ctx*ctx){RET(copy_name(ID3D11ShaderReflectionType_GetMemberTypeName((ID3D11ShaderReflectionType*)self(ctx)->host,A(0))));}
/* D3DReflect is supplied by the separately compiled original compiler DLL.
 * This host function is deliberately not registered as a successful substitute. */
static void compiler_reflect(X86Ctx*ctx){(void)ctx;rt_fatal("host reflection not used: original compiled compiler43 required");}
extern uint32_t oa_reflect_export;
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
 uint32_t iid=copy_bytes(&oa_reflect43,16),out=rt_heap_alloc(4,1),args[]={blob,(uint32_t)bytes,iid,out};uint32_t hr=rt_call(ctx,oa_reflect_export,4,args);if((int32_t)hr<0)return 11;
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
