#define IDirectInput8W_CreateDevice(p,...) ((p)->lpVtbl->CreateDevice((p), ##__VA_ARGS__))
#define IDirectInput8W_EnumDevices(p,...) ((p)->lpVtbl->EnumDevices((p), ##__VA_ARGS__))
#define IDirectInput8W_GetDeviceStatus(p,...) ((p)->lpVtbl->GetDeviceStatus((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_Acquire(p,...) ((p)->lpVtbl->Acquire((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_GetCapabilities(p,...) ((p)->lpVtbl->GetCapabilities((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_GetDeviceData(p,...) ((p)->lpVtbl->GetDeviceData((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_GetDeviceState(p,...) ((p)->lpVtbl->GetDeviceState((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_GetProperty(p,...) ((p)->lpVtbl->GetProperty((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_Poll(p,...) ((p)->lpVtbl->Poll((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_SetCooperativeLevel(p,...) ((p)->lpVtbl->SetCooperativeLevel((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_SetDataFormat(p,...) ((p)->lpVtbl->SetDataFormat((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_SetProperty(p,...) ((p)->lpVtbl->SetProperty((p), ##__VA_ARGS__))
#define IDirectInputDevice8W_Unacquire(p,...) ((p)->lpVtbl->Unacquire((p), ##__VA_ARGS__))

