/* Part of oa_vk_fullscreen.c: device setup. Included in place; not compiled on its own. */
/* ---- setup ---- */
static void init_vtables(void) {
    static const uint8_t CHILD[] = {3, 1, 1, 2, 4, 4, 3, 2, 2, 1, 2, 2, 2, 2, 2};
    static const uint8_t DXGI_DEV[] = {3, 1, 1, 4, 3, 4, 3, 2, 6, 4, 2, 2};
    static const uint8_t ADAPTER[] = {3, 1, 1, 4, 3, 4, 3, 3, 2, 3};
    static const uint8_t FACTORY[] = {3, 1, 1, 4, 3, 4, 3, 3, 3, 2, 4, 3};
    static const uint8_t CHAIN[] = {3, 1, 1, 4, 3, 4, 3, 3, 3, 4, 3, 3, 2, 6, 2, 2, 2, 2};
    void **v;
    int k;
    /* Every method fallback is native C; no executable guest-code allocation. */
#define COMMON(v) (v)[0] = (void *)o_qi; (v)[1] = (void *)o_addref; (v)[2] = (void *)o_release
    v = make_vtable("Device", DEVICE_NAMES, DEVICE_ARGS, 43);
    COMMON(v);
    v[3] = (void *)d_create_buffer; v[5] = (void *)d_create_tex2d; v[6] = (void *)d_create_tex3d; v[7] = (void *)d_create_srv; v[9] = (void *)d_create_rtv; v[10] = (void *)d_create_dsv;
    v[11] = (void *)d_create_layout; v[12] = (void *)d_create_vs; v[15] = (void *)d_create_ps; v[20] = (void *)d_create_blend; v[21] = (void *)d_create_depth; v[22] = (void *)d_create_raster;
    v[23] = (void *)d_create_sampler; v[25] = (void *)d_create_predicate; v[24] = (void *)d_create_predicate; v[27] = (void *)d_deferred; v[29] = (void *)d_check_format; v[30] = (void *)d_check_msaa;
    v[34] = (void *)d_get_private; v[35] = (void *)d_set_private; v[37] = (void *)d_feature_level; v[38] = (void *)d_flags; v[39] = (void *)d_removed; v[40] = (void *)d_immediate;
    g_vt[K_DEVICE] = v;
    v = make_vtable("Context", NULL, CONTEXT_ARGS, 115);
    COMMON(v);
    v[3] = (void *)child_get_device; v[7] = (void *)c_set_vs_cb; v[8] = (void *)c_set_ps_srv; v[9] = (void *)c_set_ps; v[10] = (void *)c_set_ps_smp; v[11] = (void *)c_set_vs; v[12] = (void *)c_draw_indexed; v[13] = (void *)c_draw;
    v[14] = (void *)c_map; v[15] = (void *)c_unmap; v[16] = (void *)c_set_ps_cb; v[17] = (void *)c_set_layout; v[18] = (void *)c_set_vb; v[19] = (void *)c_set_ib; v[24] = (void *)c_set_topo; v[25] = (void *)c_set_vs_srv; v[26] = (void *)c_set_vs_smp;
    v[27] = (void *)c_begin; v[28] = (void *)c_end; v[30] = (void *)c_set_predication; v[33] = (void *)c_set_rts; v[35] = (void *)c_set_blend; v[36] = (void *)c_set_depth; v[43] = (void *)c_set_raster; v[44] = (void *)c_set_viewports; v[45] = (void *)c_set_scissors;
    v[46] = (void *)c_copy_subresource; v[50] = (void *)c_clear_rtv; v[53] = (void *)c_clear_dsv; v[110] = (void *)c_clear_state; v[111] = (void *)c_flush;
    g_vt[K_CONTEXT] = v;
    v = make_vtable("DXGIDevice", NULL, DXGI_DEV, 12);
    COMMON(v);
    v[6] = (void *)x_get_parent; v[7] = (void *)x_get_adapter; v[10] = (void *)x_set_priority;
    g_vt[K_DXGI_DEVICE] = v;
    v = make_vtable("Adapter", NULL, ADAPTER, 10);
    COMMON(v);
    v[6] = (void *)x_get_parent; v[7] = (void *)a_enum_outputs; v[8] = (void *)a_get_desc;
    g_vt[K_ADAPTER] = v;
    v = make_vtable("Factory", NULL, FACTORY, 12);
    COMMON(v);
    v[6] = (void *)x_get_parent; v[8] = (void *)f_window_assoc; v[10] = (void *)f_create_chain;
    g_vt[K_FACTORY] = v;
    v = make_vtable("SwapChain", NULL, CHAIN, 18);
    COMMON(v);
    v[6] = (void *)x_get_parent; v[7] = (void *)child_get_device; v[8] = (void *)s_present; v[9] = (void *)s_get_buffer; v[10] = (void *)s_set_fullscreen; v[11] = (void *)s_get_fullscreen; v[12] = (void *)s_get_desc; v[13] = (void *)s_resize; v[14] = (void *)s_resize_target; v[15]=(void*)s_get_output;
    g_vt[K_CHAIN] = v;
    /* Resources, views, shaders, states: DeviceChild is 7 slots, then a few getters. */
    for (k = K_BUFFER; k <= K_PREDICATE; k++) {
        static const char *const NAMES[] = {"Buffer", "Texture", "SRV", "RTV", "DSV", "InputLayout", "VS", "PS", "Blend", "DepthState", "Raster", "Sampler", "Predicate"};
        v = make_vtable(NAMES[k - K_BUFFER], NULL, CHILD, 15);
        COMMON(v);
        v[3] = (void *)child_get_device;
        v[4] = (void *)d_get_private;
        v[5] = (void *)d_set_private;
        if (k == K_BUFFER || k == K_TEX2D) { v[7] = (void *)r_get_type; v[10] = k == K_BUFFER ? (void *)b_get_desc : (void *)t_get_desc; }
        if (k == K_SRV || k == K_RTV || k == K_DSV) v[7] = (void *)v_get_resource;
        g_vt[k] = v;
    }
}

