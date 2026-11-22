#ifndef OA_PAK_ANDROID_H
#define OA_PAK_ANDROID_H

#include "rt.h"
#include "oa_pak_decode.h"

#ifdef OA_PAK_COMPARE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static pthread_once_t oa_pak_config_once = PTHREAD_ONCE_INIT;
static int oa_pak_mode;
static unsigned oa_pak_calls;
static unsigned oa_pak_fast_calls;
static unsigned oa_pak_rejected;
static unsigned oa_pak_records;
static unsigned oa_pak_capture_ticks;
static unsigned oa_pak_capture_armed;
static char oa_pak_arm_path[2048];
static FILE *oa_pak_capture;

static void oa_pak_config(void)
{
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (!root) return;
    char path[2048];
    snprintf(path, sizeof path, "%s/oa-pak-mode", root);
    FILE *mode = fopen(path, "r");
    if (mode) {
        int value = fgetc(mode);
        if (value == '1' || value == '2') oa_pak_mode = value - '0';
        fclose(mode);
    }
    if (oa_pak_mode == 2) {
        const char *log = getenv("ME_PC_LOG_PATH");
        const char *slash = log ? strrchr(log, '/') : NULL;
        snprintf(oa_pak_arm_path, sizeof oa_pak_arm_path, "%s/oa-pak-capture-armed", root);
        if (slash) {
            snprintf(path, sizeof path, "%.*s/pak-corpus.bin", (int)(slash - log), log);
            oa_pak_capture = fopen(path, "wb");
        }
    }
    rt_log("PAK_DECODE_MODE value=%d", oa_pak_mode);
}

static void oa_pak_record(uint32_t address, uint32_t size, uint32_t produced)
{
    if (oa_pak_mode != 2 || !oa_pak_capture || !size || size > 0x2000 ||
        !produced || produced > 0x1000) return;
    if (__atomic_load_n(&oa_pak_records, __ATOMIC_RELAXED) >= 384) return;
    unsigned tick = __atomic_fetch_add(&oa_pak_capture_ticks, 1, __ATOMIC_RELAXED);
    if (!__atomic_load_n(&oa_pak_capture_armed, __ATOMIC_RELAXED)) {
        if ((tick & 63) || access(oa_pak_arm_path, F_OK)) return;
        __atomic_store_n(&oa_pak_capture_armed, 1, __ATOMIC_RELAXED);
    }
    if (tick & 31) return;
    if (__atomic_fetch_add(&oa_pak_records, 1, __ATOMIC_RELAXED) >= 384) return;
    uint32_t record[] = {size, produced};
    flockfile(oa_pak_capture);
    fwrite(record, sizeof record, 1, oa_pak_capture);
    fwrite(rt_ptr(address), 1, size, oa_pak_capture);
    fflush(oa_pak_capture);
    funlockfile(oa_pak_capture);
}

void oa_pak_report(void)
{
    rt_log("PAK_DECODE_COUNTS calls=%u fast=%u fallback=%u samples=%u",
           __atomic_load_n(&oa_pak_calls, __ATOMIC_RELAXED),
           __atomic_load_n(&oa_pak_fast_calls, __ATOMIC_RELAXED),
           __atomic_load_n(&oa_pak_rejected, __ATOMIC_RELAXED),
           __atomic_load_n(&oa_pak_records, __ATOMIC_RELAXED));
}
#endif

/* The 0x01042FF0 worker owns a 0x2000-byte input region followed by a
 * 0x1000-byte output region. Publish the decoded region only on success. */
static int oa_pak_try_decode(uint32_t address, uint32_t size, uint32_t *produced)
{
#ifdef OA_PAK_COMPARE
    pthread_once(&oa_pak_config_once, oa_pak_config);
    __atomic_fetch_add(&oa_pak_calls, 1, __ATOMIC_RELAXED);
    if (oa_pak_mode != 1) return 0;
#endif
    if (address <= UINT32_MAX - 0x3000 && size && size <= 0x2000) {
        uint8_t scratch[0x1000];
        size_t length = oa_pak_decode(rt_ptr(address), size, scratch, sizeof scratch);
        if (length) {
            memcpy(rt_ptr(address + 0x2000), scratch, length);
            *produced = (uint32_t)length;
#ifdef OA_PAK_COMPARE
            __atomic_fetch_add(&oa_pak_fast_calls, 1, __ATOMIC_RELAXED);
#endif
            return 1;
        }
    }
#ifdef OA_PAK_COMPARE
    if (__atomic_fetch_add(&oa_pak_rejected, 1, __ATOMIC_RELAXED) < 4)
        rt_log("PAK_DECODE_FALLBACK input_size=%u", size);
#endif
    return 0;
}

#endif