static uint32_t input_copy_event(const void *data, uint32_t length)
{
    uint32_t address = rt_heap_alloc(length, 0);
    memcpy(rt_ptr(address), data, length);
    return address;
}
/* Windows DirectInput8 bridge. Enumeration calls original guest callbacks. */
static HMODULE input_module;
static void input_create(X86Ctx*ctx){
 const GUID*id=(const GUID*)rt_ptr(rt_arg(ctx,2));int k=IsEqualGUID(id,&IID_IDirectInput8W)?DINPUTW:IsEqualGUID(id,&IID_IDirectInput8A)?DINPUTA:-1;if(k<0||rt_arg(ctx,4))rt_fatal("unqualified DirectInput root IID/aggregation");
 if(!input_module)input_module=LoadLibraryExW(L"dinput8.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);typedef HRESULT(WINAPI*Fn)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);Fn fn=input_module?(Fn)GetProcAddress(input_module,"DirectInput8Create"):NULL;if(!fn)rt_fatal("native DirectInput8Create unavailable");
 void*out=NULL;HRESULT hr=fn(GetModuleHandleW(NULL),rt_arg(ctx,1),id,&out,NULL);st32(rt_arg(ctx,3),SUCCEEDED(hr)?wrap(out,k):0);rt_log("real DirectInput8Create HRESULT=%#lx",(unsigned long)hr);RET(hr);
}
static void input_device(X86Ctx*ctx){Object*p=self(ctx);if(A(2))rt_fatal("DirectInput device aggregation unqualified");void*out=NULL;HRESULT hr=IDirectInput8W_CreateDevice((IDirectInput8W*)p->host,(const GUID*)rt_ptr(A(0)),(IDirectInputDevice8W**)&out,NULL);st32(A(1),SUCCEEDED(hr)?wrap(out,p->kind==DINPUTW?DIDEVICEW:DIDEVICEA):0);RET(hr);}
typedef struct{X86Ctx*ctx;uint32_t callback,user;} InputEnum;
static BOOL CALLBACK input_enum_callback(const DIDEVICEINSTANCEW*inst,void*data){InputEnum*p=data;uint32_t n=inst->dwSize;if(n<16||n>4096)rt_fatal("DirectInput enumeration structure size unqualified");uint32_t g=input_copy_event(inst,n),args[]={g,p->user};X86Ctx saved=*p->ctx;uint32_t r=rt_call(p->ctx,p->callback,2,args);*p->ctx=saved;rt_heap_free(g);return (BOOL)r;}
static void input_enum(X86Ctx*ctx){InputEnum e={ctx,A(1),A(2)};RET(IDirectInput8W_EnumDevices((IDirectInput8W*)self(ctx)->host,A(0),input_enum_callback,&e,A(3)));}
static void input_status(X86Ctx*ctx){RET(IDirectInput8W_GetDeviceStatus((IDirectInput8W*)self(ctx)->host,(const GUID*)rt_ptr(A(0))));}
static void input_acquire(X86Ctx*ctx){RET(IDirectInputDevice8W_Acquire((IDirectInputDevice8W*)self(ctx)->host));}
static void input_unacquire(X86Ctx*ctx){RET(IDirectInputDevice8W_Unacquire((IDirectInputDevice8W*)self(ctx)->host));}
static void input_state(X86Ctx*ctx){RET(IDirectInputDevice8W_GetDeviceState((IDirectInputDevice8W*)self(ctx)->host,A(0),rt_ptr(A(1))));}
static void input_caps(X86Ctx*ctx){RET(IDirectInputDevice8W_GetCapabilities((IDirectInputDevice8W*)self(ctx)->host,(DIDEVCAPS*)rt_ptr(A(0))));}
static void input_cooperative(X86Ctx*ctx){HWND w=(HWND)rt_user32_host_hwnd(A(0));if(A(0)&&!w)rt_fatal("DirectInput cooperative unmapped window");RET(IDirectInputDevice8W_SetCooperativeLevel((IDirectInputDevice8W*)self(ctx)->host,w,A(1)));}
static void input_format(X86Ctx*ctx){uint32_t g=A(0);if(ld32(g)!=24||ld32(g+4)!=16)rt_fatal("unqualified guest DirectInput data format sizes");DIDATAFORMAT d={sizeof(d),sizeof(DIOBJECTDATAFORMAT),ld32(g+8),ld32(g+12),ld32(g+16),NULL};if(d.dwNumObjs>4096)rt_fatal("input data format object bound");d.rgodf=calloc(d.dwNumObjs,sizeof(*d.rgodf));if(d.dwNumObjs&&!d.rgodf)rt_fatal("input data format allocation");for(unsigned i=0;i<d.dwNumObjs;i++){uint32_t x=ld32(g+20)+16*i,p=ld32(x);d.rgodf[i]=(DIOBJECTDATAFORMAT){p?(const GUID*)rt_ptr(p):NULL,ld32(x+4),ld32(x+8),ld32(x+12)};}HRESULT hr=IDirectInputDevice8W_SetDataFormat((IDirectInputDevice8W*)self(ctx)->host,&d);free(d.rgodf);RET(hr);}
static const GUID*input_property(uint32_t g){return g<65536?(const GUID*)(uintptr_t)g:(const GUID*)rt_ptr(g);}
static void input_set_property(X86Ctx*ctx){const GUID*p=input_property(A(0));if(p==DIPROP_APPDATA)rt_fatal("pointer-sized DirectInput APPDATA unqualified");RET(IDirectInputDevice8W_SetProperty((IDirectInputDevice8W*)self(ctx)->host,p,(const DIPROPHEADER*)rt_ptr(A(1))));}
static void input_get_property(X86Ctx*ctx){const GUID*p=input_property(A(0));if(p==DIPROP_APPDATA)rt_fatal("pointer-sized DirectInput APPDATA unqualified");RET(IDirectInputDevice8W_GetProperty((IDirectInputDevice8W*)self(ctx)->host,p,(DIPROPHEADER*)rt_ptr(A(1))));}
static void input_poll(X86Ctx*ctx){RET(IDirectInputDevice8W_Poll((IDirectInputDevice8W*)self(ctx)->host));}
static void input_data(X86Ctx*ctx){UINT stride=A(0),count=ld32(A(2));if(stride!=16&&stride!=20)rt_fatal("unqualified DirectInput object data size %u",stride);if(count>65536&&A(1))rt_fatal("input buffered event bound");DIDEVICEOBJECTDATA*d=A(1)?calloc(count?count:1,sizeof(*d)):NULL;DWORD n=count;HRESULT hr=IDirectInputDevice8W_GetDeviceData((IDirectInputDevice8W*)self(ctx)->host,sizeof(*d),d,&n,A(3));if(SUCCEEDED(hr)&&d){for(unsigned i=0;i<n;i++){memcpy(rt_ptr(A(1)+i*stride),&d[i],16);if(stride==20){if(d[i].uAppData>UINT32_MAX)rt_fatal("input event appdata not guest32");st32(A(1)+i*stride+16,(uint32_t)d[i].uAppData);}}}st32(A(2),n);free(d);RET(hr);}
