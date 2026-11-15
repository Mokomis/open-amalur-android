#include "rt.h"
/* Bring-up codec-only entry. These install no fake platform successes; any
   unsupported import invoked by the original code remains a fatal thunk. */

void rt_ue_set_fov(float f){(void)f;} void rt_ue_preview_frame(void){} void rt_ue_present(void){}

void rt_ue_request_turn(int degrees){(void)degrees;rt_fatal("UE3 turn hook not applicable to Amalur");}
