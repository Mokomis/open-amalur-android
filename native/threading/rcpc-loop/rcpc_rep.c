#include "x86rt.h"
/* Private helper ABI: caller proves host alignment and no 32-bit guest wrap.
   rt_mem_init establishes x86_mem_base before workers; it is stable for their lifetime. */
void oa_rcpc_rep_words(uint32_t src,uint32_t dst,uint32_t words,int backwards) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const uintptr_t base=X86_MEM_BASE;
    uintptr_t source=base+(uintptr_t)src,destination=base+(uintptr_t)dst;
    const uintptr_t step=backwards?(uintptr_t)-4:(uintptr_t)4;
    while(words) {
        uint32_t value=__atomic_load_n((const uint32_t*)source,__ATOMIC_ACQUIRE);
        __atomic_store_n((uint32_t*)destination,value,__ATOMIC_RELEASE);
        source+=step;destination+=step;words--;
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
