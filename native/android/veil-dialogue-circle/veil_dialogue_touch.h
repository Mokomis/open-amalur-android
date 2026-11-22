#include <stddef.h>

__attribute__((visibility("hidden"))) int veil_dialogue_touch(int action, int x, int y,
                                                             int width, int height);
__attribute__((visibility("hidden"))) int veil_dialogue_command(const char *, const char *, const char *);
__attribute__((visibility("hidden"))) int veil_dialogue_read(const char *, const char *, char *, size_t);
