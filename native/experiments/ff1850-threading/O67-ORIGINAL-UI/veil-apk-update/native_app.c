#include "title_cover.h"
#include "veil_native_scaling.h"
#include "waterfall_state.h"
#include "veil_dialogue_touch.h"
#include "veil_environment.h"
#include "../../../../runtime/rt.h"
__attribute__((visibility("hidden"))) void veil_shadow_init(void);
__attribute__((visibility("hidden"))) void veil_tablet_viewport(unsigned, unsigned);
__attribute__((visibility("hidden"))) void veil_tablet_touch_mode(void);
__attribute__((visibility("hidden"))) void veil_tablet_poll_actions(void);
__attribute__((visibility("hidden"))) int veil_tablet_map_open(void);
__attribute__((visibility("hidden"))) int veil_tablet_death_menu_active(void);
__attribute__((visibility("hidden"))) void veil_tablet_death_menu_tap(void);
/* Full original-engine host; no scene replay and no replacement game logic. */
#include <android/native_window.h>
#include <dlfcn.h>
#include <android_native_app_glue.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <android/input.h>
#include <android/window.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
static struct android_app*oa_app;static int started;
static char *selected_storage_path;
static char *selected_source_path;
static pthread_mutex_t input_looper_lock = PTHREAD_MUTEX_INITIALIZER;
static ALooper *input_looper;

__attribute__((visibility("hidden"))) void veil_tablet_wake_input(void)
{
    pthread_mutex_lock(&input_looper_lock);
    if (input_looper) ALooper_wake(input_looper);
    pthread_mutex_unlock(&input_looper_lock);
}

#include "display_android.h"
#include <time.h>
enum { BASE_WIDTH = 1920, BASE_HEIGHT = 1080 };
static int display_width, display_height;
static _Atomic int render_width, render_height;
static _Atomic int buffer_width, buffer_height;
static OaViewport display_viewport;
#include "oa_android_window_lifecycle.h"

static void game_window_size(ANativeActivity *activity, int *width, int *height)
{
    JNIEnv *env = NULL;
    int status = (*activity->vm)->GetEnv(activity->vm, (void **)&env, JNI_VERSION_1_6);
    int attached = status == JNI_EDETACHED;
    if (attached) {
        if ((*activity->vm)->AttachCurrentThread(activity->vm, &env, NULL) != JNI_OK) return;
    } else if (status != JNI_OK) return;
    jclass type = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID method = type ? (*env)->GetMethodID(env, type, "gameWindowSize", "()J") : NULL;
    jlong size = method ? (*env)->CallLongMethod(env, activity->clazz, method) : 0;
    if (type) (*env)->DeleteLocalRef(env, type);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        if (attached) (*activity->vm)->DetachCurrentThread(activity->vm);
        return;
    }
    if (attached) (*activity->vm)->DetachCurrentThread(activity->vm);
    int window_width = (int)((uint64_t)size >> 32);
    int window_height = (int)((uint64_t)size & UINT32_MAX);
    if (window_width > 0 && window_height > 0) {
        *width = window_width;
        *height = window_height;
    }
}

static void game_display_sizes(ANativeActivity *activity, int width, int height)
{
    JNIEnv *env = NULL;
    int status = (*activity->vm)->GetEnv(activity->vm, (void **)&env, JNI_VERSION_1_6);
    int attached = status == JNI_EDETACHED;
    if (attached) {
        if ((*activity->vm)->AttachCurrentThread(activity->vm, &env, NULL) != JNI_OK) return;
    } else if (status != JNI_OK) return;
    jclass type = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID method = type ? (*env)->GetMethodID(env, type, "gameDisplaySizes", "()[I") : NULL;
    jintArray sizes = method ? (jintArray)(*env)->CallObjectMethod(env, activity->clazz, method) : NULL;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (sizes) {
        jsize count = (*env)->GetArrayLength(env, sizes);
        jint *values = (*env)->GetIntArrayElements(env, sizes, NULL);
        if (values) {
            veil_display_resolutions(values, count / 2, width, height);
            char available[8];
            veil_native_scaling_read("DEFAULT", "VeilResolutionCount", available);
            __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                                "DISPLAY_MODES os_modes=%d choices=%s window=%dx%d",
                                count / 2, available, width, height);
            (*env)->ReleaseIntArrayElements(env, sizes, values, JNI_ABORT);
        }
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (sizes) (*env)->DeleteLocalRef(env, sizes);
    if (type) (*env)->DeleteLocalRef(env, type);
    if (attached) (*activity->vm)->DetachCurrentThread(activity->vm);
}

