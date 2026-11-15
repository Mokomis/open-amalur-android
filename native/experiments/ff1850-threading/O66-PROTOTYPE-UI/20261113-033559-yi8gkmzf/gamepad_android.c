#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <jni.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

extern void rt_android_pad(uint32_t index, int connected, uint16_t buttons,
                           uint8_t lt, uint8_t rt, int16_t lx, int16_t ly,
                           int16_t rx, int16_t ry);
extern void rt_android_pad_rumble(uint32_t index, uint16_t *left, uint16_t *right);

enum { MAX_PADS = 4, NO_DEVICE = -1 };
enum {
    DPAD_UP = 1, DPAD_DOWN = 2, DPAD_LEFT = 4, DPAD_RIGHT = 8,
    START = 0x10, BACK = 0x20, LEFT_STICK = 0x40, RIGHT_STICK = 0x80,
    LEFT_BUMPER = 0x100, RIGHT_BUMPER = 0x200,
    BUTTON_A = 0x1000, BUTTON_B = 0x2000, BUTTON_X = 0x4000, BUTTON_Y = 0x8000,
};

typedef struct { int axis; float min, max; } Axis;
typedef struct {
    int id, input_id, vendor, product;
    uint16_t buttons, hat;
    uint8_t trigger[2], trigger_key[2];
    int16_t sticks[4];
    int right_x, right_y;
    Axis triggers[2];
    uint16_t rumble[2];
} Pad;

static Pad pads[MAX_PADS];
static jclass input_device;
static jmethodID get_ids, get_device, get_name, get_vendor, get_product;
static jmethodID get_sources, get_range, range_min, range_max;
static JNIEnv *env;
static jobject rumble_activity;
static jmethodID rumble_method;
static int focused;
static time_t last_scan = -1;

static int has_source(int sources, int source)
{
    return (sources & source) == source;
}

static int controller_source(int source)
{
    return has_source(source, AINPUT_SOURCE_GAMEPAD) ||
           has_source(source, AINPUT_SOURCE_JOYSTICK);
}

#include "oa_pause.h"

static void publish(unsigned slot)
{
    const Pad *pad = &pads[slot];
    if (oa_pause_pad(slot, pad->id != NO_DEVICE && focused,
                     pad->buttons | pad->hat, pad->sticks[0], pad->sticks[1])) return;
    rt_android_pad(slot, pad->id != NO_DEVICE && focused, pad->buttons | pad->hat,
                   pad->trigger_key[0] ? UINT8_MAX : pad->trigger[0],
                   pad->trigger_key[1] ? UINT8_MAX : pad->trigger[1],
                   pad->sticks[0], pad->sticks[1], pad->sticks[2], pad->sticks[3]);
}

static Axis get_axis(jobject device, int axis)
{
    Axis result = { .axis = NO_DEVICE };
    jobject range = (*env)->CallObjectMethod(env, device, get_range, axis, AINPUT_SOURCE_JOYSTICK);
    if (range) {
        result.axis = axis;
        result.min = (*env)->CallFloatMethod(env, range, range_min);
        result.max = (*env)->CallFloatMethod(env, range, range_max);
        (*env)->DeleteLocalRef(env, range);
    }
    return result;
}

static int is_rumble_endpoint(jobject device)
{
    if (!device) return 0;
    jstring name = (*env)->CallObjectMethod(env, device, get_name);
    if (!name) return 0;
    const char *text = (*env)->GetStringUTFChars(env, name, NULL);
    int endpoint = 0;
    if (text) {
        static const char suffix[] = " (rumble)";
        size_t length = strlen(text);
        size_t suffix_length = sizeof suffix - 1;
        endpoint = length >= suffix_length && !strcmp(text + length - suffix_length, suffix);
        (*env)->ReleaseStringUTFChars(env, name, text);
    }
    (*env)->DeleteLocalRef(env, name);
    return endpoint;
}

