#include "rt.h"
#include "x86rt.h"

#include <android/log.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define HIDDEN __attribute__((visibility("hidden")))
enum { STOCK_STEP = 1, STEP_COUNT = 4 };
enum { OCCLUSION_OFF, OCCLUSION_STOCK, OCCLUSION_HIGH, OCCLUSION_COUNT };
static const float high_occlusion_strength = 1.35f;
static const unsigned resolutions[STEP_COUNT] = {512, 1024, 1536, 2048};
static const float distances[STEP_COUNT] = {0.75f, 1.0f, 1.25f, 1.5f};
static const double blur_strengths[STEP_COUNT] = {0.0, 0.5, 1.0, 1.5};

static pthread_once_t settings_once = PTHREAD_ONCE_INIT;
static pthread_once_t cascades_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t settings_lock = PTHREAD_MUTEX_INITIALIZER;
static char settings_path[PATH_MAX];
static unsigned requested_resolution = STOCK_STEP;
static unsigned requested_distance = STOCK_STEP;
static unsigned boot_resolution = STOCK_STEP;
static unsigned boot_distance = STOCK_STEP;
static unsigned requested_vegetation = STOCK_STEP;
static unsigned boot_vegetation = STOCK_STEP;
static unsigned requested_occlusion = OCCLUSION_STOCK;
static unsigned boot_occlusion = OCCLUSION_STOCK;
static unsigned requested_depth_of_field = 2;
static unsigned boot_depth_of_field = 2;
HIDDEN unsigned veil_shadow_map_size = 1024;
HIDDEN float veil_vegetation_scale = 1.0f;
HIDDEN unsigned veil_occlusion_enabled = 1;
HIDDEN double veil_depth_of_field_scale = 1.0;

extern void f_00C43700(X86Ctx *ctx);
extern void f_00D13ED0(X86Ctx *ctx);
extern void f_00CA0AA0(X86Ctx *ctx);
extern void f_00C74280(X86Ctx *ctx);
extern void f_00C6F160(X86Ctx *ctx);

static void load_settings(void)
{
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (root == NULL)
        return;
    int length = snprintf(settings_path, sizeof settings_path, "%s/veil-shadows", root);
    if (length < 0 || (size_t)length >= sizeof settings_path) {
        settings_path[0] = 0;
        return;
    }
    FILE *file = fopen(settings_path, "r");
    if (file != NULL) {
        char line[80];
        if (fgets(line, sizeof line, file) && fgetc(file) == EOF) {
            unsigned resolution, distance, vegetation = STOCK_STEP, occlusion = OCCLUSION_STOCK;
            unsigned depth_of_field = 2;
            char extra;
            int fields = sscanf(line, "%u %u %u %u %u %c", &resolution, &distance,
                                &vegetation, &occlusion, &depth_of_field, &extra);
            if (fields >= 2 && fields <= 5 && resolution < STEP_COUNT &&
                distance < STEP_COUNT && vegetation < STEP_COUNT && occlusion < OCCLUSION_COUNT &&
                depth_of_field < STEP_COUNT) {
                requested_resolution = resolution;
                requested_distance = distance;
                requested_vegetation = vegetation;
                requested_occlusion = occlusion;
                requested_depth_of_field = depth_of_field;
            }
        }
        fclose(file);
    }
    boot_resolution = requested_resolution;
    veil_shadow_map_size = resolutions[boot_resolution];
    boot_distance = requested_distance;
    boot_vegetation = requested_vegetation;
    boot_occlusion = requested_occlusion;
    veil_occlusion_enabled = boot_occlusion != OCCLUSION_OFF;
    boot_depth_of_field = requested_depth_of_field;
    veil_depth_of_field_scale = blur_strengths[boot_depth_of_field];
    veil_vegetation_scale = distances[boot_vegetation];
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                        "VEIL_SHADOWS boot map=%u distance=%.2f",
                        veil_shadow_map_size, distances[boot_distance]);
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                        "VEIL_DRAW boot vegetation=%.2f", veil_vegetation_scale);
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                        "VEIL_AO boot mode=%u", boot_occlusion);
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                        "VEIL_DOF boot blur=%.2f", veil_depth_of_field_scale);
}

HIDDEN void veil_shadow_init(void)
{
    pthread_once(&settings_once, load_settings);
}

static unsigned *shadow_setting(const char *section, const char *key)
{
    if (!section || !key || strcasecmp(section, "DEFAULT"))
        return NULL;
    if (!strcasecmp(key, "VeilShadowMapResolution"))
        return &requested_resolution;
    if (!strcasecmp(key, "VeilShadowDistance"))
        return &requested_distance;
    if (!strcasecmp(key, "VeilVegetationDistance"))
        return &requested_vegetation;
    if (!strcasecmp(key, "VeilAmbientOcclusion"))
        return &requested_occlusion;
    if (!strcasecmp(key, "VeilDepthOfField"))
        return &requested_depth_of_field;
    return NULL;
}

HIDDEN int veil_shadow_ini_read(const char *section, const char *key, char value[2])
{
    const unsigned *setting = shadow_setting(section, key);
    if (!setting && section && key && !strcasecmp(section, "DEFAULT")) {
        if (!strcasecmp(key, "VeilShadowMapResolutionActive"))
            setting = &boot_resolution;
        if (!strcasecmp(key, "VeilShadowDistanceActive"))
            setting = &boot_distance;
        if (!strcasecmp(key, "VeilVegetationDistanceActive"))
            setting = &boot_vegetation;
        if (!strcasecmp(key, "VeilAmbientOcclusionActive"))
            setting = &boot_occlusion;
        if (!strcasecmp(key, "VeilDepthOfFieldActive"))
            setting = &boot_depth_of_field;
    }
    if (!setting)
        return 0;
    veil_shadow_init();
    pthread_mutex_lock(&settings_lock);
    value[0] = '0' + *setting;
    value[1] = 0;
    pthread_mutex_unlock(&settings_lock);
    return 1;
}

