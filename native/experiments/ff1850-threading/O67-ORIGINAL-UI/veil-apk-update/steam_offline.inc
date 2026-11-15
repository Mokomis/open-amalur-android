/* Offline platform compatibility for the owner's original engine. No Steam SDK,
 * network, authentication, cloud sync, or remote ownership claims. File storage
 * operates only on imported working copies inside this Android app. */
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <unistd.h>
static char offline_root[2048],save_root[2048],offline_language[64]="english";
static uint64_t offline_user;
static int offline_ready,stats_pending;
typedef struct{uint32_t flags;char*name,*display,*description;} OfflineAchievement;
static OfflineAchievement achievements[2048];static uint32_t achievement_total;
typedef struct{char name[512];int bytes;uint64_t timestamp;} OfflineSave;
static OfflineSave saves[1024];static int save_count;
static pthread_mutex_t offline_io=PTHREAD_MUTEX_INITIALIZER;
#include "steam_user_stats.inc"
static uint32_t read_u32(FILE*f){uint32_t v;if(fread(&v,4,1,f)!=1)rt_fatal("offline profile truncated");return v;}
static char*read_text(FILE*f,uint32_t n){if(n>1048576)rt_fatal("offline text bound");char*s=calloc((size_t)n+1,1);if(!s||fread(s,1,n,f)!=n)rt_fatal("offline text read failed");return s;}
static int valid_save_name(const char*n){if(!n||!*n||strlen(n)>500||*n=='/'||strchr(n,'\\'))return 0;for(const char*q=n;*q;q++){if((q==n||q[-1]=='/')&&(*q=='.'||*q=='/'))return 0;if((unsigned char)*q<32||*q==':')return 0;}return 1;}
static int save_path(char*p,size_t cap,const char*n){if(!valid_save_name(n))return 0;return snprintf(p,cap,"%s/%s",save_root,n)<(int)cap;}
static int compare_save(const void*a,const void*b){return strcmp(((const OfflineSave*)a)->name,((const OfflineSave*)b)->name);}
static void scan_saves(const char*rel){char path[3072];snprintf(path,sizeof path,"%s/%s",save_root,rel);DIR*d=opendir(path);if(!d)return;struct dirent*e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char name[512],full[3072];if(snprintf(name,sizeof name,"%s%s%s",rel,*rel?"/":"",e->d_name)>=(int)sizeof name)rt_fatal("save name bound");if(!save_path(full,sizeof full,name))rt_fatal("invalid local save path");struct stat st;if(lstat(full,&st))continue;if(S_ISDIR(st.st_mode))scan_saves(name);else if(S_ISREG(st.st_mode)){if(save_count==1024||st.st_size>64*1024*1024)rt_fatal("local save bounds");OfflineSave*s=&saves[save_count++];snprintf(s->name,sizeof s->name,"%s",name);s->bytes=(int)st.st_size;s->timestamp=st.st_mtime;}}closedir(d);}
static void refresh_saves(void){save_count=0;scan_saves("");qsort(saves,save_count,sizeof(*saves),compare_save);}
static bool offline_init(void){if(offline_ready)return true;const char*r=getenv("OA_OFFLINE_ROOT");if(!r)rt_fatal("offline app storage root unavailable");snprintf(offline_root,sizeof offline_root,"%s",r);snprintf(save_root,sizeof save_root,"%s/offline-saves/working",r);char profile[2304];snprintf(profile,sizeof profile,"%s/offline-profile.bin",r);FILE*f=fopen(profile,"rb");if(f){char magic[8];if(fread(magic,1,8,f)!=8||memcmp(magic,"OAOFF01\0",8))rt_fatal("offline profile format");if(fread(&offline_user,8,1,f)!=1)rt_fatal("offline profile user");achievement_total=read_u32(f);uint32_t lang=read_u32(f);if(achievement_total>2048||lang>=sizeof offline_language)rt_fatal("offline profile bounds");if(fread(offline_language,1,lang,f)!=lang)rt_fatal("offline language truncated");offline_language[lang]=0;for(uint32_t i=0;i<achievement_total;i++){OfflineAchievement*a=&achievements[i];a->flags=read_u32(f);uint32_t n=read_u32(f),d=read_u32(f),desc=read_u32(f);a->name=read_text(f,n);a->display=read_text(f,d);a->description=read_text(f,desc);}fclose(f);}else rt_log("OFFLINE: starting a local profile without imported platform metadata");refresh_saves();load_local_stats();offline_ready=1;rt_log("OFFLINE platform ready: %d local save copies, %u imported achievements; Steam and network unused",save_count,achievement_total);return true;}
static bool offline_restart(uint32_t id){if(id!=1041720)rt_fatal("offline target application mismatch");return false;}
static void offline_shutdown(void){/* Private storage remains; no external SDK lifecycle. */}
static int offline_handle(void){return 1;}
static void*offline_root_interface(const char*v){return !strcmp(v,"SteamClient017")?(void*)"SteamClient017":NULL;}
static void*offline_interface(void*h,int user,int pipe,const char*v){(void)h;if(user!=1||pipe!=1||!v||!*v)rt_fatal("offline interface arguments");return (void*)v;}
static void*offline_utils(void*h,int pipe,const char*v){return offline_interface(h,1,pipe,v);}
static bool offline_base_game(void*h,uint32_t id){(void)h;return offline_ready&&id==1041720;}
/* The folder importer verifies the copied archive's hash. At launch,
 * recognize the imported depot by its file format and size. */
