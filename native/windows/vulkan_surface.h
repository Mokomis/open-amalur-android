#ifndef OA_WINDOWS_VULKAN_SURFACE_H
#define OA_WINDOWS_VULKAN_SURFACE_H

/* Win32 surface definitions from the Vulkan 1.4 API. The renderer loads them
   through vkGetInstanceProcAddr just like the Android surface entry point. */
typedef struct {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    HINSTANCE hinstance;
    HWND hwnd;
} OaWin32SurfaceCreateInfo;

typedef VkResult (VKAPI_PTR *PFN_vkCreateWin32SurfaceKHR)(
    VkInstance, const OaWin32SurfaceCreateInfo *, const VkAllocationCallbacks *, VkSurfaceKHR *);

#endif
