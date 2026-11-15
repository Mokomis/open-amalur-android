#include "present_overlap.h"
#include "rt.h"
#include "x86rt.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

void rt_event_set(uint32_t handle);

enum {
    PRESENT_REQUEST = 0x019FC028u,
    PRESENT_ACK = 0x019FC004u,
    MANAGER = 0x019FDF54u,
    CALLBACK_LIST = 0x019FB218u,
    ANIMATION_GROUP = 0x019FDF8Cu,
    PURE_DESCRIPTOR = 0x00C35E70u,
    EMPTY_CALLBACK = 0x00A84EA0u,
};

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t original_request = PTHREAD_COND_INITIALIZER;
static _Atomic bool enabled;
static _Atomic bool idle;
static bool armed, early, joined;
static _Thread_local bool presenter;
static uint32_t armed_request, armed_ack;
static uint64_t requested_ns, completed_ns, joined_ns;
static uint64_t started, acknowledged, hidden_ns, rejected, report_after;

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

void oa_present_mode(bool on)
{
    if (atomic_exchange_explicit(&enabled,on,memory_order_relaxed) != on)
        rt_log("SIM_MEMORY_MODE mode=%d ns=%llu",on,(unsigned long long)monotonic_ns());
}

void oa_present_draw_ready(bool completed)
{
    atomic_store_explicit(&idle,completed,memory_order_release);
}

void oa_present_presenter_wait(uint32_t caller)
{
    pthread_mutex_lock(&gate);
    presenter = caller == 0x00A661C5u;
    armed = presenter;
    armed_request = ld32(PRESENT_REQUEST);
    armed_ack = ld32(PRESENT_ACK);
    pthread_mutex_unlock(&gate);
}

static bool range(uint32_t p, uint64_t n)
{
    return p >= 0x10000u && (uint64_t)p + n <= 0xf0000000ull;
}

static bool qualify_packets(void)
{
    const uint32_t group = ld32(ANIMATION_GROUP);
    const uint32_t model = ld32(MANAGER);
    const uint32_t list = ld32(CALLBACK_LIST);
    if (!range(model,0x14c) || !range(group,0x14) || !range(list,8))
        return false;
    if (ld32(group + 4) != 0 || ld32(group + 0x10) != UINT32_MAX)
        return false;
    const uint32_t callbacks = ld32(list), callback_count = ld32(list + 4);
    if (callback_count > 64 || (callback_count && !range(callbacks,callback_count*4u)))
        return false;
    for (uint32_t i = 0; i < callback_count; ++i) {
        const uint32_t callback = ld32(callbacks + 4*i);
        if (!range(callback,4))
            return false;
        const uint32_t vtable = ld32(callback);
        if (!range(vtable,16) || ld32(vtable+12) != EMPTY_CALLBACK)
            return false;
    }

    const uint32_t count = ld32(model + 0x148), queue = ld32(model + 0x144);
    const uint32_t owner_count = ld32(model + 0xc8), owners = ld32(model + 0xc4);
    const uint32_t asset_count = ld32(model + 0x5c);
    const uint32_t assets = ld32(model + 0x18), flags = ld32(model + 0x28);
    if (!count || count > 512 || owner_count < 2 || owner_count > 65536 ||
        asset_count < 2 || asset_count > 65536 || !range(queue,count*4u) ||
        !range(owners,owner_count*4u) || !range(assets,asset_count*4u) ||
        !range(flags,asset_count))
        return false;

    unsigned packets = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = ld32(queue+4*i);
        if (id >= owner_count)
            id = 0;
        uint32_t owner = ld32(owners+4*id);
        if (!owner)
            owner = ld32(owners+4);
        if (!range(owner,0x1cc))
            return false;
        uint32_t id_asset = ld32(owner + 0x198);
        if (id_asset >= asset_count)
            return false;
        const uint8_t path = ld8(flags + id_asset) & 0x14u;
        if (path != 4u) {
            const uint32_t model_vtable = ld32(model);
            if (!(path & 0x10u) && id_asset >= 2u)
                return false;
            if (!range(model_vtable,0x58) || ld32(model_vtable + 0x54) != 0x00A6E0B0u ||
                (ld8(flags + 1) & 0x14u) != 4u)
                return false;
            // The original fallback returns resident slot 1 without loading it.
            id_asset = 1;
        }
        const uint32_t n = ld32(owner + 0x18);
        if (!n)
            continue;
        const uint32_t asset = ld32(assets + 4*id_asset), owner_packets = ld32(owner + 0x14);
        if (n > 128 || (packets += n) > 2048 || !range(asset,0x50) || !range(owner_packets,n*20u))
            return false;
        const uint32_t desc = ld32(asset + 0x4c);
        if (!range(desc,n*0x5cu))
            return false;
        for (uint32_t j = 0; j < n; ++j) {
            if ((int32_t)ld32(owner_packets + j*20u + 4) < 0)
                continue;
            const uint32_t vt = ld32(desc + j*0x5cu);
            if (!range(vt,0x24) || ld32(vt+0x20) != PURE_DESCRIPTOR)
                return false;
        }
    }
    return packets > 0;
}

