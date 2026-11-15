
#ifndef OA_DRAW_RCPC_CONTROL_H
#define OA_DRAW_RCPC_CONTROL_H
extern unsigned oa_draw_rcpc_control,oa_draw_rcpc_seen;
extern int oa_diagnostics_enabled;
void oa_draw_rcpc_set_mode(int wanted);
int oa_draw_rcpc_get_mode(void);
unsigned oa_draw_rcpc_get_seen(void);
static inline int oa_draw_rcpc_allowed(void){
 if(!__atomic_load_n(&oa_draw_rcpc_control,__ATOMIC_RELAXED)||__atomic_load_n(&oa_diagnostics_enabled,__ATOMIC_RELAXED))return 0;
#if defined(__aarch64__)
 unsigned long fpcr;__asm__ volatile("mrs %0, fpcr":"=r"(fpcr));if(fpcr&0x9f00u)return 0;
#endif
 return 1;
}
static inline void oa_draw_rcpc_note(unsigned bit){
 if(!(__atomic_load_n(&oa_draw_rcpc_seen,__ATOMIC_RELAXED)&bit))__atomic_fetch_or(&oa_draw_rcpc_seen,bit,__ATOMIC_RELAXED);
}
#endif
