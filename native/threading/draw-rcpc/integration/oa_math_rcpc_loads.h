
#ifndef OA_MATH_RCPC_LOADS_H
#define OA_MATH_RCPC_LOADS_H
#include "x86rt.h"
static inline void oa_math_full_fence(void){
#if defined(__aarch64__)
 __asm__ volatile("dmb ish":::"memory");
#else
 __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}
static inline uint8_t oa_math_ld8(uint32_t a){
#if defined(__aarch64__)
 uint32_t v;uintptr_t p=X86_PTR(a);
 __asm__ volatile("ldaprb %w0, [%1]":"=r"(v):"r"(p):"memory");return(uint8_t)v;
#else
 return ld8(a);
#endif
}
static inline uint32_t oa_math_ld32(uint32_t a){
#if defined(__aarch64__)
 uintptr_t p=X86_PTR(a);if(p&3)return ld32(a);
 uint32_t v;__asm__ volatile("ldapr %w0, [%1]":"=r"(v):"r"(p):"memory");return v;
#else
 return ld32(a);
#endif
}
static inline uint64_t oa_math_ld64(uint32_t a){
#if defined(__aarch64__)
 uintptr_t p=X86_PTR(a);if(p&7)return ld64(a);
 uint64_t v;__asm__ volatile("ldapr %0, [%1]":"=r"(v):"r"(p):"memory");return v;
#else
 return ld64(a);
#endif
}
static inline float oa_math_ldf(uint32_t a){uint32_t u=oa_math_ld32(a);float f;memcpy(&f,&u,4);return f;}
#endif