static bool offline_dlc(void *handle, uint32_t id)
{
    (void)handle;
    if (!offline_ready || id != 1278260)
        return false;
    const char *game = getenv("OA_GAME_ROOT");
    if (!game)
        return false;
    char path[2304];
    int count = snprintf(path, sizeof path, "%s/data_1278260/dlc_mit_0.pak", game);
    struct stat archive;
    if (count < 0 || (size_t)count >= sizeof path || stat(path, &archive)
            || !S_ISREG(archive.st_mode) || archive.st_size != INT64_C(5098328372))
        return false;

    char magic[4];
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    bool available = fread(magic, 1, sizeof magic, file) == sizeof magic
                  && !memcmp(magic, "KARl", sizeof magic);
    fclose(file);
    rt_log("OFFLINE: local Fatesworn archive %s", available ? "found" : "unrecognized");
    return available;
}
static const char*offline_lang(void*h){(void)h;return offline_language;}
static const char*offline_persona(void*h){(void)h;return "Local player";}
static uint64_t offline_identity(void*h){(void)h;return offline_user;}
static bool offline_logged_on(void*h){(void)h;return false;}
static bool offline_no_controller(void*h){(void)h;return false;}
static void offline_controller_frame(void*h){(void)h;}
static int offline_controllers(void*h,uint64_t*out){(void)h;(void)out;return 0;}
static uint64_t offline_action(void*h,const char*n){(void)h;(void)n;return 0;}
static bool offline_stats(void *h)
{
    (void)h;
    if (!offline_ready) return false;
    pthread_mutex_lock(&stat_lock);
    stats_pending = 1;
    pthread_mutex_unlock(&stat_lock);
    return true;
}
static uint32_t offline_achievement_count(void*h){(void)h;return achievement_total;}
static const char*offline_achievement_name(void*h,uint32_t i){(void)h;return i<achievement_total?achievements[i].name:NULL;}
static OfflineAchievement*achievement(const char*n){for(uint32_t i=0;i<achievement_total;i++)if(!strcmp(n,achievements[i].name))return &achievements[i];return NULL;}
static const char*offline_achievement_display(void*h,const char*n,const char*k){(void)h;OfflineAchievement*a=achievement(n);if(!a)return "";return !strcmp(k,"name")?a->display:!strcmp(k,"desc")?a->description:"";}
static bool offline_achievement_get(void*h,const char*n,bool*v){(void)h;OfflineAchievement*a=achievement(n);*v=a&&(a->flags&2);return a&&(a->flags&1);}
static int offline_count(void*h){(void)h;return save_count;}
static const char*offline_name(void*h,int i,int*bytes){(void)h;if(i<0||i>=save_count){*bytes=0;return NULL;}*bytes=saves[i].bytes;return saves[i].name;}
static bool offline_exists(void*h,const char*n){(void)h;char p[3072];struct stat st;return save_path(p,sizeof p,n)&&!stat(p,&st)&&S_ISREG(st.st_mode);}
static int offline_size(void*h,const char*n){(void)h;char p[3072];struct stat st;return save_path(p,sizeof p,n)&&!stat(p,&st)&&S_ISREG(st.st_mode)?(int)st.st_size:0;}
static uint64_t offline_timestamp(void*h,const char*n){(void)h;char p[3072];struct stat st;return save_path(p,sizeof p,n)&&!stat(p,&st)?(uint64_t)st.st_mtime:0;}
static int offline_read(void*h,const char*n,void*out,int bytes){(void)h;char p[3072];if(bytes<0||bytes>64*1024*1024||!out||!save_path(p,sizeof p,n))return 0;pthread_mutex_lock(&offline_io);FILE*f=fopen(p,"rb");int count=f?(int)fread(out,1,bytes,f):0;if(f)fclose(f);pthread_mutex_unlock(&offline_io);return count;}
static bool offline_quota(void*h,uint64_t*total,uint64_t*avail){(void)h;struct statvfs st;if(statvfs(offline_root,&st))return false;*total=(uint64_t)st.f_blocks*st.f_frsize;*avail=(uint64_t)st.f_bavail*st.f_frsize;return true;}
static bool offline_cloud_disabled(void*h){(void)h;return false;}
static bool offline_write(void*h,const char*n,const void*data,int bytes){(void)h;char p[3072],tmp[3100];if(bytes<0||bytes>64*1024*1024||(!data&&bytes)||!save_path(p,sizeof p,n))return false;pthread_mutex_lock(&offline_io);for(char*q=p+strlen(save_root)+1;*q;q++)if(*q=='/'){*q=0;if(mkdir(p,0700)&&errno!=EEXIST){*q='/';pthread_mutex_unlock(&offline_io);return false;}*q='/';}snprintf(tmp,sizeof tmp,"%s.tmp",p);FILE*f=fopen(tmp,"wb");bool ok=f&&fwrite(data,1,bytes,f)==(size_t)bytes;if(f){if(fflush(f))ok=false;if(fclose(f))ok=false;}if(ok)ok=rename(tmp,p)==0;if(!ok)unlink(tmp);else refresh_saves();pthread_mutex_unlock(&offline_io);return ok;}
static bool offline_delete(void*h,const char*n){(void)h;char p[3072];if(!save_path(p,sizeof p,n))return false;pthread_mutex_lock(&offline_io);bool ok=unlink(p)==0;if(ok)refresh_saves();pthread_mutex_unlock(&offline_io);return ok;}
/* Windows guest layouts from Steamworks SDK: 1101 is 24 bytes, 1102 is 16. */
typedef struct { int user, id; unsigned char *param; int bytes; } OfflineCallback;
static unsigned char stats_event[24];
static int stats_event_id, stats_event_size;
static void offline_manual_init(void) {}