/* Match the drawable to the physical display aspect and letterbox the game image. */
static void configure_drawable(struct android_app *app)
{
    ANativeWindow *window = app->window;
    display_width = ANativeWindow_getWidth(window);
    display_height = ANativeWindow_getHeight(window);
    // A recreated surface can already have scaled buffer dimensions.
    game_window_size(app->activity, &display_width, &display_height);
    if (display_width <= 0 || display_height <= 0) {
        display_width = BASE_WIDTH;
        display_height = BASE_HEIGHT;
    }

    const char *root = selected_storage_path;
    game_display_sizes(app->activity, display_width, display_height);
    int hardware_percent = veil_native_scaling_percent(root);
    int width, height;
    int native = veil_render_resolution(root, display_width, display_height, &width, &height);
    int render_percent = hardware_percent ? hardware_percent : 100;
    render_width = width * render_percent / 100;
    render_height = height * render_percent / 100;
    veil_tablet_viewport(render_width, render_height);
    display_viewport = oa_centered_viewport(display_width, display_height, render_width, render_height);
    buffer_width = (int)((int64_t)display_width * render_width / display_viewport.width);
    buffer_height = (int)((int64_t)display_height * render_height / display_viewport.height);
    int result = ANativeWindow_setBuffersGeometry(window, hardware_percent ? buffer_width : 0,
                                                  hardware_percent ? buffer_height : 0, 0);
    if (!hardware_percent || result) {
        buffer_width = display_width;
        buffer_height = display_height;
    }
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur",
                        "RENDER_SCALE mode=%s percent=%d base=%dx%d game=%dx%d buffer=%dx%d display=%dx%d viewport=%d,%d %dx%d geometry_result=%d hardware_percent=%d",
                        native ? "native" : "fixed", render_percent,
                        width, height,
                        render_width, render_height,
                        buffer_width, buffer_height, display_width, display_height,
                        display_viewport.x, display_viewport.y, display_viewport.width, display_viewport.height, result, hardware_percent);
}

int oa_android_buffer_width(void) { return buffer_width; }
int oa_android_buffer_height(void) { return buffer_height; }

