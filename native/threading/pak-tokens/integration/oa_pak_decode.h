#ifndef OA_PAK_DECODE_H
#define OA_PAK_DECODE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const uint8_t *next;
    const uint8_t *end;
    unsigned tag;
} OaPakBits;

/* Tags share the input cursor with literal bytes. The low sentinel moves up
 * as each bit is read. Refill when it reaches bit 7. */
static inline int oa_pak_bit(OaPakBits *bits)
{
    unsigned tag = bits->tag;
    if (tag == 0x80) {
        if (bits->next == bits->end) return -1;
        tag = ((unsigned)*bits->next++ << 1) | 1;
        bits->tag = tag & 0xff;
        return (int)(tag >> 8);
    }
    bits->tag = (tag << 1) & 0xff;
    return (int)(tag >> 7);
}

static inline uint32_t oa_pak_gamma(OaPakBits *bits)
{
    uint32_t value = 1;
    for (;;) {
        int bit, more;
        if (bits->tag & 0x3f) {
            bit = (int)(bits->tag >> 7);
            more = (int)((bits->tag >> 6) & 1);
            bits->tag = (bits->tag << 2) & 0xff;
        } else {
            bit = oa_pak_bit(bits);
            if (bit < 0) return 0;
            more = oa_pak_bit(bits);
        }
        if (value > UINT32_MAX / 2) return 0;
        value = (value << 1) | (unsigned)bit;
        if (more <= 0) return more == 0 ? value : 0;
    }
}

static inline void oa_pak_copy(uint8_t *dest, size_t distance, size_t length)
{
    const uint8_t *source = dest - distance;
    if (length <= 8) {
        for (size_t i = 0; i < length; ++i)
            dest[i] = source[i];
    } else if (distance >= length) {
        memcpy(dest, source, length);
    } else if (distance == 1) {
        memset(dest, *source, length);
    } else {
        memcpy(dest, source, distance);
        size_t copied = distance;
        while (copied < length) {
            size_t chunk = length - copied;
            if (chunk > copied) chunk = copied;
            memcpy(dest + copied, dest, chunk);
            copied += chunk;
        }
    }
}

/* Returns zero on rejection. The caller owns disjoint buffers and only
 * publishes the output after a complete end-terminated block. */
static size_t oa_pak_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t capacity)
{
    if (!src_size || !capacity) return 0;
    OaPakBits bits = {src + 1, src + src_size, 0x80};
    uint8_t *out = dst;
    uint8_t *end = dst + capacity;
    uint32_t previous_distance = 0;
    int previous_match = 0;
    *out++ = src[0];

    for (;;) {
        int first = oa_pak_bit(&bits);
        if (first < 0) return 0;
        if (first == 0) {
            /* The sentinel stops the zero run before the next tag byte. */
            if (bits.tag < 0x80) {
                unsigned extra = (unsigned)__builtin_clz(bits.tag << 24);
                unsigned count = extra + 1;
                if (count > (size_t)(bits.end - bits.next) ||
                    count > (size_t)(end - out)) return 0;
                memcpy(out, bits.next, count);
                bits.next += count;
                out += count;
                bits.tag = (bits.tag << extra) & 0xff;
            } else {
                if (bits.next == bits.end || out == end) return 0;
                *out++ = *bits.next++;
            }
            previous_match = 0;
            continue;
        }

        uint32_t distance, length;
        int second = oa_pak_bit(&bits);
        if (second < 0) return 0;
        if (second == 0) {
            uint32_t high = oa_pak_gamma(&bits);
            if (!previous_match && high == 2) {
                distance = previous_distance;
                length = oa_pak_gamma(&bits);
            } else {
                uint32_t bias = previous_match ? 2 : 3;
                if (high < bias || high - bias > 0xffffff || bits.next == bits.end) return 0;
                distance = ((high - bias) << 8) | *bits.next++;
                length = oa_pak_gamma(&bits);
                if (!length || length > UINT32_MAX - 2) return 0;
                length += distance >= 0x7d00;
                length += distance >= 0x500;
                if (distance < 0x80) length += 2;
            }
        } else {
            int third = oa_pak_bit(&bits);
            if (third < 0) return 0;
            if (third != 0) {
                if (bits.tag & 0xf) {
                    distance = bits.tag >> 4;
                    bits.tag = (bits.tag << 4) & 0xff;
                } else {
                    distance = 0;
                    for (unsigned k = 0; k < 4; ++k) {
                        int bit = oa_pak_bit(&bits);
                        if (bit < 0) return 0;
                        distance = (distance << 1) | (unsigned)bit;
                    }
                }
                if (out == end || distance > (size_t)(out - dst)) return 0;
                *out = distance ? out[-(ptrdiff_t)distance] : 0;
                ++out;
                previous_match = 0;
                continue;
            }
            if (bits.next == bits.end) return 0;
            unsigned token = *bits.next++;
            distance = token >> 1;
            if (!distance) return (size_t)(out - dst);
            length = 2 + (token & 1);
        }

        if (!length || !distance || distance > (size_t)(out - dst) ||
            length > (size_t)(end - out)) return 0;
        oa_pak_copy(out, distance, length);
        out += length;
        previous_distance = distance;
        previous_match = 1;
    }
}

#endif
