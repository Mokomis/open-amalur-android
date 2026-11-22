/* Real Windows D3D11/DXGI bridge for the isolated original-engine experiment.
 * Every unsupported COM method stops. The returned objects are real D3D11
 * objects, represented by guest32 handles; no rendering success is fabricated.
 */
#define COBJMACROS
#include "oa_android_win.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#ifdef _WIN32
#include <dinput.h>
#include <netlistmgr.h>
#endif

#define DIRECTINPUT_VERSION 0x0800


#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"
#define A(i) rt_arg(ctx,(i)+1)
#define RET(v) (ctx->eax=(uint32_t)(v))
#define SLOT(I,M) (offsetof(I##Vtbl,M)/sizeof(void*))
enum Kind{DEV,GFX_CONTEXT,DXDEV,DXDEV1,ADAPTER,ADAPTER1,FACTORY,FACTORY1,OUTPUT,SWAPCHAIN,TEX2D,BUFFER,RTV,DSV,SRV,VS,PS,GS,HS,DS,CS,BLEND,DEPTHSTATE,RASTER,SAMPLER,LAYOUT,SREF,CBREF,VARREF,TYPEREF,NETMGR,DINPUTW,DINPUTA,DIDEVICEW,DIDEVICEA,XAUDIO27,AMASTER,ASOURCE,ASUBMIX,QUERY,TEX1D,TEX3D,CMDLIST,AUDIOFX,NKINDS};
static const char *kind_names[]={"ID3D11Device","ID3D11DeviceContext","IDXGIDevice","IDXGIDevice1","IDXGIAdapter","IDXGIAdapter1","IDXGIFactory","IDXGIFactory1","IDXGIOutput","IDXGISwapChain","ID3D11Texture2D","ID3D11Buffer","ID3D11RenderTargetView","ID3D11DepthStencilView","ID3D11ShaderResourceView","ID3D11VertexShader","ID3D11PixelShader","ID3D11GeometryShader","ID3D11HullShader","ID3D11DomainShader","ID3D11ComputeShader","ID3D11BlendState","ID3D11DepthStencilState","ID3D11RasterizerState","ID3D11SamplerState","ID3D11InputLayout","ID3D11ShaderReflection43","ReflectionConstantBuffer","ReflectionVariable","ReflectionType","INetworkListManager","IDirectInput8W","IDirectInput8A","IDirectInputDevice8W","IDirectInputDevice8A","IXAudio2.7","XAudioMasterVoice","XAudioSourceVoice","XAudioSubmixVoice","ID3D11Query","ID3D11Texture1D","ID3D11Texture3D","ID3D11CommandList","XAudio27EffectIUnknown"};
typedef struct {uint32_t guest;void *host;int kind,alive;uint32_t root,first_child,next_child;} Object;
#define OBJECT_LIMIT 65536u
#define HASH_LIMIT 262144u
static Object objects[OBJECT_LIMIT];static unsigned nobjects;
static uint32_t object_hash[HASH_LIMIT],free_objects[OBJECT_LIMIT];static unsigned nfree;
static SRWLOCK object_lock=SRWLOCK_INIT;
static uint32_t object_key(void*h,int k){uintptr_t v=(uintptr_t)h;return (uint32_t)(((v>>4)^(v>>32)^((uintptr_t)k*0x9e3779b1u))&(HASH_LIMIT-1));}
static void forget_object(unsigned i){Object*p=&objects[i];if(!p->alive)return;uint32_t h=object_key(p->host,p->kind);for(unsigned n=0;n<HASH_LIMIT;n++,h=(h+1)&(HASH_LIMIT-1)){if(!object_hash[h])break;if(object_hash[h]==i+1){object_hash[h]=UINT32_MAX;break;}}p->alive=0;if(nfree==OBJECT_LIMIT)rt_fatal("object free list corrupt");free_objects[nfree++]=i;}

