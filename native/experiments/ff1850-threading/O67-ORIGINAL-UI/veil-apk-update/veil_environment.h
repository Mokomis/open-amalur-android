#ifndef OA_VEIL_ENVIRONMENT_H
#define OA_VEIL_ENVIRONMENT_H

#include <android/asset_manager.h>
#include <stdint.h>

int veil_environment_read(const char *section, const char *key, char value[8]);
int veil_environment_write(const char *section, const char *key, const char *value);
float veil_night_darkness(void);
void veil_ambience_load(AAssetManager *assets);
void veil_ambience_mix(float *output, uint32_t frames, uint32_t rate, int foreground);

#endif