static int save_settings(void)
{
    if (!settings_path[0])
        return 0;
    char temporary[PATH_MAX + sizeof ".tmp"];
    snprintf(temporary, sizeof temporary, "%s.tmp", settings_path);
    FILE *file = fopen(temporary, "w");
    if (file == NULL)
        return 0;
    int ok = fprintf(file, "%u %u %u %u %u\n", requested_resolution, requested_distance,
                     requested_vegetation, requested_occlusion, requested_depth_of_field) > 0;
    ok = (fclose(file) == 0) && ok;
    if (ok && host_rename(temporary, settings_path))
        return 1;
    unlink(temporary);
    return 0;
}

HIDDEN int veil_shadow_ini_write(const char *section, const char *key, const char *value)
{
    unsigned *setting = shadow_setting(section, key);
    if (!setting)
        return -1;
    int count = setting == &requested_occlusion ? OCCLUSION_COUNT : STEP_COUNT;
    if (!value || value[0] < '0' || value[0] >= '0' + count || value[1])
        return 0;

    veil_shadow_init();
    unsigned selection = value[0] - '0';
    pthread_mutex_lock(&settings_lock);
    unsigned old = *setting;
    int ok = 1;
    if (old != selection) {
        *setting = selection;
        ok = save_settings();
        if (!ok)
            *setting = old;
        __android_log_print(ok ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "OpenAmalur",
                            "VEIL_SHADOWS %s map=%u distance=%.2f vegetation=%.2f ao=%u dof=%.2f",
                            ok ? "requested" : "save failed",
                            resolutions[requested_resolution], distances[requested_distance],
                            distances[requested_vegetation], requested_occlusion,
                            blur_strengths[requested_depth_of_field]);
    }
    pthread_mutex_unlock(&settings_lock);
    return ok;
}

static void apply_cascade_distance(void)
{
    veil_shadow_init();
    const float scale = distances[boot_distance];

    /* The retail startup copies five preset records here. Preserve the close
       cascade splits and expand only the far cascade and its matching cutoff. */
    for (unsigned quality = 1; quality < 5; ++quality) {
        uint32_t preset = 0x019668bc + quality * 0x4c;
        unsigned count = lds32(preset);
        if (count != 3 && count != 4)
            continue;
        uint32_t far_split = preset + 0x1c + count * 4;
        uint32_t cutoff = preset + 0x34;
        float stock = ldsf(far_split);
        if (stock != ldsf(cutoff))
            continue;
        float far = stock * scale;
        if (scale != 1.0f) {
            stsf(far_split, far);
            stsf(cutoff, far);
        }
        __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                            "VEIL_SHADOWS cascade preset=%u count=%u far=%.0f",
                            quality, count, far);
    }
}

HIDDEN void veil_shadow_prepare_cascades(X86Ctx *ctx)
{
    pthread_once(&cascades_once, apply_cascade_distance);
    f_00C43700(ctx);
}

HIDDEN void veil_shadow_build_cascades(X86Ctx *ctx)
{
    /* Argument five is used for the camera's shadow texel alignment. */
    sts32(ctx->esp + 20, veil_shadow_map_size);
    f_00D13ED0(ctx);
}

HIDDEN void veil_vegetation_select_cells(X86Ctx *ctx)
{
    static atomic_uint samples;
    const uint32_t grid = ctx->ecx;
    const uint32_t bounds = lds32(ctx->esp + 8);
    f_00CA0AA0(ctx);
    if (atomic_load_explicit(&samples, memory_order_relaxed) >= 16 ||
        atomic_fetch_add_explicit(&samples, 1, memory_order_relaxed) >= 16)
        return;
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                        "VEIL_DRAW radius=%.0f cells=%d,%d,%d,%d",
                        ldsf(grid + 0x14c) * veil_vegetation_scale,
                        (int32_t)lds32(bounds), (int32_t)lds32(bounds + 4),
                        (int32_t)lds32(bounds + 8), (int32_t)lds32(bounds + 12));
}

HIDDEN void veil_occlusion_render(X86Ctx *ctx)
{
    static atomic_flag reported = ATOMIC_FLAG_INIT;
    /* The caller builds this parameter block on its own stack for each pass.
       The renderer uses the intensity both directly and in its radius falloff. */
    const uint32_t params = lds32(ctx->esp + 12);
    const uint32_t intensity_address = params + 0x3c;
    const float intensity = ldsf(intensity_address);
    const int high = boot_occlusion == OCCLUSION_HIGH;
    const float adjusted = high ? intensity * high_occlusion_strength : intensity;
    if (high)
        stsf(intensity_address, adjusted);
    if (!atomic_flag_test_and_set_explicit(&reported, memory_order_relaxed))
        __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                            "VEIL_AO original pass intensity=%.3f stock=%.3f",
                            adjusted, intensity);
    f_00C74280(ctx);
    if (high)
        stsf(intensity_address, intensity);
}

HIDDEN void veil_depth_of_field_render(X86Ctx *ctx)
{
    static atomic_flag reported = ATOMIC_FLAG_INIT;
    if (!atomic_flag_test_and_set_explicit(&reported, memory_order_relaxed))
        __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                            "VEIL_DOF original pass blur radius=%.1f",
                            6.0 * veil_depth_of_field_scale);
    f_00C6F160(ctx);
}
