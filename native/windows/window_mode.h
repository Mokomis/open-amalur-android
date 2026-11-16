#ifndef OA_WINDOWS_WINDOW_MODE_H
#define OA_WINDOWS_WINDOW_MODE_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* Fullscreen temporarily switches the monitor mode. Borderless retains the
   desktop mode; both present a popup covering the monitor, not its work area. */
static struct {
    int requested, effective, changed, changing, ready;
    HWND window;
    MONITORINFOEXW monitor;
    DEVMODEW desktop, fullscreen;
} oa_window_mode;

static void oa_window_mode_restore(void)
{
    if (!oa_window_mode.changed) return;
    LONG status = ChangeDisplaySettingsExW(oa_window_mode.monitor.szDevice,
                                         &oa_window_mode.desktop, NULL, 0, NULL);
    fprintf(stderr, "WINDOW_MODE restore status=%ld\n", status);
    if (status == DISP_CHANGE_SUCCESSFUL) oa_window_mode.changed = 0;
}

static void oa_window_mode_shutdown(void)
{
    oa_window_mode.ready = 0;
    oa_window_mode_restore();
}

static void oa_window_mode_fit(void)
{
    MONITORINFOEXW monitor = {0};
    monitor.cbSize = sizeof monitor;
    HMONITOR target = MonitorFromWindow(oa_window_mode.window, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(target, (MONITORINFO *)&monitor)) return;
    RECT r = monitor.rcMonitor;
    if (oa_window_mode.effective == 2 && oa_window_mode.changed) {
        r.right = r.left + (LONG)oa_window_mode.fullscreen.dmPelsWidth;
        r.bottom = r.top + (LONG)oa_window_mode.fullscreen.dmPelsHeight;
    }
    SetWindowPos(oa_window_mode.window, HWND_TOP, r.left, r.top,
                 r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static int oa_window_mode_apply(void)
{
    LONG status = ChangeDisplaySettingsExW(oa_window_mode.monitor.szDevice,
                                         &oa_window_mode.fullscreen, NULL, CDS_FULLSCREEN, NULL);
    fprintf(stderr, "WINDOW_MODE fullscreen %lux%lu status=%ld\n",
            oa_window_mode.fullscreen.dmPelsWidth, oa_window_mode.fullscreen.dmPelsHeight, status);
    oa_window_mode.changed = status == DISP_CHANGE_SUCCESSFUL;
    return oa_window_mode.changed;
}

static void oa_window_mode_event(HWND window, UINT message, WPARAM value)
{
    if (!oa_window_mode.ready || window != oa_window_mode.window || oa_window_mode.changing) return;
    oa_window_mode.changing = 1;
    if (message == WM_ACTIVATEAPP && oa_window_mode.effective == 2) {
        if (!value) {
            ShowWindow(window, SW_MINIMIZE);
            oa_window_mode_restore();
        } else {
            if (!oa_window_mode.changed && !oa_window_mode_apply()) oa_window_mode.effective = 1;
            ShowWindow(window, SW_RESTORE);
            oa_window_mode_fit();
        }
    } else if (message == WM_DISPLAYCHANGE && oa_window_mode.effective != 0 && !IsIconic(window)) {
        oa_window_mode_fit();
    } else if (message == WM_DESTROY || (message == WM_ENDSESSION && value)) {
        oa_window_mode_shutdown();
    }
    oa_window_mode.changing = 0;
}

static HWND oa_window_mode_create(int width, int height, const wchar_t *title, const wchar_t *class_name)
{
    oa_window_mode.effective = oa_window_mode.requested;
    oa_window_mode.monitor.cbSize = sizeof oa_window_mode.monitor;
    HMONITOR target = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    int have_monitor = GetMonitorInfoW(target, (MONITORINFO *)&oa_window_mode.monitor);
    oa_window_mode.desktop.dmSize = sizeof oa_window_mode.desktop;
    int have_desktop = have_monitor && EnumDisplaySettingsW(oa_window_mode.monitor.szDevice,
                                  ENUM_CURRENT_SETTINGS, &oa_window_mode.desktop);
    if (!have_monitor) oa_window_mode.effective = 0;
    if (oa_window_mode.effective == 2) {
        int found = 0;
        for (DWORD i = 0; have_desktop; ++i) {
            DEVMODEW mode = {0};
            mode.dmSize = sizeof mode;
            if (!EnumDisplaySettingsW(oa_window_mode.monitor.szDevice, i, &mode)) break;
            if (mode.dmPelsWidth != (DWORD)width || mode.dmPelsHeight != (DWORD)height || mode.dmBitsPerPel != 32)
                continue;
            if (mode.dmDisplayOrientation != oa_window_mode.desktop.dmDisplayOrientation) continue;
            oa_window_mode.fullscreen = mode;
            found = 1;
            if (mode.dmDisplayFrequency == oa_window_mode.desktop.dmDisplayFrequency) break;
        }
        if (!found || !oa_window_mode_apply()) {
            oa_window_mode.effective = 1;
            MessageBoxW(NULL, L"This display cannot use the selected fullscreen resolution. "
                        L"The game will use Borderless Fullscreen. Choose another resolution to try Fullscreen again.",
                        L"Open Amalur", MB_OK | MB_ICONINFORMATION);
        }
    }
    DWORD style = oa_window_mode.effective ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT bounds = {0, 0, width, height};
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (oa_window_mode.effective) {
        GetMonitorInfoW(target, (MONITORINFO *)&oa_window_mode.monitor);
        bounds = oa_window_mode.monitor.rcMonitor;
        x = bounds.left; y = bounds.top;
        if (oa_window_mode.effective == 2) {
            bounds.right = bounds.left + width;
            bounds.bottom = bounds.top + height;
        }
    } else {
        AdjustWindowRect(&bounds, style, FALSE);
    }
    HWND window = CreateWindowExW(0, class_name, title, style,
                                 x, y, bounds.right - bounds.left, bounds.bottom - bounds.top,
                                 NULL, NULL, GetModuleHandleW(NULL), NULL);
    oa_window_mode.window = window;
    if (!window) oa_window_mode_restore();
    else {
        oa_window_mode.ready = 1;
        ShowWindow(window, SW_SHOW);
        fprintf(stderr, "WINDOW_MODE requested=%d effective=%d client=%dx%d\n",
                oa_window_mode.requested, oa_window_mode.effective,
                bounds.right - bounds.left, bounds.bottom - bounds.top);
    }
    return window;
}
#endif
