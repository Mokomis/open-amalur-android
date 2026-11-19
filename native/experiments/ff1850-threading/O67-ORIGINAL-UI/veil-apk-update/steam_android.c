/* Original-engine platform ABI, backed by local Android storage in offline builds. */
#include "oa_android_win.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"
#define ARG(i) rt_arg(ctx,i)
#define RET(v) (ctx->eax=(uint32_t)(v))
typedef struct {
    uint32_t guest;
    void *host;
    char version[96];
    HleDef methods[128];
    char names[128][112];
} Proxy;
static Proxy proxies[64];
static unsigned nproxies;
static HMODULE library;
static int initialized;
uint32_t rt_self_last_target(void);
const char *rt_thunk_name(uint32_t);
#ifdef OA_STEAM_BRIDGE
#include "steam_remote.inc"
static void*symbol(const char*name){return oa_remote_symbol(name);}
#else
#include "steam_offline.inc"
static void*symbol(const char*name){return oa_offline_symbol(name);}
#endif
static void shutdown_native(void) {
    if(initialized){((void(*)(void))symbol("SteamAPI_Shutdown"))();initialized=0;}
}
static Proxy *self(X86Ctx *ctx) {
    for(unsigned i=0;i<nproxies;i++)if(proxies[i].guest==ctx->ecx)return &proxies[i];
    rt_fatal("unknown Steam proxy receiver %#x",ctx->ecx);
}
static void unsupported(X86Ctx *ctx) {
    (void)ctx;
    rt_fatal("platform compatibility adapter has no method %s; no host call made",rt_thunk_name(rt_self_last_target()));
}
static void *method(Proxy *p,unsigned slot){
#ifdef OA_STEAM_BRIDGE
return oa_remote_method(p->host,p->version,slot);
#else
return oa_offline_method(p->host,p->version,slot);
#endif
}
static uint32_t make_proxy(void *host,const char *version);
static void get_three(X86Ctx *ctx,unsigned slot) {
    Proxy *p=self(ctx);const char *v=rt_str(ARG(2));
    if(strcmp(p->version,"SteamClient017"))rt_fatal("Steam getter on unexpected interface");
    void *h=((void*(*)(void*,int,int,const char*))method(p,slot))(p->host,(int)ARG(0),(int)ARG(1),v);
    rt_log("platform compatibility interface %s: %s",v,h?"available":"unavailable");
    RET(h?make_proxy(h,v):0);
}
#define GETTER(N) static void get_##N(X86Ctx *ctx){get_three(ctx,N);}
GETTER(5) GETTER(8) GETTER(10) GETTER(11) GETTER(13) GETTER(15)
GETTER(16) GETTER(17) GETTER(18) GETTER(23) GETTER(25) GETTER(26)
GETTER(27) GETTER(28) GETTER(29) GETTER(30) GETTER(34) GETTER(35) GETTER(36)
static void get_utils(X86Ctx *ctx) {
    Proxy *p=self(ctx);const char *v=rt_str(ARG(1));
    void *h=((void*(*)(void*,int,const char*))method(p,9))(p->host,(int)ARG(0),v);
    rt_log("platform compatibility interface %s: %s",v,h?"available":"unavailable");RET(h?make_proxy(h,v):0);
}
static void controller_init(X86Ctx*ctx){Proxy*p=self(ctx);bool ok=((bool(*)(void*))method(p,0))(p->host);rt_log("platform controller Init -> %u",ok);RET(ok);}
static void controller_shutdown(X86Ctx*ctx){Proxy*p=self(ctx);RET(((bool(*)(void*))method(p,1))(p->host));}
static void controller_frame(X86Ctx*ctx){Proxy*p=self(ctx);((void(*)(void*))method(p,2))(p->host);}
static void controller_connected(X86Ctx*ctx){Proxy*p=self(ctx);uint64_t handles[16];int n=((int(*)(void*,uint64_t*))method(p,3))(p->host,handles);if(n<0||n>16)rt_fatal("Steam controller handle bound");for(int i=0;i<n;i++){st32(ARG(0)+i*8,(uint32_t)handles[i]);st32(ARG(0)+i*8+4,(uint32_t)(handles[i]>>32));}RET(n);}
static void controller_action_handle(X86Ctx*ctx,unsigned slot){Proxy*p=self(ctx);uint64_t h=((uint64_t(*)(void*,const char*))method(p,slot))(p->host,rt_str(ARG(0)));ctx->eax=(uint32_t)h;ctx->edx=(uint32_t)(h>>32);}
static void controller_set_handle(X86Ctx*ctx){controller_action_handle(ctx,5);}
static void controller_digital_handle(X86Ctx*ctx){controller_action_handle(ctx,12);}
static void controller_analog_handle(X86Ctx*ctx){controller_action_handle(ctx,15);}
/* SteamUtils009 slot 26 is IsSteamInBigPictureMode in the game's ABI. */
static void utils_big_picture(X86Ctx *ctx)
{
#ifdef OA_STEAM_BRIDGE
    Proxy *p = self(ctx);
    RET(((bool(*)(void *))method(p,26))(p->host));
#else
    RET(0); /* Offline Android has no Steam Big Picture session. */
#endif
}
/* The original text-entry path falls back when the Steam gamepad overlay
 * cannot be shown; preserve that branch in offline Android. */