extern int main(int,char**);
void oa_perf_start(const char*);
extern void rt_android_set_window(ANativeWindow*),rt_android_set_foreground(int);
extern ANativeWindow*rt_android_window(void);
extern void rt_android_key(uint32_t,uint32_t,int,int);
extern void rt_android_mouse_move(int,int),rt_android_mouse_button(int,int,int,int);
extern void rt_android_mouse_wheel(int,int,int);
extern void oa_vk_android_initialize_log(const char*);
ANativeWindow*oa_android_window(void){return rt_android_window();}
int oa_android_width(void){int width=atomic_load(&render_width);return width>0?width:BASE_WIDTH;}
int oa_android_height(void){int height=atomic_load(&render_height);return height>0?height:BASE_HEIGHT;}
int oa_android_connectivity(void){if(!oa_app)return -1;JNIEnv*env=NULL;JavaVM*vm=oa_app->activity->vm;int attach=(*vm)->GetEnv(vm,(void**)&env,JNI_VERSION_1_6)==JNI_EDETACHED;if(attach&&(*vm)->AttachCurrentThread(vm,&env,NULL)!=JNI_OK)return -1;int flags=-1;jobject activity=oa_app->activity->clazz;jclass ac=(*env)->GetObjectClass(env,activity);jmethodID get=(*env)->GetMethodID(env,ac,"getSystemService","(Ljava/lang/String;)Ljava/lang/Object;");jstring name=(*env)->NewStringUTF(env,"connectivity");jobject cm=get?(*env)->CallObjectMethod(env,activity,get,name):NULL;if(cm&&!(*env)->ExceptionCheck(env)){jclass cc=(*env)->GetObjectClass(env,cm);jmethodID gn=(*env)->GetMethodID(env,cc,"getActiveNetwork","()Landroid/net/Network;");jobject network=gn?(*env)->CallObjectMethod(env,cm,gn):NULL;flags=0;if(network){flags=0x20;jmethodID gc=(*env)->GetMethodID(env,cc,"getNetworkCapabilities","(Landroid/net/Network;)Landroid/net/NetworkCapabilities;");jobject cap=gc?(*env)->CallObjectMethod(env,cm,gc,network):NULL;if(cap){jclass cl=(*env)->GetObjectClass(env,cap);jmethodID has=(*env)->GetMethodID(env,cl,"hasCapability","(I)Z");if(has&&(*env)->CallBooleanMethod(env,cap,has,12)&&(*env)->CallBooleanMethod(env,cap,has,16))flags=0x40;(*env)->DeleteLocalRef(env,cl);(*env)->DeleteLocalRef(env,cap);}(*env)->DeleteLocalRef(env,network);}(*env)->DeleteLocalRef(env,cc);(*env)->DeleteLocalRef(env,cm);}if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);flags=-1;}(*env)->DeleteLocalRef(env,name);(*env)->DeleteLocalRef(env,ac);if(attach)(*vm)->DetachCurrentThread(vm);return flags;}
static void*engine_thread(void*unused){(void)unused;const char*root=selected_storage_path;if(!root){__android_log_print(ANDROID_LOG_ERROR,"OpenAmalur","selected game path unavailable");return NULL;}char run[2048],overlay[2048],path[2048],game[2048],koa[2048];snprintf(run,sizeof run,"%s/run-%lld",root,(long long)time(NULL));mkdir(run,0770);snprintf(overlay,sizeof overlay,"%s/overlay",run);mkdir(overlay,0770);chdir(run);
{char folder[2048];snprintf(folder,sizeof folder,"%s/overlay",overlay);mkdir(folder,0770);snprintf(folder,sizeof folder,"%s/overlay/kaiko",overlay);mkdir(folder,0770);snprintf(folder,sizeof folder,"%s/overlay/kaiko/koa",overlay);mkdir(folder,0770);snprintf(folder,sizeof folder,"%s/overlay/kaiko/koa/personal.ini",overlay);FILE*ini=fopen(folder,"wb");if(ini){const char*text="[DEFAULT]\nDisplayFullscreen=0\n";fputc(0xff,ini);fputc(0xfe,ini);for(const char*c=text;*c;c++){fputc(*c,ini);fputc(0,ini);}fclose(ini);}}
snprintf(path,sizeof path,"%s/pc-runtime.log",run);setenv("ME_PC_LOG_PATH",path,1);snprintf(path,sizeof path,"%s/oa-vulkan.log",run);oa_vk_android_initialize_log(path);char install[2048], ready[2048];
snprintf(ready, sizeof ready, "%s/imported-install/.ready", root);
snprintf(install, sizeof install, "%s%s", root, access(ready, R_OK) == 0 ? "/imported-install" : "");
if (selected_source_path) {
    snprintf(install, sizeof install, "%s", selected_source_path);
    snprintf(path, sizeof path, "%s/veil-patch_0.pak", root);
    if (access(path, R_OK) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "OpenAmalur", "SD Veil archive unavailable");
        return NULL;
    }
    setenv("OA_VEIL_PATCH_PATH", path, 1);
} else unsetenv("OA_VEIL_PATCH_PATH");
snprintf(koa,sizeof koa,"%s/koa.exe",install);snprintf(game,sizeof game,"%s%s",install,selected_source_path?"":"/game");snprintf(path,sizeof path,"%s/binkw32.dll",install);setenv("OA_BINK_PATH",path,1);setenv("OA_OFFLINE_ROOT",root,1);setenv("OA_GAME_ROOT",game,1);veil_shadow_init();{long online=sysconf(_SC_NPROCESSORS_ONLN);int cpus=online>0?(online>8?8:(int)online):1;char config[2048],value[16];snprintf(config,sizeof config,"%s/guest-cpus",root);FILE*cf=fopen(config,"r");if(cf){int requested=0;if(fscanf(cf,"%d",&requested)==1&&requested>=1&&requested<=8&&requested<=online)cpus=requested;fclose(cf);}snprintf(value,sizeof value,"%d",cpus);setenv("ME_PC_CPUS",value,1);__android_log_print(ANDROID_LOG_INFO,"OpenAmalur","ENGINE_CPUS online=%ld reported=%d; existing ordered guest memory enabled",online,cpus);}setenv("ME_PC_MOUSE_CAPTURE","0",1);snprintf(path,sizeof path,"%s/fatesworn-trace",root);if (access(path,F_OK)==0) setenv("ME_PC_LOG_FILES","1",1); else unsetenv("ME_PC_LOG_FILES");unsetenv("ME_PC_LOG_EH");setenv("OA_PE_PATH",koa,1);
FILE*f=fopen("nocull","wb");if(f){fputs("Disabled during initial native parity bring-up; original engine decides visibility.\n",f);fclose(f);}/* runtime option, no original game state patched */
snprintf(path,sizeof path,"%s/latest-run.txt",root);f=fopen(path,"w");if(f){fputs(run,f);fclose(f);}__android_log_print(ANDROID_LOG_INFO,"OpenAmalur","starting original compiled game, native ARM64; log=%s",run);
oa_perf_start(root);char*argv[]={"OpenAmalur",koa,game,overlay,NULL};int rc=main(4,argv);__android_log_print(ANDROID_LOG_INFO,"OpenAmalur","original entry returned %d",rc);return NULL;}
void oa_gamepad_init(JNIEnv *, jobject);
void oa_gamepad_focus(int);
void oa_gamepad_poll(void);
int oa_gamepad_input(AInputEvent *);

