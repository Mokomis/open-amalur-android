/* Part of oa_vk_fullscreen.c: frames, upload ring and one-shot commands. Included in place; not compiled on its own. */
/* ---- frames, ring and one-shot commands ---- */
typedef struct {
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired;
    vk_buffer rings[8];
    int nrings, ring_cur;
    uint32_t ring_off;
    uint32_t upload_owner_count;
    oa_buffer *upload_owners[UPLOAD_OWNERS];
    uint64_t frame;
    int submitted;
    VkQueryPool qpool;
    VkQueryPool timing_queries;
    uint64_t submit_time_ns;
    uint32_t qused, qreset;
    oa_pred **qlist;
} oa_frame;
static oa_frame g_f[FRAMES];
static oa_frame *g_cur;
static FILE *gpu_timing_log;
static double gpu_timestamp_period;
static uint64_t gpu_timestamp_mask;
static void texture_upload_collect(void);
static VkCommandPool g_upload_pool;
static VkCommandBuffer g_upload_cmd;
static VkFence g_upload_fence;
static int g_ring_overflow;

static void *ring_alloc(uint32_t size, VkBuffer *buf, uint32_t *off) {
    oa_frame *f = g_cur;
    uint32_t a = (uint32_t)g_ubo_align, o = (f->ring_off + a - 1) & ~(a - 1);
    if (size > RING_SIZE) { logf_("ring request of %u bytes is too large", size); size = RING_SIZE; }
    if (o + size > RING_SIZE) {
        f->ring_cur++;
        if (f->ring_cur == f->nrings) {
            if (f->nrings < 8 && make_buffer(&f->rings[f->nrings], RING_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
                f->nrings++;
                logf_("ring grew to %d chunks", f->nrings);
            } else {
                if (!g_ring_overflow) { g_ring_overflow = 1; logf_("ring overflow"); }
                f->ring_cur = 0;
            }
        }
        o = 0;
    }
    f->ring_off = o + size;
    *buf = f->rings[f->ring_cur].buf;
    *off = o;
    return f->rings[f->ring_cur].ptr + o;
}

static void desc_retire_pools(void);
#define QUERY_MAX 32768u
static long g_n_culled, g_n_tests, g_n_test_skipped;
static int g_cull = 1;

static void read_queries(oa_frame *f) {
    static uint32_t r[QUERY_MAX];
    uint32_t i, n = f->qused;
    f->qreset = n ? n : f->qreset;
    f->qused = 0;
    if (!n) return;
    if (pvkGetQueryPoolResults(g_dev, f->qpool, 0, n, n * 4, r, 4, 0)) return;
    for (i = 0; i < n; i++) f->qlist[i]->acc = 0;
    for (i = 0; i < n; i++) f->qlist[i]->acc |= r[i] != 0;
    for (i = 0; i < n; i++) {
        f->qlist[i]->visible = f->qlist[i]->acc;
        f->qlist[i]->tested_frame = f->frame;
    }
}

static void frame_begin(void) {
    oa_frame *f = &g_f[g_frame % FRAMES];
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (f->submitted) {
        VkResult completed=pvkWaitForFences(g_dev, 1, &f->fence, VK_TRUE, UINT64_MAX);
        if(completed!=VK_SUCCESS)rt_fatal("Vulkan frame fence wait failed: %d",completed);
        if (gpu_timing_log && f->timing_queries) {
            uint64_t ticks[2];
            if (pvkGetQueryPoolResults(g_dev, f->timing_queries, 0, 2,
                                       sizeof(ticks), ticks, sizeof(ticks[0]),
                                       VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                double elapsed_us = (double)((ticks[1] - ticks[0]) & gpu_timestamp_mask) *
                    gpu_timestamp_period / 1000.0;
                fprintf(gpu_timing_log, "%llu,%llu,%.3f\n", (unsigned long long)f->frame,
                        (unsigned long long)f->submit_time_ns, elapsed_us);
                if (!(f->frame % 60)) fflush(gpu_timing_log);
            }
        }
        ++g_present_completed;
        present_gc_completed(f->frame);
        if(pvkResetFences(g_dev, 1, &f->fence)!=VK_SUCCESS)rt_fatal("Vulkan frame fence reset failed");
        f->submitted = 0;
        g_done_frame = f->frame;
        read_queries(f);
    }
    /* Once the frame fence has signalled, save only the still-current
       resource versions before this ring is recycled. */
    for (uint32_t i = 0; i < f->upload_owner_count; ++i)
        if (f->upload_owners[i]) restore_upload_shadow(f->upload_owners[i]);
    f->upload_owner_count = 0;
    texture_upload_collect();
    collect_gone();
    desc_retire_pools();
    pvkResetCommandPool(g_dev, f->pool, 0);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pvkBeginCommandBuffer(f->cmd, &bi);
    if (f->timing_queries) {
        pvkCmdResetQueryPool(f->cmd, f->timing_queries, 0, 2);
        pvkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, f->timing_queries, 0);
    }
    if (f->qreset) {
        pvkCmdResetQueryPool(f->cmd, f->qpool, 0, f->qreset);
        f->qreset = 0;
    }
    f->ring_off = 0;
    f->ring_cur = 0;
    f->frame = g_frame;
    g_cur = f;
}

#include "oa_texture_upload_queue.h"

static void barrier_all(VkCommandBuffer c) {
    VkMemoryBarrier m = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    m.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    m.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    pvkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &m, 0, NULL, 0, NULL);
}

static void image_layout(VkCommandBuffer c, VkImage i, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = i;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    pvkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

static int64_t g_t_upload, g_uploads;
static void upload_begin(void) {
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    pvkResetCommandPool(g_dev, g_upload_pool, 0);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pvkBeginCommandBuffer(g_upload_cmd, &bi);
}

/* The queue is shared with the render thread, so submits go through g_lock. */
static void upload_submit(void) {
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    pvkEndCommandBuffer(g_upload_cmd);
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_upload_cmd;
    pvkQueueSubmit(g_queue, 1, &si, g_upload_fence);
    pvkWaitForFences(g_dev, 1, &g_upload_fence, VK_TRUE, UINT64_MAX);
    pvkResetFences(g_dev, 1, &g_upload_fence);
}
