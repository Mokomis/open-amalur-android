#define _GNU_SOURCE
#include "x86rt.h"
#include "rcpc_copy_capture.h"
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#endif
extern unsigned oa_rcpc_copy_control;
enum {CAPTURE_LIMIT=4096};
typedef struct {uint32_t pc,dst,src,bytes,sse;unsigned valid,enabled,ready;} CopySample;
static CopySample samples[CAPTURE_LIMIT];
static unsigned claimed,completed;
/* Only the existing performance-poll thread calls poll; sample slots have one owner. */
static int started,written;
static int safe_read(uint32_t addr,void *dest,size_t bytes) {
    if((uint64_t)addr+bytes>UINT64_C(0x100000000))return 0;
#if defined(_WIN32)
    SIZE_T got=0;
    return ReadProcessMemory(GetCurrentProcess(),(const void*)X86_PTR(addr),dest,bytes,&got)&&got==bytes;
#else
    struct iovec local={dest,bytes},remote={(void*)X86_PTR(addr),bytes};
    return syscall(__NR_process_vm_readv,getpid(),&local,1,&remote,1,0)==(long)bytes;
#endif
}
void oa_rcpc_copy_capture_sample(X86Ctx *ctx) {
    unsigned slot=__atomic_load_n(&claimed,__ATOMIC_RELAXED);
    do {if(slot>=CAPTURE_LIMIT)return;}
    while(!__atomic_compare_exchange_n(&claimed,&slot,slot+1,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    CopySample *s=&samples[slot];
    uint32_t args[4]={0};
    s->enabled=__atomic_load_n(&oa_rcpc_copy_control,__ATOMIC_RELAXED)&1u;
    /* Diagnostic-only snapshot uses a fault-contained OS read, never ld32/memcpy of an unknown guest pointer. */
    if(safe_read(ctx->esp,args,sizeof args)) {
        s->pc=args[0];s->dst=args[1];s->src=args[2];s->bytes=args[3];s->valid=1;
        if(safe_read(0x1a0cd64u,&s->sse,sizeof s->sse))s->valid|=2;
    }
    __atomic_store_n(&s->ready,1,__ATOMIC_RELEASE);
    if(__atomic_add_fetch(&completed,1,__ATOMIC_ACQ_REL)==CAPTURE_LIMIT)
        __atomic_fetch_and(&oa_rcpc_copy_control,~2u,__ATOMIC_RELAXED);
}
unsigned oa_rcpc_copy_capture_count(void){return __atomic_load_n(&completed,__ATOMIC_ACQUIRE);}
unsigned oa_rcpc_copy_capture_claimed(void){return __atomic_load_n(&claimed,__ATOMIC_RELAXED);}
void oa_rcpc_copy_capture_poll(int requested,const char *output_path) {
    if(requested&&!started) {started=1;__atomic_fetch_or(&oa_rcpc_copy_control,2u,__ATOMIC_RELAXED);}
    if(!started||written||oa_rcpc_copy_capture_count()!=CAPTURE_LIMIT)return;
    FILE *out=fopen(output_path,"w");if(!out)return;
    fprintf(out,"{\"limit\":4096,\"capture_complete\":true,\"argument_snapshot\":\"fault-contained self-process read before original function\",\"samples\":[");
    for(unsigned i=0;i<CAPTURE_LIMIT;i++) {
        CopySample *s=&samples[i];
        if(!__atomic_load_n(&s->ready,__ATOMIC_ACQUIRE)){fclose(out);return;}
        fprintf(out,"%s{\"pc\":%u,\"dst\":%u,\"src\":%u,\"bytes\":%u,\"sse\":%u,\"valid\":%u,\"enabled\":%u}",i?",":"",s->pc,s->dst,s->src,s->bytes,s->sse,s->valid,s->enabled);
    }
    fputs("]}\n",out);int failed=ferror(out);if(fclose(out))failed=1;if(!failed)written=1;
}
