#include "x86rt.h"
#ifndef OA_RCPC_FIXTURE
#include "oa_probe_fast.h"
#else
void oa_probe_enter(X86Ctx *ctx,uint32_t pc);
#endif
#include "oa_qualified_sse.h"
#include "rcpc_copy_control.h"
#if defined(__aarch64__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#ifdef OA_RCPC_TEST_FORCE_CAPS
#error Test-only forced capabilities must never be used on ARM
#endif
#endif
unsigned oa_rcpc_copy_control;
static int rcpc_copy_seen;
void oa_rcpc_copy_capture_sample(X86Ctx *ctx);
void oa_rcpc_copy_set_mode(int wanted) {
    int supported=0;
#if defined(__aarch64__)
    unsigned long cap=getauxval(AT_HWCAP);
    supported=(cap&HWCAP_LRCPC)&&(cap&HWCAP_ILRCPC);
#elif defined(OA_RCPC_TEST_FORCE_CAPS)
    supported=1;
#endif
    unsigned old=__atomic_load_n(&oa_rcpc_copy_control,__ATOMIC_RELAXED),next;
    do {next=(old&~1u)|(unsigned)(wanted&&supported);} while(!__atomic_compare_exchange_n(&oa_rcpc_copy_control,&old,next,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
}
int oa_rcpc_copy_get_mode(void){return (int)(__atomic_load_n(&oa_rcpc_copy_control,__ATOMIC_RELAXED)&1u);}
int oa_rcpc_copy_get_seen(void){return __atomic_load_n(&rcpc_copy_seen,__ATOMIC_RELAXED);}
static inline void note_rcpc_success(void){
    if(!__atomic_load_n(&rcpc_copy_seen,__ATOMIC_RELAXED))__atomic_store_n(&rcpc_copy_seen,1,__ATOMIC_RELAXED);
}
#define OA_RCPC_NOTE_SUCCESS() note_rcpc_success()
#include "rcpc_rep.h"
void oa_rcpc_copy_original(X86Ctx *ctx);
void f_011BDEA0(X86Ctx *ctx);
__attribute__((noinline)) void oa_rcpc_copy_candidate(X86Ctx *X86_RESTRICT ctx);

#ifdef OA_RCPC_FIXTURE
#define OA_TEXTURE_COPY_LOG(...) ((void)0)
#else
#include "rt.h"
#define OA_TEXTURE_COPY_LOG rt_log
#endif
#include <time.h>

/* 0 uses the existing copy. 1 enables the qualified bulk copy.
 * Diagnostic modes 2 and 3 time the original and bulk loops respectively. */
int oa_texture_copy_mode;
unsigned oa_texture_copy_hits;

static uint64_t texture_copy_time_ns(void)
{
#ifdef OA_RCPC_FIXTURE
    return 0;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
#endif
}

static int try_texture_copy(const X86Ctx *ctx, uint32_t src, uint32_t dst,
                            uint32_t words, int backwards)
{
    int mode = __atomic_load_n(&oa_texture_copy_mode, __ATOMIC_RELAXED);
    uint64_t bytes = (uint64_t)words * 4u;
    if (!mode || backwards || bytes < 16384u || ctx->esp < 24u ||
        ctx->esp > UINT32_MAX - 16u || !((X86_PTR(src) | X86_PTR(dst)) & 3u) ||
        (uint64_t)src + bytes > UINT32_MAX || (uint64_t)dst + bytes > UINT32_MAX ||
        !((uint64_t)src + bytes <= dst || (uint64_t)dst + bytes <= src))
        return 0;

    uint32_t stack_begin = ctx->esp - 24u;
    uint32_t stack_end = ctx->esp + 16u;
    if (!((uint64_t)src + bytes <= stack_begin || src >= stack_end) ||
        !((uint64_t)dst + bytes <= stack_begin || dst >= stack_end) ||
        lds32(ctx->esp) != 0x00c5aac4u)
        return 0;

    /* This return PC follows the disjoint copy into the freshly allocated
     * texture buffer. The caller initializes the texture and frees the buffer
     * after this call. Keep the original register advances and tail byte copy. */
    uint64_t start = mode >= 2 ? texture_copy_time_ns() : 0;
    if (mode == 1 || mode == 3) {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        memcpy((void *)X86_PTR(dst), (const void *)X86_PTR(src), (size_t)bytes);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
    } else {
        for (uint32_t offset = 0; offset < bytes; offset += 4u)
            st32(dst + offset, ld32(src + offset));
    }
    unsigned hits = __atomic_add_fetch(&oa_texture_copy_hits, 1u, __ATOMIC_RELAXED);
    if (mode >= 2) {
        uint64_t end = texture_copy_time_ns();
        OA_TEXTURE_COPY_LOG("TEXTURE_GUEST_COPY mode=%d bytes=%llu ns=%llu duration_ns=%llu",
                            mode, (unsigned long long)bytes, (unsigned long long)start,
                            (unsigned long long)(end - start));
        (void)end;
    } else if (hits == 1) {
        OA_TEXTURE_COPY_LOG("TEXTURE_GUEST_COPY_ON bytes=%llu",
                            (unsigned long long)bytes);
    }
    (void)start;
    return 1;
}

#define OA_TEXTURE_FORWARD_REP(src, dst, words, direction) do {          \
    if (try_texture_copy(ctx, (src), (dst), (words), (direction))) {     \
        uint32_t bytes = (words) * 4u;                                   \
        (src) += bytes;                                                  \
        (dst) += bytes;                                                  \
        (words) = 0;                                                     \
    } else {                                                             \
        OA_RCPC_REP((src), (dst), (words), (direction));                  \
    }                                                                    \
} while (0)

#define f_011BDD20 oa_rcpc_copy_candidate
#include "BDD20_texture.inc"
#undef f_011BDD20
void f_011BDD20(X86Ctx *X86_RESTRICT ctx){
    unsigned control=__atomic_load_n(&oa_rcpc_copy_control,__ATOMIC_RELAXED);
    if(control&2u)oa_rcpc_copy_capture_sample(ctx);
    if(control&1u)oa_rcpc_copy_candidate(ctx);
    else oa_rcpc_copy_original(ctx);
}
