#ifndef OA_RCPC_COPY_CAPTURE_H
#define OA_RCPC_COPY_CAPTURE_H
void oa_rcpc_copy_capture_poll(int requested,const char *output_path);
unsigned oa_rcpc_copy_capture_count(void);
unsigned oa_rcpc_copy_capture_claimed(void);
#endif
