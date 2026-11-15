#ifndef OA_RCPC_REP_H
#define OA_RCPC_REP_H
#include "x86rt.h"
void oa_rcpc_rep_words(uint32_t src,uint32_t dst,uint32_t words,int backwards);
#ifndef OA_RCPC_MIN_WORDS
#define OA_RCPC_MIN_WORDS 8u
#endif
static inline int oa_rcpc_rep_eligible(uint32_t src,uint32_t dst,uint32_t words,int backwards) {
    uint64_t bytes=(uint64_t)words*4u;
    if(words<OA_RCPC_MIN_WORDS || bytes>UINT32_MAX || ((X86_PTR(src)|X86_PTR(dst))&3u))return 0;
    if(backwards)return bytes<=(uint64_t)src && bytes<=(uint64_t)dst && src<=UINT32_MAX-4u && dst<=UINT32_MAX-4u;
    return (uint64_t)src+bytes<=UINT32_MAX && (uint64_t)dst+bytes<=UINT32_MAX;
}
#ifndef OA_RCPC_NOTE_SUCCESS
#define OA_RCPC_NOTE_SUCCESS() ((void)0)
#endif
#define OA_RCPC_REP(src_,dst_,words_,df_) do { \
    if(oa_rcpc_rep_eligible((src_),(dst_),(words_),(df_))) { \
        oa_rcpc_rep_words((src_),(dst_),(words_),(df_)); \
        OA_RCPC_NOTE_SUCCESS(); \
        uint32_t oa_rcpc_delta_=(words_)*4u; \
        (src_)+=(df_)?0u-oa_rcpc_delta_:oa_rcpc_delta_; \
        (dst_)+=(df_)?0u-oa_rcpc_delta_:oa_rcpc_delta_; (words_)=0; \
    } else { \
        while(words_) { st32((dst_),ld32(src_)); \
            (src_)+=(df_)?4294967292u:4u; (dst_)+=(df_)?4294967292u:4u; (words_)--; } \
    } \
} while(0)
#endif
