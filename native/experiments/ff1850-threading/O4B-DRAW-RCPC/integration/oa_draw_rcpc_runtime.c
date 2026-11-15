#include "x86rt.h"

#include "oa_draw_rcpc_control.h"
#if defined(__aarch64__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#ifdef OA_DRAW_TEST_FORCE_CAPS
#error Test capability bypass is forbidden on ARM
#endif
#endif
unsigned oa_draw_rcpc_control,oa_draw_rcpc_seen;
void oa_draw_rcpc_set_mode(int wanted){
 int supported=0;
#if defined(__aarch64__)
 unsigned long cap=getauxval(AT_HWCAP);supported=(cap&HWCAP_LRCPC)&&(cap&HWCAP_ILRCPC);
#elif defined(OA_DRAW_TEST_FORCE_CAPS)
 supported=1;
#endif
 __atomic_store_n(&oa_draw_rcpc_control,wanted&&supported,__ATOMIC_RELAXED);
}
int oa_draw_rcpc_get_mode(void){return __atomic_load_n(&oa_draw_rcpc_control,__ATOMIC_RELAXED);}
unsigned oa_draw_rcpc_get_seen(void){return __atomic_load_n(&oa_draw_rcpc_seen,__ATOMIC_RELAXED);}

/* ONLY C99640's calls returning to C9967D/C996C6 may use these wrappers.
 * Admission reads host controls/FPCR only: no extra guest stack or pointer read. */
void f_00AC4440(X86Ctx*),f_00ABC070(X86Ctx*);
void oa_draw_candidate_f_00AC4440(X86Ctx*),oa_draw_candidate_f_00ABC070(X86Ctx*);
void oa_draw_c99640_matrices(X86Ctx *ctx){
 if(oa_draw_rcpc_allowed()){oa_draw_rcpc_note(1u);oa_draw_candidate_f_00AC4440(ctx);}else f_00AC4440(ctx);
}
void oa_draw_c99640_multiply(X86Ctx *ctx){
 if(oa_draw_rcpc_allowed()){oa_draw_rcpc_note(2u);oa_draw_candidate_f_00ABC070(ctx);}else f_00ABC070(ctx);
}
