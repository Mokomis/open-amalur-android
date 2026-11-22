/* Part of oa_vk_fullscreen.c: DXGI swap chain and presentation. Included in place; not compiled on its own. */
/* ---- DXGI ---- */
static HRESULT COM x_get_parent(obj *o, const GUID *iid, void **out) {
    obj *p = o->kind == K_DXGI_DEVICE ? g_adapter : g_factory;
    (void)iid;
    o_addref(p);
    *out = p;
    return S_OK_;
}
static HRESULT COM x_get_adapter(obj *o, void **out) { (void)o; o_addref(g_adapter); *out = g_adapter; return S_OK_; }
static HRESULT COM x_set_priority(obj *o, INT p) { (void)o; (void)p; return S_OK_; }

static HRESULT COM a_enum_outputs(obj *o, UINT i, void **out) { (void)o; (void)i; *out = NULL; return DXGI_NOT_FOUND; }
static HRESULT COM a_get_desc(obj*o,DXGI_ADAPTER_DESC*d){(void)o;VkPhysicalDeviceProperties p;pvkGetPhysicalDeviceProperties(g_phys,&p);memset(d,0,sizeof(*d));for(unsigned i=0;i<127&&p.deviceName[i];i++)d->Description[i]=(unsigned char)p.deviceName[i];d->VendorId=p.vendorID;d->DeviceId=p.deviceID;for(unsigned i=0;i<g_mem.memoryHeapCount;i++)if(g_mem.memoryHeaps[i].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)d->DedicatedVideoMemory+=g_mem.memoryHeaps[i].size;return S_OK_;}