void oa_present_producer_begin(void)
{
    if (!atomic_load_explicit(&enabled,memory_order_relaxed) ||
        !atomic_load_explicit(&idle,memory_order_acquire))
        return;

    const bool qualify = qualify_packets();
    pthread_mutex_lock(&gate);
    if (!qualify) {
        ++rejected;
    } else if (!early && armed && atomic_load_explicit(&idle,memory_order_acquire) &&
        armed_request == ld32(PRESENT_REQUEST) && armed_ack == ld32(PRESENT_ACK) &&
        armed_request >= 0x100u && armed_ack >= 0x100u &&
        !(armed_request & 3u) && !(armed_ack & 3u) && armed_request != armed_ack) {
        early = true;
        joined = false;
        armed = false;
        completed_ns = 0;
        joined_ns = 0;
        requested_ns = monotonic_ns();
        ++started;
        rt_event_set(armed_request);
    }
    const uint64_t now = monotonic_ns();
    if (now >= report_after) {
        rt_log("PRESENT_OVERLAP ns=%llu started=%llu acknowledged=%llu rejected=%llu hidden_ns=%llu",
            (unsigned long long)now,(unsigned long long)started,(unsigned long long)acknowledged,
            (unsigned long long)rejected,(unsigned long long)hidden_ns);
        report_after = now + 250000000u;
    }
    pthread_mutex_unlock(&gate);
}

bool oa_present_draw_join(void)
{
    atomic_store_explicit(&idle,false,memory_order_release);
    pthread_mutex_lock(&gate);
    const bool already_requested = early;
    armed = false;
    if (early) {
        joined_ns = monotonic_ns();
        joined = true;
        pthread_cond_broadcast(&original_request);
    }
    pthread_mutex_unlock(&gate);
    return already_requested;
}

void oa_present_host_return(void)
{
    if (!presenter) return;
    pthread_mutex_lock(&gate);
    if (early) {
        completed_ns = monotonic_ns();
        while (early && !joined)
            pthread_cond_wait(&original_request,&gate);
    }
    pthread_mutex_unlock(&gate);
}

void oa_present_ack(void)
{
    pthread_mutex_lock(&gate);
    if (early && joined) {
        ++acknowledged;
        if (completed_ns >= requested_ns && joined_ns >= requested_ns)
            hidden_ns += (completed_ns < joined_ns ? completed_ns : joined_ns)-requested_ns;
        early = false;
    }
    pthread_mutex_unlock(&gate);
}

void oa_present_cancel(void)
{
    atomic_store_explicit(&idle,false,memory_order_release);
    pthread_mutex_lock(&gate);
    early = armed = false;
    pthread_cond_broadcast(&original_request);
    pthread_mutex_unlock(&gate);
}
