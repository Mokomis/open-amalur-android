/* Isolated Amalur startup experiment. No game-function substitutes. */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

extern const X86FnEntry x86_fns_amalur[];
extern const uint32_t x86_fns_amalur_count;
extern const X86FnEntry x86_fns_bink[];
extern const uint32_t x86_fns_bink_count;
const RtModuleSpec rt_module_specs[] = {{0}};
const int rt_module_spec_count = 0;
void rt_kernel32_register(void), rt_sync_register(void), rt_files_register(void);
void oa_platform_register(void),oa_environment_register(void),oa_prepare_virtual_profile(void);
void oa_ini_register(void),rt_user32_register(void),oa_find_ex_register(void);
void oa_steam_register(void);
void oa_graphics_register(void);
int oa_reflection_check(X86Ctx*,const char*);
extern uint8_t rt_input_keys[256],rt_input_buttons[8];
extern int32_t rt_input_mouse[3];
void rt_fs_init(const char *, const char *);
int rt_thunk_implemented(uint32_t);
extern int oa_diagnostics_enabled;
static int bounded_startup;
static _Atomic unsigned long long calls;
static _Atomic uint32_t seen[131072];
uint32_t oa_reflect_export;
extern const X86FnEntry x86_fns_compiler43[];
extern const uint32_t x86_fns_compiler43_count;
void oa_crt_register(void);
static FILE *trace;
static uint64_t start_ns;

void oa_probe_slow_enter(X86Ctx *ctx, uint32_t address)
{
    if (!__atomic_load_n(&oa_diagnostics_enabled,__ATOMIC_RELAXED)) return;
    unsigned long long total=atomic_fetch_add_explicit(&calls,1,memory_order_relaxed)+1;
    uint32_t i=(address*2654435761u)>>15;int fresh=0;
    for(unsigned tries=0;tries<131072;tries++,i=(i+1)&131071){uint32_t old=atomic_load_explicit(&seen[i],memory_order_relaxed);if(old==address)break;if(!old && atomic_compare_exchange_strong_explicit(&seen[i],&old,address,memory_order_relaxed,memory_order_relaxed)){fresh=1;break;}if(old==address)break;}
    if (total < 200 || fresh) {
        fprintf(trace,"FUNC %08x ret=%08x esp=%08x ecx=%08x calls=%llu\n",
                address, ld32(ctx->esp),ctx->esp,ctx->ecx,calls);
        fflush(trace);
    }
    if (address == 0x010761F0u) {
        fprintf(trace,"MILESTONE original WinMain reached after %llu translated calls\n",calls);
        fflush(trace);
        if (getenv("OA_STOP_AT_WINMAIN")) exit(42);
    }
    uint64_t now=host_ticks_ns(); /* retained only when original diagnostics are enabled */
    if (bounded_startup && (total > 6000000000ull || now - start_ns > 600000000000ull))
        rt_fatal("bounded startup budget exhausted at %#x after %llu calls",address,calls);
}

void oa_probe_enter(X86Ctx*ctx,uint32_t address){oa_probe_slow_enter(ctx,address);}

/* Refuse the source runtime's optional editor import stubs. */
int rt_stub_for(const char *dll,const char *name,HleFn *fn,uint8_t *nargs,int *data)
{ (void)dll;(void)name;(void)fn;(void)nargs;(void)data;return 0; }

/* These are optional Mirror's Edge diagnostics/settings hooks, disabled here. */
void rt_ue_fov_tick(void) {}
void rt_ue_bones_tick(void) {}
void rt_ue_achievements_tick(void) {}
void rt_ue_font_tick(void) {}
void rt_ue_aspect_tick(void) {}
void rt_ue_dump_names(const char *p) {(void)p;}
void rt_ue_fov_probe(void) {}
void rt_ue_debug_throw(X86Ctx *ctx) {(void)ctx;}

static RtModule*oa_tls_module;
static void setup_static_tls(RtModule *m,RtThread *t)
{
    uint32_t pe=ld32(m->base+0x3c), rva=ld32(m->base+pe+24+96+9*8);
    if (!rva) return;
    uint32_t d=m->base+rva, begin=ld32(d),end=ld32(d+4),index=ld32(d+8);
    uint32_t callbacks=ld32(d+12),zero=ld32(d+16);
    if (end < begin || end-begin+zero > 0x100000) rt_fatal("invalid static TLS bounds");
    uint32_t vector=rt_heap_alloc(4,1),block=rt_heap_alloc(end-begin+zero,1);
    memcpy(rt_ptr(block),rt_ptr(begin),end-begin);
    st32(vector,block);if(ld32(index)!=0)rt_fatal("unexpected PE static TLS slot index");st32(t->teb+0x2c,vector);
    rt_log("probe loader: actual PE static TLS template %u bytes + %u zero, slot 0",end-begin,zero);
    if (callbacks && ld32(callbacks)) rt_fatal("TLS callbacks need separate qualification; stop before entry");
}

void oa_initialize_new_thread_tls(RtThread*t){if(!oa_tls_module)rt_fatal("guest worker created before PE TLS module binding");setup_static_tls(oa_tls_module,t);}