static void configure_axes(Pad *pad, jobject device)
{
    pad->buttons = pad->hat = 0;
    memset(pad->sticks, 0, sizeof pad->sticks);
    memset(pad->trigger, 0, sizeof pad->trigger);
    memset(pad->trigger_key, 0, sizeof pad->trigger_key);
    pad->right_x = AMOTION_EVENT_AXIS_Z;
    pad->right_y = AMOTION_EVENT_AXIS_RZ;
    pad->triggers[0] = (Axis){ AMOTION_EVENT_AXIS_LTRIGGER, 0, 1 };
    pad->triggers[1] = (Axis){ AMOTION_EVENT_AXIS_RTRIGGER, 0, 1 };
    if (device) {
        if (get_axis(device, pad->right_x).axis == NO_DEVICE &&
            get_axis(device, AMOTION_EVENT_AXIS_RX).axis != NO_DEVICE)
            pad->right_x = AMOTION_EVENT_AXIS_RX;
        if (get_axis(device, pad->right_y).axis == NO_DEVICE &&
            get_axis(device, AMOTION_EVENT_AXIS_RY).axis != NO_DEVICE)
            pad->right_y = AMOTION_EVENT_AXIS_RY;
        const int aliases[] = { AMOTION_EVENT_AXIS_BRAKE, AMOTION_EVENT_AXIS_GAS };
        for (unsigned i = 0; i < 2; ++i) {
            Axis range = get_axis(device, pad->triggers[i].axis);
            if (range.axis == NO_DEVICE) range = get_axis(device, aliases[i]);
            if (range.axis != NO_DEVICE && range.max > range.min) pad->triggers[i] = range;
        }
    }
}

static int pad_slot(int id, jobject device)
{
    int free_slot = NO_DEVICE;
    for (unsigned i = 0; i < MAX_PADS; ++i) {
        if (pads[i].id == id || (pads[i].id != NO_DEVICE && pads[i].input_id == id)) return i;
        if (free_slot == NO_DEVICE && pads[i].id == NO_DEVICE) free_slot = i;
    }
    int vendor = device ? (*env)->CallIntMethod(env, device, get_vendor) : 0;
    int product = device ? (*env)->CallIntMethod(env, device, get_product) : 0;
    if (is_rumble_endpoint(device)) {
        int match = NO_DEVICE;
        for (unsigned i = 0; i < MAX_PADS; ++i) {
            if (pads[i].id == NO_DEVICE || !vendor || !product ||
                pads[i].vendor != vendor || pads[i].product != product) continue;
            if (match != NO_DEVICE) return NO_DEVICE;
            match = i;
        }
        if (match == NO_DEVICE) return NO_DEVICE;
        Pad *pad = &pads[match];
        if (pad->input_id != pad->id) return NO_DEVICE;
        pad->input_id = id;
        configure_axes(pad, device);
        publish(match);
        __android_log_print(ANDROID_LOG_INFO, "OpenAmalur", "GAMEPAD companion device=%d slot=%d", id, match);
        return match;
    }
    if (free_slot == NO_DEVICE) return NO_DEVICE;
    Pad *pad = &pads[free_slot];
    *pad = (Pad){ .id = id, .input_id = id, .vendor = vendor, .product = product };
    configure_axes(pad, device);
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur", "GAMEPAD connected device=%d slot=%d", id, free_slot);
    publish(free_slot);
    return free_slot;
}

void oa_gamepad_init(JNIEnv *thread_env, jobject activity)
{
    env = thread_env;
    rumble_activity = activity;
    jclass activity_class = (*env)->GetObjectClass(env, activity);
    rumble_method = (*env)->GetMethodID(env, activity_class, "rumbleController", "(III)V");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, activity_class);
    for (unsigned i = 0; i < MAX_PADS; ++i) pads[i].id = NO_DEVICE;
    input_device = (*env)->FindClass(env, "android/view/InputDevice");
    get_ids = (*env)->GetStaticMethodID(env, input_device, "getDeviceIds", "()[I");
    get_device = (*env)->GetStaticMethodID(env, input_device, "getDevice", "(I)Landroid/view/InputDevice;");
    get_name = (*env)->GetMethodID(env, input_device, "getName", "()Ljava/lang/String;");
    get_vendor = (*env)->GetMethodID(env, input_device, "getVendorId", "()I");
    get_product = (*env)->GetMethodID(env, input_device, "getProductId", "()I");
    get_sources = (*env)->GetMethodID(env, input_device, "getSources", "()I");
    get_range = (*env)->GetMethodID(env, input_device, "getMotionRange", "(II)Landroid/view/InputDevice$MotionRange;");
    jclass range = (*env)->FindClass(env, "android/view/InputDevice$MotionRange");
    range_min = (*env)->GetMethodID(env, range, "getMin", "()F");
    range_max = (*env)->GetMethodID(env, range, "getMax", "()F");
    (*env)->DeleteLocalRef(env, range);
}

