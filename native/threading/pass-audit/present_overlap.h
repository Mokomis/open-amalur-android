#ifndef OA_PRESENT_OVERLAP_H
#define OA_PRESENT_OVERLAP_H
#include <stdbool.h>
#include <stdint.h>

void oa_present_mode(bool enabled);
void oa_present_draw_ready(bool completed);
void oa_present_producer_begin(void);
void oa_present_presenter_wait(uint32_t caller);
bool oa_present_draw_join(void);
void oa_present_host_return(void);
void oa_present_ack(void);
void oa_present_cancel(void);

#endif
