#include <android/asset_manager.h>
#include <stdio.h>
#include <stdlib.h>

struct AAsset {
    size_t size;
    unsigned char bytes[];
};

AAsset *AAssetManager_open(AAssetManager *manager, const char *name, int mode)
{
    (void)mode;
    char path[2048];
    int written = snprintf(path, sizeof path, "%s/%s", manager->directory, name);
    if (written < 0 || (size_t)written >= sizeof path) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    AAsset *asset = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        if (size > 0 && size <= 128 * 1024 * 1024) {
            rewind(file);
            asset = malloc(sizeof *asset + (size_t)size);
            if (asset) {
                asset->size = (size_t)size;
                if (fread(asset->bytes, 1, asset->size, file) != asset->size) {
                    free(asset);
                    asset = NULL;
                }
            }
        }
    }
    fclose(file);
    return asset;
}

const void *AAsset_getBuffer(AAsset *asset) { return asset->bytes; }
int64_t AAsset_getLength(AAsset *asset) { return asset->size; }
int64_t AAsset_getLength64(AAsset *asset) { return asset->size; }
void AAsset_close(AAsset *asset) { free(asset); }
