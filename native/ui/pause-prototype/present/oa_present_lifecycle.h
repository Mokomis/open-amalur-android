
/* Swapchain generations and presentation waits. Tested with the same functions
   on host mocks; caller serializes queue operations and owns frame fences. */
#ifndef OA_PRESENT_LIFECYCLE_H
#define OA_PRESENT_LIFECYCLE_H
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#ifndef OA_PRESENT_CALLOC
#define OA_PRESENT_CALLOC calloc
#endif
typedef struct {
    PFN_vkCreateSwapchainKHR create;
    PFN_vkDestroySwapchainKHR destroy;
    PFN_vkGetSwapchainImagesKHR images;
    PFN_vkCreateSemaphore semaphore;
    PFN_vkDestroySemaphore destroy_semaphore;
    PFN_vkAcquireNextImageKHR acquire;
    PFN_vkCreateFence fence;
    PFN_vkDestroyFence destroy_fence;
    PFN_vkGetFenceStatus fence_status;
    PFN_vkResetFences reset_fences;
    PFN_vkWaitForFences wait_fences;
    int maintenance;
    PFN_vkCreateImageView create_view;
    PFN_vkDestroyImageView destroy_view;

} OaPresentOps;
typedef struct OaPresentGeneration {
    VkSwapchainKHR handle;
    VkImage *images;
    VkImageView *views;
    VkSemaphore *finished;
    VkFence *present_fences;
    uint8_t *fence_pending;
    uint64_t last_submit_frame;
    uint32_t count, first_present_index;
    int presented, used_for_present, retire_proven;
    uint64_t retire_frame;
    struct OaPresentGeneration *next;
} OaPresentGeneration;
typedef struct {
    OaPresentGeneration *current, *retired;
    int recreate, recheck;
} OaPresentState;
typedef struct {
    VkResult result;
    uint32_t index;
    int valid, retirement_proof;
    VkFence present_fence;
} OaPresentAcquired;

static int oa_present_startup_retry_allowed(const OaPresentState *s)
{
    return !s->current && !s->retired;
}

static int oa_present_surface_changed(VkExtent2D current,VkSurfaceTransformFlagBitsKHR chosen,
    uint32_t images,const VkSurfaceCapabilitiesKHR *cap,uint32_t width,uint32_t height,
    VkExtent2D *effective,VkSurfaceTransformFlagBitsKHR *transform)
{
    *effective=cap->currentExtent;
    if(effective->width==UINT32_MAX){
        effective->width=width<cap->minImageExtent.width?cap->minImageExtent.width:
            width>cap->maxImageExtent.width?cap->maxImageExtent.width:width;
        effective->height=height<cap->minImageExtent.height?cap->minImageExtent.height:
            height>cap->maxImageExtent.height?cap->maxImageExtent.height:height;
    }
    *transform=(cap->supportedTransforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)?
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:cap->currentTransform;
    return effective->width!=current.width || effective->height!=current.height || *transform!=chosen ||
        images<cap->minImageCount || (cap->maxImageCount && images>cap->maxImageCount) ||
        !(cap->supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT);
}