static void utils_gamepad_text_input(X86Ctx *ctx)
{
#ifdef OA_STEAM_BRIDGE
    Proxy *p = self(ctx);
    RET(((bool(*)(void *,int,int,const char *,uint32_t,const char *))method(p,20))(
        p->host,(int)ARG(0),(int)ARG(1),rt_str(ARG(2)),ARG(3),
        ARG(4)?rt_str(ARG(4)):NULL));
#else
    RET(0);
#endif
}
static void dlc_installed(X86Ctx*ctx){Proxy*p=self(ctx);RET(((bool(*)(void*,uint32_t))method(p,7))(p->host,ARG(0)));}
static void subscribed(X86Ctx *ctx){
    Proxy *p=self(ctx);
    bool ok=((bool(*)(void*,uint32_t))method(p,6))(p->host,ARG(0));
    rt_log("MILESTONE local base-game check app %u -> %s",ARG(0),ok?"owned":"not owned");RET(ok);
}
static void user_id(X86Ctx*ctx){Proxy*p=self(ctx);uint64_t id=((uint64_t(*)(void*))symbol("SteamAPI_ISteamUser_GetSteamID"))(p->host);uint32_t g=ARG(0);st32(g,(uint32_t)id);st32(g+4,(uint32_t)(id>>32));RET(g);rt_log("platform compatibility user identity returned via original hidden-result ABI (identity not logged)");}
static void user_logged_on(X86Ctx*ctx){Proxy*p=self(ctx);RET(((bool(*)(void*))symbol("SteamAPI_ISteamUser_BLoggedOn"))(p->host));}
static uint32_t guest_text(const char*s){if(!s)return 0;size_t n=strlen(s)+1;if(n>1048576)rt_fatal("Steam text outside probe bound");uint32_t g=rt_heap_alloc(n,0);memcpy(rt_ptr(g),s,n);return g;}
static void achievement_name(X86Ctx*ctx){RET(guest_text(((const char*(*)(void*,uint32_t))symbol("SteamAPI_ISteamUserStats_GetAchievementName"))(self(ctx)->host,ARG(0))));}
static void achievement_count(X86Ctx*ctx){RET(((uint32_t(*)(void*))symbol("SteamAPI_ISteamUserStats_GetNumAchievements"))(self(ctx)->host));}
static void achievement_display(X86Ctx*ctx){RET(guest_text(((const char*(*)(void*,const char*,const char*))symbol("SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute"))(self(ctx)->host,rt_str(ARG(0)),rt_str(ARG(1)))));}
static void achievement_get(X86Ctx*ctx){bool value=false;bool ok=((bool(*)(void*,const char*,bool*))symbol("SteamAPI_ISteamUserStats_GetAchievement"))(self(ctx)->host,rt_str(ARG(0)),&value);if(ARG(1))st8(ARG(1),value);RET(ok);}
static void current_stats(X86Ctx*ctx){RET(((bool(*)(void*))symbol("SteamAPI_ISteamUserStats_RequestCurrentStats"))(self(ctx)->host));}
#ifndef OA_STEAM_BRIDGE
static void stat_get(X86Ctx *ctx, uint32_t type)
{
    uint32_t bits;
    bool ok = ARG(0) && ARG(1) && read_local_stat(rt_str(ARG(0)), type, &bits);
    if (ok) st32(ARG(1), bits);
    RET(ok);
}
static void stat_set(X86Ctx *ctx, uint32_t type)
{
    const char *name = ARG(0) ? rt_str(ARG(0)) : NULL;
    bool ok = write_local_stat(name, type, ARG(1));
    static unsigned logged;
    unsigned mask = 1u << type;
    if (!(__atomic_fetch_or(&logged, mask, __ATOMIC_RELAXED) & mask))
        rt_log("OFFLINE: SetStat type=%s name=%s accepted=%u", type == STAT_FLOAT ? "float" : "int",
               name ? name : "(null)", ok);
    RET(ok);
}
static void stat_get_int(X86Ctx *ctx) { stat_get(ctx, STAT_INT32); }
static void stat_get_float(X86Ctx *ctx) { stat_get(ctx, STAT_FLOAT); }
static void stat_set_int(X86Ctx *ctx) { stat_set(ctx, STAT_INT32); }
static void stat_set_float(X86Ctx *ctx) { stat_set(ctx, STAT_FLOAT); }
static void stats_store(X86Ctx *ctx)
{
    bool ok = commit_local_stats();
    rt_log("OFFLINE: StoreStats local result=%u", ok);
    RET(ok);
}
/* Leaderboards are remote-only. Complete the original async polling path with
 * an empty local result so its UI can leave the pending state without Steam. */
