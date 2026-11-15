
#ifndef OA_TEXTURE_UPLOAD_QUEUE_H
#define OA_TEXTURE_UPLOAD_QUEUE_H

enum {
    OA_UPLOAD_SMALL_SLOTS=4, OA_UPLOAD_LARGE_SLOTS=2,
    OA_UPLOAD_SLOTS=OA_UPLOAD_SMALL_SLOTS+OA_UPLOAD_LARGE_SLOTS,
    OA_UPLOAD_SMALL_LIMIT=4u<<20, OA_UPLOAD_LIMIT=24u<<20
};
typedef struct {
    VkCommandPool pool;
    VkCommandBuffer command;
    VkFence fence;
    vk_buffer staging;
    uint32_t capacity;
    obj *image_owner;
} OaTextureUpload;
static OaTextureUpload texture_uploads[OA_UPLOAD_SLOTS];
static pthread_mutex_t texture_upload_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned texture_upload_next[2], texture_upload_pending;
static int texture_upload_mode;
static uint64_t texture_upload_calls, texture_upload_fallbacks, texture_upload_staging_bytes;

static void texture_upload_finish(OaTextureUpload *slot, int wait)
{
    if (!slot->image_owner) return;
    VkResult status = pvkGetFenceStatus(g_dev, slot->fence);
    if (status == VK_NOT_READY && wait) {
        status = pvkWaitForFences(g_dev, 1, &slot->fence, VK_TRUE, UINT64_MAX);
    }
    if (status == VK_NOT_READY && !wait) return;
    if (status != VK_SUCCESS) rt_fatal("texture upload completion failed %d", status);
    if (pvkResetFences(g_dev, 1, &slot->fence) != VK_SUCCESS)
        rt_fatal("texture upload fence reset failed");
    obj *owner = slot->image_owner;
    slot->image_owner = NULL;
    __atomic_fetch_sub(&texture_upload_pending, 1, __ATOMIC_RELEASE);
    o_release(owner);
}

static void texture_upload_collect(void)
{
    if (!__atomic_load_n(&texture_upload_pending, __ATOMIC_ACQUIRE) ||
        pthread_mutex_trylock(&texture_upload_lock)) return;
    for (unsigned i=0; i<OA_UPLOAD_SLOTS; ++i) texture_upload_finish(&texture_uploads[i],0);
    pthread_mutex_unlock(&texture_upload_lock);
}

static int texture_upload_setup(OaTextureUpload *slot)
{
    if (slot->pool) return 1;
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandBufferAllocateInfo allocation = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    pool.queueFamilyIndex = g_qfam;
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if (pvkCreateCommandPool(g_dev,&pool,NULL,&slot->pool) != VK_SUCCESS) return 0;
    allocation.commandPool = slot->pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (pvkAllocateCommandBuffers(g_dev,&allocation,&slot->command) == VK_SUCCESS &&
        pvkCreateFence(g_dev,&fence,NULL,&slot->fence) == VK_SUCCESS) return 1;
    pvkDestroyCommandPool(g_dev,slot->pool,NULL);
    slot->pool = VK_NULL_HANDLE;
    return 0;
}

static OaTextureUpload *texture_upload_acquire(uint32_t bytes)
{
    unsigned large=bytes > OA_UPLOAD_SMALL_LIMIT;
    unsigned first=large ? OA_UPLOAD_SMALL_SLOTS : 0;
    OaTextureUpload *slot = &texture_uploads[first+texture_upload_next[large]];
    texture_upload_finish(slot, 1);
    if (!texture_upload_setup(slot)) return NULL;
    if (slot->capacity < bytes) {
        uint32_t capacity=65536;
        if (large) {
            capacity=(bytes+OA_UPLOAD_SMALL_LIMIT-1)&~(OA_UPLOAD_SMALL_LIMIT-1);
        } else {
            while (capacity < bytes) capacity *= 2;
        }
        vk_buffer replacement={0};
        int ok=make_buffer(&replacement,capacity,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        if (!ok) return NULL;
        if (slot->staging.buf) {
            pvkDestroyBuffer(g_dev,slot->staging.buf,NULL);
            pvkFreeMemory(g_dev,slot->staging.mem,NULL);
        }
        __atomic_fetch_add(&texture_upload_staging_bytes,capacity-slot->capacity,__ATOMIC_RELAXED);
        slot->staging=replacement;
        slot->capacity=capacity;
    }
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (pvkResetCommandPool(g_dev,slot->pool,0) != VK_SUCCESS ||
        pvkBeginCommandBuffer(slot->command,&begin) != VK_SUCCESS)
        rt_fatal("texture upload command begin failed");
    return slot;
}

static void texture_upload_queue(OaTextureUpload *slot, obj *owner)
{
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    if (pvkEndCommandBuffer(slot->command) != VK_SUCCESS)
        rt_fatal("texture upload command end failed");
    submit.commandBufferCount=1;
    submit.pCommandBuffers=&slot->command;
    o_addref(owner);
    EnterCriticalSection(&g_lock);
    VkResult status=pvkQueueSubmit(g_queue,1,&submit,slot->fence);
    LeaveCriticalSection(&g_lock);
    if (status != VK_SUCCESS) rt_fatal("texture upload queue submission failed %d",status);
    slot->image_owner=owner;
    __atomic_fetch_add(&texture_upload_pending,1,__ATOMIC_RELEASE);
    unsigned large=(unsigned)(slot-texture_uploads)>=OA_UPLOAD_SMALL_SLOTS;
    unsigned slots=large ? OA_UPLOAD_LARGE_SLOTS : OA_UPLOAD_SMALL_SLOTS;
    texture_upload_next[large]=(texture_upload_next[large]+1)%slots;
}

#endif
