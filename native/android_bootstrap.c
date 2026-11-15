#include <android/log.h>
#include <android/native_activity.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>

void ANativeActivity_onCreate(ANativeActivity *activity, void *state, size_t size)
{
    char path[PATH_MAX];
    int length = snprintf(path, sizeof(path), "%s/local-engine/liboaoriginal.so",
                          activity->internalDataPath);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        ANativeActivity_finish(activity);
        return;
    }

    // The Java launcher verifies the release hash and loads this file first.
    void *engine = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
    void (*start)(ANativeActivity *, void *, size_t) =
        engine ? dlsym(engine, "ANativeActivity_onCreate") : NULL;
    if (!start) {
        __android_log_print(ANDROID_LOG_ERROR, "OpenAmalur", "Local engine is unavailable: %s", dlerror());
        ANativeActivity_finish(activity);
        return;
    }
    start(activity, state, size);
    dlclose(engine);
}