static void update_rumble(unsigned slot, uint16_t left, uint16_t right)
{
    Pad *pad = &pads[slot];
    if (!rumble_method || pad->id == NO_DEVICE ||
        (left == pad->rumble[0] && right == pad->rumble[1])) return;
    pad->rumble[0] = left;
    pad->rumble[1] = right;
    (*env)->CallVoidMethod(env, rumble_activity, rumble_method, pad->id, (int)left, (int)right);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        __android_log_print(ANDROID_LOG_WARN, "OpenAmalur", "Controller vibration request failed");
    }
}

void oa_gamepad_poll(void)
{
    if (!env || !focused) return;
    for (unsigned slot = 0; slot < MAX_PADS; ++slot) {
        if (pads[slot].id == NO_DEVICE) continue;
        uint16_t left, right;
        rt_android_pad_rumble(slot, &left, &right);
        update_rumble(slot, left, right);
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (last_scan == now.tv_sec) return;
    last_scan = now.tv_sec;
    jintArray ids = (*env)->CallStaticObjectMethod(env, input_device, get_ids);
    if (!ids) return;
    const jsize count = (*env)->GetArrayLength(env, ids);
    unsigned seen = 0;
    /* Release absent devices before filling slots so a replacement can connect in this scan. */
    for (unsigned i = 0; i < MAX_PADS; ++i) {
        if (pads[i].id == NO_DEVICE) continue;
        jobject dev = (*env)->CallStaticObjectMethod(env, input_device, get_device, pads[i].id);
        if (dev) {
            if (pads[i].input_id != pads[i].id) {
                jobject companion = (*env)->CallStaticObjectMethod(env, input_device, get_device, pads[i].input_id);
                if (companion) (*env)->DeleteLocalRef(env, companion);
                else {
                    update_rumble(i, 0, 0);
                    pads[i].input_id = pads[i].id;
                    configure_axes(&pads[i], dev);
                    publish(i);
                }
            }
            (*env)->DeleteLocalRef(env, dev);
            seen |= 1u << i;
        } else {
            update_rumble(i, 0, 0);
            pads[i] = (Pad){ .id = NO_DEVICE };
            publish(i);
        }
    }
    if (seen != (1u << MAX_PADS) - 1) {
        for (jsize i = 0; i < count; ++i) {
            jint id;
            (*env)->GetIntArrayRegion(env, ids, i, 1, &id);
            jobject dev = (*env)->CallStaticObjectMethod(env, input_device, get_device, id);
            if (!dev) continue;
            if (controller_source((*env)->CallIntMethod(env, dev, get_sources))) pad_slot(id, dev);
            (*env)->DeleteLocalRef(env, dev);
        }
    }
    (*env)->DeleteLocalRef(env, ids);
}

void oa_gamepad_focus(int active)
{
    if (focused == active) return;
    focused = active;
    last_scan = -1;
    if (active) {
        oa_gamepad_poll();
    } else {
        for (unsigned i = 0; i < MAX_PADS; ++i) {
            Pad *pad = &pads[i];
            update_rumble(i, 0, 0);
            pad->buttons = pad->hat = 0;
            memset(pad->sticks, 0, sizeof pad->sticks);
            memset(pad->trigger, 0, sizeof pad->trigger);
            memset(pad->trigger_key, 0, sizeof pad->trigger_key);
            publish(i);
            pad->id = NO_DEVICE;
        }
    }
}

static uint16_t xinput_button(int key)
{
    switch (key) {
    case AKEYCODE_DPAD_UP: return DPAD_UP;
    case AKEYCODE_DPAD_DOWN: return DPAD_DOWN;
    case AKEYCODE_DPAD_LEFT: return DPAD_LEFT;
    case AKEYCODE_DPAD_RIGHT: return DPAD_RIGHT;
    case AKEYCODE_BUTTON_START: return START;
    case AKEYCODE_BUTTON_SELECT: case AKEYCODE_BACK: return BACK;
    case AKEYCODE_BUTTON_THUMBL: return LEFT_STICK;
    case AKEYCODE_BUTTON_THUMBR: return RIGHT_STICK;
    case AKEYCODE_BUTTON_L1: return LEFT_BUMPER;
    case AKEYCODE_BUTTON_R1: return RIGHT_BUMPER;
    case AKEYCODE_BUTTON_A: case AKEYCODE_DPAD_CENTER: return BUTTON_A;
    case AKEYCODE_BUTTON_B: return BUTTON_B;
    case AKEYCODE_BUTTON_X: return BUTTON_X;
    case AKEYCODE_BUTTON_Y: return BUTTON_Y;
    default: return 0;
    }
}

static int16_t stick(float value)
{
    value = fmaxf(-1.0f, fminf(1.0f, value));
    return (int16_t)lrintf(value * (value < 0.0f ? 32768.0f : 32767.0f));
}

static float axis(AInputEvent *event, int index)
{
    float v = AMotionEvent_getAxisValue(event, index, 0);
    return isfinite(v) ? v : 0.0f;
}

int oa_gamepad_input(AInputEvent *event)
{
    int id = AInputEvent_getDeviceId(event);
    if (!env || !focused || id < 0) return 0;
    int source = AInputEvent_getSource(event);
    int type = AInputEvent_getType(event);
    int slot = NO_DEVICE;
    for (unsigned i = 0; i < MAX_PADS; ++i) {
        if (pads[i].id == NO_DEVICE) continue;
        if (pads[i].id == id || pads[i].input_id == id) { slot = i; break; }
    }
    if (slot == NO_DEVICE && controller_source(source)) {
        jobject dev = (*env)->CallStaticObjectMethod(env, input_device, get_device, id);
        slot = pad_slot(id, dev);
        if (dev) (*env)->DeleteLocalRef(env, dev);
    }
    if (slot == NO_DEVICE) return controller_source(source);
    Pad *pad = &pads[slot];
    if (id != pad->input_id) return controller_source(source);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int key = AKeyEvent_getKeyCode(event);
        int action = AKeyEvent_getAction(event);
        if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) return 0;
        int down = action == AKEY_EVENT_ACTION_DOWN;
        if (key == AKEYCODE_BUTTON_L2 || key == AKEYCODE_BUTTON_R2) {
            pad->trigger_key[key == AKEYCODE_BUTTON_R2] = down;
        } else {
            uint16_t bit = xinput_button(key);
            if (!bit) return 0;
            if (down) pad->buttons |= bit;
            else pad->buttons &= ~bit;
        }
    } else if (type == AINPUT_EVENT_TYPE_MOTION && has_source(source, AINPUT_SOURCE_JOYSTICK)) {
        if ((AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) != AMOTION_EVENT_ACTION_MOVE) return 1;
        pad->sticks[0] = stick(axis(event, AMOTION_EVENT_AXIS_X));
        pad->sticks[1] = stick(-axis(event, AMOTION_EVENT_AXIS_Y));
        pad->sticks[2] = stick(axis(event, pad->right_x));
        pad->sticks[3] = stick(-axis(event, pad->right_y));
        float x = axis(event, AMOTION_EVENT_AXIS_HAT_X), y = axis(event, AMOTION_EVENT_AXIS_HAT_Y);
        pad->hat = (x < -0.5f ? DPAD_LEFT : x > 0.5f ? DPAD_RIGHT : 0) |
                   (y < -0.5f ? DPAD_UP : y > 0.5f ? DPAD_DOWN : 0);
        for (unsigned i = 0; i < 2; ++i) {
            const Axis *trigger = &pad->triggers[i];
            float v = (axis(event, trigger->axis) - trigger->min) / (trigger->max - trigger->min);
            pad->trigger[i] = (uint8_t)lrintf(fmaxf(0.0f, fminf(1.0f, v)) * 255.0f);
        }
    } else {
        return 0;
    }
    publish(slot);
    return 1;
}
