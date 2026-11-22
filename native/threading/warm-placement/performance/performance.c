#include "present_overlap.h"
#include "oa_draw_rcpc_control.h"
#include "oa_math_rcpc_control.h"
#include "oa_vis_prepass.h"
#include "rcpc_loop_control.h"
#include "rcpc_copy_control.h"
#include "rcpc_copy_capture.h"
/* Low-overhead, local-only frame measurements and runtime diagnostic switch. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "rt.h"
int oa_diagnostics_enabled;
static char draw_math_rcpc_path[2048];
static char math_rcpc_path[2048];
int oa_present_headroom_mailbox;
extern void oa_sync_set_mode(int), oa_sync_report(void);
extern int oa_sync_get_mode(void);
extern void oa_sync_set_wake_one(int);
extern int oa_sync_get_wake_one(void);
static char visibility_prepass_control_path[2048],visibility_prepass_validate_path[2048],visibility_prepass_workers_path[2048];
static char rcpc_loop_control_path[2048];
static char rcpc_copy_control_path[2048],rcpc_copy_capture_path[2048];
static char sync_control[2048],sync_mode_control[2048],semaphore_wake_one_control[2048];

int oa_fast_mapping=1;
int oa_fast_map_registry=1;
int oa_fast_draw_preparation=1;
int oa_fast_pass_batches=1;
int oa_fast_handwritten_draw=0;
int oa_fast_engine_cores=0;
int oa_adpf_mode=0;
extern unsigned oa_adpf_active_mask;
extern uint64_t oa_adpf_reports[2];
static char adpf_disable_control[2048];
static char engine_core_control[2048];
static char texture_copy_control[2048];
extern int oa_texture_copy_mode;
static void read_engine_cores(void) {
 FILE *file = fopen(engine_core_control,"r");
 int ch = file ? fgetc(file) : EOF;
 if (file) fclose(file);
 __atomic_store_n(&oa_fast_engine_cores, ch >= '1' && ch <= '5' ? ch - '0' : 0, __ATOMIC_RELAXED);
}
static char handwritten_draw_enable[2048];
void oa_handwritten_draw_report(void);

static char pass_batch_control[2048];
void oa_batch_report(void);

static void read_pass_batch_control(void) {
    __atomic_store_n(&oa_fast_pass_batches, access(pass_batch_control,F_OK)!=0, __ATOMIC_RELAXED);
}

int oa_direct_constant_upload=1;
static char upload_control[2048];
void oa_upload_report(void);

static void read_upload_control(void)
{
    const int enabled = access(upload_control, F_OK) != 0;
    __atomic_store_n(&oa_direct_constant_upload, enabled, __ATOMIC_RELAXED);
}

static char draw_control[2048];

static void read_draw_control(void)
{
    const int enabled = access(draw_control, F_OK) != 0;
    __atomic_store_n(&oa_fast_draw_preparation, enabled, __ATOMIC_RELAXED);
}

uint64_t oa_rpc_total_ns,oa_rpc_total_calls;
static char control[2048],mapping_control[2048],registry_control[2048];
static FILE*metrics;
static uint64_t previous,last_report,previous_cpu,frame,total_frames;
static uint64_t rpc_previous,rpc_calls_previous,draw_previous;
static double intervals[8192],cpu_sum;
static unsigned count;
static uint64_t ns(clockid_t id){struct timespec t;clock_gettime(id,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static int order(const void*a,const void*b){double x=*(const double*)a,y=*(const double*)b;return(x>y)-(x<y);}
static char overlap_control[2048];
static void control_read(void){
 FILE *texture_file = fopen(texture_copy_control,"r");
 int texture_char = texture_file ? fgetc(texture_file) : EOF;
 if (texture_file) fclose(texture_file);
 int texture_mode = texture_char >= '0' && texture_char <= '3' ? texture_char - '0' : 1;
 int previous_texture_mode = __atomic_exchange_n(&oa_texture_copy_mode,texture_mode,__ATOMIC_RELAXED);
 if (previous_texture_mode!=texture_mode)
     rt_log("TEXTURE_COPY_CONTROL mode=%d ns=%llu",texture_mode,(unsigned long long)ns(CLOCK_MONOTONIC));
 oa_draw_rcpc_set_mode(access(draw_math_rcpc_path,F_OK)==0);
 oa_math_rcpc_set_mode(access(math_rcpc_path,F_OK)==0);
 unsigned vis_workers=2;FILE *vis_file=fopen(visibility_prepass_workers_path,"r");
 if(vis_file){int requested;if(fscanf(vis_file,"%d",&requested)==1&&requested>=0&&requested<=2)vis_workers=(unsigned)requested;fclose(vis_file);}
 oa_visibility_prepass_set_workers(vis_workers);
 oa_visibility_prepass_control(access(visibility_prepass_control_path,F_OK)==0,access(visibility_prepass_validate_path,F_OK)==0);
 int sync_mode=access(sync_control,F_OK)==0;
 FILE *sync_file=fopen(sync_mode_control,"r");
 if(sync_file){int requested;if(fscanf(sync_file,"%d",&requested)==1 && requested>=0 && requested<=3)sync_mode=requested;fclose(sync_file);}
 oa_sync_set_mode(sync_mode);
 oa_sync_set_wake_one(access(semaphore_wake_one_control,F_OK)==0);
 oa_rcpc_copy_set_mode(access(rcpc_copy_control_path,F_OK)==0);
 oa_rcpc_loop_set_mode(access(rcpc_loop_control_path,F_OK)==0);
 oa_rcpc_copy_capture_poll(access(rcpc_copy_capture_path,F_OK)==0,"rcpc-copy-capture.json");

 /* Android performance hints are on by default; an adpf-disable file turns them off. */
 __atomic_store_n(&oa_adpf_mode, access(adpf_disable_control,F_OK)!=0, __ATOMIC_RELAXED);
 read_engine_cores();
 __atomic_store_n(&oa_fast_handwritten_draw, access(handwritten_draw_enable, F_OK)==0, __ATOMIC_RELAXED);