static void app_cmd(struct android_app*app,int32_t cmd){if(cmd==APP_CMD_INIT_WINDOW&&app->window){pthread_mutex_lock(&oa_window_state.mutex);configure_drawable(app);uint64_t generation=oa_window_publish_locked(app->window);pthread_mutex_unlock(&oa_window_state.mutex);__android_log_print(ANDROID_LOG_INFO,"OpenAmalur","WINDOW init generation=%llu window=%p game=%dx%d",(unsigned long long)generation,(void*)app->window,oa_android_width(),oa_android_height());void *window_api = dlopen("libnativewindow.so", RTLD_NOW | RTLD_LOCAL); int32_t (*set_rate)(ANativeWindow *,float,int8_t)=window_api?dlsym(window_api,"ANativeWindow_setFrameRate"):NULL;int32_t vote=set_rate?set_rate(app->window,120.0f,0):-1;__android_log_print(ANDROID_LOG_INFO,"OpenAmalur","VEIL_REFRESH requested=120 status=%d",vote);if(window_api)dlclose(window_api);rt_android_set_window(app->window);rt_android_set_foreground(1);if(!started){started=1;pthread_t t;pthread_attr_t a;pthread_attr_init(&a);pthread_attr_setstacksize(&a,16u<<20);pthread_attr_setdetachstate(&a,PTHREAD_CREATE_DETACHED);int rc=pthread_create(&t,&a,engine_thread,NULL);pthread_attr_destroy(&a);if(rc)__android_log_print(ANDROID_LOG_ERROR,"OpenAmalur","engine thread creation failed %d",rc);}}else if(cmd==APP_CMD_TERM_WINDOW){uint64_t generation=oa_window_publish(NULL);rt_android_set_window(NULL);__android_log_print(ANDROID_LOG_INFO,"OpenAmalur","WINDOW term generation=%llu retained_game=%dx%d",(unsigned long long)generation,oa_android_width(),oa_android_height());}else if(cmd==APP_CMD_GAINED_FOCUS){
    oa_gamepad_focus(1);
    rt_android_set_foreground(1);
 }else if(cmd==APP_CMD_LOST_FOCUS){
    oa_gamepad_focus(0);
    rt_android_set_foreground(0);
 }}
