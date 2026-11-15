#ifndef OA_PAUSE_H
#define OA_PAUSE_H

#include <stdint.h>

struct AAssetManager;
struct AInputEvent;

void oa_pause_configure(struct AAssetManager *assets, const char *root);
void oa_pause_native(unsigned active);
int oa_pause_input(const struct AInputEvent *event, float x, float y, int width, int height);
int oa_pause_pad(unsigned slot, int connected, uint16_t buttons, int16_t x, int16_t y);
void oa_pause_poll(void);
#endif