typedef struct { uint64_t handle; int callback; } OfflineApiCall;
static OfflineApiCall offline_calls[64];
static uint64_t next_offline_call = 1;
static pthread_mutex_t offline_call_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t begin_offline_call(int callback)
{
    pthread_mutex_lock(&offline_call_lock);
    unsigned slot = 0;
    while (slot < 64 && offline_calls[slot].handle) ++slot;
    if (slot == 64) { pthread_mutex_unlock(&offline_call_lock); rt_fatal("offline API call bound"); }
    uint64_t handle = next_offline_call++;
    if (!handle) handle = next_offline_call++;
    offline_calls[slot] = (OfflineApiCall){handle, callback};
    pthread_mutex_unlock(&offline_call_lock);
    return handle;
}
static void return_offline_call(X86Ctx *ctx, int callback)
{
    uint64_t handle = begin_offline_call(callback);
    ctx->eax = (uint32_t)handle;
    ctx->edx = (uint32_t)(handle >> 32);
}
static void leaderboard_find(X86Ctx *ctx)
{
    if (ARG(0)) (void)rt_str(ARG(0));
    return_offline_call(ctx, 1104); /* LeaderboardFindResult_t */
    static unsigned logged;
    if (!logged++) rt_log("OFFLINE: leaderboards unavailable; returning empty results locally");
}
static void leaderboard_download(X86Ctx *ctx)
{
    (void)ARG(0); (void)ARG(1); (void)ARG(2); (void)ARG(3); (void)ARG(4);
    return_offline_call(ctx, 1105); /* LeaderboardScoresDownloaded_t */
}
static uint64_t offline_call_arg(X86Ctx *ctx)
{
    return (uint64_t)ARG(0) | ((uint64_t)ARG(1) << 32);
}
static void utils_api_call_completed(X86Ctx *ctx)
{
    uint64_t handle = offline_call_arg(ctx);
    bool found = false;
    pthread_mutex_lock(&offline_call_lock);
    for (unsigned i = 0; i < 64; ++i) if (offline_calls[i].handle == handle && handle) { found = true; break; }
    pthread_mutex_unlock(&offline_call_lock);
    if (ARG(2)) st8(ARG(2), !found);
    RET(found);
}
static void utils_api_call_result(X86Ctx *ctx)
{
    uint64_t handle = offline_call_arg(ctx);
    uint32_t out = ARG(2), bytes = ARG(3);
    int callback = (int)ARG(4);
    bool valid = out && ((callback == 1104 && bytes == 16) || (callback == 1105 && bytes == 24));
    bool found = false;
    if (valid) {
        pthread_mutex_lock(&offline_call_lock);
        for (unsigned i = 0; i < 64; ++i) if (offline_calls[i].handle == handle && handle && offline_calls[i].callback == callback) {
            offline_calls[i].handle = 0;
            found = true;
            break;
        }
        pthread_mutex_unlock(&offline_call_lock);
    }
    if (ARG(5)) st8(ARG(5), !found);
    if (found) memset(rt_ptr(out), 0, bytes); /* not found / zero entries */
    RET(found);
}
#endif
static void remote_count(X86Ctx*ctx){Proxy*p=self(ctx);int n=((int(*)(void*))method(p,18))(p->host);rt_log("local platform file count=%d",n);RET(n);}
static void remote_name(X86Ctx*ctx){Proxy*p=self(ctx);int bytes=0;const char*s=((const char*(*)(void*,int,int*))method(p,19))(p->host,(int)ARG(0),&bytes);if(ARG(1))st32(ARG(1),(uint32_t)bytes);RET(guest_text(s));}
static void remote_exists(X86Ctx*ctx){Proxy*p=self(ctx);RET(((bool(*)(void*,const char*))method(p,13))(p->host,rt_str(ARG(0))));}
static void remote_size(X86Ctx*ctx){Proxy*p=self(ctx);RET(((int(*)(void*,const char*))method(p,15))(p->host,rt_str(ARG(0))));}
static void remote_write(X86Ctx*ctx){Proxy*p=self(ctx);int n=(int)ARG(2);if(n<0||n>64*1024*1024)rt_fatal("local save write bound");RET(((bool(*)(void*,const char*,const void*,int))method(p,0))(p->host,rt_str(ARG(0)),ARG(1)?rt_ptr(ARG(1)):NULL,n));}
static void remote_delete(X86Ctx*ctx){Proxy*p=self(ctx);RET(((bool(*)(void*,const char*))method(p,6))(p->host,rt_str(ARG(0))));}
static void remote_read(X86Ctx*ctx){Proxy*p=self(ctx);int n=(int)ARG(2);if(n<0||n>64*1024*1024)rt_fatal("Steam remote read size outside probe bound");RET(((int(*)(void*,const char*,void*,int))method(p,1))(p->host,rt_str(ARG(0)),ARG(1)?rt_ptr(ARG(1)):NULL,n));}
static void remote_quota(X86Ctx*ctx){Proxy*p=self(ctx);RET(((bool(*)(void*,uint64_t*,uint64_t*))method(p,20))(p->host,(uint64_t*)rt_ptr(ARG(0)),(uint64_t*)rt_ptr(ARG(1))));}
static void remote_timestamp(X86Ctx*ctx){Proxy*p=self(ctx);uint64_t t=((uint64_t(*)(void*,const char*))method(p,16))(p->host,rt_str(ARG(0)));ctx->eax=(uint32_t)t;ctx->edx=(uint32_t)(t>>32);}
static void remote_account_cloud(X86Ctx*ctx){Proxy*p=self(ctx);RET(((bool(*)(void*))method(p,21))(p->host));}
static void remote_app_cloud(X86Ctx*ctx){Proxy*p=self(ctx);RET(((bool(*)(void*))method(p,22))(p->host));}
/* The original UI can request friend and DLC-store overlays. Offline Android
 * has no overlay host; these void calls must return without opening one. */