#include "oa_pipeline_cache.h"

static int init_vulkan(void) {
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkCreateInstance create;
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    const char *IEXT[5] = {"VK_KHR_surface", OA_SURFACE_EXTENSION};
    const char *device_extensions[5] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceMapMemoryPlacedFeaturesEXT placed = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT
    };
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance={
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT
    };
    int maintenance_enabled=0;
    VkPhysicalDevice devs[8];
    uint32_t n = 8, i;
    VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    float prio = 1.0f;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceProperties prop;
    VkQueueFamilyProperties qf[16];
    VkFormatProperties fp;
    if (!lib) { logf_("vulkan-1.dll not found"); return 0; }
    gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
    create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    app.apiVersion = VK_API_VERSION_1_3;
    app.pApplicationName = "Opem Amalur";
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = IEXT;
    PFN_vkEnumerateInstanceExtensionProperties instance_extensions=(PFN_vkEnumerateInstanceExtensionProperties)gipa(NULL,"vkEnumerateInstanceExtensionProperties");
    int instance_maintenance=0,instance_cap2=0;
    uint32_t instance_count=0;
    if(instance_extensions && instance_extensions(NULL,&instance_count,NULL)==VK_SUCCESS && instance_count){
        VkExtensionProperties *available=calloc(instance_count,sizeof(*available));
        if(available && instance_extensions(NULL,&instance_count,available)==VK_SUCCESS){
            for(uint32_t j=0;j<instance_count;++j){
                hdr_colorspace |= !strcmp(available[j].extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
                instance_maintenance|=!strcmp(available[j].extensionName,VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
                instance_cap2|=!strcmp(available[j].extensionName,VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
            }
        }
        free(available);
    }
    if(instance_maintenance && instance_cap2) {
        IEXT[ici.enabledExtensionCount++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
        IEXT[ici.enabledExtensionCount++] = VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
    }
    if(hdr_colorspace) IEXT[ici.enabledExtensionCount++] = VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;

    if (create(&ici, NULL, &g_instance)) { logf_("vkCreateInstance failed"); return 0; }
#define X(f) p##f = (PFN_##f)gipa(g_instance, #f);
    VK_FUNCS(X)
#undef X
    pvkEnumeratePhysicalDevices(g_instance, &n, devs);
    g_phys = devs[0];
    for (i = 0; i < n; i++) {
        pvkGetPhysicalDeviceProperties(devs[i], &prop);
        if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { g_phys = devs[i]; break; }
    }
    pvkGetPhysicalDeviceProperties(g_phys, &prop);
    logf_("GPU %s api %u.%u", prop.deviceName, VK_API_VERSION_MAJOR(prop.apiVersion), VK_API_VERSION_MINOR(prop.apiVersion));
    g_ubo_align = prop.limits.minUniformBufferOffsetAlignment;
    af_device_limit = prop.limits.maxSamplerAnisotropy;
    logf_("AF device sampler limit=%.0f", (double)af_device_limit);
    pvkGetPhysicalDeviceMemoryProperties(g_phys, &g_mem);
    n = 16;
    pvkGetPhysicalDeviceQueueFamilyProperties(g_phys, &n, qf);
    for (i = 0; i < n; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_qfam = i; break; }
    short_render_waits = renderer_option("veil-short-render-waits", 0);
    prepare_shader_variants = renderer_option("veil-shader-preparation", 1);
    record_shader_variants = renderer_option("veil-record-shaders", 1);
    logf_("RENDER_WAITS short=%d", short_render_waits);
    unsigned timestamp_bits = qf[g_qfam].timestampValidBits;
    if (timestamp_bits && renderer_option("veil-gpu-timing", 0)) {
        gpu_timing_log = open_renderer_file(".oa-pacing-benchmark/gpu-timing.csv", "w");
        gpu_timestamp_period = prop.limits.timestampPeriod;
        gpu_timestamp_mask = UINT64_MAX >> (64 - timestamp_bits);
        if (gpu_timing_log) fputs("frame,submit_time_ns,gpu_us\n", gpu_timing_log);
    }
    pvkGetPhysicalDeviceFormatProperties(g_phys, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) g_d24s8 = VK_FORMAT_D32_SFLOAT_S8_UINT;
    qi.queueFamilyIndex = g_qfam;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_FALSE;
    f2.pNext = &f13;
    f2.features.textureCompressionBC = VK_TRUE;
    f2.features.samplerAnisotropy = VK_TRUE;
    f2.features.depthClamp = VK_TRUE;
    f2.features.depthBiasClamp = VK_TRUE;
    f2.features.independentBlend = VK_TRUE;
    f2.features.dualSrcBlend = VK_TRUE;
    f2.features.fillModeNonSolid = VK_TRUE;
    f2.features.robustBufferAccess = VK_TRUE;
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qi;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = device_extensions;
    uint32_t extension_count = 0;
    int has_map2 = 0, has_placed = 0, has_maintenance=0, has_metadata=0;
    if (!pvkEnumerateDeviceExtensionProperties(g_phys, NULL, &extension_count, NULL) && extension_count) {
        VkExtensionProperties *extensions = calloc(extension_count, sizeof(*extensions));
        if (extensions && !pvkEnumerateDeviceExtensionProperties(g_phys, NULL, &extension_count, extensions)) {
            for (uint32_t j = 0; j < extension_count; ++j) {
                has_metadata |= !strcmp(extensions[j].extensionName, VK_EXT_HDR_METADATA_EXTENSION_NAME);
                has_maintenance |= !strcmp(extensions[j].extensionName,VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
                has_map2 |= !strcmp(extensions[j].extensionName, VK_KHR_MAP_MEMORY_2_EXTENSION_NAME);
                has_placed |= !strcmp(extensions[j].extensionName, VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME);
            }
        }
        free(extensions);
    }
    if (has_map2 && has_placed) {
        VkPhysicalDeviceFeatures2 features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &placed
        };
        pvkGetPhysicalDeviceFeatures2(g_phys, &features);
        if (placed.memoryMapPlaced) {
            VkPhysicalDeviceMapMemoryPlacedPropertiesEXT limits = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_PROPERTIES_EXT
            };
            VkPhysicalDeviceProperties2 properties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &limits
            };
            pvkGetPhysicalDeviceProperties2(g_phys, &properties);
            g_map_alignment = limits.minPlacedMemoryMapAlignment;
            f13.pNext = &placed;
            device_extensions[1] = VK_KHR_MAP_MEMORY_2_EXTENSION_NAME;
            device_extensions[2] = VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME;
            dci.enabledExtensionCount = 3;
        }
    }
    if(has_maintenance && (instance_maintenance && instance_cap2)){
        VkPhysicalDeviceFeatures2 features={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext=&maintenance;
        pvkGetPhysicalDeviceFeatures2(g_phys,&features);
        if(maintenance.swapchainMaintenance1){
            maintenance.pNext=f13.pNext;f13.pNext=&maintenance;
            device_extensions[dci.enabledExtensionCount++]=VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
            maintenance_enabled=1;
        }
    }
    if (hdr_colorspace && has_metadata)
        device_extensions[dci.enabledExtensionCount++] = VK_EXT_HDR_METADATA_EXTENSION_NAME;
    logf_("WINDOW maintenance extension=%d feature=%u instance_dependencies=%d enabled=%d",
          has_maintenance,maintenance.swapchainMaintenance1,(instance_maintenance && instance_cap2),maintenance_enabled);
    {
        VkResult r = pvkCreateDevice(g_phys, &dci, NULL, &g_dev);
        if (r && dci.enabledExtensionCount > 1) {
            logf_("Vulkan optional device features failed %d, retrying standard device", r);
            f13.pNext = NULL;
            dci.enabledExtensionCount = 1;
            g_map_alignment = 0;maintenance_enabled=0;has_metadata=0;
            r = pvkCreateDevice(g_phys, &dci, NULL, &g_dev);
        }
        if (r) { logf_("vkCreateDevice failed %d", r); return 0; }
    }
#define X(f) p##f = (PFN_##f)pvkGetDeviceProcAddr(g_dev, #f);
    VKD_FUNCS(X)
#undef X
    if (hdr_colorspace && has_metadata)
        hdr_set_metadata = (PFN_vkSetHdrMetadataEXT)pvkGetDeviceProcAddr(g_dev, "vkSetHdrMetadataEXT");
    if (g_map_alignment)
        g_map_placed = (PFN_vkMapMemory2KHR)pvkGetDeviceProcAddr(g_dev, "vkMapMemory2KHR");
    g_dynamic_map_mode = g_map_placed != NULL;
    texture_upload_mode=1;
    rt_log("TEXTURE_UPLOAD_QUEUE mode=%d slots=%u max_bytes=%u",texture_upload_mode,OA_UPLOAD_SLOTS,OA_UPLOAD_LIMIT);
    g_present_ops=(OaPresentOps){
        pvkCreateSwapchainKHR,pvkDestroySwapchainKHR,pvkGetSwapchainImagesKHR,
        pvkCreateSemaphore,pvkDestroySemaphore,pvkAcquireNextImageKHR,
        pvkCreateFence,pvkDestroyFence,pvkGetFenceStatus,pvkResetFences,pvkWaitForFences,maintenance_enabled,
        pvkCreateImageView,pvkDestroyImageView
    };
    logf_("WINDOW present_fence_cleanup active=%d fallback_max_retained_windows=%d",
          maintenance_enabled,OA_MAX_RETAINED_WINDOWS);
    pvkGetDeviceQueue(g_dev, g_qfam, 0, &g_queue);
    {
        VkCommandPoolCreateInfo cp = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        VkCommandBufferAllocateInfo ca = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        VkFenceCreateInfo fc = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkSemaphoreCreateInfo sc = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        pipeline_cache_init();
        start_shader_preparation();
        cp.queueFamilyIndex = g_qfam;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        for (i = 0; i < FRAMES; i++) {
            pvkCreateCommandPool(g_dev, &cp, NULL, &g_f[i].pool);
            ca.commandPool = g_f[i].pool;
            pvkAllocateCommandBuffers(g_dev, &ca, &g_f[i].cmd);
            pvkCreateFence(g_dev, &fc, NULL, &g_f[i].fence);
            pvkCreateSemaphore(g_dev, &sc, NULL, &g_f[i].acquired);
            {
                VkQueryPoolCreateInfo qc = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                qc.queryType = VK_QUERY_TYPE_OCCLUSION;
                qc.queryCount = QUERY_MAX;
                if (pvkCreateQueryPool(g_dev, &qc, NULL, &g_f[i].qpool)) { logf_("query pool failed"); return 0; }
                g_f[i].qlist = calloc(QUERY_MAX, sizeof(oa_pred *));
                g_f[i].qreset = QUERY_MAX;
            }
            if (gpu_timing_log) {
                const VkQueryPoolCreateInfo timing = {
                    .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                    .queryType = VK_QUERY_TYPE_TIMESTAMP,
                    .queryCount = 2,
                };
                if (pvkCreateQueryPool(g_dev, &timing, NULL, &g_f[i].timing_queries) != VK_SUCCESS) {
                    fclose(gpu_timing_log);
                    gpu_timing_log = NULL;
                }
            }
            if (!make_buffer_with_mapping(&g_f[i].rings[0], RING_SIZE,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1, 0)) {
                logf_("ring failed");
                return 0;
            }
            g_f[i].nrings = 1;
        }
        pvkCreateCommandPool(g_dev, &cp, NULL, &g_upload_pool);
        ca.commandPool = g_upload_pool;
        pvkAllocateCommandBuffers(g_dev, &ca, &g_upload_cmd);
        pvkCreateFence(g_dev, &fc, NULL, &g_upload_fence);
    }
    if (!make_buffer(&g_zero_ubo, 65536, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return 0;
    memset(g_zero_ubo.ptr, 0, 65536);
    g_pipes = calloc(g_pipes_cap, sizeof(pipe_entry));
    desc_new_pool();
    return 1;
}

static void init_dummies(void) {
    uint32_t d2[11] = {1, 1, 1, 1, 28, 1, 0, 0, 8, 0, 0}, dc[11] = {1, 1, 1, 6, 28, 1, 0, 0, 8, 0, 4}, d3[9] = {1, 1, 1, 1, 28, 0, 8, 0, 0};
    uint32_t smp[13] = {0x15, 3, 3, 3, 0, 1, 1, 0, 0, 0, 0, 0, 0x7f7fffff};
    create_texture(d2, NULL, &g_dummy2d, 0);
    create_texture(dc, NULL, &g_dummycube, 0);
    create_texture(d3, NULL, &g_dummy3d, 1);
    d_create_srv(NULL, g_dummy2d, NULL, &g_null2d);
    d_create_srv(NULL, g_dummycube, NULL, &g_nullcube);
    d_create_srv(NULL, g_dummy3d, NULL, &g_null3d);
    d_create_sampler(NULL, smp, &g_null_sampler);
}

/* If Present stops, log where every thread is: instruction pointer plus code addresses found on its stack. */
static HRESULT oa_vk_create_device(void *adapter, int type, HMODULE sw, UINT flags, const void *levels, UINT n, UINT sdk, obj **device, UINT *level, oa_context **ctx) {
    static int ready;
    (void)adapter; (void)type; (void)sw; (void)levels; (void)n; (void)sdk;
    logf_("D3D11CreateDevice flags=%x", flags);
    if (!ready) {
        init_vtables();
        if (!init_vulkan()) return E_FAIL_;
        g_device_obj.vtbl = g_vt[K_DEVICE];
        g_device_obj.ref = 1;
        g_device_obj.kind = K_DEVICE;
        g_dxgi_device = new_obj(sizeof(obj), K_DXGI_DEVICE);
        g_adapter = new_obj(sizeof(obj), K_ADAPTER);
        g_factory = new_obj(sizeof(obj), K_FACTORY);
        g_ctx = (oa_context *)new_obj(sizeof(oa_context), K_CONTEXT);
        init_dummies();
        frame_begin();
        ready = 1;

    }
    if (device) { o_addref(&g_device_obj); *device = &g_device_obj; }
    if (ctx) { o_addref(&g_ctx->o); *ctx = g_ctx; }
    if (level) *level = 0xb000;
    return S_OK_;
}

void oa_vk_android_initialize_log(const char*path){InitializeCriticalSection(&g_lock);g_log=fopen(path,"w");}
HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter*a,D3D_DRIVER_TYPE type,HMODULE sw,UINT flags,const D3D_FEATURE_LEVEL*levels,UINT n,UINT sdk,ID3D11Device**device,D3D_FEATURE_LEVEL*level,ID3D11DeviceContext**context){return oa_vk_create_device(a,type,sw,flags,levels,n,sdk,(obj**)device,(UINT*)level,(oa_context**)context);}
