#ifndef OA_CPU_ADDITIONS_H
#define OA_CPU_ADDITIONS_H
static inline void oa_cpu_pause(void){
#if defined(__i386__) || defined(__x86_64__)
 __builtin_ia32_pause();
#elif defined(__aarch64__)
 __asm__ volatile("yield" ::: "memory");
#else
 __asm__ volatile("" ::: "memory");
#endif
}
static inline uint64_t oa_mmx_psraw(uint64_t a,uint64_t n){
 uint64_t r=0;unsigned shift=n>15?15:(unsigned)n;
 for(unsigned i=0;i<4;i++){int16_t v=(int16_t)(a>>(16*i));r|=(uint64_t)(uint16_t)(v>>shift)<<(16*i);}return r;
}
static inline uint64_t oa_mmx_pmullw(uint64_t a,uint64_t b){
 uint64_t r=0;for(unsigned i=0;i<4;i++){uint32_t x=(uint16_t)(a>>(16*i)),y=(uint16_t)(b>>(16*i));r|=(uint64_t)(uint16_t)(x*y)<<(16*i);}return r;
}
#endif