static bool offline_manual_next(int pipe, OfflineCallback *out)
{
    (void)pipe;
    pthread_mutex_lock(&stat_lock);
    if (!stats_event_id && (stats_pending || stored_events_pending)) {
        const uint64_t game = 1041720;
        const uint32_t success = 1;
        memset(stats_event, 0, sizeof stats_event);
        memcpy(stats_event, &game, sizeof game);
        memcpy(stats_event + 8, &success, sizeof success);
        if (stats_pending) {
            memcpy(stats_event + 16, &offline_user, sizeof offline_user);
            stats_event_id = 1101;
            stats_event_size = 24;
            stats_pending = 0;
        } else {
            stats_event_id = 1102;
            stats_event_size = 16;
            --stored_events_pending;
        }
    }
    bool ready = stats_event_id != 0;
    if (ready) *out = (OfflineCallback){1, stats_event_id, stats_event, stats_event_size};
    pthread_mutex_unlock(&stat_lock);
    return ready;
}

static void offline_manual_free(int pipe)
{
    (void)pipe;
    pthread_mutex_lock(&stat_lock);
    stats_event_id = 0;
    pthread_mutex_unlock(&stat_lock);
}

static void offline_manual_run(int pipe) { (void)pipe; }
static void offline_run_callbacks(void) { offline_manual_run(1); }
static void*oa_offline_method(void*h,const char*v,unsigned slot){(void)h;
 if(!strcmp(v,"SteamClient017")){if(slot==9)return(void*)offline_utils;switch(slot){case 5:case 8:case 10:case 11:case 13:case 15:case 16:case 17:case 18:case 23:case 25:case 26:case 27:case 28:case 29:case 30:case 34:case 35:case 36:return(void*)offline_interface;}}
 if(!strcmp(v,"STEAMAPPS_INTERFACE_VERSION008")){if(slot==4)return(void*)offline_lang;if(slot==6)return(void*)offline_base_game;if(slot==7)return(void*)offline_dlc;}
 if(!strcmp(v,"SteamController006")){if(slot==0||slot==1)return(void*)offline_no_controller;if(slot==2)return(void*)offline_controller_frame;if(slot==3)return(void*)offline_controllers;if(slot==5||slot==12||slot==15)return(void*)offline_action;}
 if(!strcmp(v,"STEAMREMOTESTORAGE_INTERFACE_VERSION014")){switch(slot){case 0:return(void*)offline_write;case 1:return(void*)offline_read;case 6:return(void*)offline_delete;case 13:return(void*)offline_exists;case 15:return(void*)offline_size;case 16:return(void*)offline_timestamp;case 18:return(void*)offline_count;case 19:return(void*)offline_name;case 20:return(void*)offline_quota;case 21:case 22:return(void*)offline_cloud_disabled;}}
 rt_fatal("offline platform method not implemented: %s.%u",v,slot);}
