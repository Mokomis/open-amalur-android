#include <android/asset_manager.h>

__attribute__((visibility("hidden"))) void veil_waterfall_assets(AAssetManager *);
__attribute__((visibility("hidden"))) AAsset *veil_waterfall_open(void);
__attribute__((visibility("hidden"))) int veil_waterfall_command(const char *, const char *, const char *);
__attribute__((visibility("hidden"))) unsigned veil_waterfall_frame(void);
