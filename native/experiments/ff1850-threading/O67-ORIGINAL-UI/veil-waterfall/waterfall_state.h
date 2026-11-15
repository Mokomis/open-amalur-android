#include <android/asset_manager.h>
#include <stdint.h>

typedef struct {
    float left, top, width, height;
} VeilWaterfallRect;

__attribute__((visibility("hidden"))) void veil_waterfall_assets(AAssetManager *);
__attribute__((visibility("hidden"))) const uint8_t *veil_waterfall_load(size_t);
__attribute__((visibility("hidden"))) int veil_waterfall_command(const char *, const char *, const char *);
__attribute__((visibility("hidden"))) unsigned veil_waterfall_frame(VeilWaterfallRect *);
