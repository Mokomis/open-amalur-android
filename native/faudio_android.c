/* FAudio platform services for an Android NativeActivity. */
#include "FAudio_internal.h"
#include "experiments/ff1850-threading/O67-ORIGINAL-UI/veil-apk-update/veil_environment.h"
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    FAudio *engine;
    AAudioStream *stream;
    pthread_t thread;
    float *buffer;
    int channels, rate, quantum;
    atomic_bool stop;
    int started;
} AudioDevice;

static _Atomic(AudioDevice *) ambience_output;
extern int rt_android_is_foreground(void);

void FAudio_Log(const char *message)
{
    __android_log_write(ANDROID_LOG_INFO, "OpenAmalurAudio", message);
}

static void close_output(AudioDevice *device)
{
    if (!device->stream) return;
    AAudioStream_requestStop(device->stream);
    AAudioStream_close(device->stream);
    device->stream = NULL;
}

static int open_output(AudioDevice *device)
{
    AAudioStreamBuilder *builder = NULL;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) return result;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setSampleRate(builder, device->rate);
    AAudioStreamBuilder_setChannelCount(builder, device->channels);
    result = AAudioStreamBuilder_openStream(builder, &device->stream);
    AAudioStreamBuilder_delete(builder);
    if (result == AAUDIO_OK && (AAudioStream_getSampleRate(device->stream) != device->rate ||
        AAudioStream_getChannelCount(device->stream) != device->channels ||
        AAudioStream_getFormat(device->stream) != AAUDIO_FORMAT_PCM_FLOAT)) {
        close_output(device);
        return AAUDIO_ERROR_INVALID_FORMAT;
    }
    return result;
}

static void *mix_output(void *opaque)
{
    AudioDevice *device = opaque;
    pthread_setname_np(pthread_self(), "OA audio");
    setpriority(PRIO_PROCESS, 0, -5);
    int logged_audio = 0;
    int stream_running = 0;
    while (!atomic_load_explicit(&device->stop, memory_order_relaxed)) {
        if (!rt_android_is_foreground()) {
            if (device->stream && stream_running) {
                close_output(device);
                stream_running = 0;
                FAudio_Log("AAudio suspended in background");
            }
            FAudio_sleep(25);
            continue;
        }
        if (!device->stream) {
            if (open_output(device) != AAUDIO_OK) {
                FAudio_sleep(250);
                continue;
            }
        }
        if (!stream_running) {
            if (AAudioStream_requestStart(device->stream) != AAUDIO_OK) {
                close_output(device);
                FAudio_sleep(250);
                continue;
            }
            stream_running = 1;
            FAudio_Log("AAudio active in foreground");
        }
        const size_t samples = (size_t)device->quantum * device->channels;
        memset(device->buffer, 0, samples * sizeof(float));
        FAudio_INTERNAL_UpdateEngine(device->engine, device->buffer);
        if (!logged_audio) {
            for (size_t i = 0; i < samples; ++i) {
                if (device->buffer[i] > 0.0001f || device->buffer[i] < -0.0001f) {
                    logged_audio = 1;
                    FAudio_Log("AAudio mixer generated non-silent game PCM");
                    break;
                }
            }
        }
        if (logged_audio && device->channels == 2) {
            AudioDevice *unassigned = NULL;
            if (atomic_load(&ambience_output) == NULL)
                atomic_compare_exchange_strong(&ambience_output, &unassigned, device);
            if (atomic_load(&ambience_output) == device)
                veil_ambience_mix(device->buffer, device->quantum, device->rate, rt_android_is_foreground());
        }
        int written = 0;
        while (written < device->quantum && rt_android_is_foreground() &&
               !atomic_load_explicit(&device->stop, memory_order_relaxed)) {
            aaudio_result_t frames = AAudioStream_write(device->stream,
                device->buffer + (size_t)written * device->channels, device->quantum - written, 30000000);
            if (frames < 0) {
                close_output(device);
                stream_running = 0;
                FAudio_sleep(20);
                break;
            }
            written += frames;
        }
    }
    AudioDevice *current = device;
    atomic_compare_exchange_strong(&ambience_output, &current, NULL);
    return NULL;
}

static pthread_once_t mixer_init = PTHREAD_ONCE_INIT;
static void configure_mixer(void) { FAudio_INTERNAL_InitSIMDFunctions(0, 1); }

void FAudio_PlatformAddRef(void) { pthread_once(&mixer_init, configure_mixer); }
void FAudio_PlatformRelease(void) {}

