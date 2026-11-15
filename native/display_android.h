#ifndef OA_DISPLAY_ANDROID_H
#define OA_DISPLAY_ANDROID_H

#include <stdint.h>

typedef struct { int32_t x, y, width, height; } OaViewport;

static inline OaViewport oa_centered_viewport(int32_t width, int32_t height,
                                               int32_t content_width, int32_t content_height)
{
    OaViewport viewport = { .width = width, .height = height };
    if ((int64_t)width * content_height > (int64_t)height * content_width) {
        viewport.width = (int32_t)((int64_t)height * content_width / content_height);
        viewport.x = (width - viewport.width) / 2;
    } else {
        viewport.height = (int32_t)((int64_t)width * content_height / content_width);
        viewport.y = (height - viewport.height) / 2;
    }
    return viewport;
}

static inline int32_t oa_viewport_input(int32_t input, int32_t start, int32_t span, int32_t pixels)
{
    if (span <= 0 || pixels <= 0 || input <= start) return 0;
    if (input - start >= span) return pixels - 1;
    return (int32_t)((int64_t)(input - start) * pixels / span);
}

#endif