int main(int argc,char **argv)
{
    if(argc!=4 && argc!=5) {fprintf(stderr,"usage: probe <koa.exe> <read-only game root> <private overlay> [--initializer-check]\n");return 2;}
    trace=fopen("function-trace.txt","w");if(!trace)return 3;
    start_ns=host_ticks_ns();bounded_startup=getenv("OA_BOUND_STARTUP")!=NULL;
    host_install_fault_handler();rt_mem_init();
    oa_platform_register();oa_environment_register();oa_crt_register();
    rt_sync_register();rt_files_register();rt_kernel32_register();
    oa_ini_register();oa_find_ex_register();oa_graphics_register();rt_user32_register();
    oa_steam_register();
    rt_fs_init(argv[2],argv[3]);oa_prepare_virtual_profile();
    RtModule *m=rt_load_module(argv[1],"koa.exe",x86_fns_amalur,x86_fns_amalur_count);
    oa_tls_module=m;
    const char*bink_path=getenv("OA_BINK_PATH");RtModule*bink=NULL;
    if(bink_path){bink=rt_load_module(bink_path,"binkw32.dll",x86_fns_bink,x86_fns_bink_count);rt_resolve_imports(bink);}
    RtModule*compiler=NULL;const char*compiler_path=getenv("OA_COMPILER43_PATH");
    if(compiler_path){compiler=rt_load_module(compiler_path,"d3dcompiler_43.dll",x86_fns_compiler43,x86_fns_compiler43_count);rt_resolve_imports(compiler);oa_reflect_export=rt_export(compiler,"D3DReflect");}
    rt_resolve_imports(m);
    RtThread *t=rt_thread_create_main(4u<<20);
    st32(rt_peb()+8,m->base);setup_static_tls(m,t);
    if(argc==5 && !strcmp(argv[4],"--reflection-check")){if(!compiler)rt_fatal("original compiler module missing");rt_module_init_tree(compiler,&t->ctx);const char*input=getenv("OA_REFLECTION_INPUT");if(!input)return 9;return oa_reflection_check(&t->ctx,input);}
    if(argc==5 && !strcmp(argv[4],"--initializer-check")) {
        const uint32_t cases[]={0,0x3f800000,0xbf800000,0x3dcccccd,0x4640e6ae,0x00800000,0x00000001,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345};
        FILE *f=fopen("initializer-proof.bin","wb");if(!f)return 4;
        for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++){
            t->ctx.mxcsr=0x1f80;
            st32(0x1951d60,cases[i]);
            rt_call(&t->ctx,0x1355b70,0,NULL);rt_call(&t->ctx,0x1355b90,0,NULL);
            fwrite(&cases[i],4,1,f);fwrite(rt_ptr(0x19f3780),4,1,f);fwrite(rt_ptr(0x19f3b40),16,1,f);
        }
        fclose(f);rt_log("initializer proof complete: actual original routines, 12 inputs");return 0;
    }
    if(!bink)rt_fatal("actual translated Bink module required for startup");
    rt_module_init_tree(m,&t->ctx);
    if(argc==5 && !strcmp(argv[4],"--bink-check")){
        const char*path="C:\\OpenAmalur\\data\\videos\\logo_thq_nordic.bik";uint32_t g=rt_heap_alloc(strlen(path)+1,0);memcpy(rt_ptr(g),path,strlen(path)+1);uint32_t args[]={g,0};uint32_t handle=rt_call(&t->ctx,rt_export(bink,"_BinkOpen@8"),2,args);if(!handle)rt_fatal("translated BinkOpen failed");uint32_t w=ld32(handle),h=ld32(handle+4),frames=ld32(handle+8);if(!w||!h||w>8192||h>8192)rt_fatal("Bink dimensions unqualified");uint32_t frame=frames<61?frames:61,seek[]={handle,frame,0};rt_call(&t->ctx,rt_export(bink,"_BinkGoto@12"),3,seek);rt_call(&t->ctx,rt_export(bink,"_BinkDoFrame@4"),1,&handle);uint32_t bytes=w*h*4,buffer=rt_heap_alloc(bytes,1),copy[]={handle,buffer,w*4,h,0,0,3};uint32_t copied=rt_call(&t->ctx,rt_export(bink,"_BinkCopyToBuffer@28"),7,copy);FILE*f=fopen("bink-translated-frame61.bin","wb");if(!f)return 15;fwrite(rt_ptr(buffer),1,bytes,f);fclose(f);rt_log("BINK_PROOF width=%u height=%u frames=%u requested=%u reported=%u bytes=%u copy_result=%u",w,h,frames,frame,ld32(handle+12),bytes,copied);rt_call(&t->ctx,rt_export(bink,"_BinkClose@4"),1,&handle);return 0;
    }
    rt_log("MILESTONE entering actual executable entry %#x; image is data, not executed x86",m->entry);
    uint32_t result=rt_call(&t->ctx,m->entry,0,NULL);
    rt_log("entry returned %u",result);
    return (int)result;
}