static unsigned scan_code(unsigned vk){static const unsigned char letters[26]={30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};if(vk>='A'&&vk<='Z')return letters[vk-'A'];if(vk>=49&&vk<=57)return vk-47;if(vk==48)return 11;if(vk==13)return 28;if(vk==27)return 1;if(vk==32)return 57;return 0;}
static struct {
    int x, y;
    int active, pressed, ended;
    uint64_t due;
} touch;

static uint64_t touch_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void finish_touch(void)
{
    if (touch.pressed)
        rt_android_mouse_button(0, 0, touch.x, touch.y);
    touch.active = touch.pressed = touch.ended = 0;
    touch.due = 0;
}

static void poll_touch(void)
{
    if (!touch.due || touch_ms() < touch.due) return;
    if (touch.pressed) {
        finish_touch();
        return;
    }
    rt_android_mouse_button(0, 1, touch.x, touch.y);
    touch.pressed = 1;
    touch.due = touch.ended ? touch_ms() + 45 : 0;
}

static void touch_event(int action, int x, int y)
{
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        finish_touch();
        touch.active = 1;
        touch.due = touch_ms() + 55;
    }
    if (action == AMOTION_EVENT_ACTION_CANCEL) {
        finish_touch();
        return;
    }
    if (!touch.active || touch.ended) return;
    touch.x = x;
    touch.y = y;
    rt_android_mouse_move(x, y);
    if (action == AMOTION_EVENT_ACTION_UP) {
        if (touch.pressed) finish_touch();
        else touch.ended = 1;
    }
}

static int death_menu_touch(int action, int x, int y)
{
    static int captured, start_x, start_y;
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        captured = veil_tablet_death_menu_active();
        start_x = x;
        start_y = y;
    }
    if (!captured) return 0;
    if (action == AMOTION_EVENT_ACTION_UP) {
        if (abs(x - start_x) * 80 < render_width && abs(y - start_y) * 80 < render_height) {
            rt_android_mouse_move(x, y);
            veil_tablet_death_menu_tap();
        }
        captured = 0;
    } else if (action == AMOTION_EVENT_ACTION_CANCEL) {
        captured = 0;
    }
    return 1;
}

static int map_pinch(AInputEvent *event, int action, int x, int y)
{
    static int pinching;
    static float last_span;
    if (action == AMOTION_EVENT_ACTION_DOWN) pinching = 0;
    if (action == AMOTION_EVENT_ACTION_POINTER_DOWN && !pinching && veil_tablet_map_open()) {
        finish_touch();
        pinching = 1;
        last_span = 0;
    }
    if (!pinching) return 0;
    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL)
        pinching = 0;
    if (AMotionEvent_getPointerCount(event) < 2 || !veil_tablet_map_open()) return 1;
    float span = hypotf(AMotionEvent_getX(event, 1) - AMotionEvent_getX(event, 0),
                         AMotionEvent_getY(event, 1) - AMotionEvent_getY(event, 0));
    if (action == AMOTION_EVENT_ACTION_POINTER_DOWN || last_span == 0) last_span = span;
    if (action == AMOTION_EVENT_ACTION_MOVE && span > 0 &&
        (span > last_span * 1.15f || span < last_span / 1.15f)) {
        rt_android_mouse_move(x, y);
        rt_android_mouse_wheel(span > last_span ? 120 : -120, x, y);
        last_span = span;
    }
    return 1;
}

