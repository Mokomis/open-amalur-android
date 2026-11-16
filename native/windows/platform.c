/* Windows-only platform bridge for the isolated startup experiment.
 * Game routines are never replaced here. ARM implementations are not claimed.
 */
#include <windows.h>
#include <objbase.h>
#include <xinput.h>
#include <string.h>
#include "rt.h"
#define ARG(i) rt_arg(ctx,i)
#define RET(v) (ctx->eax=(uint32_t)(v))
extern void *rt_user32_host_hwnd(uint32_t);
extern int oa_windows_render_width, oa_windows_render_height;
#define DESKTOP_TOKEN 0xd0000000u
static HDC dcs[32];
static HCURSOR cursors[64];
static HICON icons[32];
static HKL layouts[16];
static HMODULE resource_image;
static uint32_t cursor_token(HCURSOR c){
    if(!c)return 0;
    for(unsigned i=0;i<64;i++)if(cursors[i]==c)return 0xd2000000u+4*i;
    for(unsigned i=0;i<64;i++)if(!cursors[i]){cursors[i]=c;return 0xd2000000u+4*i;}
    rt_fatal("cursor table full");
}
static HCURSOR cursor_from_guest(uint32_t h){
    if(!h)return NULL;
    unsigned i=(h-0xd2000000u)/4;
    if(h<0xd2000000u || (h&3) || i>=64 || !cursors[i])rt_fatal("unmapped guest cursor %#x",h);
    return cursors[i];
}
static HMODULE resource_module(uint32_t h){
    if(!h)return NULL;
    if(h!=0x400000)rt_fatal("resource module %#x outside this probe",h);
    if(!resource_image){
        const char *path=getenv("OA_PE_PATH");if(!path)rt_fatal("no private resource image path");
        resource_image=LoadLibraryExA(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if(!resource_image)rt_fatal("resource image could not load: %lu",GetLastError());
    }
    return resource_image;
}
static const void *resource_name(uint32_t p){return p<0x10000?(const void*)(uintptr_t)p:rt_ptr(p);}
static void load_cursor_a(X86Ctx *ctx){RET(cursor_token(LoadCursorA(resource_module(ARG(0)),resource_name(ARG(1)))));}
static void load_cursor_w(X86Ctx *ctx){RET(cursor_token(LoadCursorW(resource_module(ARG(0)),resource_name(ARG(1)))));}
static void set_cursor(X86Ctx *ctx){RET(cursor_token(SetCursor(cursor_from_guest(ARG(0)))));}
static void destroy_cursor(X86Ctx *ctx){RET(DestroyCursor(cursor_from_guest(ARG(0))));}
static void load_icon_w(X86Ctx *ctx){
    HICON h=LoadIconW(resource_module(ARG(0)),resource_name(ARG(1)));
    if(!h){RET(0);return;}
    for(unsigned i=0;i<32;i++)if(!icons[i] || icons[i]==h){icons[i]=h;RET(0xd3000000u+4*i);return;}
    rt_fatal("icon table full");
}
static void set_rect(X86Ctx *ctx){RET(SetRect((RECT*)rt_ptr(ARG(0)),ARG(1),ARG(2),ARG(3),ARG(4)));}
static void adjust_rect(X86Ctx *ctx){RET(AdjustWindowRect((RECT*)rt_ptr(ARG(0)),ARG(1),ARG(2)));}
static void key_name(X86Ctx *ctx){RET(GetKeyNameTextW(ARG(0),(wchar_t*)rt_ptr(ARG(1)),ARG(2)));}
static void map_key_w(X86Ctx *ctx){RET(MapVirtualKeyW(ARG(0),ARG(1)));}
static void map_key_a(X86Ctx *ctx){RET(MapVirtualKeyA(ARG(0),ARG(1)));}
static void keyboard_layout(X86Ctx *ctx){
    HKL h=GetKeyboardLayout(ARG(0));if(!h){RET(0);return;}
    for(unsigned i=0;i<16;i++)if(!layouts[i] || layouts[i]==h){layouts[i]=h;RET(0xd4000000u+4*i);return;}
    rt_fatal("keyboard layout table full");
}
static void unicode_key(X86Ctx *ctx){
    uint32_t h=ARG(6),i=(h-0xd4000000u)/4;
    if(h<0xd4000000u || (h&3) || i>=16 || !layouts[i])rt_fatal("unknown keyboard layout %#x",h);
    RET(ToUnicodeEx(ARG(0),ARG(1),(BYTE*)rt_ptr(ARG(2)),(wchar_t*)rt_ptr(ARG(3)),ARG(4),ARG(5),layouts[i]));
}
static void uppercase(X86Ctx *ctx){
    uint32_t p=ARG(0);
    if(p<0x10000){RET((uintptr_t)CharUpperW((wchar_t*)(uintptr_t)p));return;}
    CharUpperW((wchar_t*)rt_ptr(p));RET(p);
}
static void double_click(X86Ctx *ctx){RET(GetDoubleClickTime());}
static HWND hwnd_from_guest(uint32_t h){return h==DESKTOP_TOKEN?GetDesktopWindow():(HWND)rt_user32_host_hwnd(h);}
extern uint32_t rt_user32_guest_hwnd(void*);
static void parent_window(X86Ctx*ctx){HWND h=GetParent(hwnd_from_guest(ARG(0)));uint32_t g=h?rt_user32_guest_hwnd(h):0;if(h&&!g)rt_fatal("native parent window outside original app mapping");RET(g);}
static void is_window(X86Ctx*ctx){RET(IsWindow(hwnd_from_guest(ARG(0))));}
static void desktop_window(X86Ctx *ctx){RET(DESKTOP_TOKEN);}
static void get_dc(X86Ctx *ctx){
    HDC dc=GetDC(ARG(0)?hwnd_from_guest(ARG(0)):NULL);
    if(!dc){RET(0);return;}
    for(unsigned i=0;i<32;i++)if(!dcs[i]){dcs[i]=dc;RET(0xd1000000u+4*i);return;}
    rt_fatal("DC table exhausted");
}
static HDC dc_from_guest(uint32_t h){unsigned i=(h-0xd1000000u)/4;return h>=0xd1000000u && !(h&3) && i<32?dcs[i]:NULL;}
static void release_dc(X86Ctx *ctx){
    HDC dc=dc_from_guest(ARG(1));if(!dc)rt_fatal("unknown guest DC %#x",ARG(1));
    int ok=ReleaseDC(ARG(0)?hwnd_from_guest(ARG(0)):NULL,dc);dcs[(ARG(1)-0xd1000000u)/4]=NULL;RET(ok);
}
static void device_caps(X86Ctx *ctx){HDC dc=dc_from_guest(ARG(0));if(!dc)rt_fatal("unknown guest DC %#x",ARG(0));RET(GetDeviceCaps(dc,ARG(1)));}
static void window_rect(X86Ctx *ctx){RECT r;BOOL ok=GetWindowRect(hwnd_from_guest(ARG(0)),&r);if(ok)memcpy(rt_ptr(ARG(1)),&r,16);RET(ok);}
static void client_rect(X86Ctx *ctx)
{
    RECT r;
    BOOL ok = GetClientRect(hwnd_from_guest(ARG(0)), &r);
    if (ok) {
        if (ARG(0) != DESKTOP_TOKEN) {
            r.right = oa_windows_render_width;
            r.bottom = oa_windows_render_height;
        }
        memcpy(rt_ptr(ARG(1)), &r, sizeof r);
    }
    RET(ok);
}
static void system_metrics(X86Ctx *ctx)
{
    switch (ARG(0)) {
    case SM_CXSCREEN: case SM_CXFULLSCREEN: RET(oa_windows_render_width); break;
    case SM_CYSCREEN: case SM_CYFULLSCREEN: RET(oa_windows_render_height); break;
    default: RET(GetSystemMetrics(ARG(0))); break;
    }
}
static void co_init(X86Ctx *ctx) { RET(CoInitializeEx(NULL,ARG(1))); }
static void co_uninit(X86Ctx *ctx) { (void)ctx;CoUninitialize(); }
static void co_free(X86Ctx *ctx) { rt_heap_free(ARG(0)); }
static void memory_status(X86Ctx *ctx) {
    MEMORYSTATUSEX m={sizeof(m)};
    if(ld32(ARG(0))!=sizeof(m)){st32(rt_thread_self()->teb+0x34,87);RET(0);return;}
    BOOL ok=GlobalMemoryStatusEx(&m);
    if(ok)memcpy(rt_ptr(ARG(0)),&m,sizeof(m));
    RET(ok);
}
static void native_info(X86Ctx *ctx) {
    SYSTEM_INFO s;GetNativeSystemInfo(&s);uint32_t p=ARG(0);
    memset(rt_ptr(p),0,36);
    st16(p,s.wProcessorArchitecture);st16(p+2,s.wReserved);st32(p+4,s.dwPageSize);
    st32(p+8,0x10000);st32(p+12,0x7ffeffff);st32(p+16,(uint32_t)s.dwActiveProcessorMask);
    st32(p+20,s.dwNumberOfProcessors);st32(p+24,s.dwProcessorType);
    st32(p+28,s.dwAllocationGranularity);st16(p+32,s.wProcessorLevel);st16(p+34,s.wProcessorRevision);
}
static void system_directory(X86Ctx *ctx) {
    const char path[]="C:\\Windows\\System32";uint32_t n=ARG(1),len=sizeof(path)-1;
    if(n<=len){RET(len+1);return;}memcpy(rt_ptr(ARG(0)),path,sizeof(path));RET(len);
}
static void execution_state(X86Ctx*ctx){RET(SetThreadExecutionState(ARG(0)));}
static void call_window_proc(X86Ctx*ctx){uint32_t fn=ARG(0),a[]={ARG(1),ARG(2),ARG(3),ARG(4)};RET(rt_call(ctx,fn,4,a));}
static void register_raw_input(X86Ctx*ctx){UINT n=ARG(1);if(ARG(2)!=12||n>64)rt_fatal("unqualified raw input device array");RAWINPUTDEVICE d[64];for(UINT i=0;i<n;i++){uint32_t g=ARG(0)+12*i,h=ld32(g+8);d[i]=(RAWINPUTDEVICE){ld16(g),ld16(g+2),ld32(g+4),h?hwnd_from_guest(h):NULL};if(h&&!d[i].hwndTarget)rt_fatal("raw input window unmapped");}RET(RegisterRawInputDevices(d,n,sizeof(*d)));}
static void keyboard_state(X86Ctx*ctx){RET(GetKeyboardState((BYTE*)rt_ptr(ARG(0))));}
HRAWINPUT oa_windows_raw_input(uint32_t token);

static void raw_input_error(X86Ctx *ctx, DWORD error)
{
    st32(rt_thread_self()->teb + 0x34, error);
    RET(UINT32_MAX);
}

static void get_raw_input_data(X86Ctx *ctx)
{
    const unsigned guest_header_size = 16;
    HRAWINPUT input = oa_windows_raw_input(ARG(0));
    const unsigned command = ARG(1);
    uint32_t destination = ARG(2), size_ptr = ARG(3);
    if (!input || !size_ptr || ARG(4) != guest_header_size ||
        (command != RID_HEADER && command != RID_INPUT)) {
        raw_input_error(ctx, ERROR_INVALID_PARAMETER);
        return;
    }
    UINT native_size = 0;
    if (GetRawInputData(input, command, NULL, &native_size, sizeof(RAWINPUTHEADER)) == UINT32_MAX) {
        raw_input_error(ctx, GetLastError());
        return;
    }
    if (native_size < sizeof(RAWINPUTHEADER) || native_size > 1024 * 1024) {
        raw_input_error(ctx, ERROR_INVALID_DATA);
        return;
    }
    unsigned payload = native_size - sizeof(RAWINPUTHEADER);
    unsigned guest_size = guest_header_size + payload;
    unsigned available = ld32(size_ptr);
    st32(size_ptr, guest_size);
    if (!destination) { RET(0); return; }
    if (available < guest_size) {
        raw_input_error(ctx, ERROR_INSUFFICIENT_BUFFER);
        return;
    }
    RAWINPUT *data = malloc(native_size);
    if (!data) { raw_input_error(ctx, ERROR_NOT_ENOUGH_MEMORY); return; }
    UINT read = GetRawInputData(input, command, data, &native_size, sizeof(RAWINPUTHEADER));
    if (read != native_size) {
        DWORD error = read == UINT32_MAX ? GetLastError() : ERROR_INVALID_DATA;
        free(data);
        raw_input_error(ctx, error);
        return;
    }
    st32(destination, data->header.dwType);
    st32(destination + 4, data->header.dwSize - sizeof(RAWINPUTHEADER) + guest_header_size);
    st32(destination + 8, (uint32_t)(uintptr_t)data->header.hDevice);
    st32(destination + 12, (uint32_t)data->header.wParam);
    memcpy(rt_ptr(destination + guest_header_size), &data->data, payload);
    free(data);
    RET(guest_size);
}
static HMODULE xinput_module;
static FARPROC xinput_symbol(const char*name){if(!xinput_module)xinput_module=LoadLibraryExW(L"xinput1_3.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);FARPROC f=xinput_module?GetProcAddress(xinput_module,name):NULL;if(!f)rt_fatal("native XInput export unavailable");return f;}
extern int veil_options_is_visible(void);
#include "../options_navigation.h"
static OaOptionsNavigation options_navigation[4];
static void xinput_state(X86Ctx *ctx)
{
    XINPUT_STATE *state = (XINPUT_STATE *)rt_ptr(ARG(1));
    DWORD slot = ARG(0);
    DWORD result = ((DWORD(WINAPI *)(DWORD, XINPUT_STATE *))
                   xinput_symbol("XInputGetState"))(slot, state);
    if (slot < 4 && result == ERROR_SUCCESS)
        oa_options_navigation(&options_navigation[slot], veil_options_is_visible(),
                              &state->Gamepad.wButtons, &state->Gamepad.bLeftTrigger,
                              &state->Gamepad.bRightTrigger, &state->Gamepad.sThumbLX,
                              &state->Gamepad.sThumbLY);
    RET(result);
}
static void xinput_caps(X86Ctx*ctx){RET(((DWORD(WINAPI*)(DWORD,DWORD,XINPUT_CAPABILITIES*))xinput_symbol("XInputGetCapabilities"))(ARG(0),ARG(1),(XINPUT_CAPABILITIES*)rt_ptr(ARG(2))));}
static void xinput_enable(X86Ctx*ctx){((void(WINAPI*)(BOOL))xinput_symbol("XInputEnable"))(ARG(0));}
static void xinput_set(X86Ctx*ctx){RET(((DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*))xinput_symbol("XInputSetState"))(ARG(0),(XINPUT_VIBRATION*)rt_ptr(ARG(1))));}
#include "security_windows.inc"
int oa_audio_co_create(X86Ctx *);
int oa_windows_network_create(X86Ctx *);
void oa_audio_register(void);

static void co_create(X86Ctx *ctx)
{
    if (oa_audio_co_create(ctx) || oa_windows_network_create(ctx)) return;
    st32(ARG(4), 0);
    RET(REGDB_E_CLASSNOTREG);
}

static void module_unavailable(X86Ctx *ctx)
{
    st32(rt_thread_self()->teb + 0x34, ERROR_MOD_NOT_FOUND);
    RET(0);
}

static const HleDef platform[]={
 {"kernel32.dll","SetThreadExecutionState",execution_state,1},
 {"user32.dll","CallWindowProcA",call_window_proc,5},
 {"user32.dll","CallWindowProcW",call_window_proc,5},
 {"advapi32.dll","OpenProcessToken",open_process_token,3},
 {"advapi32.dll","GetTokenInformation",token_information,5},
 {"advapi32.dll","ConvertSidToStringSidA",sid_string,2},
 {"kernel32.dll","CloseHandle",close_handle,1},
 {"user32.dll","RegisterRawInputDevices",register_raw_input,3},
 {"user32.dll","GetRawInputData",get_raw_input_data,5},
 {"user32.dll","GetKeyboardState",keyboard_state,1},
 {"xinput1_3.dll","#2",xinput_state,2},{"xinput1_3.dll","XInputGetState",xinput_state,2},
 {"xinput1_3.dll","#3",xinput_set,2},{"xinput1_3.dll","XInputSetState",xinput_set,2},
 {"xinput1_3.dll","#4",xinput_caps,3},{"xinput1_3.dll","XInputGetCapabilities",xinput_caps,3},
 {"xinput1_3.dll","#5",xinput_enable,1},{"xinput1_3.dll","XInputEnable",xinput_enable,1},
 {"user32.dll","GetParent",parent_window,1},
 {"user32.dll","IsWindow",is_window,1},
 {"user32.dll","SetRect",set_rect,5},
 {"user32.dll","AdjustWindowRect",adjust_rect,3},
 {"user32.dll","LoadIconW",load_icon_w,2},
 {"user32.dll","GetKeyNameTextW",key_name,3},
 {"user32.dll","MapVirtualKeyW",map_key_w,2},
 {"user32.dll","MapVirtualKeyA",map_key_a,2},
 {"user32.dll","GetKeyboardLayout",keyboard_layout,1},
 {"user32.dll","ToUnicodeEx",unicode_key,7},
 {"user32.dll","CharUpperW",uppercase,1},
 {"user32.dll","GetDoubleClickTime",double_click,0},
 {"user32.dll","LoadCursorA",load_cursor_a,2},
 {"user32.dll","LoadCursorW",load_cursor_w,2},
 {"user32.dll","SetCursor",set_cursor,1},
 {"user32.dll","DestroyCursor",destroy_cursor,1},
 {"user32.dll","GetDesktopWindow",desktop_window,0},
 {"user32.dll","GetDC",get_dc,1},
 {"user32.dll","ReleaseDC",release_dc,2},
 {"user32.dll","GetWindowRect",window_rect,2},
 {"user32.dll","GetClientRect",client_rect,2},
 {"user32.dll","GetSystemMetrics",system_metrics,1},
 {"gdi32.dll","GetDeviceCaps",device_caps,2},
 {"ole32.dll","CoInitializeEx",co_init,2},
 {"ole32.dll","CoUninitialize",co_uninit,0},
 {"ole32.dll","CoTaskMemFree",co_free,1},
 {"ole32.dll","CoCreateInstance",co_create,5},
 {"kernel32.dll","LoadLibraryExW",module_unavailable,3},
 {"kernel32.dll","GlobalMemoryStatusEx",memory_status,1},
 {"kernel32.dll","GetNativeSystemInfo",native_info,1},
 {"kernel32.dll","GetSystemDirectoryA",system_directory,2},
};
void oa_platform_register(void)
{
    oa_audio_register();
    rt_hle_register(platform, sizeof(platform) / sizeof(*platform));
}
