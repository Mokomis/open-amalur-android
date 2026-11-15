#ifndef OA_PROBE_FAST_H
#define OA_PROBE_FAST_H
#include "x86rt.h"
extern int oa_diagnostics_enabled;
void oa_probe_slow_enter(X86Ctx*,uint32_t);
/* Disabled diagnostics cost one predictable local branch, without a host call. */
static inline void oa_probe_enter(X86Ctx*ctx,uint32_t address){
 if(__builtin_expect(__atomic_load_n(&oa_diagnostics_enabled,__ATOMIC_RELAXED),0))oa_probe_slow_enter(ctx,address);
}
#endif