static void mouse_event(AInputEvent *event, int action, int x, int y)
{
    static int previous_buttons;
    static const int button_mask[] = {
        AMOTION_EVENT_BUTTON_PRIMARY, AMOTION_EVENT_BUTTON_SECONDARY, AMOTION_EVENT_BUTTON_TERTIARY
    };
    rt_android_mouse_move(x, y);
    if (action == AMOTION_EVENT_ACTION_SCROLL) {
        float scroll = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
        rt_android_mouse_wheel((int)lroundf(fminf(100, fmaxf(-100, scroll)) * 120), x, y);
    }
    int buttons = action == AMOTION_EVENT_ACTION_CANCEL ? 0 : AMotionEvent_getButtonState(event);
    for (int i = 0; i < 3; ++i) {
        if ((buttons ^ previous_buttons) & button_mask[i])
            rt_android_mouse_button(i, !!(buttons & button_mask[i]), x, y);
    }
    previous_buttons = buttons;
}

static int intro_movie_input(AInputEvent *event)
{
    int source = AInputEvent_getSource(event);
    switch (AInputEvent_getType(event)) {
    case AINPUT_EVENT_TYPE_MOTION:
        if ((source & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN) {
            int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
            return oa_intro_movie_input(action == AMOTION_EVENT_ACTION_UP);
        }
        return 0;
    case AINPUT_EVENT_TYPE_KEY: {
        int key = AKeyEvent_getKeyCode(event);
        int gamepad = (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD;
        int button = gamepad && ((key >= AKEYCODE_BUTTON_A && key <= AKEYCODE_BUTTON_MODE) ||
                                 (key >= AKEYCODE_DPAD_UP && key <= AKEYCODE_DPAD_CENTER));
        if (button || key == AKEYCODE_ENTER || key == AKEYCODE_SPACE || key == AKEYCODE_ESCAPE)
            return oa_intro_movie_input(AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP);
        return 0;
    }
    default:
        return 0;
    }
}

static int32_t input(struct android_app*app,AInputEvent*e){(void)app;if(intro_movie_input(e)||oa_gamepad_input(e))return 1;int type=AInputEvent_getType(e);
    if(type==AINPUT_EVENT_TYPE_KEY){int key=AKeyEvent_getKeyCode(e),action=AKeyEvent_getAction(e);unsigned vk=0;if(key>=AKEYCODE_A&&key<=AKEYCODE_Z)vk='A'+key-AKEYCODE_A;else if(key>=AKEYCODE_0&&key<=AKEYCODE_9)vk='0'+key-AKEYCODE_0;else if(key==AKEYCODE_ENTER||key==AKEYCODE_BUTTON_A)vk=13;else if(key==AKEYCODE_BACK||key==AKEYCODE_ESCAPE||key==AKEYCODE_BUTTON_B)vk=27;else if(key==AKEYCODE_SPACE)vk=32;if(vk&&(action==AKEY_EVENT_ACTION_DOWN||action==AKEY_EVENT_ACTION_UP)){rt_android_key(vk,scan_code(vk),0,action==AKEY_EVENT_ACTION_DOWN);return 1;}}else if(type==AINPUT_EVENT_TYPE_MOTION){int action=AMotionEvent_getAction(e)&AMOTION_EVENT_ACTION_MASK;int x=AMotionEvent_getX(e,0),y=AMotionEvent_getY(e,0);if(display_width>0&&display_height>0){
        int cover = veil_title_cover_is_presented();
        if (cover) {
            OaViewport crop = oa_title_crop(display_width, display_height, render_width, render_height);
            int offset = cover == VEIL_COVER_TITLE ? crop.x : 0;
            x = offset + oa_viewport_input(x, 0, display_width, crop.width);
            y = oa_viewport_input(y, 0, display_height, render_height);
        } else {
            x = oa_viewport_input(x, display_viewport.x, display_viewport.width, render_width);
            y = oa_viewport_input(y, display_viewport.y, display_viewport.height, render_height);
        }
    }
    if ((AInputEvent_getSource(e) & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN) {
        if (action == AMOTION_EVENT_ACTION_DOWN) veil_tablet_touch_mode();
        if (death_menu_touch(action, x, y)) return 1;
        if (map_pinch(e, action, x, y)) return 1;
        if (veil_dialogue_touch(action, x, y, render_width, render_height)) return 1;
        touch_event(action, x, y);
        return 1;
    }
    if ((AInputEvent_getSource(e) & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE) {
        mouse_event(e, action, x, y);
        return 1;
    }
    rt_android_mouse_move(x,y);if(action==AMOTION_EVENT_ACTION_DOWN)rt_android_mouse_button(0,1,x,y);else if(action==AMOTION_EVENT_ACTION_UP||action==AMOTION_EVENT_ACTION_CANCEL)rt_android_mouse_button(0,0,x,y);return 1;}return 0;}
static int select_game_storage(ANativeActivity *activity, JNIEnv *env)
{
    if (!env) return 0;
    jclass type = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID method = type ? (*env)->GetMethodID(env, type, "gameStoragePath",
                                                   "()Ljava/lang/String;") : NULL;
    jstring path = method ? (jstring)(*env)->CallObjectMethod(env, activity->clazz, method) : NULL;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        path = NULL;
    }
    if (path) {
        const char *text = (*env)->GetStringUTFChars(env, path, NULL);
        if (text) {
            selected_storage_path = strdup(text);
            (*env)->ReleaseStringUTFChars(env, path, text);
        }
        (*env)->DeleteLocalRef(env, path);
    }
    if (type) {
        jmethodID source_method = (*env)->GetMethodID(env, type, "gameSourcePath",
                                                       "()Ljava/lang/String;");
        jstring source = source_method ? (jstring)(*env)->CallObjectMethod(env,
                activity->clazz, source_method) : NULL;
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            source = NULL;
        }
        if (source) {
            const char *value = (*env)->GetStringUTFChars(env, source, NULL);
            if (value) {
                selected_source_path = strdup(value);
                (*env)->ReleaseStringUTFChars(env, source, value);
            }
            (*env)->DeleteLocalRef(env, source);
        }
    }
    if (type) (*env)->DeleteLocalRef(env, type);
    if (!selected_storage_path) return 0;
    __android_log_print(ANDROID_LOG_INFO, "OpenAmalur", "Game storage: %s", selected_storage_path);
    return 1;
}

void android_main(struct android_app *app)
{
    oa_app = app;
    pthread_mutex_lock(&input_looper_lock);
    input_looper = app->looper;
    pthread_mutex_unlock(&input_looper_lock);
    veil_waterfall_assets(app->activity->assetManager);
    veil_ambience_load(app->activity->assetManager);
    app->onAppCmd = app_cmd;
    app->onInputEvent = input;
    JNIEnv *env = NULL;
    JavaVM *vm = app->activity->vm;
    int attach = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED;
    if (attach && (*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) env = NULL;
    if (!select_game_storage(app->activity, env)) {
        __android_log_print(ANDROID_LOG_ERROR, "OpenAmalur", "Selected game storage is unavailable");
        if (attach && env) (*vm)->DetachCurrentThread(vm);
        ANativeActivity_finish(app->activity);
        return;
    }
    if (env) oa_gamepad_init(env, app->activity->clazz);

    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    while (!app->destroyRequested) {
        int events;
        struct android_poll_source *source = NULL;
        int timeout = touch.due ? 10 : 1000;
        int id = ALooper_pollOnce(timeout, NULL, &events, (void **)&source);
        if (id >= 0 && source) source->process(app, source);
        oa_gamepad_poll();
        veil_tablet_poll_actions();
        if (!oa_app->window) finish_touch();
        else poll_touch();
    }
    pthread_mutex_lock(&input_looper_lock);
    input_looper = NULL;
    pthread_mutex_unlock(&input_looper_lock);
    oa_gamepad_focus(0);
    rt_android_set_foreground(0);
    if (attach && env) (*vm)->DetachCurrentThread(vm);
}