static VkPresentModeKHR oa_present_mode(const VkPresentModeKHR *modes,uint32_t count,int requested)
{
    if(requested)for(uint32_t i=0;i<count;++i)if(modes[i]==VK_PRESENT_MODE_MAILBOX_KHR)return modes[i];
    return VK_PRESENT_MODE_FIFO_KHR;
}
static void oa_present_free(const OaPresentOps *ops,VkDevice device,OaPresentGeneration *g)
{
    if(!g)return;
    for(uint32_t i=0;i<g->count;++i)
        if(g->finished && g->finished[i])ops->destroy_semaphore(device,g->finished[i],NULL);
    for(uint32_t i=0;i<g->count;++i)
        if(g->present_fences && g->present_fences[i])ops->destroy_fence(device,g->present_fences[i],NULL);
    free(g->present_fences);free(g->fence_pending);
    if(g->views) {
        for(uint32_t i=0;i<g->count;++i)
            if(g->views[i])ops->destroy_view(device,g->views[i],NULL);
        free(g->views);
    }
    if(g->handle)ops->destroy(device,g->handle,NULL);
    free(g->finished);free(g->images);free(g);
}
static void oa_present_retire(OaPresentState *s)
{
    if(s->current){s->current->next=s->retired;s->retired=s->current;s->current=NULL;}
    s->recreate=1;
}
/* oldSwapchain is retired by CreateSwapchain even if creation fails. */
static VkResult oa_present_replace(OaPresentState *s,const OaPresentOps *ops,VkDevice device,
                                   VkSwapchainCreateInfoKHR *ci)
{
    /* Retired chains cannot be supplied as oldSwapchain, nor silently bypassed. */
    if(!s->current && s->retired)return VK_ERROR_NATIVE_WINDOW_IN_USE_KHR;
    OaPresentGeneration *g=OA_PRESENT_CALLOC(1,sizeof(*g));
    if(!g)return VK_ERROR_OUT_OF_HOST_MEMORY;
    ci->oldSwapchain=s->current?s->current->handle:VK_NULL_HANDLE;
    VkResult r=ops->create(device,ci,NULL,&g->handle);
    oa_present_retire(s);
    if(r!=VK_SUCCESS){free(g);return r;}
    for(unsigned attempt=0;attempt<3;++attempt){
        uint32_t count=0;
        r=ops->images(device,g->handle,&count,NULL);
        if(r!=VK_SUCCESS || !count || count>4096){if(r==VK_SUCCESS)r=VK_ERROR_INITIALIZATION_FAILED;break;}
        VkImage *images=OA_PRESENT_CALLOC(count,sizeof(*images));
        if(!images){r=VK_ERROR_OUT_OF_HOST_MEMORY;break;}
        r=ops->images(device,g->handle,&count,images);
        if(r==VK_SUCCESS){g->images=images;g->count=count;break;}
        free(images);if(r!=VK_INCOMPLETE)break;
    }
    if(r!=VK_SUCCESS || !g->count){oa_present_free(ops,device,g);return r==VK_SUCCESS?VK_ERROR_INITIALIZATION_FAILED:r;}
    if(ci->imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        g->views = calloc(g->count, sizeof(*g->views));
        if(!g->views){oa_present_free(ops,device,g);return VK_ERROR_OUT_OF_HOST_MEMORY;}
        for(uint32_t i=0;i<g->count;++i) {
            VkImageViewCreateInfo view = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = g->images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = ci->imageFormat,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            };
            r = ops->create_view(device,&view,NULL,&g->views[i]);
            if(r!=VK_SUCCESS){oa_present_free(ops,device,g);return r;}
        }
    }
    g->finished=OA_PRESENT_CALLOC(g->count,sizeof(*g->finished));
    if(!g->finished){oa_present_free(ops,device,g);return VK_ERROR_OUT_OF_HOST_MEMORY;}
    VkSemaphoreCreateInfo sc={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for(uint32_t i=0;i<g->count;++i){
        r=ops->semaphore(device,&sc,NULL,&g->finished[i]);
        if(r!=VK_SUCCESS){oa_present_free(ops,device,g);return r;}
    }
    if(ops->maintenance){
        g->present_fences=OA_PRESENT_CALLOC(g->count,sizeof(*g->present_fences));
        g->fence_pending=OA_PRESENT_CALLOC(g->count,sizeof(*g->fence_pending));
        if(!g->present_fences || !g->fence_pending){oa_present_free(ops,device,g);return VK_ERROR_OUT_OF_HOST_MEMORY;}
        VkFenceCreateInfo fc={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        for(uint32_t i=0;i<g->count;++i){
            r=ops->fence(device,&fc,NULL,&g->present_fences[i]);
            if(r!=VK_SUCCESS){oa_present_free(ops,device,g);return r;}
        }
    }
    s->current=g;s->recreate=0;s->recheck=0;
    return VK_SUCCESS;
}
/* Separate WSI fences: GPU frame fences cannot prove presentation ownership.
   Reuse only after explicit signal; if all are busy, wait a bounded 100ms BEFORE
   acquiring an image so timeout leaves no unconsumed acquire semaphore. */
static VkResult oa_present_fence_available(OaPresentGeneration *g,const OaPresentOps *ops,
                                         VkDevice device,VkFence *out)
{
    *out=VK_NULL_HANDLE;
    if(!ops->maintenance)return VK_SUCCESS;
    for(unsigned attempt=0;attempt<2;++attempt){
        for(uint32_t i=0;i<g->count;++i){
            if(g->fence_pending[i]){
                VkResult r=ops->fence_status(device,g->present_fences[i]);
                if(r==VK_NOT_READY)continue;
                if(r!=VK_SUCCESS)return r;
                r=ops->reset_fences(device,1,&g->present_fences[i]);
                if(r!=VK_SUCCESS)return r;
                g->fence_pending[i]=0;
            }
            *out=g->present_fences[i];return VK_SUCCESS;
        }
        if(!attempt){
            VkResult r=ops->wait_fences(device,g->count,g->present_fences,VK_FALSE,100000000ull);
            if(r!=VK_SUCCESS)return r;
        }
    }
    return VK_NOT_READY;
}
static void oa_present_fence_queued(OaPresentGeneration *g,VkFence fence)
{
    if(!fence)return;
    for(uint32_t i=0;i<g->count;++i)if(g->present_fences[i]==fence){g->fence_pending[i]=1;return;}
}
static int oa_present_fences_done(OaPresentGeneration *g,const OaPresentOps *ops,VkDevice device)
{
    if(!g->used_for_present)return 1;
    if(!ops->maintenance)return 0;
    for(uint32_t i=0;i<g->count;++i)
        if(g->fence_pending[i] && ops->fence_status(device,g->present_fences[i])!=VK_SUCCESS)return 0;
    return 1;
}
static OaPresentAcquired oa_present_acquire(OaPresentState *s,const OaPresentOps *ops,
                                          VkDevice device,VkSemaphore ready)
{
    OaPresentAcquired a={VK_ERROR_OUT_OF_DATE_KHR,UINT32_MAX,0,0};
    if(!s->current){s->recreate=1;return a;}
    a.result=oa_present_fence_available(s->current,ops,device,&a.present_fence);
    if(a.result!=VK_SUCCESS)return a;
    a.result=ops->acquire(device,s->current->handle,100000000ull,ready,VK_NULL_HANDLE,&a.index);
    if(a.result==VK_SUCCESS || a.result==VK_SUBOPTIMAL_KHR){
        if(a.index>=s->current->count){a.result=VK_ERROR_INITIALIZATION_FAILED;a.index=UINT32_MAX;return a;}
        a.valid=1;
        a.retirement_proof=s->current->presented && a.index==s->current->first_present_index;
        if(a.result==VK_SUBOPTIMAL_KHR)s->recheck=1;
    }else{
        a.index=UINT32_MAX;
        if(a.result==VK_ERROR_OUT_OF_DATE_KHR)s->recreate=1;
    }
    return a;
}
/* Call only after a successful queue submission that waits on this acquisition. */
static void oa_present_submitted(OaPresentState *s,const OaPresentAcquired *a,uint64_t frame)
{
    if(a->valid && s->current)s->current->last_submit_frame=frame;
    if(!a->valid || !a->retirement_proof)return;
    for(OaPresentGeneration *g=s->retired;g;g=g->next)
        if(!g->retire_proven){g->retire_proven=1;g->retire_frame=frame;}
}
static void oa_present_result(OaPresentState *s,uint32_t index,VkResult r)
{
    if(s->current)s->current->used_for_present=1;
    if((r==VK_SUCCESS || r==VK_SUBOPTIMAL_KHR) && s->current && !s->current->presented){
        s->current->presented=1;s->current->first_present_index=index;
    }
    if(r==VK_SUBOPTIMAL_KHR)s->recheck=1;
    if(r==VK_ERROR_OUT_OF_DATE_KHR)s->recreate=1;
}
/* Called only following a successful wait on a submitted frame fence.
   All submissions use one queue; later completion also covers earlier proof frames. */
static void oa_present_collect(OaPresentState *s,const OaPresentOps *ops,VkDevice device,uint64_t completed)
{
    OaPresentGeneration **at=&s->retired;
    while(*at){
        OaPresentGeneration *g=*at;
        if((!ops->maintenance && g->retire_proven && g->retire_frame<=completed) ||
           (ops->maintenance && g->last_submit_frame<=completed && oa_present_fences_done(g,ops,device))){
            *at=g->next;oa_present_free(ops,device,g);
        }else at=&g->next;
    }
}
/* A generation that has never been presented can be freed after GPU completion.
   Presented generations without acquire proof remain process-owned, never freed
   speculatively merely because vkQueueWaitIdle returned. */
static OaPresentGeneration *oa_present_release(OaPresentState *s,const OaPresentOps *ops,VkDevice device)
{
    oa_present_retire(s);
    OaPresentGeneration *held=NULL,*g=s->retired;s->retired=NULL;
    while(g){
        OaPresentGeneration *next=g->next;
        if(oa_present_fences_done(g,ops,device))oa_present_free(ops,device,g);
        else{g->next=held;held=g;}
        g=next;
    }
    return held;
}
#endif