static void*oa_offline_symbol(const char*n){
#define OFF_SYMBOL(S,F) if(!strcmp(n,S))return(void*)F
 OFF_SYMBOL("SteamAPI_Init",offline_init);OFF_SYMBOL("SteamAPI_Shutdown",offline_shutdown);OFF_SYMBOL("SteamAPI_RestartAppIfNecessary",offline_restart);OFF_SYMBOL("SteamAPI_GetHSteamUser",offline_handle);OFF_SYMBOL("SteamAPI_GetHSteamPipe",offline_handle);OFF_SYMBOL("SteamInternal_CreateInterface",offline_root_interface);OFF_SYMBOL("SteamAPI_ISteamUser_GetSteamID",offline_identity);OFF_SYMBOL("SteamAPI_ISteamUser_BLoggedOn",offline_logged_on);OFF_SYMBOL("SteamAPI_ISteamFriends_GetPersonaName",offline_persona);OFF_SYMBOL("SteamAPI_ISteamUserStats_GetNumAchievements",offline_achievement_count);OFF_SYMBOL("SteamAPI_ISteamUserStats_GetAchievementName",offline_achievement_name);OFF_SYMBOL("SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute",offline_achievement_display);OFF_SYMBOL("SteamAPI_ISteamUserStats_GetAchievement",offline_achievement_get);OFF_SYMBOL("SteamAPI_ISteamUserStats_RequestCurrentStats",offline_stats);OFF_SYMBOL("SteamAPI_ManualDispatch_Init",offline_manual_init);OFF_SYMBOL("SteamAPI_ManualDispatch_RunFrame",offline_manual_run);OFF_SYMBOL("SteamAPI_ManualDispatch_GetNextCallback",offline_manual_next);OFF_SYMBOL("SteamAPI_ManualDispatch_FreeLastCallback",offline_manual_free);OFF_SYMBOL("SteamAPI_RunCallbacks",offline_run_callbacks);
#undef OFF_SYMBOL
 rt_fatal("offline platform export not implemented: %s",n);}
