#ifndef OA_WINDOWS_DISPLAY_MODES_H
#define OA_WINDOWS_DISPLAY_MODES_H

#include <windows.h>
#include <limits.h>
#include <stdlib.h>

typedef struct {
    int *sizes;
    int count, capacity, devices;
    int width, height;
} OaWindowsDisplayModes;

static int oa_windows_add_display_size(OaWindowsDisplayModes *list, DWORD width, DWORD height)
{
    if (!width || !height || width > INT_MAX || height > INT_MAX) return 1;
    for (int i = 0; i < list->count; ++i)
        if (list->sizes[2*i] == (int)width && list->sizes[2*i+1] == (int)height) return 1;
    if (list->count == list->capacity) {
        if (list->capacity > INT_MAX / 2) return 0;
        int capacity = list->capacity ? list->capacity * 2 : 32;
        int *sizes = realloc(list->sizes, (size_t)capacity * 2 * sizeof *sizes);
        if (!sizes) return 0;
        list->sizes = sizes;
        list->capacity = capacity;
    }
    list->sizes[2*list->count] = (int)width;
    list->sizes[2*list->count+1] = (int)height;
    ++list->count;
    return 1;
}

static int oa_windows_device_sizes(OaWindowsDisplayModes *list, const wchar_t *device)
{
    // EnumDisplaySettings filters the driver's modes to those available for
    // this display. Do not request raw modes that disregard monitor support.
    for (DWORD index = 0; ; ++index) {
        DEVMODEW mode = {0};
        mode.dmSize = sizeof mode;
        if (!EnumDisplaySettingsW(device, index, &mode)) break;
        if (!oa_windows_add_display_size(list, mode.dmPelsWidth, mode.dmPelsHeight)) return 0;
    }
    return 1;
}

static int oa_windows_display_modes(OaWindowsDisplayModes *list)
{
    *list = (OaWindowsDisplayModes){0};
    list->width = GetSystemMetrics(SM_CXSCREEN);
    list->height = GetSystemMetrics(SM_CYSCREEN);
    for (DWORD index = 0; ; ++index) {
        DISPLAY_DEVICEW device = {0};
        device.cb = sizeof device;
        if (!EnumDisplayDevicesW(NULL, index, &device, 0)) break;
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) ||
            (device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)) continue;
        ++list->devices;
        if (!oa_windows_device_sizes(list, device.DeviceName)) goto unavailable;
    }
    // Some remote/virtual display drivers expose modes only through the
    // default display name. Still include its modes when device enumeration
    // is unavailable, and retain the current desktop as the safe fallback.
    if (!list->count && !oa_windows_device_sizes(list, NULL)) goto unavailable;
    return list->count > 0;
unavailable:
    free(list->sizes);
    list->sizes = NULL;
    list->count = list->capacity = 0;
    return 0;
}
#endif
