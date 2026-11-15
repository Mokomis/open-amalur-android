#ifndef OA_TITLE_COVER_H
#define OA_TITLE_COVER_H

#include "display_android.h"
#include <stdint.h>

__attribute__((visibility("hidden"))) int veil_title_is_visible(void);
__attribute__((visibility("hidden"))) int veil_options_is_visible(void);
__attribute__((visibility("hidden"))) void veil_title_presented(int full_screen);
__attribute__((visibility("hidden"))) int veil_title_cover_is_presented(void);

enum { VEIL_COVER_NONE, VEIL_COVER_TITLE, VEIL_COVER_OPTIONS };

/* Trim the sides of the 16:9 title artwork to fill a narrower tablet.
 * The menu layout keeps its text inside the crop on 3:2 through 16:9. */
static inline OaViewport oa_title_crop(int display_w, int display_h, int source_w, int source_h)
{
    OaViewport crop = {.width = source_w, .height = source_h};
    if (display_w > 0 && display_h > 0 && source_w > 0 && source_h > 0
            && (int64_t)display_w * 2 >= (int64_t)display_h * 3
            && (int64_t)display_w * source_h < (int64_t)display_h * source_w) {
        crop.width = (int)((int64_t)source_h * display_w / display_h);
        crop.x = (source_w - crop.width) * 3 / 8;
    }
    return crop;
}

#endif
