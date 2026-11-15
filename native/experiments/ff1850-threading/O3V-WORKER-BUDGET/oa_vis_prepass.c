
#define _GNU_SOURCE
#include "oa_vis_prepass.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <xmmintrin.h>
typedef CRITICAL_SECTION Mutex;typedef CONDITION_VARIABLE Cond;typedef HANDLE Thread;
static Mutex lock;static Cond work_cv[2],done_cv;static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK init_once(PINIT_ONCE a,PVOID b,PVOID*c){(void)a;(void)b;(void)c;InitializeCriticalSection(&lock);InitializeConditionVariable(&work_cv[0]);InitializeConditionVariable(&work_cv[1]);InitializeConditionVariable(&done_cv);return TRUE;}
#define INIT() InitOnceExecuteOnce(&once,init_once,0,0)
#define LOCK() EnterCriticalSection(&lock)
#define UNLOCK() LeaveCriticalSection(&lock)
#define WAIT(c) SleepConditionVariableCS(&(c),&lock,INFINITE)
#define WAKE(c) WakeAllConditionVariable(&(c))
#define THREAD_FN DWORD WINAPI
#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_cv[2]={PTHREAD_COND_INITIALIZER,PTHREAD_COND_INITIALIZER},done_cv=PTHREAD_COND_INITIALIZER;
typedef pthread_t Thread;
#define INIT() ((void)0)
#define LOCK() pthread_mutex_lock(&lock)
#define UNLOCK() pthread_mutex_unlock(&lock)
#define WAIT(c) pthread_cond_wait(&(c),&lock)
#define WAKE(c) pthread_cond_broadcast(&(c))
#define THREAD_FN void*
#endif
extern void rt_log(const char*,...) __attribute__((format(printf,1,2)));
extern int oa_diagnostics_enabled;
extern void f_00C98FE0(X86Ctx*);
#ifndef _WIN32
extern int oa_engine_original_allowed(unsigned,void*,size_t);
#endif
static int enabled,seen,faulted,busy;
static unsigned requested_helpers=2,active_helpers;
static unsigned validate_left,validate_marker;
static uint64_t batches_total,shapes_total,helper_total,validated_total,mismatch_total;
static Thread workers[2];
static unsigned initialized,shutdown_flag,generation,pending,worker_ready,worker_ok;
static unsigned begin[3],end[3],worker_status[2];
static uint32_t fp_control;
static OaVpInput job;
static uint8_t results[OA_VP_MAX_SHAPES];
#ifndef _WIN32
static cpu_set_t helper_mask,original_mask;
static int tids[2];
#endif
static uint32_t fp_get_control(void){
#if defined(__aarch64__)
 uint64_t v;__asm__ volatile("mrs %0, fpcr":"=r"(v));return (uint32_t)v;
#else
 return _mm_getcsr()&~63u;
#endif
}
static uint32_t fp_get_status(void){
#if defined(__aarch64__)
 uint64_t v;__asm__ volatile("mrs %0, fpsr":"=r"(v));return (uint32_t)v;
#else
 return _mm_getcsr()&63u;
#endif
}
static void fp_set(uint32_t control,uint32_t status){
#if defined(__aarch64__)
 uint64_t a=control,b=status;__asm__ volatile("msr fpcr, %0\nmsr fpsr, %1"::"r"(a),"r"(b));
#else
 _mm_setcsr(control|(status&63u));
#endif
}
static int fp_supported(uint32_t control){
#if defined(__aarch64__)
 return !(control&0x9f00u);
#else
 return (control&0x1f80u)==0x1f80u;
#endif
}
static void evaluate(unsigned b,unsigned e){
#ifdef OA_VP_FIXTURE_GATE
 extern void oa_vp_fixture_gate(void);oa_vp_fixture_gate();
#endif
 for(unsigned k=b;k<e;++k)results[k]=(uint8_t)oa_vp_predicate(&job,job.shape+80*k);}
