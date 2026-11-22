#ifndef OA_WINDOWS_ASSET_MANAGER_H
#define OA_WINDOWS_ASSET_MANAGER_H

#include <stddef.h>
#include <stdint.h>

typedef struct { const char *directory; } AAssetManager;
typedef struct AAsset AAsset;
enum { AASSET_MODE_BUFFER = 3 };

AAsset *AAssetManager_open(AAssetManager *, const char *, int);
const void *AAsset_getBuffer(AAsset *);
int64_t AAsset_getLength(AAsset *);
int64_t AAsset_getLength64(AAsset *);
void AAsset_close(AAsset *);

#endif
