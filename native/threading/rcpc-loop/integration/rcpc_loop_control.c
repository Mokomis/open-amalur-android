#include <stdint.h>
#include "rcpc_loop_control.h"
#if defined(__aarch64__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#ifdef OA_RCPC_TEST_FORCE_CAPS
#error Test-only forced capabilities must never be used on ARM
#endif
#endif
void oa_rcpc_rep_words_R(uint32_t,uint32_t,uint32_t,int);
void oa_rcpc_rep_words_T(uint32_t,uint32_t,uint32_t,int);
static int rcpc_loop_enabled,rcpc_loop_seen;
void oa_rcpc_loop_set_mode(int wanted) {
 int supported=0;
#if defined(__aarch64__)
 unsigned long cap=getauxval(AT_HWCAP);
 supported=(cap&HWCAP_LRCPC)&&(cap&HWCAP_ILRCPC);
#elif defined(OA_RCPC_TEST_FORCE_CAPS)
 supported=1;
#endif
 __atomic_store_n(&rcpc_loop_enabled,wanted&&supported,__ATOMIC_RELAXED);
}
int oa_rcpc_loop_get_mode(void){return __atomic_load_n(&rcpc_loop_enabled,__ATOMIC_RELAXED);}
int oa_rcpc_loop_get_seen(void){return __atomic_load_n(&rcpc_loop_seen,__ATOMIC_RELAXED);}
/* Existing R caller retains alignment/wrap/threshold guard and copy feature gate. */
void oa_rcpc_rep_words(uint32_t src,uint32_t dst,uint32_t words,int backwards) {
 if(__atomic_load_n(&rcpc_loop_enabled,__ATOMIC_RELAXED)) {
  oa_rcpc_rep_words_T(src,dst,words,backwards);
  if(!__atomic_load_n(&rcpc_loop_seen,__ATOMIC_RELAXED))__atomic_store_n(&rcpc_loop_seen,1,__ATOMIC_RELAXED);
 } else oa_rcpc_rep_words_R(src,dst,words,backwards);
}
