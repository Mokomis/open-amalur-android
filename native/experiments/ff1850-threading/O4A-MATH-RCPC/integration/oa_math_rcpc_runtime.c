#include "x86rt.h"

#include "oa_math_rcpc_control.h"
#if defined(__aarch64__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#ifdef OA_MATH_TEST_FORCE_CAPS
#error Test capability bypass is forbidden on ARM
#endif
#endif
unsigned oa_math_rcpc_control,oa_math_rcpc_seen;
void oa_math_rcpc_set_mode(int wanted){
 int supported=0;
#if defined(__aarch64__)
 unsigned long cap=getauxval(AT_HWCAP);supported=(cap&HWCAP_LRCPC)&&(cap&HWCAP_ILRCPC);
#elif defined(OA_MATH_TEST_FORCE_CAPS)
 supported=1;
#endif
 __atomic_store_n(&oa_math_rcpc_control,wanted&&supported,__ATOMIC_RELAXED);
}
int oa_math_rcpc_get_mode(void){return __atomic_load_n(&oa_math_rcpc_control,__ATOMIC_RELAXED);}
unsigned oa_math_rcpc_get_seen(void){return __atomic_load_n(&oa_math_rcpc_seen,__ATOMIC_RELAXED);}

void f_00AC5770(X86Ctx*),f_00AC4B10(X86Ctx*);
void oa_math_candidate_f_00AC5770(X86Ctx*),oa_math_candidate_f_00AC4B10(X86Ctx*);
void oa_math_loop_compose(X86Ctx *ctx){
 if(oa_math_rcpc_allowed()&&ctx->esp<=0xffffff3fu&&ctx->ecx==ctx->esp+0xc0u){
  oa_math_rcpc_note(1u);oa_math_candidate_f_00AC5770(ctx);
 }else f_00AC5770(ctx);
}
void oa_math_loop_convert(X86Ctx *ctx){
 if(oa_math_rcpc_allowed()&&ctx->esp<=0xffffff43u&&ctx->ecx==ctx->esp+0xbcu){
  oa_math_rcpc_note(2u);oa_math_candidate_f_00AC4B10(ctx);
 }else f_00AC4B10(ctx);
}