static VkResult create_swapchain(oa_chain *s) {
    VkSurfaceCapabilitiesKHR cap;
    VkSwapchainCreateInfoKHR ci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    VkSurfaceFormatKHR fmts[128];
    uint32_t n = 128, i;
    VkBool32 sup = 0;
    VkResult query=pvkGetPhysicalDeviceSurfaceSupportKHR(g_phys,g_qfam,s->surface,&sup);
    if(query!=VK_SUCCESS)return query;
    if(!sup)return VK_ERROR_INITIALIZATION_FAILED;
    query=pvkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_phys,s->surface,&cap);
    if(query!=VK_SUCCESS)return query;
    VkResult formats=pvkGetPhysicalDeviceSurfaceFormatsKHR(g_phys,s->surface,&n,fmts);
    if(formats!=VK_SUCCESS && formats!=VK_INCOMPLETE)return formats;
    if(!n)return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if(!(cap.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))return VK_ERROR_FORMAT_NOT_SUPPORTED;
    uint32_t mode_count=0;
    VkPresentModeKHR *modes=NULL;
    VkResult mode_result=pvkGetPhysicalDeviceSurfacePresentModesKHR(g_phys,s->surface,&mode_count,NULL);
    if(mode_result==VK_SUCCESS && mode_count && mode_count<=1024){
        modes=calloc(mode_count,sizeof(*modes));
        if(modes)mode_result=pvkGetPhysicalDeviceSurfacePresentModesKHR(g_phys,s->surface,&mode_count,modes);
        else mode_result=VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    ci.presentMode=mode_result==VK_SUCCESS && modes?oa_present_mode(modes,mode_count,s->mailbox_requested):VK_PRESENT_MODE_FIFO_KHR;
    if(!s->mode_logged){
        logf_("PRESENT startup headroom_requested=%d modes_query=%d modes=%u; throughput is render-submit loop iterations; successful submits/requests counted separately; displayed FPS unmeasured",
              s->mailbox_requested,mode_result,mode_count);
        if(mode_result==VK_SUCCESS && modes)for(uint32_t j=0;j<mode_count;++j)logf_("PRESENT supported_mode=%d",modes[j]);
        s->mode_logged=1;
    }
    free(modes);
    uint32_t selected = 0;
    for (i = 0; i < n; ++i) {
        if (fmts[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
        selected = i;
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) break;
    }
    if (hdr_requested && hdr_colorspace && (cap.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        for (i = 0; i < n; ++i) {
            VkFormat format = fmts[i].format;
            if (fmts[i].colorSpace != VK_COLOR_SPACE_HDR10_ST2084_EXT ||
                (format != VK_FORMAT_A2B10G10R10_UNORM_PACK32 && format != VK_FORMAT_A2R10G10B10_UNORM_PACK32))
                continue;
            if (!hdr_output_pipeline(format) || !hdr_texture_view(s->back)) continue;
            selected = i;
            break;
        }
    }
    s->swap_fmt = fmts[selected].format;
    ci.imageColorSpace = fmts[selected].colorSpace;
    int hdr_format = ci.imageColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT;
    if (hdr_requested && !hdr_format) {
        logf_("HDR unavailable on this surface: colorspace_extension=%d usage=%#x formats=%u", hdr_colorspace, cap.supportedUsageFlags, n);
        for (i = 0; i < n; ++i) logf_("HDR surface offer format=%d colorspace=%d", fmts[i].format, fmts[i].colorSpace);
    }
    s->extent = cap.currentExtent;
    if (s->extent.width == UINT32_MAX) {
        extern int oa_android_buffer_width(void), oa_android_buffer_height(void);
        uint32_t width = oa_android_buffer_width(), height = oa_android_buffer_height();
        s->extent.width = width < cap.minImageExtent.width ? cap.minImageExtent.width :
            width > cap.maxImageExtent.width ? cap.maxImageExtent.width : width;
        s->extent.height = height < cap.minImageExtent.height ? cap.minImageExtent.height :
            height > cap.maxImageExtent.height ? cap.maxImageExtent.height : height;
    }
    if(!s->extent.width || !s->extent.height)return VK_NOT_READY;
    ci.surface = s->surface;
    uint32_t desired=ci.presentMode==VK_PRESENT_MODE_MAILBOX_KHR?3:2;
    ci.minImageCount = cap.minImageCount < desired ? desired : cap.minImageCount;
    if (cap.maxImageCount && ci.minImageCount > cap.maxImageCount) ci.minImageCount = cap.maxImageCount;
    ci.imageFormat = s->swap_fmt;
    ci.imageExtent = s->extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = hdr_format ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    /* The original engine renders upright Android window coordinates; it does
       not pre-rotate pixels into the display's native portrait orientation. */
    ci.preTransform = (cap.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : cap.currentTransform;
    logf_("surface transform current=%#x supported=%#x chosen=%#x extent=%ux%u window=%ux%u",
        cap.currentTransform, cap.supportedTransforms, ci.preTransform,
        s->extent.width,s->extent.height,s->w,s->h);
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.clipped = VK_TRUE;
    if(s->present.current){
        EnterCriticalSection(&g_lock);
        VkResult idle=pvkQueueWaitIdle(g_queue);
        LeaveCriticalSection(&g_lock);
        if(idle!=VK_SUCCESS){logf_("PRESENT recreate queue idle failed %d",idle);return idle;}
    }
    VkResult result=oa_present_replace(&s->present,&g_present_ops,g_dev,&ci);
    if(result!=VK_SUCCESS && ci.presentMode==VK_PRESENT_MODE_MAILBOX_KHR &&
       oa_present_startup_retry_allowed(&s->present)){
        logf_("PRESENT MAILBOX creation failed %d; fallback FIFO",result);
        ci.presentMode=VK_PRESENT_MODE_FIFO_KHR;
        ci.minImageCount=cap.minImageCount<2?2:cap.minImageCount;
        if(cap.maxImageCount && ci.minImageCount>cap.maxImageCount)ci.minImageCount=cap.maxImageCount;
        result=oa_present_replace(&s->present,&g_present_ops,g_dev,&ci);
    }
    if(result!=VK_SUCCESS){logf_("PRESENT swapchain failed %d (retired-handle retry forbidden)",result);return result;}
    hdr_active = hdr_format;
    if (hdr_active) hdr_metadata(s->present.current->handle);
    logf_("HDR actual=%d requested=%d colorspace=%d format=%d engine=%ux%u surface=%ux%u frame=%llu",
          hdr_active, hdr_requested, ci.imageColorSpace, s->swap_fmt, s->w, s->h,
          s->extent.width, s->extent.height, (unsigned long long)g_frame);
    s->present_mode=ci.presentMode;
    s->configured_transform=ci.preTransform;
    logf_("PRESENT swapchain %ux%u format=%d mode=%d requested_images=%u actual_images=%u caps=%u..%u",
          s->extent.width,s->extent.height,s->swap_fmt,s->present_mode,ci.minImageCount,
          s->present.current->count,cap.minImageCount,cap.maxImageCount);
    return VK_SUCCESS;
}

static VkResult present_check_surface(oa_chain *s)
{
    if(!s->present.recheck || s->present.recreate || g_frame<s->next_surface_check)return VK_SUCCESS;
    s->next_surface_check=g_frame+120;
    s->present.recheck=0;
    VkSurfaceCapabilitiesKHR cap;
    VkResult r=pvkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_phys,s->surface,&cap);
    if(r!=VK_SUCCESS){logf_("PRESENT capability recheck result=%d",r);return r;}
    extern int oa_android_buffer_width(void),oa_android_buffer_height(void);
    VkExtent2D effective;
    VkSurfaceTransformFlagBitsKHR transform;
    int changed=oa_present_surface_changed(s->extent,s->configured_transform,
        s->present.current?s->present.current->count:0,&cap,
        oa_android_buffer_width(),oa_android_buffer_height(),&effective,&transform);
    if(changed)s->present.recreate=1;
    logf_("PRESENT SUBOPTIMAL recheck changed=%d extent=%ux%u->%ux%u chosen_transform=%#x->%#x surface_current=%#x",
        changed,s->extent.width,s->extent.height,effective.width,effective.height,
        s->configured_transform,transform,cap.currentTransform);
    return VK_SUCCESS;
}

static void present_gc_orphans(void)
{
    oa_present_orphan **at=&g_present_orphans;
    while(*at){
        oa_present_orphan *o=*at;
        OaPresentGeneration **g=&o->generations;
        while(*g){
            OaPresentGeneration *candidate=*g;
            if(oa_present_fences_done(candidate,&g_present_ops,g_dev)){
                *g=candidate->next;oa_present_free(&g_present_ops,g_dev,candidate);
            }else g=&candidate->next;
        }
        if(!o->generations){
            *at=o->next;pvkDestroySurfaceKHR(g_instance,o->surface,NULL);
            if(o->native_window)ANativeWindow_release(o->native_window);
            free(o);--g_present_orphan_count;
            logf_("WINDOW old presentation resources collected; retained=%u",g_present_orphan_count);
        }else at=&o->next;
    }
}
static void present_gc_completed(uint64_t frame)
{
    for(oa_chain *s=g_present_chains;s;s=s->present_next)
        oa_present_collect(&s->present,&g_present_ops,g_dev,frame);
    present_gc_orphans();
}
/* Caller has completed the GPU work using this chain. WSI completion is separate. */
static void present_detach_surface(oa_chain *s)
{
    OaPresentGeneration *held=oa_present_release(&s->present,&g_present_ops,g_dev);
    if(held){
        present_gc_orphans();
        if(g_present_orphan_count>=OA_MAX_RETAINED_WINDOWS)
            rt_fatal("Android window retirement limit reached (%u); restart app; WSI fences have not completed",g_present_orphan_count);
        oa_present_orphan *orphan=calloc(1,sizeof(*orphan));
        if(!orphan)rt_fatal("present retirement metadata allocation failed");
        orphan->generations=held;orphan->surface=s->surface;orphan->native_window=s->native_window;
        orphan->next=g_present_orphans;g_present_orphans=orphan;++g_present_orphan_count;
        logf_("WINDOW detached old generation; retained=%u maintenance=%d",g_present_orphan_count,g_present_ops.maintenance);
    }else{
        if(s->surface)pvkDestroySurfaceKHR(g_instance,s->surface,NULL);
        if(s->native_window)ANativeWindow_release(s->native_window);
    }
    s->surface=VK_NULL_HANDLE;s->native_window=NULL;
    memset(&s->present,0,sizeof(s->present));
}
static VkResult present_ensure_window(oa_chain *s,ANativeWindow *window,uint64_t generation)
{
    if(!window)return VK_NOT_READY;
    present_gc_orphans();
    /* A formerly detached window may return before its WSI fences finish.
       Do not create a second independent swapchain on that same native window. */
    for(oa_present_orphan *o=g_present_orphans;o;o=o->next)
        if(o->native_window==window)return VK_NOT_READY;
    if(s->surface_lost && s->window_generation==generation)return VK_NOT_READY;
    if(s->native_window==window && s->surface && !s->surface_lost){
        if(s->window_generation!=generation){
            s->window_generation=generation;s->present.recreate=1;
            logf_("WINDOW same owned window resumed generation=%llu; recreate using oldSwapchain",(unsigned long long)generation);
        }
        return VK_SUCCESS;
    }
    if(s->surface){
        EnterCriticalSection(&g_lock);
        VkResult idle=pvkQueueWaitIdle(g_queue);
        LeaveCriticalSection(&g_lock);
        if(idle!=VK_SUCCESS)return idle;
        present_detach_surface(s);
        present_gc_orphans();
        for(oa_present_orphan *o=g_present_orphans;o;o=o->next)
            if(o->native_window==window)return VK_NOT_READY;
    }
    ANativeWindow_acquire(window);
    VkAndroidSurfaceCreateInfoKHR info={VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    info.window=window;
    VkResult result=pvkCreateAndroidSurfaceKHR(g_instance,&info,NULL,&s->surface);
    if(result!=VK_SUCCESS){ANativeWindow_release(window);return result;}
    s->native_window=window;s->window_generation=generation;s->surface_lost=0;
    s->present.recreate=1;s->next_surface_check=0;s->suboptimal_reported=0;
    logf_("WINDOW attached generation=%llu window=%p new_surface=%p",(unsigned long long)generation,(void*)window,(void*)s->surface);
    return VK_SUCCESS;
}
static void present_chain_release(oa_chain *s)
{
    oa_chain **at=&g_present_chains;
    while(*at && *at!=s)at=&(*at)->present_next;
    if(*at)*at=s->present_next;
    present_detach_surface(s);
    if(s->back)o_release(&s->back->o);
}

static void make_back(oa_chain *s) {
    uint32_t d[11] = {0};
    d[0] = s->w; d[1] = s->h; d[2] = 1; d[3] = 1; d[4] = 28; d[5] = 1; d[7] = 0; d[8] = 0x28;
    create_texture(d, NULL, &s->back, 0);
    s->back->is_backbuffer = 1;
    s->back->fmt = s->fmt;
    s->back->desc[4] = s->fmt;
}

static HRESULT COM f_create_chain(obj *f, obj *device, const DXGI_SWAP_CHAIN_DESC *desc, oa_chain **out) {
    oa_chain *s = (oa_chain *)new_obj(sizeof(oa_chain), K_CHAIN);
    (void)f; (void)device;
    extern int oa_present_headroom_mailbox;
    s->mailbox_requested=oa_present_headroom_mailbox;
    s->w=desc->BufferDesc.Width;s->h=desc->BufferDesc.Height;s->fmt=desc->BufferDesc.Format;s->hwnd=desc->OutputWindow;
    region_ceiling_width = s->w;
    region_ceiling_height = s->h;
    render_region_level = OA_RESOLUTION_FULL;
    uint64_t generation;
    ANativeWindow *window=oa_android_present_lock(&generation);
    VkResult attached=present_ensure_window(s,window,generation);
    if(attached==VK_SUCCESS)attached=create_swapchain(s);
    oa_android_present_unlock();
    if(attached!=VK_SUCCESS && attached!=VK_NOT_READY){
        logf_("WINDOW initial attach failed %d",attached);
        present_chain_release(s);free(s);return E_FAIL_;
    }
    s->present_next=g_present_chains;g_present_chains=s;
    make_back(s);
    logf_("chain %ux%u fmt %u hwnd %p", s->w, s->h, s->fmt, (void *)s->hwnd);
    *out = s;
    return S_OK_;
}
static HRESULT COM f_window_assoc(obj *f, HWND h, UINT flags) { (void)f; (void)h; (void)flags; return S_OK_; }

static HRESULT COM s_get_buffer(oa_chain *s, UINT i, const GUID *iid, void **out) {
    (void)i; (void)iid;
    o_addref(&s->back->o);
    *out = s->back;
    return S_OK_;
}

static void pipeline_cache_maybe_save(uint64_t frame);
static uint64_t g_frames_shown;
static int64_t g_t_wait, g_t_acquire, g_t_submit;
void oa_perf_frame(uint64_t,unsigned,unsigned);
#include "oa_waterfall.inc"

#include "oa_thermal.h"
extern int veil_tablet_gameplay_active(void);
extern int rt_android_is_foreground(void);

static void update_render_resolution(oa_chain *s, uint64_t now, int presented)
{
    if (!presented || !rt_android_is_foreground()) {
        oa_resolution_reset(&s->resolution_timing);
        return;
    }

    int enabled = veil_dynamic_resolution_enabled() && !hdr_active && !hdr_requested;
    if (!enabled) oa_resolution_reset(&s->resolution_timing);
    if (enabled && (!veil_tablet_gameplay_active() || now < s->region_ready_after)) {
        oa_resolution_reset(&s->resolution_timing);
        return;
    }

    unsigned level = enabled ? oa_resolution_next(&s->resolution_timing, now, render_region_level)
                             : OA_RESOLUTION_FULL;
    if (enabled) {
        static OaThermal thermal;
        unsigned previous = thermal.floor;
        unsigned floor = oa_thermal_poll(&thermal, now);
        if (floor != previous)
            logf_("THERMAL floor %u -> %u status=%d headroom=%.2f", previous, floor,
                  thermal.last_status, (double)thermal.last_headroom);
        if (level < floor) level = floor;
    }
    if (level == render_region_level) return;
    unsigned width = oa_resolution_dimension(s->w, level);
    unsigned height = oa_resolution_dimension(s->h, level);
    if (level != OA_RESOLUTION_FULL && (width < 640 || height < 360)) return;
    logf_("DYNAMIC_RESOLUTION region %ux%u -> %ux%u time_ns=%llu",
          oa_resolution_dimension(s->w, render_region_level),
          oa_resolution_dimension(s->h, render_region_level),
          width, height, (unsigned long long)now);
    render_region_level = level;
    s->region_step_time = now;
    s->region_ready_after = now + 1000000000ull;
    oa_resolution_reset(&s->resolution_timing);
}

static HRESULT COM s_present(oa_chain *s, UINT sync, UINT flags) {
    LARGE_INTEGER q0, q1, q2;
    oa_frame *f = g_cur;
    oa_context *c = g_ctx;
    uint32_t idx = UINT32_MAX;
    OaPresentAcquired acquired={VK_ERROR_OUT_OF_DATE_KHR,UINT32_MAX,0,0};
    VkResult r;
    VkImageBlit b = {0};
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if(!s->sync_logged){
        s->original_sync=sync;s->original_flags=flags;s->sync_logged=1;
        logf_("PRESENT original SyncInterval=%u flags=%#x; backend_mode=%d headroom=%d (simulation cadence not qualified)",
              sync,flags,s->present_mode,s->mailbox_requested);
    }
    end_rendering(c);
    QueryPerformanceCounter(&q0);
    hdr_poll(s);
    af_poll();
    oa_waterfall_render(s, f, c);
    uint64_t window_generation;
    ANativeWindow *window=oa_android_present_lock(&window_generation);
    r=present_ensure_window(s,window,window_generation);
    if(r!=VK_SUCCESS && r!=VK_NOT_READY)rt_fatal("Vulkan Android window attach failed: %d",r);
    int window_ready=r==VK_SUCCESS;
    if(!window)++g_present_no_window;
    if(window_ready){
        r=present_check_surface(s);
        if(r==VK_ERROR_SURFACE_LOST_KHR){
            s->surface_lost=1;window_ready=0;
            logf_("WINDOW surface lost during capability recheck; wait for next Android generation");
        }else if(r!=VK_SUCCESS)rt_fatal("Vulkan surface capability recheck failed: %d",r);
    }
    if(!window_ready)acquired.result=VK_NOT_READY;
    for(unsigned attempt=0;window_ready && attempt<2;++attempt){
        if(s->present.recreate || !s->present.current){
            VkResult created=create_swapchain(s);
            if(created!=VK_SUCCESS){
                acquired.result=created;
                acquired.index=UINT32_MAX;
                acquired.valid=0;
                break;
            }
        }
        acquired=oa_present_acquire(&s->present,&g_present_ops,g_dev,f->acquired);
        if(acquired.valid){
            if(acquired.result==VK_SUBOPTIMAL_KHR && !(s->suboptimal_reported&1)){
                s->suboptimal_reported|=1;
                logf_("PRESENT acquire result=%d SUBOPTIMAL usable; recheck capabilities, no unconditional recreation",acquired.result);
            }
            break;
        }
        if(acquired.result!=VK_ERROR_OUT_OF_DATE_KHR)break;
    }
    QueryPerformanceCounter(&q1);
    g_t_acquire += q1.QuadPart - q0.QuadPart;
    if(acquired.result==VK_ERROR_SURFACE_LOST_KHR){
        s->surface_lost=1;
        logf_("WINDOW acquire surface lost result=%d; wait for next Android generation",acquired.result);
    }
    if(!acquired.valid && acquired.result!=VK_ERROR_OUT_OF_DATE_KHR &&
       acquired.result!=VK_ERROR_SURFACE_LOST_KHR &&
       acquired.result!=VK_TIMEOUT && acquired.result!=VK_NOT_READY)
        rt_fatal("Vulkan acquire failed before blit/submission: %d",acquired.result);
    if(acquired.valid){
        idx=acquired.index;
    if (hdr_active) {
        veil_title_presented(0);
        hdr_render_present(s, f, idx);
    } else {
    barrier_all(f->cmd);
    image_layout(f->cmd, s->present.current->images[idx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    b.srcSubresource.aspectMask = b.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.srcSubresource.layerCount = b.dstSubresource.layerCount = 1;
    b.srcOffsets[1].x = (int32_t)s->w; b.srcOffsets[1].y = (int32_t)s->h; b.srcOffsets[1].z = 1;
    OaViewport viewport = oa_centered_viewport(s->extent.width, s->extent.height, s->w, s->h);
    OaViewport crop = oa_title_crop(s->extent.width, s->extent.height, s->w, s->h);
    int cover = VEIL_COVER_NONE;
    if (crop.width < (int)s->w) {
        if (veil_options_is_visible())
            cover = VEIL_COVER_OPTIONS;
        else if (veil_title_is_visible())
            cover = VEIL_COVER_TITLE;
    }
    if (cover) {
        int offset = cover == VEIL_COVER_TITLE ? crop.x : 0;
        b.srcOffsets[0].x = offset;
        b.srcOffsets[1].x = offset + crop.width;
        viewport = (OaViewport){.width = (int)s->extent.width, .height = (int)s->extent.height};
    }
    veil_title_presented(cover);
    if (viewport.width != (int32_t)s->extent.width || viewport.height != (int32_t)s->extent.height) {
        const VkClearColorValue black = { .float32 = {0, 0, 0, 1} };
        const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        pvkCmdClearColorImage(f->cmd, s->present.current->images[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
        VkImageMemoryBarrier clear = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s->present.current->images[idx],
            .subresourceRange = range,
        };
        pvkCmdPipelineBarrier(f->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, NULL, 0, NULL, 1, &clear);
    }
    b.dstOffsets[0] = (VkOffset3D){viewport.x, viewport.y, 0};
    b.dstOffsets[1] = (VkOffset3D){viewport.x + viewport.width, viewport.y + viewport.height, 1};
    pvkCmdBlitImage(f->cmd, s->back->image, VK_IMAGE_LAYOUT_GENERAL, s->present.current->images[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
    image_layout(f->cmd, s->present.current->images[idx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    }else{
        ++g_present_deferred;
        if(window_ready || (g_present_deferred%120)==1)logf_("PRESENT deferred acquire=%d window_ready=%d; offscreen submit only",acquired.result,window_ready);
    }
    if (f->timing_queries)
        pvkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f->timing_queries, 1);
    if(pvkEndCommandBuffer(f->cmd)!=VK_SUCCESS)rt_fatal("Vulkan frame command buffer end failed");
    si.waitSemaphoreCount = acquired.valid?1:0;
    si.pWaitSemaphores = &f->acquired;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f->cmd;
    si.signalSemaphoreCount = acquired.valid?1:0;
    si.pSignalSemaphores = acquired.valid?&s->present.current->finished[idx]:NULL;
    QueryPerformanceCounter(&q1);
    EnterCriticalSection(&g_lock);
    r=pvkQueueSubmit(g_queue,1,&si,f->fence);
    if(r!=VK_SUCCESS){
        LeaveCriticalSection(&g_lock);
        rt_fatal("Vulkan queue submit failed; frame fence not armed: %d",r);
    }
    f->submitted=1;
    ++g_present_submitted;
    oa_present_submitted(&s->present,&acquired,g_frame);
    if(acquired.valid){
        pi.waitSemaphoreCount=1;
        pi.pWaitSemaphores=&s->present.current->finished[idx];
        pi.swapchainCount=1;
        pi.pSwapchains=&s->present.current->handle;
        pi.pImageIndices=&idx;
        VkSwapchainPresentFenceInfoEXT present_fence={VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
        if(acquired.present_fence){
            present_fence.swapchainCount=1;present_fence.pFences=&acquired.present_fence;
            pi.pNext=&present_fence;
            oa_present_fence_queued(s->present.current,acquired.present_fence);
        }
        r=pvkQueuePresentKHR(g_queue,&pi);
        oa_present_result(&s->present,idx,r);
        if(r==VK_SUBOPTIMAL_KHR && !(s->suboptimal_reported&2)){
            s->suboptimal_reported|=2;
            logf_("PRESENT present result=%d SUBOPTIMAL usable; recheck capabilities, no unconditional recreation",r);
        }
        if(r==VK_SUCCESS || r==VK_SUBOPTIMAL_KHR)++g_present_requests;
        else if(r==VK_ERROR_OUT_OF_DATE_KHR)
            logf_("PRESENT out of date after submit; recreate next frame");
        else if(r==VK_ERROR_SURFACE_LOST_KHR){
            s->surface_lost=1;
            logf_("WINDOW present surface lost result=%d; wait for next Android generation",r);
        }else{
            LeaveCriticalSection(&g_lock);
            rt_fatal("Vulkan present failed after valid submission: %d",r);
        }
    }
    LeaveCriticalSection(&g_lock);
    oa_android_present_unlock();
    if(!window_ready)usleep(16000); /* Avoid an uncapped render loop while the app has no surface. */
    QueryPerformanceCounter(&q2);
    f->submit_time_ns = q2.QuadPart;
    int presented = acquired.valid && (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR);
    if (presented && s->displayed_region_level != render_region_level) {
        logf_("DYNAMIC_RESOLUTION presented %ux%u time_ns=%llu ready=1 scaled_passes=%u frame=%llu step_ns=%llu",
              oa_resolution_dimension(s->w, render_region_level),
              oa_resolution_dimension(s->h, render_region_level), (unsigned long long)q2.QuadPart,
              render_scaled_passes, (unsigned long long)g_frame, (unsigned long long)s->region_step_time);
        s->displayed_region_level = render_region_level;
    }
    render_scaled_passes = 0;
    g_t_submit += q2.QuadPart - q1.QuadPart;
    g_sample = (g_frames_shown & 7) == 0;
    if (g_frames_shown % 120 == 0) {
        if (pipeline_timing_log) fflush(pipeline_timing_log);
        pipeline_cache_maybe_save(g_frames_shown);
        if (g_frames_shown % 600 == 0) {
            rt_log("TEXTURE_UPLOAD_COUNTS queued=%llu sync=%llu staging_bytes=%llu",
                   (unsigned long long)__atomic_load_n(&texture_upload_calls,__ATOMIC_RELAXED),
                   (unsigned long long)__atomic_load_n(&texture_upload_fallbacks,__ATOMIC_RELAXED),
                   (unsigned long long)__atomic_load_n(&texture_upload_staging_bytes,__ATOMIC_RELAXED));
        }
        int want = GetFileAttributesA("Z:\\storage\\emulated\\0\\Download\\oa\\nocull") == INVALID_FILE_ATTRIBUTES && GetFileAttributesA("nocull") == INVALID_FILE_ATTRIBUTES;
        if (want != g_cull) { g_cull = want; logf_("culling %s at frame %llu", want ? "on" : "off", (unsigned long long)g_frames_shown); }
    }

    static int last_draw_preparation = -1;
    int draw_preparation = __atomic_load_n(&oa_fast_draw_preparation, __ATOMIC_RELAXED);
    if (draw_preparation != last_draw_preparation && g_n_draw) {
        logf_("draw preparation used %s", draw_preparation ? "cached pipeline and lazy descriptor writes" : "full pipeline lookup and eager descriptor writes");
        last_draw_preparation = draw_preparation;
    }
    if (++g_frames_shown % 600 == 1) {
        static DWORD last_tick;
        static long last_draws, last_skipq;
        DWORD now = GetTickCount();
        LARGE_INTEGER fq;
        double ms;
        QueryPerformanceFrequency(&fq);
        ms = 1000.0 / (double)fq.QuadPart / 600.0;
        logf_("frame %llu fps %.1f draws/f %ld skipq/f %ld keptq %ld pipes %u | ms/f gpuwait %.2f acquire %.2f submit %.2f | uploads %lld %.1f ms total | null srv %ld dim bad %ld", (unsigned long long)g_frames_shown, last_tick && now != last_tick ? 600000.0 / (now - last_tick) : 0.0, (g_n_draw - last_draws) / 600, (g_n_skip_query - last_skipq) / 600, g_n_query_kept, g_pipes_n, g_t_wait * ms, g_t_acquire * ms, g_t_submit * ms, (long long)__atomic_exchange_n(&g_uploads,0,__ATOMIC_RELAXED), (double)__atomic_exchange_n(&g_t_upload,0,__ATOMIC_RELAXED) * 1000.0 / (double)fq.QuadPart, g_n_null_srv, g_n_dim_bad);
        logf_("  ms/f draw calls %.2f map %.2f unmap %.2f | per frame bindset %ld newset %ld vb %ld pipe %ld ib %ld | sets %u pools %ld | unbound %ld no-view %ld", g_t_draw * ms * 8, g_t_map * ms * 8, g_t_unmap * ms * 8, g_c_push / 600, g_c_set_miss / 600, g_c_vb / 600, g_c_pipe / 600, g_c_ib / 600, g_desc_n, g_c_pools, g_n_null_srv, g_n_no_view);
        g_c_set_miss = 0;
        logf_("PRESENT counts submitted=%llu gpu_completed=%llu accepted_requests=%llu deferred=%llu no_window=%llu window_generation=%llu mode=%d displayed_fps=unmeasured",
              (unsigned long long)g_present_submitted,(unsigned long long)g_present_completed,
              (unsigned long long)g_present_requests,(unsigned long long)g_present_deferred,
              (unsigned long long)g_present_no_window,(unsigned long long)window_generation,s->present_mode);
        logf_("  culling %d: hidden skipped/f %ld tests/f %ld tests saved/f %ld", g_cull, g_n_culled / 600, g_n_tests / 600, g_n_test_skipped / 600);
        g_n_culled = g_n_tests = g_n_test_skipped = 0;
        g_t_draw = g_t_map = g_t_unmap = 0; g_c_push = g_c_vb = g_c_pipe = g_c_ib = 0;
        g_t_wait = g_t_acquire = g_t_submit = 0;
        last_draws = g_n_draw; last_skipq = g_n_skip_query;
        last_tick = now;
    }
    pass_audit_present();
    g_frame++;
    {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        frame_begin();
        QueryPerformanceCounter(&t1);
        g_t_wait += t1.QuadPart - t0.QuadPart;
    }
    c->pipeline_valid = 0;
    c->cache_valid = 0;
    oa_perf_frame((uint64_t)g_n_draw, oa_resolution_dimension(s->w, render_region_level),
                  oa_resolution_dimension(s->h, render_region_level));
    update_render_resolution(s, q2.QuadPart, presented);
    return S_OK_;
}
static HRESULT COM s_get_desc(oa_chain*s,DXGI_SWAP_CHAIN_DESC*d){memset(d,0,sizeof(*d));d->BufferDesc.Width=s->w;d->BufferDesc.Height=s->h;d->BufferDesc.Format=s->fmt;d->SampleDesc.Count=1;d->BufferUsage=0x20;d->BufferCount=1;d->OutputWindow=s->hwnd;d->Windowed=TRUE;return S_OK_;}
static HRESULT COM s_get_output(oa_chain*s,void**out){(void)s;if(!out)return E_INVALIDARG;*out=NULL;return DXGI_NOT_FOUND;}
static HRESULT COM s_set_fullscreen(oa_chain *s, BOOL f, void *t) { (void)s; (void)f; (void)t; return S_OK_; }
static HRESULT COM s_get_fullscreen(oa_chain *s, BOOL *f, void **t) { (void)s; if (f) *f = FALSE; if (t) *t = NULL; return S_OK_; }
static HRESULT COM s_resize(oa_chain *s, UINT n, UINT w, UINT h, UINT fmt, UINT fl) {
    (void)n; (void)fl;
    if (w) s->w = w;
    if (h) s->h = h;
    if (fmt) s->fmt = fmt;
    logf_("ResizeBuffers %ux%u", s->w, s->h);
    end_rendering(g_ctx);
    o_release(&s->back->o);
    make_back(s);
    region_ceiling_width = s->w;
    region_ceiling_height = s->h;
    render_region_level = OA_RESOLUTION_FULL;
    s->displayed_region_level = OA_RESOLUTION_FULL;
    oa_resolution_reset(&s->resolution_timing);
    return S_OK_;
}
static HRESULT COM s_resize_target(oa_chain *s, const void *d) { (void)s; (void)d; return S_OK_; }