void FAudio_PlatformInit(FAudio *engine, uint32_t flags, uint32_t index,
                        FAudioWaveFormatExtensible *format, uint32_t *size, void **out)
{
    *out = NULL;
    if (index != 0) return;
    AudioDevice *device = calloc(1, sizeof *device);
    if (!device) return;
    device->engine = engine;
    device->channels = format->Format.nChannels;
    device->rate = format->Format.nSamplesPerSec;
    device->quantum = flags & FAUDIO_1024_QUANTUM ? device->rate * 1024 / 48000 : device->rate / 100;
    if (device->quantum <= 0 || device->channels <= 0) {
        free(device);
        return;
    }
    device->buffer = calloc((size_t)device->quantum * device->channels, sizeof(float));
    if (!device->buffer || open_output(device) != AAUDIO_OK) {
        free(device->buffer);
        free(device);
        return;
    }
    *size = device->quantum;
    *out = device;
}

int oa_faudio_start_output(FAudio *engine)
{
    AudioDevice *device = engine->platform;
    if (!device) return 0;
    if (pthread_create(&device->thread, NULL, mix_output, device) != 0) return 0;
    device->started = 1;
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalurAudio", "AAudio started rate=%d channels=%d quantum=%d",
                        device->rate, device->channels, device->quantum);
    return 1;
}

void FAudio_PlatformQuit(void *opaque)
{
    AudioDevice *device = opaque;
    if (!device) return;
    atomic_store_explicit(&device->stop, 1, memory_order_relaxed);
    if (device->started) pthread_join(device->thread, NULL);
    close_output(device);
    free(device->buffer);
    free(device);
}

uint32_t FAudio_PlatformGetDeviceCount(void) { return 1; }

uint32_t FAudio_PlatformGetDeviceDetails(uint32_t index, FAudioDeviceDetails *details)
{
    if (index != 0) return FAUDIO_E_INVALID_CALL;
    memset(details, 0, sizeof *details);
    details->DeviceID[0] = '0';
    const char *name = "Android game audio";
    for (size_t i = 0; name[i]; ++i) details->DisplayName[i] = name[i];
    details->Role = FAudioGlobalDefaultDevice;
    WriteWaveFormatExtensible(&details->OutputFormat, 2, 48000, &DATAFORMAT_SUBTYPE_PCM);
    return 0;
}

typedef struct {
    pthread_t thread;
    FAudioThreadFunc function;
    void *argument;
    int32_t result;
    char name[16];
} AudioThread;

static void *audio_thread(void *opaque)
{
    AudioThread *thread = opaque;
    if (thread->name[0]) pthread_setname_np(pthread_self(), thread->name);
    thread->result = thread->function(thread->argument);
    return NULL;
}

FAudioThread FAudio_PlatformCreateThread(FAudioThreadFunc function, const char *name, void *data)
{
    AudioThread *thread = calloc(1, sizeof *thread);
    if (!thread) return NULL;
    thread->function = function;
    thread->argument = data;
    if (name) snprintf(thread->name, sizeof thread->name, "%s", name);
    if (pthread_create(&thread->thread, NULL, audio_thread, thread)) {
        free(thread);
        return NULL;
    }
    return thread;
}

void FAudio_PlatformWaitThread(FAudioThread handle, int32_t *result)
{
    AudioThread *thread = handle;
    if (!thread) return;
    pthread_join(thread->thread, NULL);
    if (result) *result = thread->result;
    free(thread);
}

void FAudio_PlatformThreadPriority(FAudioThreadPriority priority)
{
    setpriority(PRIO_PROCESS, 0, priority == FAUDIO_THREAD_PRIORITY_LOW ? 5 : 0);
}

uint64_t FAudio_PlatformGetThreadID(void) { return (uintptr_t)pthread_self(); }

FAudioMutex FAudio_PlatformCreateMutex(void)
{
    pthread_mutex_t *mutex = malloc(sizeof *mutex);
    if (!mutex) abort();
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(mutex, &attr)) abort();
    pthread_mutexattr_destroy(&attr);
    return mutex;
}

void FAudio_PlatformDestroyMutex(FAudioMutex mutex) { pthread_mutex_destroy(mutex); free(mutex); }
void FAudio_PlatformLockMutex(FAudioMutex mutex) { pthread_mutex_lock(mutex); }
void FAudio_PlatformUnlockMutex(FAudioMutex mutex) { pthread_mutex_unlock(mutex); }

void FAudio_sleep(uint32_t ms)
{
    struct timespec delay = {ms / 1000, (long)(ms % 1000) * 1000000};
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
}

uint32_t FAudio_timems(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint32_t)((uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000);
}