read_pass_batch_control();read_draw_control();read_upload_control();int registry=access(registry_control,F_OK)!=0;int prev_registry=__atomic_exchange_n(&oa_fast_map_registry,registry,__ATOMIC_RELAXED);if(prev_registry!=registry)rt_log("PERF active-range map registry %s",registry?"ON":"OFF");int fast=access(mapping_control,F_OK)!=0;int was=__atomic_exchange_n(&oa_fast_mapping,fast,__ATOMIC_RELAXED);if(was!=fast)rt_log("PERF fast mapping %s",fast?"ON":"OFF");
 struct stat marker;
 int wanted = stat(control, &marker) == 0 && S_ISREG(marker.st_mode);
 int old = __atomic_exchange_n(&oa_diagnostics_enabled, wanted, __ATOMIC_RELAXED);
 if (old != wanted) rt_log("PERF diagnostic instrumentation %s", wanted ? "ON" : "OFF");
}
void oa_perf_start(const char*root){
 snprintf(overlap_control,sizeof overlap_control,"%s/sim-memory-enable",root);
 snprintf(texture_copy_control,sizeof texture_copy_control,"%s/combat-texture-copy",root);
 snprintf(draw_math_rcpc_path,sizeof draw_math_rcpc_path,"%s/draw-math-rcpc",root);
 snprintf(math_rcpc_path,sizeof math_rcpc_path,"%s/math-loop-rcpc",root);
 snprintf(visibility_prepass_workers_path,sizeof visibility_prepass_workers_path,"%s/visibility-prepass-workers",root);
 snprintf(visibility_prepass_control_path,sizeof visibility_prepass_control_path,"%s/visibility-prepass-enable",root);
 snprintf(visibility_prepass_validate_path,sizeof visibility_prepass_validate_path,"%s/visibility-prepass-validate",root);
 char present_control[2048];
 snprintf(present_control,sizeof present_control,"%s/present-headroom-mailbox",root);
 oa_present_headroom_mailbox=access(present_control,F_OK)==0;
 rt_log("PRESENT startup-only headroom=%d; fps counts completed render-submit iterations including deferred offscreen frames; displayed FPS unmeasured",oa_present_headroom_mailbox);
 snprintf(rcpc_loop_control_path,sizeof rcpc_loop_control_path,"%s/rcpc-loop",root);
 snprintf(rcpc_copy_control_path,sizeof rcpc_copy_control_path,"%s/rcpc-copy",root);
 snprintf(rcpc_copy_capture_path,sizeof rcpc_copy_capture_path,"%s/rcpc-copy-capture",root);
 snprintf(sync_control,sizeof sync_control,"%s/targeted-sync-enable",root);
 snprintf(sync_mode_control,sizeof sync_mode_control,"%s/sync-mode",root);
 snprintf(semaphore_wake_one_control,sizeof semaphore_wake_one_control,"%s/semaphore-wake-one",root);

 snprintf(adpf_disable_control,sizeof adpf_disable_control,"%s/adpf-disable",root);
 snprintf(engine_core_control,sizeof engine_core_control,"%s/fast-engine-cores",root);
 snprintf(handwritten_draw_enable,sizeof handwritten_draw_enable,"%s/handwritten-draw-enable",root);

 snprintf(pass_batch_control, sizeof pass_batch_control, "%s/pass-batch-reference", root);

 snprintf(upload_control, sizeof upload_control, "%s/upload-copy-reference", root);

 snprintf(draw_control, sizeof draw_control, "%s/draw-preparation-reference", root);
 /* The crash reporter owns the diagnostics directory. Tracing is opt-in. */
 snprintf(control, sizeof control, "%s/enable-engine-tracing", root);
 snprintf(mapping_control,sizeof mapping_control,"%s/mapping-compat",root);snprintf(registry_control,sizeof registry_control,"%s/mapping-scan-compat",root);metrics=fopen("performance.jsonl","w");if(metrics)setvbuf(metrics,NULL,_IOFBF,65536);control_read();rt_log("PERF native benchmark mode; original function tracing=%d; build=%s",oa_diagnostics_enabled,
#ifdef OA_BUILD_PROFILE
OA_BUILD_PROFILE
#else
"O1"
#endif
);}
void oa_perf_frame(uint64_t draws,unsigned width,unsigned height){uint64_t now=ns(CLOCK_MONOTONIC),cpu=ns(CLOCK_THREAD_CPUTIME_ID);total_frames++;
 static uint64_t last_overlap_poll;
 if (now-last_overlap_poll>=250000000u) {
     last_overlap_poll=now;
     oa_present_mode(access(overlap_control,F_OK)==0);
 }
 if(!previous){previous=last_report=now;previous_cpu=cpu;draw_previous=draws;return;}
 double dt=(now-previous)*1e-6;previous=now;cpu_sum+=(cpu-previous_cpu)*1e-6;previous_cpu=cpu;if(count<8192)intervals[count++]=dt;frame++;
 if(now-last_report<5000000000ull)return;
 uint64_t rn=__atomic_load_n(&oa_rpc_total_ns,__ATOMIC_RELAXED),rc=__atomic_load_n(&oa_rpc_total_calls,__ATOMIC_RELAXED);
 double elapsed=(now-last_report)*1e-9;double ordered[8192];memcpy(ordered,intervals,count*sizeof(double));qsort(ordered,count,sizeof(double),order);
 if(metrics){fprintf(metrics,"{\"monotonic_ns\":%llu,\"total_frames\":%llu,\"frames\":%llu,\"seconds\":%.6f,\"fps\":%.3f,\"fps_kind\":\"render_submit_loop\",\"p50_ms\":%.3f,\"p95_ms\":%.3f,\"render_thread_cpu_ms_per_frame\":%.3f,\"rpc_ms_per_frame\":%.3f,\"rpc_calls_per_frame\":%.3f,\"draws_per_frame\":%.1f,\"width\":%u,\"height\":%u,\"fast_mapping\":%d,\"fast_map_registry\":%d,\"fast_draw\":%d,\"direct_upload\":%d,\"fast_pass_batch\":%d,\"fast_handwritten_draw\":%d,\"fast_engine_cores\":%d,\"sync_mode\":%d,\"semaphore_wake_one\":%d,\"rcpc_copy\":%d,\"rcpc_copy_seen\":%d,\"draw_math_rcpc\":%d,\"draw_math_rcpc_seen\":%u,\"math_loop_rcpc\":%d,\"math_loop_rcpc_seen\":%u,\"rcpc_loop\":%d,\"rcpc_loop_seen\":%d,\"visibility_prepass\":%d,\"visibility_prepass_workers\":%u,\"visibility_prepass_seen\":%d,\"visibility_prepass_batches\":%llu,\"visibility_prepass_shapes\":%llu,\"visibility_prepass_helper_shapes\":%llu,\"visibility_prepass_validated\":%llu,\"visibility_prepass_mismatches\":%llu,\"adpf\":%d,\"adpf_target_ns\":16666667,\"adpf_sessions\":%u,\"adpf_draw_reports\":%llu,\"adpf_sim_reports\":%llu,\"diagnostics\":%d,\"build\":\"%s\"}\n",(unsigned long long)now,(unsigned long long)total_frames,(unsigned long long)frame,elapsed,frame/elapsed,ordered[count/2],ordered[(count-1)*95/100],cpu_sum/frame,(rn-rpc_previous)*1e-6/frame,(rc-rpc_calls_previous)/(double)frame,(draws-draw_previous)/(double)frame,width,height,__atomic_load_n(&oa_fast_mapping,__ATOMIC_RELAXED),__atomic_load_n(&oa_fast_map_registry,__ATOMIC_RELAXED),__atomic_load_n(&oa_fast_draw_preparation,__ATOMIC_RELAXED),__atomic_load_n(&oa_direct_constant_upload,__ATOMIC_RELAXED),__atomic_load_n(&oa_fast_pass_batches,__ATOMIC_RELAXED),__atomic_load_n(&oa_fast_handwritten_draw,__ATOMIC_RELAXED),__atomic_load_n(&oa_fast_engine_cores,__ATOMIC_RELAXED),oa_sync_get_mode(),oa_sync_get_wake_one(),oa_rcpc_copy_get_mode(),oa_rcpc_copy_get_seen(),oa_draw_rcpc_get_mode(),oa_draw_rcpc_get_seen(),oa_math_rcpc_get_mode(),oa_math_rcpc_get_seen(),oa_rcpc_loop_get_mode(),oa_rcpc_loop_get_seen(),oa_visibility_prepass_mode(),oa_visibility_prepass_workers(),oa_visibility_prepass_seen(),(unsigned long long)oa_visibility_prepass_batches(),(unsigned long long)oa_visibility_prepass_shapes(),(unsigned long long)oa_visibility_prepass_helper_shapes(),(unsigned long long)oa_visibility_prepass_validated(),(unsigned long long)oa_visibility_prepass_mismatches(),__atomic_load_n(&oa_adpf_mode,__ATOMIC_RELAXED),__atomic_load_n(&oa_adpf_active_mask,__ATOMIC_RELAXED),(unsigned long long)__atomic_load_n(&oa_adpf_reports[0],__ATOMIC_RELAXED),(unsigned long long)__atomic_load_n(&oa_adpf_reports[1],__ATOMIC_RELAXED),__atomic_load_n(&oa_diagnostics_enabled,__ATOMIC_RELAXED),
#ifdef OA_BUILD_PROFILE
OA_BUILD_PROFILE
#else
"O1"
#endif
 );fflush(metrics);}
 oa_visibility_prepass_report();
 oa_sync_report();
 oa_upload_report();
 oa_batch_report();
 oa_handwritten_draw_report();
 last_report=now;frame=0;count=0;cpu_sum=0;rpc_previous=rn;rpc_calls_previous=rc;draw_previous=draws;control_read();}
