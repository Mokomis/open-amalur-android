/* Capture THIS reference CPU's RCP/RSQRT approximation, not game outputs.
 * Scan every mantissa; refuse a compact table if low-bit invariance is absent.
 * Build with MSVC x64: cl /O2 sse_profile.c /Fe:sse-profile.exe
 * Run: sse-profile.exe sse_profile.h
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <intrin.h>
#include <xmmintrin.h>

static uint32_t hardware(uint32_t bits, int kind) {
    float f; __m128 x; uint32_t result;
    memcpy(&f, &bits, 4); x = _mm_set_ss(f);
    x = kind ? _mm_rsqrt_ss(x) : _mm_rcp_ss(x);
    f = _mm_cvtss_f32(x); memcpy(&result, &f, 4); return result;
}

int main(int argc, char **argv) {
    int cpu[4]; uint32_t *a; FILE *out;
    if (argc != 2) return 2;
    __cpuid(cpu, 1);
    a = (uint32_t *)malloc((1u << 23) * 4);
    out = fopen(argv[1], "w"); if (!a || !out) return 2;
    fprintf(out, "/* Reference CPUID(1).EAX = 0x%08x; generated from CPU instructions. */\n", cpu[0]);
    for (int kind = 0; kind < 3; kind++) {
        uint32_t base = kind == 2 ? 0x40000000u : 0x3f800000u;
        unsigned shift = 23;
        for (uint32_t m = 0; m < (1u << 23); m++) {
            a[m] = hardware(base | m, kind != 0);
            if (m && a[m] != a[m-1]) {
                unsigned bit = 0; while (((m >> bit) & 1) == 0) bit++;
                if (bit < shift) shift = bit;
            }
        }
        if (shift < 8) { fprintf(stderr, "profile too large, kind %d shift %u\n", kind, shift); return 3; }
        fprintf(out, "#define OA_APPROX_SHIFT_%d %u\nstatic const uint32_t oa_approx_%d[%u] = {\n", kind, shift, kind, 1u << (23-shift));
        for (uint32_t m = 0; m < (1u << 23); m += 1u << shift)
            fprintf(out, "0x%08xu,%s", a[m], ((m >> shift) & 7) == 7 ? "\n" : "");
        fprintf(out, "};\n");
        printf("kind %d all 8388608 mantissas checked; shift %u entries %u\n", kind, shift, 1u << (23-shift));
    }
    fclose(out); free(a); return 0;
}