typedef struct {uint32_t vtable;HleDef methods[128];char names[128][80];} Table;
static Table tables[NKINDS];
extern void *rt_user32_host_hwnd(uint32_t);
uint32_t rt_self_last_target(void);const char *rt_thunk_name(uint32_t);
static Object *object(uint32_t g){
    if(!g)rt_fatal("NULL graphics object");
    uint32_t i=ld32(g+4);
    if(i>=nobjects || objects[i].guest!=g || !objects[i].alive)rt_fatal("unmapped/dead graphics object %#x",g);
    return &objects[i];
}
static Object *self(X86Ctx *ctx){return object(rt_arg(ctx,0));}
static void *host_object(uint32_t g){return g?object(g)->host:NULL;}
static void unsupported(X86Ctx *ctx){Object *p=self(ctx);rt_fatal("real graphics bridge lacks %s on %s; no native method called",rt_thunk_name(rt_self_last_target()),kind_names[p->kind]);}
static int iid_kind(const GUID *id,int fallback){
#ifdef _WIN32
    if(IsEqualGUID(id,&IID_INetworkListManager))return NETMGR;
    if(IsEqualGUID(id,&IID_IDirectInput8W))return DINPUTW;
    if(IsEqualGUID(id,&IID_IDirectInput8A))return DINPUTA;
    if(IsEqualGUID(id,&IID_IDirectInputDevice8W))return DIDEVICEW;
    if(IsEqualGUID(id,&IID_IDirectInputDevice8A))return DIDEVICEA;
#endif
    if(IsEqualGUID(id,&IID_ID3D11CommandList))return CMDLIST;
    if(IsEqualGUID(id,&IID_ID3D11Texture1D))return TEX1D;
    if(IsEqualGUID(id,&IID_ID3D11Texture3D))return TEX3D;
    if(IsEqualGUID(id,&IID_ID3D11Predicate))return QUERY;
    if(IsEqualGUID(id,&IID_ID3D11Query))return QUERY;
    if(IsEqualGUID(id,&IID_ID3D11Asynchronous))return fallback;
    if(IsEqualGUID(id,&IID_IUnknown))return fallback;
    if(IsEqualGUID(id,&IID_ID3D11Device))return DEV;
    if(IsEqualGUID(id,&IID_ID3D11DeviceContext))return GFX_CONTEXT;
    if(IsEqualGUID(id,&IID_IDXGIDevice))return DXDEV;
    if(IsEqualGUID(id,&IID_IDXGIDevice1))return DXDEV1;
    if(IsEqualGUID(id,&IID_IDXGIAdapter))return ADAPTER;
    if(IsEqualGUID(id,&IID_IDXGIAdapter1))return ADAPTER1;
    if(IsEqualGUID(id,&IID_IDXGIFactory))return FACTORY;
    if(IsEqualGUID(id,&IID_IDXGIFactory1))return FACTORY1;
    if(IsEqualGUID(id,&IID_IDXGIOutput))return OUTPUT;
    if(IsEqualGUID(id,&IID_IDXGISwapChain))return SWAPCHAIN;
    if(IsEqualGUID(id,&IID_ID3D11Texture2D))return TEX2D;
    if(IsEqualGUID(id,&IID_ID3D11Buffer))return BUFFER;
    if(IsEqualGUID(id,&IID_ID3D11RenderTargetView))return RTV;
    if(IsEqualGUID(id,&IID_ID3D11DepthStencilView))return DSV;
    if(IsEqualGUID(id,&IID_ID3D11ShaderResourceView))return SRV;
#define QTYPE(T,K) if(IsEqualGUID(id,&IID_##T))return K
    QTYPE(ID3D11VertexShader,VS);QTYPE(ID3D11PixelShader,PS);QTYPE(ID3D11GeometryShader,GS);
    QTYPE(ID3D11HullShader,HS);QTYPE(ID3D11DomainShader,DS);QTYPE(ID3D11ComputeShader,CS);
    QTYPE(ID3D11BlendState,BLEND);QTYPE(ID3D11DepthStencilState,DEPTHSTATE);QTYPE(ID3D11RasterizerState,RASTER);
    QTYPE(ID3D11SamplerState,SAMPLER);QTYPE(ID3D11InputLayout,LAYOUT);
    if(IsEqualGUID(id,&IID_ID3D11Resource) || IsEqualGUID(id,&IID_ID3D11DeviceChild))return fallback;
    return -1;
}
static uint32_t wrap(void*,int);
static uint32_t wrap_dependent(void*,int,Object*);
static const GUID oa_reflect43;
static void query(X86Ctx *ctx){
    Object *p=self(ctx);const GUID *id=(const GUID*)rt_ptr(A(0));void *out=NULL;
    HRESULT hr=IUnknown_QueryInterface((IUnknown*)p->host,id,&out);
    if(SUCCEEDED(hr)){
        int kind=p->kind==SREF && IsEqualGUID(id,&oa_reflect43) ? SREF : iid_kind(id,p->kind);
        if(kind<0){IUnknown_Release((IUnknown*)out);rt_fatal("unqualified graphics IID %08lx-%04x-%04x",id->Data1,id->Data2,id->Data3);}
        st32(A(1),wrap(out,kind));rt_log("real graphics QI -> %s",kind_names[kind]);
    }else if(A(1))st32(A(1),0);
    RET(hr);
}
static void addref(X86Ctx *ctx){RET(IUnknown_AddRef((IUnknown*)self(ctx)->host));}
static void release(X86Ctx *ctx){Object*p=self(ctx);ULONG n=IUnknown_Release((IUnknown*)p->host);if(!n){AcquireSRWLockExclusive(&object_lock);if(p->kind==SREF){uint32_t child=p->first_child;while(child){Object*c=&objects[child-1];uint32_t next=c->next_child;forget_object(child-1);child=next;}}forget_object((unsigned)(p-objects));ReleaseSRWLockExclusive(&object_lock);}RET(n);}
static void parent(X86Ctx *ctx){
    Object *p=self(ctx);const GUID *id=(const GUID*)rt_ptr(A(0));void *out=NULL;
    HRESULT hr=IDXGIObject_GetParent((IDXGIObject*)p->host,id,&out);
    if(SUCCEEDED(hr)){
        int kind=iid_kind(id,-1);
        if(kind<0){IUnknown_Release((IUnknown*)out);rt_fatal("unqualified graphics parent IID %08lx",id->Data1);}
        st32(A(1),wrap(out,kind));rt_log("real graphics parent -> %s",kind_names[kind]);
    }else st32(A(1),0);RET(hr);
}
static void get_adapter(X86Ctx *ctx){IDXGIAdapter *out=NULL;HRESULT hr=IDXGIDevice_GetAdapter((IDXGIDevice*)self(ctx)->host,&out);st32(A(0),SUCCEEDED(hr)?wrap(out,ADAPTER):0);RET(hr);}
static void output_modes(X86Ctx*ctx){RET(IDXGIOutput_GetDisplayModeList((IDXGIOutput*)self(ctx)->host,A(0),A(1),(UINT*)rt_ptr(A(2)),A(3)?(DXGI_MODE_DESC*)rt_ptr(A(3)):NULL));}
static void output_closest(X86Ctx*ctx){RET(IDXGIOutput_FindClosestMatchingMode((IDXGIOutput*)self(ctx)->host,(const DXGI_MODE_DESC*)rt_ptr(A(0)),(DXGI_MODE_DESC*)rt_ptr(A(1)),(IUnknown*)host_object(A(2))));}
static void output_vblank(X86Ctx*ctx){RET(IDXGIOutput_WaitForVBlank((IDXGIOutput*)self(ctx)->host));}
static void enum_outputs(X86Ctx *ctx){IDXGIOutput *out=NULL;HRESULT hr=IDXGIAdapter_EnumOutputs((IDXGIAdapter*)self(ctx)->host,A(0),&out);st32(A(1),SUCCEEDED(hr)?wrap(out,OUTPUT):0);RET(hr);}
static void enum_adapters(X86Ctx *ctx){IDXGIAdapter *out=NULL;HRESULT hr=IDXGIFactory_EnumAdapters((IDXGIFactory*)self(ctx)->host,A(0),&out);st32(A(1),SUCCEEDED(hr)?wrap(out,ADAPTER):0);RET(hr);}
static void enum_adapters1(X86Ctx *ctx){IDXGIAdapter1 *out=NULL;HRESULT hr=IDXGIFactory1_EnumAdapters1((IDXGIFactory1*)self(ctx)->host,A(0),&out);st32(A(1),SUCCEEDED(hr)?wrap(out,ADAPTER1):0);RET(hr);}
static void adapter_desc(X86Ctx*ctx){DXGI_ADAPTER_DESC d;HRESULT hr=IDXGIAdapter_GetDesc((IDXGIAdapter*)self(ctx)->host,&d);if(SUCCEEDED(hr)){uint32_t g=A(0);memset(rt_ptr(g),0,292);for(unsigned i=0;i<128;i++)st16(g+2*i,(uint16_t)d.Description[i]);st32(g+256,d.VendorId);st32(g+260,d.DeviceId);st32(g+264,d.SubSysId);st32(g+268,d.Revision);st32(g+272,d.DedicatedVideoMemory>UINT32_MAX?UINT32_MAX:d.DedicatedVideoMemory);st32(g+276,d.DedicatedSystemMemory>UINT32_MAX?UINT32_MAX:d.DedicatedSystemMemory);st32(g+280,d.SharedSystemMemory>UINT32_MAX?UINT32_MAX:d.SharedSystemMemory);memcpy(rt_ptr(g+284),&d.AdapterLuid,8);rt_log("native Android adapter vendor=%#x device=%#x local_memory=%llu",d.VendorId,d.DeviceId,(unsigned long long)d.DedicatedVideoMemory);}RET(hr);}
static void feature_level(X86Ctx *ctx){RET(ID3D11Device_GetFeatureLevel((ID3D11Device*)self(ctx)->host));}
static void creation_flags(X86Ctx *ctx){RET(ID3D11Device_GetCreationFlags((ID3D11Device*)self(ctx)->host));}
static void format_support(X86Ctx *ctx){RET(ID3D11Device_CheckFormatSupport((ID3D11Device*)self(ctx)->host,A(0),(UINT*)rt_ptr(A(1))));}
static void sample_support(X86Ctx *ctx){RET(ID3D11Device_CheckMultisampleQualityLevels((ID3D11Device*)self(ctx)->host,A(0),A(1),(UINT*)rt_ptr(A(2))));}
static void immediate(X86Ctx *ctx){ID3D11DeviceContext *out=NULL;ID3D11Device_GetImmediateContext((ID3D11Device*)self(ctx)->host,&out);st32(A(0),wrap(out,GFX_CONTEXT));}
static void make_association(X86Ctx *ctx){HWND h=(HWND)rt_user32_host_hwnd(A(0));if(A(0)&&!h)rt_fatal("unknown guest HWND for DXGI association");RET(IDXGIFactory_MakeWindowAssociation((IDXGIFactory*)self(ctx)->host,h,A(1)));}
static void swap_desc_to_guest(uint32_t g,const DXGI_SWAP_CHAIN_DESC *d){
    memcpy(rt_ptr(g),&d->BufferDesc,28);memcpy(rt_ptr(g+28),&d->SampleDesc,8);
    st32(g+36,d->BufferUsage);st32(g+40,d->BufferCount);
    extern uint32_t rt_user32_guest_hwnd(void*);st32(g+44,rt_user32_guest_hwnd(d->OutputWindow));
    st32(g+48,d->Windowed);st32(g+52,d->SwapEffect);st32(g+56,d->Flags);
}
static void create_swap(X86Ctx *ctx){
    uint32_t g=A(1);DXGI_SWAP_CHAIN_DESC d={0};
    memcpy(&d.BufferDesc,rt_ptr(g),28);memcpy(&d.SampleDesc,rt_ptr(g+28),8);
    d.BufferUsage=ld32(g+36);d.BufferCount=ld32(g+40);d.OutputWindow=(HWND)rt_user32_host_hwnd(ld32(g+44));
    d.Windowed=ld32(g+48);d.SwapEffect=ld32(g+52);d.Flags=ld32(g+56);
    if(!d.OutputWindow)rt_fatal("swap chain has an unmapped HWND");
    rt_log("real swap request %ux%u format=%u buffers=%u windowed=%u",d.BufferDesc.Width,d.BufferDesc.Height,d.BufferDesc.Format,d.BufferCount,d.Windowed);
    if(!d.Windowed)rt_fatal("windowed-only probe requires original DisplayFullscreen=0 private INI setting");
    IDXGISwapChain *out=NULL;HRESULT hr=IDXGIFactory_CreateSwapChain((IDXGIFactory*)self(ctx)->host,(IUnknown*)host_object(A(0)),&d,&out);
    st32(A(2),SUCCEEDED(hr)?wrap(out,SWAPCHAIN):0);rt_log("MILESTONE real swap-chain creation HRESULT=%#lx",(unsigned long)hr);RET(hr);
}
static void swap_get_buffer(X86Ctx *ctx){
    const GUID *id=(const GUID*)rt_ptr(A(1));void*out=NULL;HRESULT hr=IDXGISwapChain_GetBuffer((IDXGISwapChain*)self(ctx)->host,A(0),id,&out);
    if(SUCCEEDED(hr)){int k=iid_kind(id,-1);if(k<0){IUnknown_Release((IUnknown*)out);rt_fatal("unqualified back-buffer IID");}st32(A(2),wrap(out,k));}else st32(A(2),0);RET(hr);
}
static void swap_get_desc(X86Ctx *ctx){DXGI_SWAP_CHAIN_DESC d;HRESULT hr=IDXGISwapChain_GetDesc((IDXGISwapChain*)self(ctx)->host,&d);if(SUCCEEDED(hr))swap_desc_to_guest(A(0),&d);RET(hr);}
static void swap_resize(X86Ctx *ctx){RET(IDXGISwapChain_ResizeBuffers((IDXGISwapChain*)self(ctx)->host,A(0),A(1),A(2),A(3),A(4)));}
/* Wine include/xaudio2fx.idl, XAUDIO2_VER=7; matches original PE at 01767EF4. */
static void swap_fullscreen(X86Ctx*ctx){BOOL b=FALSE;IDXGIOutput*out=NULL;HRESULT hr=IDXGISwapChain_GetFullscreenState((IDXGISwapChain*)self(ctx)->host,A(0)?&b:NULL,A(1)?&out:NULL);if(SUCCEEDED(hr)){if(A(0))st32(A(0),b);if(A(1))st32(A(1),wrap(out,OUTPUT));}RET(hr);}
static void swap_output(X86Ctx*ctx){IDXGIOutput*out=NULL;HRESULT hr=IDXGISwapChain_GetContainingOutput((IDXGISwapChain*)self(ctx)->host,&out);st32(A(0),SUCCEEDED(hr)?wrap(out,OUTPUT):0);RET(hr);}
static void swap_set_fullscreen(X86Ctx*ctx){if(A(0))rt_fatal("fullscreen mode outside windowed diagnostic scope");RET(IDXGISwapChain_SetFullscreenState((IDXGISwapChain*)self(ctx)->host,FALSE,(IDXGIOutput*)host_object(A(1))));}
static unsigned long long oa_draws;
static void swap_present(X86Ctx *ctx){

    HRESULT hr=IDXGISwapChain_Present((IDXGISwapChain*)self(ctx)->host,A(0),A(1));
    static unsigned n;if(++n<=4)rt_log("MILESTONE original renderer Present #%u HRESULT=%#lx",n,(unsigned long)hr);RET(hr);
}
static D3D11_SUBRESOURCE_DATA one_data(uint32_t p){D3D11_SUBRESOURCE_DATA d={0};if(p){d.pSysMem=ld32(p)?rt_ptr(ld32(p)):NULL;d.SysMemPitch=ld32(p+4);d.SysMemSlicePitch=ld32(p+8);}return d;}
static void create_buffer(X86Ctx *ctx){ID3D11Buffer*out=NULL;D3D11_SUBRESOURCE_DATA data=one_data(A(1));HRESULT hr=ID3D11Device_CreateBuffer((ID3D11Device*)self(ctx)->host,(const D3D11_BUFFER_DESC*)rt_ptr(A(0)),A(1)?&data:NULL,&out);st32(A(2),SUCCEEDED(hr)?wrap(out,BUFFER):0);RET(hr);}
static void create_texture(X86Ctx *ctx){
    const D3D11_TEXTURE2D_DESC *d=(const D3D11_TEXTURE2D_DESC*)rt_ptr(A(0));D3D11_SUBRESOURCE_DATA*data=NULL;
    if(A(1)){
        uint32_t m=d->MipLevels;if(!m){uint32_t n=d->Width>d->Height?d->Width:d->Height;m=1;while(n>1){n>>=1;m++;}}
        uint32_t count=m*d->ArraySize;if(!count || count>65536)rt_fatal("texture subresource count outside probe bound");
        data=calloc(count,sizeof(*data));if(!data)rt_fatal("texture descriptors allocation failed");
        for(uint32_t i=0;i<count;i++)data[i]=one_data(A(1)+12*i);
    }
    ID3D11Texture2D*out=NULL;HRESULT hr=ID3D11Device_CreateTexture2D((ID3D11Device*)self(ctx)->host,d,data,&out);free(data);st32(A(2),SUCCEEDED(hr)?wrap(out,TEX2D):0);RET(hr);
}
static void create_texture1d(X86Ctx*ctx){const D3D11_TEXTURE1D_DESC*d=(const D3D11_TEXTURE1D_DESC*)rt_ptr(A(0));D3D11_SUBRESOURCE_DATA*data=NULL;if(A(1)){UINT m=d->MipLevels;if(!m){UINT n=d->Width;m=1;while(n>1){n>>=1;m++;}}UINT count=m*d->ArraySize;if(!count||count>65536)rt_fatal("texture1D subresource bound");data=calloc(count,sizeof(*data));if(!data)rt_fatal("texture data allocation");for(UINT i=0;i<count;i++)data[i]=one_data(A(1)+12*i);}ID3D11Texture1D*out=NULL;HRESULT hr=ID3D11Device_CreateTexture1D((ID3D11Device*)self(ctx)->host,d,data,A(2)?&out:NULL);free(data);if(A(2))st32(A(2),SUCCEEDED(hr)?wrap(out,TEX1D):0);RET(hr);}
static void create_texture3d(X86Ctx*ctx){const D3D11_TEXTURE3D_DESC*d=(const D3D11_TEXTURE3D_DESC*)rt_ptr(A(0));D3D11_SUBRESOURCE_DATA*data=NULL;if(A(1)){UINT count=d->MipLevels;if(!count){UINT n=d->Width;if(n<d->Height)n=d->Height;if(n<d->Depth)n=d->Depth;count=1;while(n>1){n>>=1;count++;}}if(!count||count>32)rt_fatal("texture3D subresource bound");data=calloc(count,sizeof(*data));if(!data)rt_fatal("texture data allocation");for(UINT i=0;i<count;i++)data[i]=one_data(A(1)+12*i);}ID3D11Texture3D*out=NULL;HRESULT hr=ID3D11Device_CreateTexture3D((ID3D11Device*)self(ctx)->host,d,data,A(2)?&out:NULL);free(data);if(A(2))st32(A(2),SUCCEEDED(hr)?wrap(out,TEX3D):0);RET(hr);}
static void texture1d_desc(X86Ctx*ctx){ID3D11Texture1D_GetDesc((ID3D11Texture1D*)self(ctx)->host,(D3D11_TEXTURE1D_DESC*)rt_ptr(A(0)));}
static void texture3d_desc(X86Ctx*ctx){ID3D11Texture3D_GetDesc((ID3D11Texture3D*)self(ctx)->host,(D3D11_TEXTURE3D_DESC*)rt_ptr(A(0)));}
static void create_rtv(X86Ctx *ctx){ID3D11RenderTargetView*out=NULL;HRESULT hr=ID3D11Device_CreateRenderTargetView((ID3D11Device*)self(ctx)->host,(ID3D11Resource*)host_object(A(0)),A(1)?(const D3D11_RENDER_TARGET_VIEW_DESC*)rt_ptr(A(1)):NULL,&out);st32(A(2),SUCCEEDED(hr)?wrap(out,RTV):0);RET(hr);}
static void create_dsv(X86Ctx *ctx){ID3D11DepthStencilView*out=NULL;HRESULT hr=ID3D11Device_CreateDepthStencilView((ID3D11Device*)self(ctx)->host,(ID3D11Resource*)host_object(A(0)),A(1)?(const D3D11_DEPTH_STENCIL_VIEW_DESC*)rt_ptr(A(1)):NULL,&out);st32(A(2),SUCCEEDED(hr)?wrap(out,DSV):0);RET(hr);}
static void create_srv(X86Ctx *ctx){ID3D11ShaderResourceView*out=NULL;HRESULT hr=ID3D11Device_CreateShaderResourceView((ID3D11Device*)self(ctx)->host,(ID3D11Resource*)host_object(A(0)),A(1)?(const D3D11_SHADER_RESOURCE_VIEW_DESC*)rt_ptr(A(1)):NULL,&out);st32(A(2),SUCCEEDED(hr)?wrap(out,SRV):0);RET(hr);}
static void child_device(X86Ctx *ctx){ID3D11Device*out=NULL;ID3D11DeviceChild_GetDevice((ID3D11DeviceChild*)self(ctx)->host,&out);st32(A(0),wrap(out,DEV));}
static void resource_type(X86Ctx *ctx){ID3D11Resource_GetType((ID3D11Resource*)self(ctx)->host,(D3D11_RESOURCE_DIMENSION*)rt_ptr(A(0)));}
static void texture_desc(X86Ctx *ctx){ID3D11Texture2D_GetDesc((ID3D11Texture2D*)self(ctx)->host,(D3D11_TEXTURE2D_DESC*)rt_ptr(A(0)));}
static void buffer_desc(X86Ctx *ctx){ID3D11Buffer_GetDesc((ID3D11Buffer*)self(ctx)->host,(D3D11_BUFFER_DESC*)rt_ptr(A(0)));}
static void clear_rt(X86Ctx *ctx){ID3D11DeviceContext_ClearRenderTargetView((ID3D11DeviceContext*)self(ctx)->host,(ID3D11RenderTargetView*)host_object(A(0)),(const FLOAT*)rt_ptr(A(1)));}
static void clear_ds(X86Ctx *ctx){uint32_t bits=A(2);float depth;memcpy(&depth,&bits,4);ID3D11DeviceContext_ClearDepthStencilView((ID3D11DeviceContext*)self(ctx)->host,(ID3D11DepthStencilView*)host_object(A(0)),A(1),depth,(UINT8)A(3));}
static void set_targets(X86Ctx *ctx){uint32_t n=A(0);if(n>8)rt_fatal("too many render targets");ID3D11RenderTargetView*views[8];for(uint32_t i=0;i<n;i++)views[i]=(ID3D11RenderTargetView*)host_object(A(1)?ld32(A(1)+4*i):0);ID3D11DeviceContext_OMSetRenderTargets((ID3D11DeviceContext*)self(ctx)->host,n,views,(ID3D11DepthStencilView*)host_object(A(2)));}
static void set_private(X86Ctx *ctx){Object*p=self(ctx);const GUID*g=(const GUID*)rt_ptr(A(0));const void*d=A(2)?rt_ptr(A(2)):NULL;RET(p->kind==DEV?ID3D11Device_SetPrivateData((ID3D11Device*)p->host,g,A(1),d):ID3D11DeviceChild_SetPrivateData((ID3D11DeviceChild*)p->host,g,A(1),d));}
static void get_private(X86Ctx *ctx){Object*p=self(ctx);const GUID*g=(const GUID*)rt_ptr(A(0));void*d=A(2)?rt_ptr(A(2)):NULL;RET(p->kind==DEV?ID3D11Device_GetPrivateData((ID3D11Device*)p->host,g,(UINT*)rt_ptr(A(1)),d):ID3D11DeviceChild_GetPrivateData((ID3D11DeviceChild*)p->host,g,(UINT*)rt_ptr(A(1)),d));}
static void set_private_interface(X86Ctx *ctx){Object*p=self(ctx);const GUID*g=(const GUID*)rt_ptr(A(0));IUnknown*d=(IUnknown*)host_object(A(1));RET(p->kind==DEV?ID3D11Device_SetPrivateDataInterface((ID3D11Device*)p->host,g,d):ID3D11DeviceChild_SetPrivateDataInterface((ID3D11DeviceChild*)p->host,g,d));}
#define MAKE_SHADER(F,M,T,K) static void F(X86Ctx*ctx){T*out=NULL;HRESULT hr=ID3D11Device_##M((ID3D11Device*)self(ctx)->host,rt_ptr(A(0)),A(1),(ID3D11ClassLinkage*)host_object(A(2)),A(3)?&out:NULL);if(A(3))st32(A(3),SUCCEEDED(hr)?wrap(out,K):0);RET(hr);}
MAKE_SHADER(create_vs,CreateVertexShader,ID3D11VertexShader,VS)
MAKE_SHADER(create_ps,CreatePixelShader,ID3D11PixelShader,PS)
MAKE_SHADER(create_gs,CreateGeometryShader,ID3D11GeometryShader,GS)
MAKE_SHADER(create_hs,CreateHullShader,ID3D11HullShader,HS)
MAKE_SHADER(create_ds,CreateDomainShader,ID3D11DomainShader,DS)
MAKE_SHADER(create_cs,CreateComputeShader,ID3D11ComputeShader,CS)
#define MAKE_STATE(F,M,T,D,K) static void F(X86Ctx*ctx){T*out=NULL;HRESULT hr=ID3D11Device_##M((ID3D11Device*)self(ctx)->host,(const D*)rt_ptr(A(0)),A(1)?&out:NULL);if(A(1))st32(A(1),SUCCEEDED(hr)?wrap(out,K):0);RET(hr);}
MAKE_STATE(create_blend,CreateBlendState,ID3D11BlendState,D3D11_BLEND_DESC,BLEND)
MAKE_STATE(create_depthstate,CreateDepthStencilState,ID3D11DepthStencilState,D3D11_DEPTH_STENCIL_DESC,DEPTHSTATE)
MAKE_STATE(create_raster,CreateRasterizerState,ID3D11RasterizerState,D3D11_RASTERIZER_DESC,RASTER)
MAKE_STATE(create_sampler,CreateSamplerState,ID3D11SamplerState,D3D11_SAMPLER_DESC,SAMPLER)
static void create_query(X86Ctx*ctx){ID3D11Query*out=NULL;HRESULT hr=ID3D11Device_CreateQuery((ID3D11Device*)self(ctx)->host,(const D3D11_QUERY_DESC*)rt_ptr(A(0)),A(1)?&out:NULL);if(A(1))st32(A(1),SUCCEEDED(hr)?wrap(out,QUERY):0);RET(hr);}
static void create_predicate(X86Ctx*ctx){ID3D11Predicate*out=NULL;HRESULT hr=ID3D11Device_CreatePredicate((ID3D11Device*)self(ctx)->host,(const D3D11_QUERY_DESC*)rt_ptr(A(0)),A(1)?&out:NULL);if(A(1))st32(A(1),SUCCEEDED(hr)?wrap(out,QUERY):0);RET(hr);}
static void predicate_set(X86Ctx*ctx){ID3D11DeviceContext_SetPredication((ID3D11DeviceContext*)self(ctx)->host,(ID3D11Predicate*)host_object(A(0)),A(1));}
static void deferred_create(X86Ctx*ctx){ID3D11DeviceContext*out=NULL;HRESULT hr=ID3D11Device_CreateDeferredContext((ID3D11Device*)self(ctx)->host,A(0),A(1)?&out:NULL);if(A(1))st32(A(1),SUCCEEDED(hr)?wrap(out,GFX_CONTEXT):0);rt_log("real deferred graphics context creation HRESULT=%#lx",(unsigned long)hr);RET(hr);}
static void command_finish(X86Ctx*ctx){ID3D11CommandList*out=NULL;HRESULT hr=ID3D11DeviceContext_FinishCommandList((ID3D11DeviceContext*)self(ctx)->host,A(0),A(1)?&out:NULL);if(A(1))st32(A(1),SUCCEEDED(hr)?wrap(out,CMDLIST):0);RET(hr);}
static void command_execute(X86Ctx*ctx){ID3D11DeviceContext_ExecuteCommandList((ID3D11DeviceContext*)self(ctx)->host,(ID3D11CommandList*)host_object(A(0)),A(1));}
static void context_type(X86Ctx*ctx){RET(ID3D11DeviceContext_GetType((ID3D11DeviceContext*)self(ctx)->host));}
static void context_flags(X86Ctx*ctx){RET(ID3D11DeviceContext_GetContextFlags((ID3D11DeviceContext*)self(ctx)->host));}
static void command_flags(X86Ctx*ctx){RET(ID3D11CommandList_GetContextFlags((ID3D11CommandList*)self(ctx)->host));}
static void query_size(X86Ctx*ctx){RET(ID3D11Asynchronous_GetDataSize((ID3D11Asynchronous*)self(ctx)->host));}
static void query_desc(X86Ctx*ctx){ID3D11Query_GetDesc((ID3D11Query*)self(ctx)->host,(D3D11_QUERY_DESC*)rt_ptr(A(0)));}
static void query_begin(X86Ctx*ctx){ID3D11DeviceContext_Begin((ID3D11DeviceContext*)self(ctx)->host,(ID3D11Asynchronous*)host_object(A(0)));}
static void query_end(X86Ctx*ctx){ID3D11DeviceContext_End((ID3D11DeviceContext*)self(ctx)->host,(ID3D11Asynchronous*)host_object(A(0)));}
static void query_data(X86Ctx*ctx){RET(ID3D11DeviceContext_GetData((ID3D11DeviceContext*)self(ctx)->host,(ID3D11Asynchronous*)host_object(A(0)),A(1)?rt_ptr(A(1)):NULL,A(2),A(3)));}
static void create_layout(X86Ctx*ctx){
    UINT n=A(1);if(n>64)rt_fatal("input layout element count exceeds probe bound");D3D11_INPUT_ELEMENT_DESC d[64];
    for(UINT i=0;i<n;i++){uint32_t g=A(0)+28*i;d[i].SemanticName=rt_str(ld32(g));d[i].SemanticIndex=ld32(g+4);d[i].Format=ld32(g+8);d[i].InputSlot=ld32(g+12);d[i].AlignedByteOffset=ld32(g+16);d[i].InputSlotClass=ld32(g+20);d[i].InstanceDataStepRate=ld32(g+24);}
    ID3D11InputLayout*out=NULL;HRESULT hr=ID3D11Device_CreateInputLayout((ID3D11Device*)self(ctx)->host,d,n,rt_ptr(A(2)),A(3),A(4)?&out:NULL);if(A(4))st32(A(4),SUCCEEDED(hr)?wrap(out,LAYOUT):0);RET(hr);
}
#include "reflection_android.inc"
#include "context_windows.inc"
#include "mapping_android_fast_discard.inc"
#include "handwritten_draw_android.inc"
#define SET(K,I,M,F,N) do{tables[K].methods[SLOT(I,M)].fn=F;tables[K].methods[SLOT(I,M)].nargs=1+(N);}while(0)
#ifdef _WIN32
#include "windows/input.inc"
#include "windows/network.inc"
#endif
#include "com_slot_names.inc"
static void initialize_table(int k){
    Table *t=&tables[k];if(t->vtable)return;
    t->vtable=rt_heap_alloc(128*4,1);
    for(unsigned i=0;i<128;i++){
        const char*method_name=oa_com_slot_name(k,i);if(method_name)snprintf(t->names[i],sizeof(t->names[i]),"%s.%s",kind_names[k],method_name);else snprintf(t->names[i],sizeof(t->names[i]),"%s.slot%u",kind_names[k],i);
        t->methods[i]=(HleDef){"oa-real-d3d11",t->names[i],unsupported,1};
    }
    if(k<CBREF || (k>=NETMGR && k<=XAUDIO27) || k==QUERY || k==TEX1D || k==TEX3D || k==CMDLIST || k==AUDIOFX){SET(k,IUnknown,QueryInterface,query,2);SET(k,IUnknown,AddRef,addref,0);SET(k,IUnknown,Release,release,0);}
    if(k>=DXDEV && k<=SWAPCHAIN){SET(k,IDXGIObject,GetParent,parent,2);}
    if(k==OUTPUT){SET(k,IDXGIOutput,GetDisplayModeList,output_modes,4);SET(k,IDXGIOutput,FindClosestMatchingMode,output_closest,3);SET(k,IDXGIOutput,WaitForVBlank,output_vblank,0);}
    if(k==DXDEV || k==DXDEV1){SET(k,IDXGIDevice,GetAdapter,get_adapter,1);}
    if(k==ADAPTER || k==ADAPTER1){SET(k,IDXGIAdapter,EnumOutputs,enum_outputs,2);SET(k,IDXGIAdapter,GetDesc,adapter_desc,1);}
    if(k==FACTORY || k==FACTORY1){SET(k,IDXGIFactory,EnumAdapters,enum_adapters,2);SET(k,IDXGIFactory,MakeWindowAssociation,make_association,2);SET(k,IDXGIFactory,CreateSwapChain,create_swap,3);}
    if(k==FACTORY1){SET(k,IDXGIFactory1,EnumAdapters1,enum_adapters1,2);}
    if(k==DEV){
        SET(k,ID3D11Device,CreateDeferredContext,deferred_create,2);SET(k,ID3D11Device,CreateQuery,create_query,2);SET(k,ID3D11Device,CreatePredicate,create_predicate,2);
        SET(k,ID3D11Device,GetFeatureLevel,feature_level,0);SET(k,ID3D11Device,GetCreationFlags,creation_flags,0);
        SET(k,ID3D11Device,CheckFormatSupport,format_support,2);SET(k,ID3D11Device,CheckMultisampleQualityLevels,sample_support,3);
        SET(k,ID3D11Device,GetImmediateContext,immediate,1);
        SET(k,ID3D11Device,SetPrivateData,set_private,3);SET(k,ID3D11Device,GetPrivateData,get_private,3);SET(k,ID3D11Device,SetPrivateDataInterface,set_private_interface,2);
        SET(k,ID3D11Device,CreateBuffer,create_buffer,3);SET(k,ID3D11Device,CreateTexture2D,create_texture,3);SET(k,ID3D11Device,CreateTexture1D,create_texture1d,3);SET(k,ID3D11Device,CreateTexture3D,create_texture3d,3);
        SET(k,ID3D11Device,CreateRenderTargetView,create_rtv,3);SET(k,ID3D11Device,CreateDepthStencilView,create_dsv,3);SET(k,ID3D11Device,CreateShaderResourceView,create_srv,3);
        SET(k,ID3D11Device,CreateVertexShader,create_vs,4);SET(k,ID3D11Device,CreatePixelShader,create_ps,4);SET(k,ID3D11Device,CreateGeometryShader,create_gs,4);
        SET(k,ID3D11Device,CreateHullShader,create_hs,4);SET(k,ID3D11Device,CreateDomainShader,create_ds,4);SET(k,ID3D11Device,CreateComputeShader,create_cs,4);
        SET(k,ID3D11Device,CreateBlendState,create_blend,2);SET(k,ID3D11Device,CreateDepthStencilState,create_depthstate,2);SET(k,ID3D11Device,CreateRasterizerState,create_raster,2);SET(k,ID3D11Device,CreateSamplerState,create_sampler,2);SET(k,ID3D11Device,CreateInputLayout,create_layout,5);
    }
    if(k==GFX_CONTEXT){
        SET(k,ID3D11DeviceContext,Map,context_map,5);SET(k,ID3D11DeviceContext,Unmap,context_unmap,2);SET(k,ID3D11DeviceContext,FinishCommandList,command_finish,2);SET(k,ID3D11DeviceContext,ExecuteCommandList,command_execute,2);SET(k,ID3D11DeviceContext,GetType,context_type,0);SET(k,ID3D11DeviceContext,GetContextFlags,context_flags,0);SET(k,ID3D11DeviceContext,SetPredication,predicate_set,2);SET(k,ID3D11DeviceContext,Begin,query_begin,1);SET(k,ID3D11DeviceContext,End,query_end,1);SET(k,ID3D11DeviceContext,GetData,query_data,4);
        SET(k,ID3D11DeviceContext,ClearState,context_clear,0);SET(k,ID3D11DeviceContext,Flush,context_flush,0);
#define STAGE_SET(S) SET(k,ID3D11DeviceContext,S##SetConstantBuffers,S##_buffers,3);SET(k,ID3D11DeviceContext,S##SetShaderResources,S##_resources,3);SET(k,ID3D11DeviceContext,S##SetSamplers,S##_samplers,3);SET(k,ID3D11DeviceContext,S##SetShader,S##_shader,3)
        STAGE_SET(VS);STAGE_SET(PS);STAGE_SET(GS);STAGE_SET(HS);STAGE_SET(DS);STAGE_SET(CS);
        SET(k,ID3D11DeviceContext,IASetInputLayout,input_layout,1);SET(k,ID3D11DeviceContext,IASetPrimitiveTopology,topology,1);SET(k,ID3D11DeviceContext,IASetVertexBuffers,vertex_buffers,5);SET(k,ID3D11DeviceContext,IASetIndexBuffer,index_buffer,3);
        SET(k,ID3D11DeviceContext,RSSetState,raster_state,1);SET(k,ID3D11DeviceContext,RSSetViewports,viewports,2);SET(k,ID3D11DeviceContext,RSSetScissorRects,scissors,2);SET(k,ID3D11DeviceContext,OMSetBlendState,blend_state,3);SET(k,ID3D11DeviceContext,OMSetDepthStencilState,depth_state,2);
        SET(k,ID3D11DeviceContext,UpdateSubresource,update_subresource,6);SET(k,ID3D11DeviceContext,CopyResource,copy_resource,2);SET(k,ID3D11DeviceContext,CopySubresourceRegion,copy_subresource_region,8);SET(k,ID3D11DeviceContext,ResolveSubresource,resolve_subresource,5);SET(k,ID3D11DeviceContext,GenerateMips,generate_mips,1);
        SET(k,ID3D11DeviceContext,Draw,draw,2);SET(k,ID3D11DeviceContext,DrawIndexed,draw_indexed,3);SET(k,ID3D11DeviceContext,DrawInstanced,draw_instanced,4);SET(k,ID3D11DeviceContext,DrawIndexedInstanced,draw_indexed_instanced,5);
        SET(k,ID3D11DeviceContext,ClearRenderTargetView,clear_rt,2);SET(k,ID3D11DeviceContext,ClearDepthStencilView,clear_ds,4);SET(k,ID3D11DeviceContext,OMSetRenderTargets,set_targets,3);}
    if(k==SWAPCHAIN){SET(k,IDXGISwapChain,GetFullscreenState,swap_fullscreen,2);SET(k,IDXGISwapChain,SetFullscreenState,swap_set_fullscreen,2);SET(k,IDXGISwapChain,GetContainingOutput,swap_output,1);SET(k,IDXGISwapChain,GetBuffer,swap_get_buffer,3);SET(k,IDXGISwapChain,GetDesc,swap_get_desc,1);SET(k,IDXGISwapChain,ResizeBuffers,swap_resize,5);SET(k,IDXGISwapChain,Present,swap_present,2);}
    if((k>=TEX2D && k<=LAYOUT) || k==GFX_CONTEXT || k==QUERY || k==TEX1D || k==TEX3D || k==CMDLIST){SET(k,ID3D11DeviceChild,GetDevice,child_device,1);SET(k,ID3D11DeviceChild,SetPrivateData,set_private,3);SET(k,ID3D11DeviceChild,GetPrivateData,get_private,3);SET(k,ID3D11DeviceChild,SetPrivateDataInterface,set_private_interface,2);}
    if(k==TEX2D || k==BUFFER || k==TEX1D || k==TEX3D){SET(k,ID3D11Resource,GetType,resource_type,1);}
    if(k==TEX1D){SET(k,ID3D11Texture1D,GetDesc,texture1d_desc,1);}
    if(k==TEX3D){SET(k,ID3D11Texture3D,GetDesc,texture3d_desc,1);}
    if(k==TEX2D){SET(k,ID3D11Texture2D,GetDesc,texture_desc,1);}
    if(k==BUFFER){SET(k,ID3D11Buffer,GetDesc,buffer_desc,1);}
    if(k==SREF){
        SET(k,ID3D11ShaderReflection,GetDesc,reflect_desc,1);SET(k,ID3D11ShaderReflection,GetConstantBufferByIndex,reflect_cb_index,1);SET(k,ID3D11ShaderReflection,GetConstantBufferByName,reflect_cb_name,1);
        SET(k,ID3D11ShaderReflection,GetResourceBindingDesc,reflect_binding,2);SET(k,ID3D11ShaderReflection,GetResourceBindingDescByName,reflect_binding_name,2);
        SET(k,ID3D11ShaderReflection,GetInputParameterDesc,reflect_input,2);SET(k,ID3D11ShaderReflection,GetOutputParameterDesc,reflect_output,2);SET(k,ID3D11ShaderReflection,GetPatchConstantParameterDesc,reflect_patch,2);SET(k,ID3D11ShaderReflection,GetVariableByName,reflect_var_name,1);
    }
    if(k==CBREF){SET(k,ID3D11ShaderReflectionConstantBuffer,GetDesc,cb_desc,1);SET(k,ID3D11ShaderReflectionConstantBuffer,GetVariableByIndex,cb_var_index,1);SET(k,ID3D11ShaderReflectionConstantBuffer,GetVariableByName,cb_var_name,1);}
    if(k==VARREF){SET(k,ID3D11ShaderReflectionVariable,GetDesc,var_desc,1);SET(k,ID3D11ShaderReflectionVariable,GetType,var_type,0);SET(k,ID3D11ShaderReflectionVariable,GetBuffer,var_buffer,0);}
    if(k==TYPEREF){SET(k,ID3D11ShaderReflectionType,GetDesc,type_desc,1);SET(k,ID3D11ShaderReflectionType,GetMemberTypeByIndex,type_index,1);SET(k,ID3D11ShaderReflectionType,GetMemberTypeByName,type_name,1);SET(k,ID3D11ShaderReflectionType,GetMemberTypeName,type_member_name,1);}
#ifdef _WIN32
    if(k==NETMGR){SET(k,INetworkListManager,IsConnected,network_connected,1);SET(k,INetworkListManager,IsConnectedToInternet,network_internet,1);SET(k,INetworkListManager,GetConnectivity,network_connectivity,1);}
    if(k==DINPUTW || k==DINPUTA){SET(k,IDirectInput8W,CreateDevice,input_device,3);SET(k,IDirectInput8W,EnumDevices,input_enum,4);SET(k,IDirectInput8W,GetDeviceStatus,input_status,1);}
    if(k==DIDEVICEW || k==DIDEVICEA){SET(k,IDirectInputDevice8W,Acquire,input_acquire,0);SET(k,IDirectInputDevice8W,Unacquire,input_unacquire,0);SET(k,IDirectInputDevice8W,GetDeviceState,input_state,2);SET(k,IDirectInputDevice8W,GetCapabilities,input_caps,1);SET(k,IDirectInputDevice8W,SetCooperativeLevel,input_cooperative,2);SET(k,IDirectInputDevice8W,SetDataFormat,input_format,1);SET(k,IDirectInputDevice8W,SetProperty,input_set_property,2);SET(k,IDirectInputDevice8W,GetProperty,input_get_property,2);SET(k,IDirectInputDevice8W,GetDeviceData,input_data,4);SET(k,IDirectInputDevice8W,Poll,input_poll,0);}
#endif
    if(k==CMDLIST){SET(k,ID3D11CommandList,GetContextFlags,command_flags,0);}
    if(k==QUERY){SET(k,ID3D11Asynchronous,GetDataSize,query_size,0);SET(k,ID3D11Query,GetDesc,query_desc,1);}
    rt_hle_register(t->methods,128);
    for(unsigned i=0;i<128;i++)st32(t->vtable+4*i,rt_hle_thunk("oa-real-d3d11",t->names[i]));
}
static uint32_t wrap(void *host,int kind){
    if(!host)return 0;
    AcquireSRWLockExclusive(&object_lock);uint32_t h=object_key(host,kind),empty=UINT32_MAX;
    for(unsigned n=0;n<HASH_LIMIT;n++,h=(h+1)&(HASH_LIMIT-1)){
        uint32_t v=object_hash[h];if(v==UINT32_MAX){if(empty==UINT32_MAX)empty=h;continue;}
        if(!v){if(empty==UINT32_MAX)empty=h;break;}
        Object*p=&objects[v-1];if(p->alive&&p->host==host&&p->kind==kind){uint32_t g=p->guest;ReleaseSRWLockExclusive(&object_lock);return g;}
    }
    if(empty==UINT32_MAX)rt_fatal("graphics proxy hash capacity exceeded");
    unsigned i=nfree?free_objects[--nfree]:nobjects++;if(i>=OBJECT_LIMIT)rt_fatal("graphics proxy live capacity exceeded");
    initialize_table(kind);Object*p=&objects[i];*p=(Object){.guest=rt_heap_alloc(8,1),.host=host,.kind=kind,.alive=1};
    st32(p->guest,tables[kind].vtable);st32(p->guest+4,i);object_hash[empty]=i+1;uint32_t result=p->guest;ReleaseSRWLockExclusive(&object_lock);return result;
}
static uint32_t wrap_dependent(void*host,int kind,Object*parent){uint32_t g=wrap(host,kind);if(!g)return 0;AcquireSRWLockExclusive(&object_lock);Object*p=object(g);uint32_t root=parent->kind==SREF?(uint32_t)(parent-objects)+1:parent->root;if(!root||!objects[root-1].alive)rt_fatal("reflection child has no live owner");if(p->root&&p->root!=root)rt_fatal("reflection child owner changed");if(!p->root){p->root=root;p->next_child=objects[root-1].first_child;objects[root-1].first_child=(uint32_t)(p-objects)+1;}ReleaseSRWLockExclusive(&object_lock);return g;}
static void create_device(X86Ctx *ctx){
    uint32_t adapter=rt_arg(ctx,0),soft=rt_arg(ctx,2),levels=rt_arg(ctx,4),n=rt_arg(ctx,5);
    if(soft || n>64)rt_fatal("unadmitted software D3D module/feature list");
    ID3D11Device *dev=NULL;ID3D11DeviceContext *immediate=NULL;D3D_FEATURE_LEVEL feature=0;
    rt_log("real device request adapter=%#x driver=%u flags=%#x sdk=%u levels=%u",adapter,rt_arg(ctx,1),rt_arg(ctx,3),rt_arg(ctx,6),n);
    HRESULT hr=D3D11CreateDevice(adapter?(IDXGIAdapter*)object(adapter)->host:NULL,rt_arg(ctx,1),NULL,rt_arg(ctx,3),levels?(const D3D_FEATURE_LEVEL*)rt_ptr(levels):NULL,n,rt_arg(ctx,6),&dev,&feature,&immediate);
    if(rt_arg(ctx,7))st32(rt_arg(ctx,7),SUCCEEDED(hr)?wrap(dev,DEV):0);
    if(rt_arg(ctx,8))st32(rt_arg(ctx,8),feature);
    if(rt_arg(ctx,9))st32(rt_arg(ctx,9),SUCCEEDED(hr)?wrap(immediate,GFX_CONTEXT):0);
    rt_log("MILESTONE real D3D11CreateDevice HRESULT=%#lx feature=%#x",(unsigned long)hr,feature);RET(hr);
}
static const HleDef defs[]={
    {"d3d11.dll","D3D11CreateDevice",create_device,10},
    {"d3dcompiler_43.dll","D3DReflect",compiler_reflect,4},
#ifdef _WIN32
    {"dinput8.dll","DirectInput8Create",input_create,5},
#endif
};
void oa_graphics_register(void){rt_hle_register(defs,sizeof(defs)/sizeof(*defs));}
