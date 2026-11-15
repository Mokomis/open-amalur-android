/* Window publication and WSI transactions. Caller supplies Android/pthread APIs. */
#ifndef OA_ANDROID_WINDOW_LIFECYCLE_H
#define OA_ANDROID_WINDOW_LIFECYCLE_H
typedef struct {
    pthread_mutex_t mutex;
    ANativeWindow *window;
    uint64_t generation;
} OaAndroidWindowState;
static OaAndroidWindowState oa_window_state={PTHREAD_MUTEX_INITIALIZER,NULL,0};
/* Must run during APP_CMD_INIT/TERM_WINDOW, before native_app_glue acknowledges
   the callback. Each published window owns a reference. WSI holds this mutex
   through acquire/submit/present, so TERM acknowledgment cannot race those calls. */
static uint64_t oa_window_publish_locked(ANativeWindow *window)
{
    ANativeWindow *old=oa_window_state.window;
    if(window)ANativeWindow_acquire(window);
    oa_window_state.window=window;
    ++oa_window_state.generation;
    uint64_t generation=oa_window_state.generation;
    if(old)ANativeWindow_release(old);
    return generation;
}
static uint64_t oa_window_publish(ANativeWindow *window)
{
    pthread_mutex_lock(&oa_window_state.mutex);
    uint64_t generation=oa_window_publish_locked(window);
    pthread_mutex_unlock(&oa_window_state.mutex);
    return generation;
}
ANativeWindow *oa_android_present_lock(uint64_t *generation)
{
    pthread_mutex_lock(&oa_window_state.mutex);
    *generation=oa_window_state.generation;
    return oa_window_state.window;
}
void oa_android_present_unlock(void)
{
    pthread_mutex_unlock(&oa_window_state.mutex);
}
#endif
