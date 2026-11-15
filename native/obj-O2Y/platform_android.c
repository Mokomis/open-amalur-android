/* Android platform services for the original guest32 ABI. No game functions. */
#include "rt.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <wctype.h>
#define A(i) rt_arg(ctx,i)
#define RET(v) (ctx->eax=(uint32_t)(v))
extern void*rt_user32_host_hwnd(uint32_t);
extern uint32_t rt_user32_guest_hwnd(void*);
extern int oa_android_width(void),oa_android_height(void),oa_android_connectivity(void);
extern uint8_t oa_android_vk_down(unsigned);
extern uint32_t host_pad_get(uint32_t,uint8_t[16]);
static void error(unsigned e){st32(rt_thread_self()->teb+0x34,e);}
static void rect(X86Ctx*ctx){uint32_t p=A(0);for(unsigned i=0;i<4;i++)st32(p+4*i,A(i+1));RET(1);}
static void adjust(X86Ctx*ctx){(void)ctx;RET(1); /* borderless native activity */}
static void window_rect(X86Ctx*ctx){uint32_t g=A(1);st32(g,0);st32(g+4,0);st32(g+8,oa_android_width());st32(g+12,oa_android_height());RET(1);}
static void desktop(X86Ctx*ctx){RET(0xd0000000u);}
static void get_dc(X86Ctx*ctx){RET(0xd1000000u);}
static void release_dc(X86Ctx*ctx){RET(A(1)==0xd1000000u);}
static void device_caps(X86Ctx*ctx){switch(A(1)){case 8:RET(oa_android_width());break;case 10:RET(oa_android_height());break;case 12:RET(32);break;case 14:RET(1);break;case 88:case 90:RET(96);break;default:RET(0);break;}}
static void metrics(X86Ctx*ctx){switch(A(0)){case 0:case 78:RET(oa_android_width());break;case 1:case 79:RET(oa_android_height());break;case 80:RET(1);break;default:RET(0);break;}}
static void parent(X86Ctx*ctx){RET(0);}
static void is_window(X86Ctx*ctx){RET(A(0)==0xd0000000u||rt_user32_host_hwnd(A(0))!=NULL);}
static void memory_status(X86Ctx*ctx){uint32_t g=A(0);if(ld32(g)!=64){error(87);RET(0);return;}struct sysinfo m;if(sysinfo(&m)){error(8);RET(0);return;}uint64_t total=(uint64_t)m.totalram*m.mem_unit,avail=(uint64_t)(m.freeram+m.bufferram)*m.mem_unit;FILE*f=fopen("/proc/meminfo","r");if(f){char line[256];unsigned long long kb;while(fgets(line,sizeof line,f))if(sscanf(line,"MemAvailable: %llu kB",&kb)==1){avail=kb*1024;break;}fclose(f);}memset(rt_ptr(g),0,64);st32(g,64);st32(g+4,total?100-(uint32_t)(avail*100/total):0);st64(g+8,total);st64(g+16,avail);st64(g+24,total+(uint64_t)m.totalswap*m.mem_unit);st64(g+32,avail+(uint64_t)m.freeswap*m.mem_unit);st64(g+40,0xffff0000ull);st64(g+48,0xffff0000ull);RET(1);}
static void system_info(X86Ctx*ctx){uint32_t g=A(0),n=sysconf(_SC_NPROCESSORS_ONLN);if(!n||n>32)n=1;memset(rt_ptr(g),0,36);/* Guest ABI is x86; threads execute native ARM on the real CPU layout. */st16(g,0);st32(g+4,4096);st32(g+8,0x10000);st32(g+12,0xfffeffff);st32(g+16,n==32?UINT32_MAX:(1u<<n)-1);st32(g+20,n);st32(g+24,586);st32(g+28,65536);st16(g+32,6);st16(g+34,0x0f02);}
static _Thread_local int apartment=-1;
static void co_init(X86Ctx*ctx){int mode=A(1)&2;if(apartment!=-1&&apartment!=mode){RET(0x80010106u);return;}int old=apartment;apartment=mode;RET(old==-1?0:1);}
static void co_uninit(X86Ctx*ctx){(void)ctx;apartment=-1;}
static void co_free(X86Ctx*ctx){rt_heap_free(A(0));}
static void system_directory(X86Ctx*ctx){const char*s="C:\\Windows\\System32";unsigned n=strlen(s);if(A(1)<=n){RET(n+1);return;}memcpy(rt_ptr(A(0)),s,n+1);RET(n);}
static void execution_state(X86Ctx*ctx){RET(0x80000000u); /* app owns KEEP_SCREEN_ON while foreground */}
static void call_window_proc(X86Ctx*ctx){uint32_t p[]={A(1),A(2),A(3),A(4)};RET(rt_call(ctx,A(0),4,p));}
static void raw_input(X86Ctx*ctx){if(A(2)!=12||A(1)>64){error(87);RET(0);return;}for(unsigned i=0;i<A(1);i++){uint32_t g=A(0)+12*i;if(ld32(g+8)&&!rt_user32_host_hwnd(ld32(g+8))){error(1400);RET(0);return;}}RET(1);}
static void keyboard_state(X86Ctx*ctx){for(unsigned i=0;i<256;i++)st8(A(0)+i,oa_android_vk_down(i)?0x80:0);RET(1);}
static const struct{uint16_t vk,sc;} keys[]={{27,1},{49,2},{50,3},{51,4},{52,5},{53,6},{54,7},{55,8},{56,9},{57,10},{48,11},{8,14},{9,15},{81,16},{87,17},{69,18},{82,19},{84,20},{89,21},{85,22},{73,23},{79,24},{80,25},{13,28},{17,29},{65,30},{83,31},{68,32},{70,33},{71,34},{72,35},{74,36},{75,37},{76,38},{16,42},{90,44},{88,45},{67,46},{86,47},{66,48},{78,49},{77,50},{18,56},{32,57},{38,72},{37,75},{39,77},{40,80}};
static unsigned map(unsigned v,int reverse){for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++)if(v==(reverse?keys[i].sc:keys[i].vk))return reverse?keys[i].vk:keys[i].sc;return 0;}
static void map_key(X86Ctx*ctx){unsigned type=A(1),v=A(0);RET(type==0?map(v,0):(type==1||type==3)?map(v&255,1):type==2&&v>=32&&v<=126?v:0);}
static void key_name(X86Ctx*ctx){unsigned vk=map((A(0)>>16)&255,1),n=A(2);const char*s=NULL;char one[2]={(char)vk,0};if(vk>=33&&vk<=126)s=one;else if(vk==32)s="Space";else if(vk==13)s="Enter";else if(vk==27)s="Escape";else if(vk==16)s="Shift";else if(vk==17)s="Ctrl";else if(vk==18)s="Alt";else s="";unsigned len=strlen(s),copy=n?len<n?len:n-1:0;for(unsigned i=0;i<copy;i++)st16(A(1)+2*i,s[i]);if(n)st16(A(1)+2*copy,0);RET(copy);}
static void keyboard_layout(X86Ctx*ctx){RET(0x04090409u);}
static void unicode_key(X86Ctx*ctx){unsigned vk=A(0),n=A(4);if(!n){RET(0);return;}int shift=ld8(A(2)+16)&128;if(vk>='A'&&vk<='Z'&&!shift)vk+=32;if(vk>=32&&vk<=126){st16(A(3),vk);RET(1);}else RET(0);}
static void uppercase(X86Ctx*ctx){uint32_t g=A(0);if(g<0x10000){RET(towupper(g));return;}for(uint32_t p=g;ld16(p);p+=2)st16(p,towupper(ld16(p)));RET(g);}
static void double_click(X86Ctx*ctx){RET(500);}
static void cursor_unavailable(X86Ctx*ctx){error(1814);RET(0);}
static void cursor_set(X86Ctx*ctx){RET(0);}
static void xinput_get(X86Ctx *ctx)
{
    RET(A(1) ? host_pad_get(A(0), rt_ptr(A(1))) : 87);
}
static void xinput_caps(X86Ctx *ctx)
{
    uint8_t state[16];
    if (!A(2)) { RET(87); return; }
    uint32_t result = host_pad_get(A(0), state);
    if (result) { RET(result); return; }
    uint32_t p = A(2);
    memset(rt_ptr(p), 0, 20);
    st8(p, 1);
    st8(p + 1, 1);
    st16(p + 4, 0xf3ff);
    st8(p + 6, 255);
    st8(p + 7, 255);
    for (unsigned offset = 8; offset <= 14; offset += 2) st16(p + offset, 32767);
    RET(0);
}
static void xinput_set(X86Ctx *ctx)
{
    /* The Android host has no physical rumble adapter yet. */
    uint8_t state[16];
    RET(A(1) ? host_pad_get(A(0), state) : 87);
}
static void xinput_enable(X86Ctx*ctx){(void)ctx;}
static void module_unavailable(X86Ctx*ctx){error(126);RET(0);rt_log("Android optional LoadLibraryExW unavailable: requested original OS library");}
/* Native Android UID represented by the documented Unix SID authority 22. */
static unsigned token_active;
static void open_token(X86Ctx*ctx){if(A(0)!=UINT32_MAX){error(87);RET(0);return;}token_active=1;st32(A(2),0xd5000000u);RET(1);}
static void token_info(X86Ctx*ctx){if(A(0)!=0xd5000000u||!token_active||A(1)!=1){error(87);RET(0);return;}st32(A(4),24);if(A(3)<24||!A(2)){error(122);RET(0);return;}uint32_t g=A(2);memset(rt_ptr(g),0,24);st32(g,g+8);st8(g+8,1);st8(g+9,2);st8(g+15,22);st32(g+16,1);st32(g+20,getuid());RET(1);}
static void sid_string(X86Ctx*ctx){uint32_t s=A(0);if(ld8(s)!=1||ld8(s+1)!=2||ld8(s+7)!=22||ld32(s+8)!=1){error(1337);RET(0);return;}char out[64];snprintf(out,sizeof out,"S-1-22-1-%u",ld32(s+12));uint32_t p=rt_heap_alloc(strlen(out)+1,0);strcpy(rt_ptr(p),out);st32(A(1),p);RET(1);}
static void close_handle(X86Ctx*ctx){if(A(0)==0xd5000000u){token_active=0;RET(1);return;}extern void rt_file_close(uint32_t);rt_file_close(A(0));rt_handle_close(A(0));RET(1);}
/* Actual Android connectivity through a guest COM-compatible object. */
static const uint8_t net_suffix[12]={0x0f,0x57,0x9b,0x4a,0x8d,0x69,0x19,0x9f,0xdb,0xa5,0x72,0x3b};
static uint32_t net_object,net_refs;static HleDef net_methods[14];static char net_names[14][48];
static void net_unsupported(X86Ctx*ctx){(void)ctx;rt_fatal("Android network COM method outside implemented surface");}
static int net_iid(uint32_t p){return (ld32(p)==0xdcb00000u&&!memcmp(rt_ptr(p+4),net_suffix,12))||(ld32(p)==0&&!memcmp(rt_ptr(p+4),(uint8_t[]){0,0,0,0,0xc0,0,0,0,0,0,0,0x46},12));}
static void net_query(X86Ctx*ctx){if(net_iid(A(1))){st32(A(2),net_object);net_refs++;RET(0);}else{st32(A(2),0);RET(0x80004002u);}}
static void net_add(X86Ctx*ctx){RET(++net_refs);}
static void net_release(X86Ctx*ctx){RET(net_refs?--net_refs:0);}
static void net_status(X86Ctx*ctx,int internet){int flags=oa_android_connectivity();if(flags<0){RET(0x80004005u);return;}st16(A(1),internet?(flags&0x40?0xffff:0):(flags?0xffff:0));RET(0);}
static void net_internet(X86Ctx*ctx){net_status(ctx,1);}
static void net_connected(X86Ctx*ctx){net_status(ctx,0);}
static void net_flags(X86Ctx*ctx){int flags=oa_android_connectivity();if(flags<0){RET(0x80004005u);return;}st32(A(1),flags);RET(0);}
static uint32_t make_network(void){if(!net_object){uint32_t v=rt_heap_alloc(56,1);net_object=rt_heap_alloc(4,1);st32(net_object,v);for(unsigned i=0;i<14;i++){snprintf(net_names[i],48,"NetworkListManager.slot%u",i);net_methods[i]=(HleDef){"oa-android-platform",net_names[i],net_unsupported,1};}net_methods[0].fn=net_query;net_methods[0].nargs=3;net_methods[1].fn=net_add;net_methods[2].fn=net_release;net_methods[11].fn=net_internet;net_methods[11].nargs=2;net_methods[12].fn=net_connected;net_methods[12].nargs=2;net_methods[13].fn=net_flags;net_methods[13].nargs=2;rt_hle_register(net_methods,14);for(unsigned i=0;i<14;i++)st32(v+4*i,rt_hle_thunk("oa-android-platform",net_names[i]));}net_refs++;return net_object;}
int oa_audio_co_create(X86Ctx *);
static void co_create(X86Ctx*ctx){if(oa_audio_co_create(ctx))return;if(A(1)){st32(A(4),0);RET(0x80040110u);return;}if(ld32(A(0))==0xdcb00c01u&&!memcmp(rt_ptr(A(0)+4),net_suffix,12)&&net_iid(A(3))){st32(A(4),make_network());RET(0);return;}st32(A(4),0);rt_log("Android COM class not connected: cls=%#x iid=%#x",ld32(A(0)),ld32(A(3)));RET(0x80040154u);}
#define D(L,N,F,C) {L,N,F,C}
static const HleDef defs[]={
D("user32.dll","SetRect",rect,5),D("user32.dll","AdjustWindowRect",adjust,3),D("user32.dll","GetWindowRect",window_rect,2),D("user32.dll","GetClientRect",window_rect,2),D("user32.dll","GetDesktopWindow",desktop,0),D("user32.dll","GetParent",parent,1),D("user32.dll","IsWindow",is_window,1),D("user32.dll","GetDC",get_dc,1),D("user32.dll","ReleaseDC",release_dc,2),D("gdi32.dll","GetDeviceCaps",device_caps,2),D("user32.dll","GetSystemMetrics",metrics,1),
D("kernel32.dll","GlobalMemoryStatusEx",memory_status,1),D("kernel32.dll","GetNativeSystemInfo",system_info,1),D("kernel32.dll","GetSystemDirectoryA",system_directory,2),D("kernel32.dll","SetThreadExecutionState",execution_state,1),D("user32.dll","CallWindowProcA",call_window_proc,5),D("user32.dll","CallWindowProcW",call_window_proc,5),D("user32.dll","RegisterRawInputDevices",raw_input,3),D("user32.dll","GetKeyboardState",keyboard_state,1),D("user32.dll","MapVirtualKeyW",map_key,2),D("user32.dll","MapVirtualKeyA",map_key,2),D("user32.dll","GetKeyNameTextW",key_name,3),D("user32.dll","GetKeyboardLayout",keyboard_layout,1),D("user32.dll","ToUnicodeEx",unicode_key,7),D("user32.dll","CharUpperW",uppercase,1),D("user32.dll","GetDoubleClickTime",double_click,0),D("user32.dll","LoadCursorA",cursor_unavailable,2),D("user32.dll","LoadCursorW",cursor_unavailable,2),D("user32.dll","LoadIconW",cursor_unavailable,2),D("user32.dll","DestroyCursor",cursor_set,1),D("user32.dll","SetCursor",cursor_set,1),
D("ole32.dll","CoInitializeEx",co_init,2),D("ole32.dll","CoUninitialize",co_uninit,0),D("ole32.dll","CoTaskMemFree",co_free,1),D("ole32.dll","CoCreateInstance",co_create,5),D("advapi32.dll","OpenProcessToken",open_token,3),D("advapi32.dll","GetTokenInformation",token_info,5),D("advapi32.dll","ConvertSidToStringSidA",sid_string,2),D("kernel32.dll","CloseHandle",close_handle,1),D("kernel32.dll","LoadLibraryExW",module_unavailable,3),
D("xinput1_3.dll","#2",xinput_get,2),D("xinput1_3.dll","XInputGetState",xinput_get,2),D("xinput1_3.dll","#3",xinput_set,2),D("xinput1_3.dll","XInputSetState",xinput_set,2),D("xinput1_3.dll","#4",xinput_caps,3),D("xinput1_3.dll","XInputGetCapabilities",xinput_caps,3),D("xinput1_3.dll","#5",xinput_enable,1),D("xinput1_3.dll","XInputEnable",xinput_enable,1)
};
void rt_dinput_register(void);
void oa_audio_register(void);
void oa_platform_register(void){oa_audio_register();rt_dinput_register();rt_hle_register(defs,sizeof(defs)/sizeof(*defs));}
