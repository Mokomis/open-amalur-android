#ifndef OA_VIS_PREPASS_H
#define OA_VIS_PREPASS_H
#include <stdint.h>
#include "x86rt.h"
enum { OA_VP_MAX_SHAPES=8192, OA_VP_MAX_PLANES=16 };
typedef struct {
 uintptr_t base;
 uint32_t count,shape,mask,plane_count,mode,scale_bits;
 uint32_t eye[3],planes[OA_VP_MAX_PLANES*4];
} OaVpInput;
unsigned oa_vp_predicate(const OaVpInput*,uint32_t shape);
const uint8_t *oa_visibility_prepass_prepare(X86Ctx*);
const uint8_t *oa_vp_prepare_input(const OaVpInput*);
void oa_visibility_prepass_finish(void);
void oa_visibility_prepass_control(int enabled,int validate);
void oa_visibility_prepass_report(void);
int oa_visibility_prepass_mode(void);
int oa_visibility_prepass_seen(void);
uint64_t oa_visibility_prepass_batches(void);
uint64_t oa_visibility_prepass_shapes(void);
uint64_t oa_visibility_prepass_helper_shapes(void);
uint64_t oa_visibility_prepass_validated(void);
uint64_t oa_visibility_prepass_mismatches(void);
void oa_visibility_prepass_shutdown(void);
#endif
