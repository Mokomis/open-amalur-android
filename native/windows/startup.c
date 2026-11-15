#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "rt.h"
#include "veil_native_scaling.h"
#include "veil_environment.h"
#include "waterfall_state.h"

int oa_guest_main(int, char **);
void oa_vk_android_initialize_log(const char *);
void oa_perf_start(const char *);
void veil_shadow_init(void);
void f_01076440(X86Ctx *);
int oa_windows_render_width, oa_windows_render_height;

void veil_windows_acquire_profile(X86Ctx *ctx)
{
    uint32_t devices = ld32(0x01a03ae0u);
    if (devices) {
        uint32_t slot = devices + 0x1274u;
        for (unsigned kind = 1; kind <= 3; ++kind, slot += 0x18)
            if (ld32(slot) == kind && ld8(slot + 0x14) == 0xff)
                st8(slot + 0x14, 0);
    }
    f_01076440(ctx);
}

static void make_path(char path[2048], const char *base, const char *relative)
{
    int written = snprintf(path, 2048, "%s/%s", base, relative);
    if (written < 0 || written >= 2048) rt_fatal("Open Amalur path is too long");
}

static void normalize_separators(char *path)
{
    for (; *path; ++path)
        if (*path == '\\') *path = '/';
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: OpenAmalur <game directory> <profile directory> <Veil asset directory>\n");
        return 2;
    }
    SetProcessDPIAware();
    char game[2048], profile[2048], asset_path[2048];
    if (!_fullpath(game, argv[1], sizeof game) || !_fullpath(profile, argv[2], sizeof profile)
        || !_fullpath(asset_path, argv[3], sizeof asset_path)) return 2;
    normalize_separators(game);
    normalize_separators(profile);
    normalize_separators(asset_path);
    char koa[2048], bink[2048], overlay[2048], log[2048], saves[2048];
    make_path(koa, game, "koa.exe");
    make_path(bink, game, "binkw32.dll");
    if (GetFileAttributesA(koa) == INVALID_FILE_ATTRIBUTES || GetFileAttributesA(bink) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "Choose a Re-Reckoning folder containing koa.exe and binkw32.dll.\n");
        return 2;
    }

    host_mkdir(profile);
    make_path(saves, profile, "offline-saves");
    host_mkdir(saves);
    make_path(saves, profile, "offline-saves/working");
    host_mkdir(saves);
    make_path(overlay, profile, "runtime");
    host_mkdir(overlay);
    if (!SetCurrentDirectoryA(profile)) return 2;

    char settings[2048];
    make_path(settings, overlay, "overlay/kaiko/koa/personal.ini");
    if (GetFileAttributesA(settings) == INVALID_FILE_ATTRIBUTES) {
        char directory[2048];
        make_path(directory, overlay, "overlay");
        host_mkdir(directory);
        make_path(directory, overlay, "overlay/kaiko");
        host_mkdir(directory);
        make_path(directory, overlay, "overlay/kaiko/koa");
        host_mkdir(directory);
        FILE *file = fopen(settings, "wb");
        if (file) {
            static const wchar_t initial[] = L"\xfeff[DEFAULT]\nDisplayFullscreen=0\n";
            fwrite(initial, sizeof(wchar_t), wcslen(initial), file);
            fclose(file);
        }
    }

    _putenv_s("OA_PE_PATH", koa);
    _putenv_s("OA_BINK_PATH", bink);
    _putenv_s("OA_OFFLINE_ROOT", profile);
    _putenv_s("OA_GAME_ROOT", game);
    make_path(log, profile, "pc-runtime.log");
    _putenv_s("ME_PC_LOG_PATH", log);
    make_path(log, profile, "oa-vulkan.log");
    oa_vk_android_initialize_log(log);
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    char cpu_count[16];
    unsigned count = system.dwNumberOfProcessors;
    snprintf(cpu_count, sizeof cpu_count, "%u", count > 8 ? 8 : count);
    _putenv_s("ME_PC_CPUS", cpu_count);
    veil_render_resolution(profile, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                           &oa_windows_render_width, &oa_windows_render_height);

    static AAssetManager assets;
    assets.directory = asset_path;
    veil_waterfall_assets(&assets);
    veil_ambience_load(&assets);
    veil_shadow_init();
    oa_perf_start(profile);
    char *guest_argv[] = {argv[0], koa, game, overlay, NULL};
    return oa_guest_main(4, guest_argv);
}
