/* Compile this file only with armv8-a+rcpc. Host control and all other
 * guest/SSE code remain baseline armv8-a. Preserve the original REP body. */
#include "x86rt.h"
void oa_rcpc_rep_words(uint32_t src,uint32_t dst,uint32_t words,int backwards) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    while(words) {
        st32(dst,ld32(src));
        src += backwards ? 4294967292u : 4u;
        dst += backwards ? 4294967292u : 4u;
        words--;
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