#ifndef _WIN32
static void mask_text(const cpu_set_t*m,char*out,size_t n){size_t p=0;for(unsigned k=0;k<CPU_SETSIZE;++k)if(CPU_ISSET(k,m)){int z=snprintf(out+p,n-p,"%s%u",p?",":"",k);if(z<0||(size_t)z>=n-p)break;p+=(size_t)z;}if(!p&&n)out[0]=0;}
static int select_mask(void){
 if(oa_engine_original_allowed(1,&original_mask,sizeof original_mask))return 0;
 unsigned high=0,second=0,values[CPU_SETSIZE]={0};
 for(unsigned k=0;k<CPU_SETSIZE;++k)if(CPU_ISSET(k,&original_mask)){
  char path[96];snprintf(path,sizeof path,"/sys/devices/system/cpu/cpu%u/cpu_capacity",k);FILE*f=fopen(path,"r");
  if(!f)return 0;int ok=fscanf(f,"%u",&values[k])==1;fclose(f);if(!ok||!values[k])return 0;
  if(values[k]>high){second=high;high=values[k];}else if(values[k]<high&&values[k]>second)second=values[k];
 }
 CPU_ZERO(&helper_mask);unsigned target=second?second:high;
 for(unsigned k=0;k<CPU_SETSIZE;++k)if(CPU_ISSET(k,&original_mask)&&values[k]==target)CPU_SET(k,&helper_mask);
 return CPU_COUNT(&helper_mask)>=2&&CPU_COUNT(&original_mask)>=3;
}
#endif
static THREAD_FN worker_main(void*arg){
 unsigned id=(unsigned)(uintptr_t)arg;unsigned last=0;
 unsigned ok=1;
#ifndef _WIN32
 tids[id]=(int)syscall(SYS_gettid);pthread_setname_np(pthread_self(),id?"oa-vis-1":"oa-vis-0");
 cpu_set_t actual;int er=sched_setaffinity(0,sizeof helper_mask,&helper_mask);int gr=sched_getaffinity(0,sizeof actual,&actual);
 ok=!er&&!gr&&CPU_EQUAL(&helper_mask,&actual);
 char allowed[256],applied[256];mask_text(&original_mask,allowed,sizeof allowed);if(!gr)mask_text(&actual,applied,sizeof applied);else strcpy(applied,"unavailable");
 rt_log("VIS_PREPASS worker=%u tid=%d affinity_ok=%u original=%s effective=%s",id,tids[id],ok,allowed,applied);
#endif
 LOCK();worker_ok+=ok;++worker_ready;WAKE(done_cv);
 for(;;){
  while(!shutdown_flag&&(generation==last||id>=active_helpers))WAIT(work_cv[id]);
  if(shutdown_flag){UNLOCK();break;}
  last=generation;unsigned b=begin[id],e=end[id];uint32_t control=fp_control;UNLOCK();
  uint32_t prior_control=fp_get_control(),prior_status=fp_get_status();fp_set(control,0);
  evaluate(b,e);unsigned status=fp_get_status();fp_set(prior_control,prior_status);
  LOCK();worker_status[id]=status;if(--pending==0)WAKE(done_cv);
 }
#ifdef _WIN32
 return 0;
#else
 return NULL;
#endif
}
static int initialize_locked(void){
 if(initialized)return initialized==1;
#ifndef _WIN32
 if(!select_mask()){initialized=2;rt_log("VIS_PREPASS unavailable: no qualified original allowed CPU mask");return 0;}
#endif
 worker_ready=worker_ok=0;unsigned created=0;
 for(unsigned k=0;k<2;++k){
#ifdef _WIN32
  workers[k]=CreateThread(NULL,0,worker_main,(void*)(uintptr_t)k,0,NULL);if(!workers[k])break;
#else
  if(pthread_create(&workers[k],NULL,worker_main,(void*)(uintptr_t)k))break;
#endif
  ++created;
 }
 if(created!=2){shutdown_flag=1;WAKE(work_cv[0]);WAKE(work_cv[1]);UNLOCK();for(unsigned k=0;k<created;++k){
#ifdef _WIN32
  WaitForSingleObject(workers[k],INFINITE);CloseHandle(workers[k]);
#else
  pthread_join(workers[k],NULL);
#endif
 }LOCK();worker_ready=0;initialized=2;return 0;}
 while(worker_ready<2)WAIT(done_cv);
 initialized=worker_ok==2?1:2;
 return initialized==1;
}
const uint8_t *oa_vp_prepare_input(const OaVpInput*input){
 if(!__atomic_load_n(&enabled,__ATOMIC_RELAXED)||input->count<2048||input->count>OA_VP_MAX_SHAPES||input->plane_count>OA_VP_MAX_PLANES)return NULL;
 uint32_t control=fp_get_control();if(!fp_supported(control))return NULL;
 unsigned helpers=__atomic_load_n(&requested_helpers,__ATOMIC_RELAXED);
 int expected=0;if(!__atomic_compare_exchange_n(&busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return NULL;
 INIT();LOCK();
 if(shutdown_flag||!__atomic_load_n(&enabled,__ATOMIC_RELAXED)||(helpers&&!initialize_locked())){__atomic_store_n(&busy,0,__ATOMIC_RELEASE);WAKE(done_cv);UNLOCK();return NULL;}
 job=*input;fp_control=control;active_helpers=helpers;unsigned participants=helpers+1;
 unsigned step=(input->count+participants-1)/participants;
 for(unsigned k=0;k<participants;++k){begin[k]=k*step;end[k]=(k+1)*step;if(end[k]>input->count)end[k]=input->count;}
 pending=helpers;++generation;
 for(unsigned k=0;k<helpers;++k)WAKE(work_cv[k]);
 UNLOCK();evaluate(begin[helpers],end[helpers]);
 LOCK();while(pending)WAIT(done_cv);unsigned status=0;uint64_t helper_shapes=0;
 for(unsigned k=0;k<helpers;++k){status|=worker_status[k];helper_shapes+=end[k]-begin[k];}
 UNLOCK();fp_set(control,fp_get_status()|status);
 __atomic_fetch_add(&helper_total,helper_shapes,__ATOMIC_RELAXED);
 return results;
}
void oa_visibility_prepass_finish(void){INIT();LOCK();__atomic_store_n(&busy,0,__ATOMIC_RELEASE);WAKE(done_cv);UNLOCK();}
static int span(uint32_t a,uint64_t n){return a>=4096&&(uint64_t)a+n<=0xf0000000ull;}
static int overlap(uint32_t a,uint64_t an,uint32_t b,uint64_t bn){return (uint64_t)a+an>b&&(uint64_t)b+bn>a;}
static int validate_original(X86Ctx*ctx,const OaVpInput*input,const uint8_t*flags){
 uint32_t s=ctx->esp;unsigned char saved[512];memcpy(saved,(void*)(x86_mem_base+s-512),512);
 X86Ctx original=*ctx;int ok=1;unsigned index=0;
 memcpy((void*)(x86_mem_base+s-384),input->eye,12);memcpy((void*)(x86_mem_base+s-320),input->planes,16*input->plane_count);
 for(index=0;index<input->count;++index){
  X86Ctx check=original;check.esp=s-448;
  st32(check.esp,0);st32(check.esp+4,input->shape+80*index);st32(check.esp+8,s-384);st32(check.esp+12,s-320);
  st32(check.esp+16,input->plane_count);st32(check.esp+20,input->mask);f_00C98FE0(&check);
  if((check.eax&255)!=flags[index]){ok=0;rt_log("VIS_PREPASS mismatch index=%u original=%u parallel=%u shapes=%u",index,check.eax&255,flags[index],input->count);break;}
 }
 memcpy((void*)(x86_mem_base+s-512),saved,512);*ctx=original;
 __atomic_fetch_add(&validated_total,1,__ATOMIC_RELAXED);
 if(!ok){__atomic_fetch_add(&mismatch_total,1,__ATOMIC_RELAXED);__atomic_store_n(&faulted,1,__ATOMIC_RELAXED);__atomic_store_n(&enabled,0,__ATOMIC_RELAXED);}
 return ok;
}
const uint8_t *oa_visibility_prepass_prepare(X86Ctx*ctx){
 if(!__atomic_load_n(&enabled,__ATOMIC_RELAXED)||__atomic_load_n(&oa_diagnostics_enabled,__ATOMIC_RELAXED))return NULL;
 uint32_t s=ctx->esp;if(s<4096+512||s>0xefffffe0u)return NULL;
 uint32_t ret=lds32(s);if(ret!=0xCE94A8u&&ret!=0xCE9638u)return NULL;
 uint32_t eye=lds32(s+4),frustum=lds32(s+8),mask=lds32(s+12),descriptor=lds32(s+16),out=lds32(s+20);
 if(!span(descriptor,16)||!span(out,16)||!span(eye,12)||!span(frustum,8))return NULL;
 uint32_t count=ld32(descriptor),shape=ld32(descriptor+4),planes=ld32(frustum+4);
 if(count<2048||count>OA_VP_MAX_SHAPES||planes>OA_VP_MAX_PLANES||!span(shape,80ull*count)||!span(frustum,8+16ull*planes)||ld8(0x19FDFDAu)||ld8(0x19FDFD9u))return NULL;
 if(ld16(out+12)!=43||ld32(out+4)>ld32(out+8))return NULL;
 uint32_t allocator=ld32(0x19FBFB4u);if(!span(allocator,156)||ld32(allocator)!=0x172CED8u)return NULL;
 uint32_t parent=ld32(allocator+0x98);if(!span(parent,4)||ld32(parent)!=0x172D1C4u)return NULL;
 uint32_t output=ld32(out),capacity=ld32(out+8);uint64_t bytes=4ull*capacity;
 if(capacity&&(!span(output,bytes)||overlap(output,bytes,shape,80ull*count)||overlap(output,bytes,eye,12)||overlap(output,bytes,frustum,8+16ull*planes)||overlap(output,bytes,descriptor,16)||overlap(output,bytes,out,16)||overlap(output,bytes,s-512,536)))return NULL;
 if(overlap(out,16,shape,80ull*count)||overlap(out,16,eye,12)||overlap(out,16,frustum,8+16ull*planes)||overlap(out,16,descriptor,16))return NULL;
 if(overlap(shape,80ull*count,s-512,536)||overlap(eye,12,s-512,536)||overlap(frustum,8+16ull*planes,s-512,536)||overlap(out,16,s-512,536)||overlap(descriptor,16,s-512,536))return NULL;
 OaVpInput input={0};input.base=x86_mem_base;input.count=count;input.shape=shape;input.mask=mask;input.plane_count=planes;
 input.mode=ld8(0x19661F6u);input.scale_bits=ld32(0x19662D8u);memcpy(input.eye,(void*)(x86_mem_base+eye),12);memcpy(input.planes,(void*)(x86_mem_base+frustum+8),16*planes);
 const uint8_t*flags=oa_vp_prepare_input(&input);if(!flags)return NULL;
 unsigned left=__atomic_load_n(&validate_left,__ATOMIC_RELAXED);
 while(left&&!__atomic_compare_exchange_n(&validate_left,&left,left-1,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
 if(left){if(!validate_original(ctx,&input,flags)){oa_visibility_prepass_finish();return NULL;}}
 __atomic_store_n(&seen,1,__ATOMIC_RELAXED);__atomic_fetch_add(&batches_total,1,__ATOMIC_RELAXED);__atomic_fetch_add(&shapes_total,count,__ATOMIC_RELAXED);
 return flags;
}
void oa_visibility_prepass_control(int on,int validate){
 if(!on)__atomic_store_n(&faulted,0,__ATOMIC_RELAXED);
 int wanted=on&&!__atomic_load_n(&faulted,__ATOMIC_RELAXED);int old=__atomic_exchange_n(&enabled,wanted,__ATOMIC_RELAXED);
 if(validate&&!validate_marker)__atomic_store_n(&validate_left,16,__ATOMIC_RELAXED);validate_marker=validate;
 if(!validate)__atomic_store_n(&validate_left,0,__ATOMIC_RELAXED);
 if(old!=wanted)rt_log("VIS_PREPASS mode=%d helpers=%u threshold=2048 max_shapes=8192",wanted,__atomic_load_n(&requested_helpers,__ATOMIC_RELAXED));
}
void oa_visibility_prepass_set_workers(unsigned count){
 if(count>2)count=2;unsigned old=__atomic_exchange_n(&requested_helpers,count,__ATOMIC_RELAXED);
 if(old!=count)rt_log("VIS_PREPASS workers_requested=%u",count);
}
unsigned oa_visibility_prepass_workers(void){return __atomic_load_n(&requested_helpers,__ATOMIC_RELAXED);}
int oa_visibility_prepass_mode(void){return __atomic_load_n(&enabled,__ATOMIC_RELAXED);}
int oa_visibility_prepass_seen(void){return __atomic_load_n(&seen,__ATOMIC_RELAXED);}
#define GETTER(name,var) uint64_t name(void){return __atomic_load_n(&var,__ATOMIC_RELAXED);}
GETTER(oa_visibility_prepass_batches,batches_total) GETTER(oa_visibility_prepass_shapes,shapes_total)
GETTER(oa_visibility_prepass_helper_shapes,helper_total) GETTER(oa_visibility_prepass_validated,validated_total)
GETTER(oa_visibility_prepass_mismatches,mismatch_total)
void oa_visibility_prepass_report(void){rt_log("VIS_PREPASS_REPORT mode=%d workers_requested=%u seen=%d batches=%llu shapes=%llu helper_shapes=%llu validated=%llu mismatches=%llu validation_left=%u",
 oa_visibility_prepass_mode(),oa_visibility_prepass_workers(),oa_visibility_prepass_seen(),(unsigned long long)oa_visibility_prepass_batches(),(unsigned long long)oa_visibility_prepass_shapes(),(unsigned long long)oa_visibility_prepass_helper_shapes(),(unsigned long long)oa_visibility_prepass_validated(),(unsigned long long)oa_visibility_prepass_mismatches(),__atomic_load_n(&validate_left,__ATOMIC_RELAXED));}
void oa_visibility_prepass_shutdown(void){
 __atomic_store_n(&enabled,0,__ATOMIC_RELAXED);INIT();LOCK();while(__atomic_load_n(&busy,__ATOMIC_ACQUIRE))WAIT(done_cv);
 if(!initialized){shutdown_flag=1;UNLOCK();return;}shutdown_flag=1;WAKE(work_cv[0]);WAKE(work_cv[1]);unsigned join=worker_ready;UNLOCK();
 for(unsigned k=0;k<join;++k){
#ifdef _WIN32
  WaitForSingleObject(workers[k],INFINITE);CloseHandle(workers[k]);
#else
  pthread_join(workers[k],NULL);
#endif
 }worker_ready=0;
}
