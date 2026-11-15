/* Hardware-profiled SSE approximation backend. Generated tables describe a
 * pinned reference CPU, not a universal x86 implementation. Require independent
 * instruction comparisons before using a profile for another reference host.
 */
#ifndef OA_APPROX_H
#define OA_APPROX_H
#include "sse_profile.h"
INL u32 oa_rcp_bits(u32 x) {
    u32 sign = x & 0x80000000u, e = (x >> 23) & 255, m = x & 0x7fffffu;
    if (!e) return sign | 0x7f800000u;
    if (e == 255) return m ? x | 0x400000u : sign;
    u32 v = oa_approx_0[m >> OA_APPROX_SHIFT_0];
    int exponent = (int)(v >> 23) + 127 - (int)e;
    if (exponent <= 0) return sign;
    if (exponent >= 255) return sign | 0x7f800000u;
    return sign | ((u32)exponent << 23) | (v & 0x7fffffu);
}
INL u32 oa_rsqrt_bits(u32 x) {
    u32 sign = x & 0x80000000u, e = (x >> 23) & 255, m = x & 0x7fffffu;
    if (e == 255 && m) return x | 0x400000u;
    if (!e) return sign | 0x7f800000u;
    if (sign) return 0xffc00000u;
    if (e == 255) return 0;
    u32 v = e & 1 ? oa_approx_1[m >> OA_APPROX_SHIFT_1] : oa_approx_2[m >> OA_APPROX_SHIFT_2];
    int reference = e & 1 ? 127 : 128;
    int exponent = (int)(v >> 23) + (reference - (int)e) / 2;
    return ((u32)exponent << 23) | (v & 0x7fffffu);
}
#endif