static void overlay_to_user(X86Ctx*ctx){
#ifdef OA_STEAM_BRIDGE
    Proxy*p=self(ctx);uint64_t id=(uint64_t)ARG(1)|((uint64_t)ARG(2)<<32);
    ((void(*)(void*,const char*,uint64_t))method(p,29))(p->host,rt_str(ARG(0)),id);
#else
    (void)ctx;static unsigned logged;if(!logged++){rt_log("OFFLINE: friend overlay request ignored");}
#endif
}
static void overlay_to_store(X86Ctx*ctx){
#ifdef OA_STEAM_BRIDGE
    Proxy*p=self(ctx);((void(*)(void*,uint32_t,uint32_t))method(p,31))(p->host,ARG(0),ARG(1));
#else
    (void)ctx;static unsigned logged;if(!logged++){rt_log("OFFLINE: store overlay request ignored");}
#endif
}
static void local_presence_denied(X86Ctx*ctx){(void)rt_str(ARG(0));(void)rt_str(ARG(1));static int logged;if(!logged++){rt_log("DIAGNOSTIC POLICY: Steam SetRichPresence rejected locally; returning SDK failure, no status published");}RET(0);}
static void persona(X86Ctx*ctx){Proxy*p=self(ctx);const char*s=((const char*(*)(void*))symbol("SteamAPI_ISteamFriends_GetPersonaName"))(p->host);if(!s){RET(0);return;}size_t n=strlen(s)+1;if(n>4096)rt_fatal("Steam persona exceeds bound");uint32_t g=rt_heap_alloc(n,0);memcpy(rt_ptr(g),s,n);RET(g);}
static void language(X86Ctx *ctx){
    Proxy *p=self(ctx);const char *s=((const char*(*)(void*))method(p,4))(p->host);
    if(!s){RET(0);return;}
    size_t n=strlen(s)+1;if(n>256)rt_fatal("unexpected Steam language length");
    uint32_t g=rt_heap_alloc((uint32_t)n,0);memcpy(rt_ptr(g),s,n);RET(g);
}
static void set_method(Proxy *p,unsigned slot,HleFn fn,unsigned nargs){p->methods[slot].fn=fn;p->methods[slot].nargs=(uint8_t)nargs;}
static uint32_t make_proxy(void *host,const char *version){
    for(unsigned i=0;i<nproxies;i++)if(proxies[i].host==host && !strcmp(proxies[i].version,version))return proxies[i].guest;
    if(nproxies==64)rt_fatal("Steam proxy limit");
    Proxy *p=&proxies[nproxies++];p->host=host;snprintf(p->version,sizeof p->version,"%s",version);
    uint32_t vt=rt_heap_alloc(128*4,1);p->guest=rt_heap_alloc(8,1);st32(p->guest,vt);
    for(unsigned i=0;i<128;i++){
        snprintf(p->names[i],sizeof p->names[i],"%s.slot%u",version,i);
        p->methods[i]=(HleDef){"oa-platform-local",p->names[i],unsupported,0};
    }
    if(!strcmp(version,"SteamClient017")){
#define SETGET(N) set_method(p,N,get_##N,3)
        SETGET(5);SETGET(8);SETGET(10);SETGET(11);SETGET(13);SETGET(15);
        SETGET(16);SETGET(17);SETGET(18);SETGET(23);SETGET(25);SETGET(26);
        SETGET(27);SETGET(28);SETGET(29);SETGET(30);SETGET(34);SETGET(35);SETGET(36);
        set_method(p,9,get_utils,2);
    }else if(!strcmp(version,"STEAMREMOTESTORAGE_INTERFACE_VERSION014")){
        #ifndef OA_STEAM_BRIDGE
        set_method(p,0,remote_write,3);set_method(p,6,remote_delete,1);
#endif
        set_method(p,1,remote_read,3);set_method(p,13,remote_exists,1);set_method(p,15,remote_size,1);set_method(p,16,remote_timestamp,1);set_method(p,18,remote_count,0);set_method(p,19,remote_name,2);set_method(p,20,remote_quota,2);set_method(p,21,remote_account_cloud,0);set_method(p,22,remote_app_cloud,0);
    }else if(!strcmp(version,"SteamController006")){
        set_method(p,0,controller_init,0);set_method(p,1,controller_shutdown,0);set_method(p,2,controller_frame,0);set_method(p,3,controller_connected,1);set_method(p,5,controller_set_handle,1);set_method(p,12,controller_digital_handle,1);set_method(p,15,controller_analog_handle,1);
    }else if(!strcmp(version,"SteamUtils009")){
#ifndef OA_STEAM_BRIDGE
        set_method(p,11,utils_api_call_completed,3);
        set_method(p,13,utils_api_call_result,6);
#endif
        set_method(p,20,utils_gamepad_text_input,5);
        set_method(p,26,utils_big_picture,0);
    }else if(!strcmp(version,"STEAMUSERSTATS_INTERFACE_VERSION011")){
        set_method(p,0,current_stats,0);
#ifndef OA_STEAM_BRIDGE
        set_method(p,1,stat_get_int,2);
        set_method(p,2,stat_get_float,2);
        set_method(p,3,stat_set_int,2);
        set_method(p,4,stat_set_float,2);
        set_method(p,10,stats_store,0);
        set_method(p,23,leaderboard_find,1);
        set_method(p,28,leaderboard_download,5);
#endif
        set_method(p,6,achievement_get,2);set_method(p,12,achievement_display,2);set_method(p,14,achievement_count,0);set_method(p,15,achievement_name,1);
    }else if(!strcmp(version,"SteamFriends015")){set_method(p,0,persona,0);set_method(p,29,overlay_to_user,3);set_method(p,31,overlay_to_store,2);set_method(p,43,local_presence_denied,2);
    }else if(!strcmp(version,"SteamUser019")){set_method(p,2,user_id,1);set_method(p,1,user_logged_on,0);
    }else if(!strcmp(version,"STEAMAPPS_INTERFACE_VERSION008")){
        set_method(p,6,subscribed,1);set_method(p,7,dlc_installed,1);set_method(p,4,language,0);
    }
    rt_hle_register(p->methods,128);
    for(unsigned i=0;i<128;i++)st32(vt+4*i,rt_hle_thunk("oa-platform-local",p->names[i]));
    return p->guest;
}
static void restart(X86Ctx *ctx){
    bool r=((bool(*)(uint32_t))symbol("SteamAPI_RestartAppIfNecessary"))(ARG(0));
    rt_log("platform compatibilityAPI_RestartAppIfNecessary(%u) -> %u",ARG(0),r);RET(r);
}
static void init(X86Ctx *ctx){
    bool ok=((bool(*)(void))symbol("SteamAPI_Init"))();initialized=ok;
    rt_log("platform compatibilityAPI_Init -> %s",ok?"platform ready":"failed");RET(ok);
}
static void shutdown_hle(X86Ctx *ctx){(void)ctx;shutdown_native();}
static void huser(X86Ctx *ctx){RET(((int(*)(void))symbol("SteamAPI_GetHSteamUser"))());}
static void hpipe(X86Ctx *ctx){RET(((int(*)(void))symbol("SteamAPI_GetHSteamPipe"))());}
static void create_interface(X86Ctx *ctx){
    const char *v=rt_str(ARG(0));
    if(strcmp(v,"SteamClient017"))rt_fatal("unqualified Steam root version %s",v);
    void *h=((void*(*)(const char*))symbol("SteamInternal_CreateInterface"))(v);
    rt_log("platform compatibility root %s: %s",v,h?"available":"unavailable");RET(h?make_proxy(h,v):0);
}
static void context_init(X86Ctx *ctx){
    if(!initialized)rt_fatal("platform context before initialization");
    uint32_t data=ARG(0),context=data+8;
    if(ld32(data+4)!=1){
        uint32_t callback=ld32(data);
        X86Ctx saved=*ctx;rt_call(ctx,callback,1,&context);*ctx=saved;
        st32(data+4,1);
        rt_log("original Steam context callback %#x executed",callback);
    }
    RET(context);
}
#include "steam_callbacks.inc"
/* These are cdecl exports: nargs=0 because original callers pop arguments. */
static const HleDef exports[]={
 {"steam_api.dll","SteamAPI_RegisterCallback",register_callback,0},
 {"steam_api.dll","SteamAPI_UnregisterCallback",unregister_callback,0},
 {"steam_api.dll","SteamAPI_RunCallbacks",run_callbacks,0},
 {"steam_api.dll","SteamAPI_RestartAppIfNecessary",restart,0},
 {"steam_api.dll","SteamAPI_Init",init,0},
 {"steam_api.dll","SteamAPI_Shutdown",shutdown_hle,0},
 {"steam_api.dll","SteamAPI_GetHSteamUser",huser,0},
 {"steam_api.dll","SteamAPI_GetHSteamPipe",hpipe,0},
 {"steam_api.dll","SteamInternal_CreateInterface",create_interface,0},
 {"steam_api.dll","SteamInternal_ContextInit",context_init,0},
};
void oa_steam_register(void){rt_hle_register(exports,sizeof(exports)/sizeof(*exports));atexit(shutdown_native);}
