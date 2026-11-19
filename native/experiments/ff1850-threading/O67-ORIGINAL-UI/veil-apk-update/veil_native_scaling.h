#ifndef VEIL_NATIVE_SCALING_H
#define VEIL_NATIVE_SCALING_H

__attribute__((visibility("hidden"))) void veil_display_resolutions(const int *sizes, int count, int width, int height);

__attribute__((visibility("hidden"))) int veil_dynamic_resolution_enabled(void);
__attribute__((visibility("hidden"))) int veil_native_scaling_percent(const char *root);
__attribute__((visibility("hidden"))) int veil_render_resolution(const char *root, int window_width, int window_height, int *width, int *height);
__attribute__((visibility("hidden"))) int veil_native_scaling_read(const char *section, const char *key, char *value);
__attribute__((visibility("hidden"))) int veil_native_scaling_write(const char *section, const char *key, const char *value);

#endif
