// ============================================================================
//  ALVR client for iQiyi / Qiyu Dream VR — native glue (ported to latest ALVR)
// ----------------------------------------------------------------------------
//  This file bridges the Qiyu native VR SDK (qiyuapi) to ALVR's streaming core
//  (alvr_client_core). It is a rewrite of the original Oculus-based ALVR client
//  against the *current* ALVR C ABI. The streaming core does all the network
//  encode/decode; we only feed it head/controller tracking and render the
//  decoded video frames into the Qiyu eye buffers.
//
//  Key differences from the old ALVR C ABI (documented so future maintainers
//  know what changed):
//    * alvr_initialize() now takes an AlvrClientCapabilities struct and must be
//      preceded by alvr_initialize_android_context().
//    * alvr_get_prediction_offset_ns() was removed; we predict with
//      qiyu_PredictDisplayTime() instead.
//    * alvr_send_views_config() -> alvr_send_view_params() (AlvrViewParams[2]).
//    * alvr_send_tracking() dropped the OculusHand args; last two pointers are
//      hand-skeleton / eye-gaze (null for Qiyu).
//    * alvr_start_stream_opengl() takes an AlvrStreamConfig (swapchain + foveation).
//    * alvr_render_stream_opengl() / alvr_render_lobby_opengl() take new
//      AlvrStreamViewParams[2] / AlvrLobbyViewParams[2] structs.
//    * alvr_get_frame() now returns bool and fills (timestamp, buffer) pointers.
//    * Decoder is created via alvr_create_decoder() on the DecoderConfig event.
//    * alvr_send_active_interaction_profile() must be sent so the server maps
//      our controller buttons correctly.
// ============================================================================

#include "VrApi_Helpers.h"
#include "VrApi_Input.h"
#include "alvr_client_core.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <deque>
#include <map>
#include <thread>
#include <unistd.h>
#include <vector>
#include <time.h>
#include <stdio.h>
#include "QiyuApi.h"
#include "Hand_Trajectory_Prediction/Hand_Trajectory_Prediction.h"
#include "Jerk_Estimation/Jerk_Estimation.h"
#include "QYRenderTarget.h"

 /**
  * Foveation (QCOM extension). Kept from the original client.
  * https://www.khronos.org/registry/OpenGL/extensions/QCOM/QCOM_texture_foveated.txt
  */
#ifndef GL_EXT_framebuffer_foveated

#define GL_FOVEATION_ENABLE_BIT_QCOM                    0x0001
#define GL_FOVEATION_SCALED_BIN_METHOD_BIT_QCOM         0x0002
#define GL_FOVEATION_SUBSAMPLED_LAYOUT_METHOD_BIT_QCOM  0x0004

#define GL_TEXTURE_FOVEATED_FEATURE_BITS_QCOM           0x8BFB
#define GL_TEXTURE_FOVEATED_MIN_PIXEL_DENSITY_QCOM      0x8BFC

#ifdef GL_GLEXT_PROTOTYPES
GL_APICALL void GL_APIENTRY glTextureFoveationParametersQCOM(GLuint texure, GLuint layer, GLuint focalPoint, GLfloat focalX, GLfloat focalY, GLfloat gainX, GLfloat gainY, GLfloat foveaArea);
#endif

#define GL_APIENTRYP GL_APIENTRY*
typedef void (GL_APIENTRYP PFNGLTEXTUREFOVEATIONPARAMETERSEXT)(GLuint texture, GLuint layer, GLuint focalPoint, GLfloat focalX, GLfloat focalY, GLfloat gainX, GLfloat gainY, GLfloat foveaArea);
PFNGLTEXTUREFOVEATIONPARAMETERSEXT glTextureFoveationParametersQCOM = NULL;

#endif//GL_EXT_framebuffer_foveated

#define NUM_EYE_BUFFERS_      3

// OpenXR interaction profile used to declare Qiyu's controllers to the server.
// Qiyu controllers are touch-like (A/B/X/Y + trigger + grip + thumbstick + menu),
// so we advertise the Oculus Touch profile. If some buttons fail to register on
// the PC side, switch this to "/interaction_profiles/khr/simple_controller".
static const char *QIYU_INTERACTION_PROFILE = "/interaction_profiles/oculus/touch_controller";
static uint64_t QIYU_PROFILE_ID = 0;

// Prediction horizon used for controller trajectory prediction (seconds).
static const float CONTROLLER_PREDICTION_HORIZON_S = 0.035f;

// ---- On-device offline logging (no USB cable needed) ----------------------
// Every log line -- including ALVR's Rust panics, which flow through alvr_log
// -- is appended to a file in the app's PRIVATE directory so the user can read
// it on the next launch (Java shows it in a dialog). The path is set by Java
// via setLogFilePath(); default is the app's internal files dir, which is
// always writable on every Android version without any permission.
// fflush() after each line guarantees the last message before a native crash
// is persisted.
#include <jni.h>
#include <sys/stat.h>
#include <string.h>

static FILE *g_logFile = nullptr;
static char  g_logPath[1024] = "/data/data/alvr.client/files/alvr_runtime.log";

static void openLogFile() {
    if (g_logFile) return;
    // make sure the parent directory exists
    char dir[1024];
    strncpy(dir, g_logPath, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0700);
    }
    g_logFile = fopen(g_logPath, "a");
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_setLogFilePath(JNIEnv *env, jobject, jstring path) {
    const char *p = env->GetStringUTFChars(path, nullptr);
    if (p) {
        strncpy(g_logPath, p, sizeof(g_logPath) - 1);
        g_logPath[sizeof(g_logPath) - 1] = '\0';
        env->ReleaseStringUTFChars(path, p);
        if (g_logFile) { fclose(g_logFile); g_logFile = nullptr; }
        openLogFile();
        if (g_logFile) {
            time_t t = time(nullptr);
            struct tm *lt = localtime(&t);
            char ts[16];
            strftime(ts, sizeof(ts), "%H:%M:%S", lt);
            fprintf(g_logFile, "[%s] === log path set: %s ===\n", ts, g_logPath);
            fflush(g_logFile);
        }
    }
}

void log(AlvrLogLevel level, const char *format, ...) {
    va_list args;
    va_start(args, format);

    char buf[1024];
    int count = vsnprintf(buf, sizeof(buf), format, args);
    if (count > (int) sizeof(buf))
        count = (int) sizeof(buf);
    if (count > 0 && buf[count - 1] == '\n')
        buf[count - 1] = '\0';

    alvr_log(level, buf);

    // Mirror to on-device file for offline (no-USB) debugging.
    openLogFile();
    if (g_logFile) {
        time_t t = time(nullptr);
        struct tm *lt = localtime(&t);
        char ts[16];
        strftime(ts, sizeof(ts), "%H:%M:%S", lt);
        fprintf(g_logFile, "[%s] %s\n", ts, buf);
        fflush(g_logFile);
    }

    va_end(args);
}

#define error(...) log(ALVR_LOG_LEVEL_ERROR, __VA_ARGS__)
#define info(...)  log(ALVR_LOG_LEVEL_INFO, __VA_ARGS__)

namespace QY_GL_EXT
{
	bool InitFunction_Foveation()
	{
		glTextureFoveationParametersQCOM = (PFNGLTEXTUREFOVEATIONPARAMETERSEXT)eglGetProcAddress("glTextureFoveationParametersQCOM");
		if (glTextureFoveationParametersQCOM == NULL)
		{
			error("@@QY_GL_EXT::InitFunction_Foveation, glTextureFoveationParametersQCOM is not supported!");
			return false;
		}
		return true;
	}
	bool IsSupport_Foveation()
	{
		return glTextureFoveationParametersQCOM != NULL;
	}
}

// --- OpenXR input path hashes (computed once at static init) ---------------
uint64_t HEAD_ID = alvr_path_string_to_id("/user/head");
uint64_t LEFT_HAND_ID = alvr_path_string_to_id("/user/hand/left");
uint64_t RIGHT_HAND_ID = alvr_path_string_to_id("/user/hand/right");
uint64_t LEFT_CONTROLLER_HAPTICS_ID = alvr_path_string_to_id("/user/hand/left/output/haptic");
uint64_t RIGHT_CONTROLLER_HAPTICS_ID = alvr_path_string_to_id("/user/hand/right/output/haptic");

uint64_t MENU_CLICK_ID = alvr_path_string_to_id("/user/hand/left/input/menu/click");
uint64_t A_CLICK_ID = alvr_path_string_to_id("/user/hand/right/input/a/click");
uint64_t A_TOUCH_ID = alvr_path_string_to_id("/user/hand/right/input/a/touch");
uint64_t B_CLICK_ID = alvr_path_string_to_id("/user/hand/right/input/b/click");
uint64_t B_TOUCH_ID = alvr_path_string_to_id("/user/hand/right/input/b/touch");
uint64_t X_CLICK_ID = alvr_path_string_to_id("/user/hand/left/input/x/click");
uint64_t X_TOUCH_ID = alvr_path_string_to_id("/user/hand/left/input/x/touch");
uint64_t Y_CLICK_ID = alvr_path_string_to_id("/user/hand/left/input/y/click");
uint64_t Y_TOUCH_ID = alvr_path_string_to_id("/user/hand/left/input/y/touch");
uint64_t LEFT_SQUEEZE_CLICK_ID = alvr_path_string_to_id("/user/hand/left/input/squeeze/click");
uint64_t LEFT_SQUEEZE_VALUE_ID = alvr_path_string_to_id("/user/hand/left/input/squeeze/value");
uint64_t LEFT_TRIGGER_CLICK_ID = alvr_path_string_to_id("/user/hand/left/input/trigger/click");
uint64_t LEFT_TRIGGER_VALUE_ID = alvr_path_string_to_id("/user/hand/left/input/trigger/value");
uint64_t LEFT_TRIGGER_TOUCH_ID = alvr_path_string_to_id("/user/hand/left/input/trigger/touch");
uint64_t LEFT_THUMBSTICK_X_ID = alvr_path_string_to_id("/user/hand/left/input/thumbstick/x");
uint64_t LEFT_THUMBSTICK_Y_ID = alvr_path_string_to_id("/user/hand/left/input/thumbstick/y");
uint64_t LEFT_THUMBSTICK_CLICK_ID = alvr_path_string_to_id("/user/hand/left/input/thumbstick/click");
uint64_t LEFT_THUMBSTICK_TOUCH_ID = alvr_path_string_to_id("/user/hand/left/input/thumbstick/touch");
uint64_t LEFT_THUMBREST_TOUCH_ID = alvr_path_string_to_id("/user/hand/left/input/thumbrest/touch");
uint64_t RIGHT_SQUEEZE_CLICK_ID = alvr_path_string_to_id("/user/hand/right/input/squeeze/click");
uint64_t RIGHT_SQUEEZE_VALUE_ID = alvr_path_string_to_id("/user/hand/right/input/squeeze/value");
uint64_t RIGHT_TRIGGER_CLICK_ID = alvr_path_string_to_id("/user/hand/right/input/trigger/click");
uint64_t RIGHT_TRIGGER_VALUE_ID = alvr_path_string_to_id("/user/hand/right/input/trigger/value");
uint64_t RIGHT_TRIGGER_TOUCH_ID = alvr_path_string_to_id("/user/hand/right/input/trigger/touch");
uint64_t RIGHT_THUMBSTICK_X_ID = alvr_path_string_to_id("/user/hand/right/input/thumbstick/x");
uint64_t RIGHT_THUMBSTICK_Y_ID = alvr_path_string_to_id("/user/hand/right/input/thumbstick/y");
uint64_t RIGHT_THUMBSTICK_CLICK_ID = alvr_path_string_to_id("/user/hand/right/input/thumbstick/click");
uint64_t RIGHT_THUMBSTICK_TOUCH_ID = alvr_path_string_to_id("/user/hand/right/input/thumbstick/touch");
uint64_t RIGHT_THUMBREST_TOUCH_ID = alvr_path_string_to_id("/user/hand/right/input/thumbrest/touch");

const int MAXIMUM_TRACKING_FRAMES = 360;
const float BUTTON_EPS = 0.001f;
const float IPD_EPS = 0.001f;

const GLenum SWAPCHAIN_FORMAT = GL_RGBA8;

static float g_fTrackingOffset = 0.f;

static Jerk_Estimation leftHandJerkEstimation[3];
static Jerk_Estimation rightHandJerkEstimation[3];

struct Render_EGL {
    EGLDisplay Display;
    EGLConfig Config;
    EGLSurface TinySurface;
    EGLSurface MainSurface;
    EGLContext Context;
};

struct QYEyeBuffer {
    QYRenderTarget eyeTarget[NUM_EYE_BUFFERS_];
    int index;
};

// Negotiated stream configuration (subset of AlvrEvent::StreamingStarted).
struct StreamConfig {
    uint32_t view_width = 0;
    uint32_t view_height = 0;
    float refresh_rate_hint = 72.f;
    bool enable_foveated_encoding = false;
};

class NativeContext {
public:
    JavaVM *vm = nullptr;
    jobject context = nullptr;

    Render_EGL egl{};

    ANativeWindow *window = nullptr;

    bool running = false;
    bool streaming = false;
    bool decoderConfigured = false;
    std::thread eventsThread;

    uint32_t recommendedViewWidth = 1;
    uint32_t recommendedViewHeight = 1;
    float refreshRate = 72.f;
    StreamConfig streamConfig{};

    uint64_t ovrFrameIndex = 0;
    uint64_t lastFrameTimeUs = 0;

    std::deque<std::pair<uint64_t, qiyu_HeadPoseState>> trackingFrameMap;
    std::mutex trackingFrameMutex;

    QYEyeBuffer lobbyBuffers[2] = {};
    QYEyeBuffer streamBuffers[2] = {};

    uint8_t hmdBattery = 0;
    bool hmdPlugged = false;
    uint8_t lastLeftControllerBattery = 0;
    uint8_t lastRightControllerBattery = 0;

    float lastIpd = 0.f;
    AlvrFov lastFov{};

    std::map<uint64_t, AlvrButtonValue> previousButtonsState;

    struct HapticsState {
        uint64_t startUs;
        uint64_t endUs;
        float amplitude;
        float frequency;
        bool fresh;
        bool buffered;
    };
    HapticsState hapticsState[2]{};
};

NativeContext CTX = {};

static const char *EglErrorString(const EGLint err) {
    switch (err) {
        case EGL_SUCCESS: return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXELMAP";
        case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
        default: return "unknown";
    }
}

void eglInit() {
    EGLint major, minor;

    CTX.egl.Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(CTX.egl.Display, &major, &minor);

    const int MAX_CONFIGS = 1024;
    EGLConfig configs[MAX_CONFIGS];
    EGLint numConfigs = 0;
    if (eglGetConfigs(CTX.egl.Display, configs, MAX_CONFIGS, &numConfigs) == EGL_FALSE) {
        error("        eglGetConfigs() failed: %s", EglErrorString(eglGetError()));
        return;
    }
    const EGLint configAttribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                    EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
                                    EGL_SAMPLES, 0, EGL_NONE};
    CTX.egl.Config = 0;
    for (int i = 0; i < numConfigs; i++) {
        EGLint value = 0;

        eglGetConfigAttrib(CTX.egl.Display, configs[i], EGL_RENDERABLE_TYPE, &value);
        if ((value & EGL_OPENGL_ES3_BIT_KHR) != EGL_OPENGL_ES3_BIT_KHR) {
            continue;
        }

        eglGetConfigAttrib(CTX.egl.Display, configs[i], EGL_SURFACE_TYPE, &value);
        if ((value & (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) != (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) {
            continue;
        }

        int j = 0;
        for (; configAttribs[j] != EGL_NONE; j += 2) {
            eglGetConfigAttrib(CTX.egl.Display, configs[i], configAttribs[j], &value);
            if (value != configAttribs[j + 1]) {
                break;
            }
        }
        if (configAttribs[j] == EGL_NONE) {
            CTX.egl.Config = configs[i];
            break;
        }
    }
    if (CTX.egl.Config == 0) {
        error("        eglChooseConfig() failed: %s", EglErrorString(eglGetError()));
        return;
    }
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    CTX.egl.Context = eglCreateContext(CTX.egl.Display, CTX.egl.Config, EGL_NO_CONTEXT,
                                        contextAttribs);
    if (CTX.egl.Context == EGL_NO_CONTEXT) {
        error("        eglCreateContext() failed: %s", EglErrorString(eglGetError()));
        return;
    }
    const EGLint surfaceAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    CTX.egl.TinySurface = eglCreatePbufferSurface(CTX.egl.Display, CTX.egl.Config, surfaceAttribs);
    if (CTX.egl.TinySurface == EGL_NO_SURFACE) {
        error("        eglCreatePbufferSurface() failed: %s", EglErrorString(eglGetError()));
        eglDestroyContext(CTX.egl.Display, CTX.egl.Context);
        CTX.egl.Context = EGL_NO_CONTEXT;
        return;
    }
    if (eglMakeCurrent(CTX.egl.Display, CTX.egl.TinySurface, CTX.egl.TinySurface,
                       CTX.egl.Context) == EGL_FALSE) {
        error("        eglMakeCurrent() failed: %s", EglErrorString(eglGetError()));
        eglDestroySurface(CTX.egl.Display, CTX.egl.TinySurface);
        eglDestroyContext(CTX.egl.Display, CTX.egl.Context);
        CTX.egl.Context = EGL_NO_CONTEXT;
        return;
    }
}

void eglDestroy() {
    if (CTX.egl.Display != 0) {
        if (eglMakeCurrent(CTX.egl.Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) ==
            EGL_FALSE) {
            error("        eglMakeCurrent() failed: %s", EglErrorString(eglGetError()));
        }
    }
    if (CTX.egl.Context != EGL_NO_CONTEXT) {
        if (eglDestroyContext(CTX.egl.Display, CTX.egl.Context) == EGL_FALSE) {
            error("        eglDestroyContext() failed: %s", EglErrorString(eglGetError()));
        }
        CTX.egl.Context = EGL_NO_CONTEXT;
    }
    if (CTX.egl.TinySurface != EGL_NO_SURFACE) {
        if (eglDestroySurface(CTX.egl.Display, CTX.egl.TinySurface) == EGL_FALSE) {
            error("        eglDestroySurface() failed: %s", EglErrorString(eglGetError()));
        }
        CTX.egl.TinySurface = EGL_NO_SURFACE;
    }
    if (CTX.egl.Display != 0) {
        if (eglTerminate(CTX.egl.Display) == EGL_FALSE) {
            error("        eglTerminate() failed: %s", EglErrorString(eglGetError()));
        }
        CTX.egl.Display = 0;
    }
}

inline uint64_t getTimestampUs() {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t) tv.tv_sec * 1000 * 1000 + tv.tv_usec;
}

inline uint64_t getTimestampNs() {
    timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (uint64_t) ts.tv_sec * 1e9 + ts.tv_nsec;
}

ovrJava getOvrJava(bool initThread = false) {
    JNIEnv *env;
    if (initThread) {
        JavaVMAttachArgs args = {JNI_VERSION_1_6};
        CTX.vm->AttachCurrentThread(&env, &args);
    } else {
        CTX.vm->GetEnv((void **) &env, JNI_VERSION_1_6);
    }

    ovrJava java{};
    java.Vm = CTX.vm;
    java.Env = env;
    java.ActivityObject = CTX.context;

    return java;
}

void updateBinary(uint64_t path, bool value) {
    auto *stateRef = &CTX.previousButtonsState[path];

    AlvrButtonValue b{};
    b.tag = ALVR_BUTTON_VALUE_BINARY;
    b.binary = value;

    bool changed = (stateRef->tag != ALVR_BUTTON_VALUE_BINARY) || (stateRef->binary != value);
    if (changed) {
        *stateRef = b;
        alvr_send_button(path, b);
    }
}

void updateScalar(uint64_t path, float value) {
    auto *stateRef = &CTX.previousButtonsState[path];

    AlvrButtonValue b{};
    b.tag = ALVR_BUTTON_VALUE_SCALAR;
    b.scalar = value;

    bool changed = (stateRef->tag != ALVR_BUTTON_VALUE_SCALAR) ||
                   (fabsf(stateRef->scalar - value) > BUTTON_EPS);
    if (changed) {
        *stateRef = b;
        alvr_send_button(path, b);
    }
}

void updateButtons() {
    if(!qiyu_IsControllerInit()) {
        return;
    }

    qiyu_ControllerData left;
    qiyu_ControllerData right;
    qiyu_GetControllerData(&left, &right);

    if (left.isConnect) {
        updateBinary(MENU_CLICK_ID, left.button & BT_Home_Menu);
        updateBinary(X_CLICK_ID, left.button & BT_A_X);
        updateBinary(X_TOUCH_ID, left.buttonTouch & BT_A_X);
        updateBinary(Y_CLICK_ID, left.button & BT_B_Y);
        updateBinary(Y_TOUCH_ID, left.buttonTouch & BT_B_Y);
        updateBinary(LEFT_SQUEEZE_CLICK_ID, left.button & BT_Grip);
        updateScalar(LEFT_SQUEEZE_VALUE_ID, left.gripForce);
        updateBinary(LEFT_TRIGGER_CLICK_ID, left.button & BT_Trigger);
        updateScalar(LEFT_TRIGGER_VALUE_ID, left.triggerForce);
        updateBinary(LEFT_TRIGGER_TOUCH_ID, left.buttonTouch & BT_Trigger);
        updateScalar(LEFT_THUMBSTICK_X_ID, left.joyStickPos.x);
        updateScalar(LEFT_THUMBSTICK_Y_ID, left.joyStickPos.y);
        updateBinary(LEFT_THUMBSTICK_CLICK_ID, left.button & BT_JoyStick);
        updateBinary(LEFT_THUMBSTICK_TOUCH_ID, left.buttonTouch & BT_JoyStick);
        updateBinary(LEFT_THUMBREST_TOUCH_ID, left.buttonTouch & BT_None);
    }

    if (right.isConnect) {
        updateBinary(A_CLICK_ID, right.button & BT_A_X);
        updateBinary(A_TOUCH_ID, right.buttonTouch & BT_A_X);
        updateBinary(B_CLICK_ID, right.button & BT_B_Y);
        updateBinary(B_TOUCH_ID, right.buttonTouch & BT_B_Y);
        updateBinary(RIGHT_SQUEEZE_CLICK_ID, right.button & BT_Grip);
        updateScalar(RIGHT_SQUEEZE_VALUE_ID, right.gripForce);
        updateBinary(RIGHT_TRIGGER_CLICK_ID, right.button & BT_Trigger);
        updateScalar(RIGHT_TRIGGER_VALUE_ID, right.triggerForce);
        updateBinary(RIGHT_TRIGGER_TOUCH_ID, right.buttonTouch & BT_Trigger);
        updateScalar(RIGHT_THUMBSTICK_X_ID, right.joyStickPos.x);
        updateScalar(RIGHT_THUMBSTICK_Y_ID, right.joyStickPos.y);
        updateBinary(RIGHT_THUMBSTICK_CLICK_ID, right.button & BT_JoyStick);
        updateBinary(RIGHT_THUMBSTICK_TOUCH_ID, right.buttonTouch & BT_JoyStick);
        updateBinary(RIGHT_THUMBREST_TOUCH_ID, right.buttonTouch & BT_None);
    }
}

// Return per-eye FOV in ALVR convention (radians, up/down not top/bottom).
AlvrFov getFov(qiyu_DeviceInfo* di, int eye) {
    qiyu_ViewFrustum* pFrust = (eye == 0) ? &di->frustumLeftEye : &di->frustumRightEye;

    AlvrFov fov{};
    fov.left = (float) atan(pFrust->left / pFrust->near);
    fov.right = (float) atan(pFrust->right / pFrust->near);
    fov.up = (float) atan(pFrust->top / pFrust->near);
    fov.down = (float) atan(pFrust->bottom / pFrust->near);

    return fov;
}

static inline float getInterpupillaryDistance(qiyu_DeviceInfo* di) {
    qiyu_Vector3 delta;
    delta.x = di->frustumRightEye.position.x - di->frustumLeftEye.position.x;
    delta.y = di->frustumRightEye.position.y - di->frustumLeftEye.position.y;
    delta.z = di->frustumRightEye.position.z - di->frustumLeftEye.position.z;
    return sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
}

void getPlayspaceArea(float *width, float *height) {
    qiyu_Vector3 bboxScale = qiyu_GetBoundaryDimensions();
    *width = 2.0f * bboxScale.x;
    *height = 2.0f * bboxScale.z;
}

uint8_t getControllerBattery(int index) {
    if(!qiyu_IsControllerInit()) {
        return 0;
    }
    qiyu_ControllerData left;
    qiyu_ControllerData right;
    qiyu_GetControllerData(&left, &right);
    return (uint8_t) (index == 0 ? left.batteryLevel : right.batteryLevel);
}

void updateHapticsState() {
    if(!qiyu_IsControllerInit()) {
        return;
    }

    for (uint32_t deviceIndex = 0; deviceIndex < CI_COUNT; deviceIndex++) {
        qiyu_ControllerMask curCaps = deviceIndex ? qiyu_ControllerMask::CM_Right
                                                  : qiyu_ControllerMask::CM_Left;
        int curHandIndex = deviceIndex ? 0 : 1;
        auto &s = CTX.hapticsState[curHandIndex];

        uint64_t currentUs = getTimestampUs();

        if (s.fresh) {
            s.startUs = s.startUs + currentUs;
            s.endUs = s.startUs + s.endUs;
            s.fresh = false;
        }

        if (s.startUs <= 0) {
            if (s.buffered) {
                qiyu_StopControllerVibration(curCaps);
                s.buffered = false;
            }
            continue;
        }

        if (currentUs >= s.endUs) {
            s.startUs = 0;
            if (s.buffered) {
                qiyu_StopControllerVibration(curCaps);
                s.buffered = false;
            }
            continue;
        }

        qiyu_StartControllerVibration(curCaps, s.amplitude, (float) (s.endUs - currentUs) / 1e6);
        s.buffered = true;
    }
}

// Build the per-eye view params (pose + fov) the server needs for foveated
// encoding. The eye pose is the frustum position/rotation relative to the head.
void fillViewParams(qiyu_DeviceInfo* di, AlvrViewParams outParams[2]) {
    for (int eye = 0; eye < 2; eye++) {
        qiyu_ViewFrustum* fr = (eye == 0) ? &di->frustumLeftEye : &di->frustumRightEye;
        outParams[eye].pose.orientation.x = fr->rotation.x;
        outParams[eye].pose.orientation.y = fr->rotation.y;
        outParams[eye].pose.orientation.z = fr->rotation.z;
        outParams[eye].pose.orientation.w = fr->rotation.w;
        outParams[eye].pose.position[0] = fr->position.x;
        outParams[eye].pose.position[1] = fr->position.y;
        outParams[eye].pose.position[2] = fr->position.z;
        outParams[eye].fov = getFov(di, eye);
    }
}

// low frequency events + tracking thread
void eventsThread() {
    auto java = getOvrJava(true);

    jclass cls = java.Env->GetObjectClass(java.ActivityObject);
    jmethodID onStreamStartMethod = java.Env->GetMethodID(cls, "onStreamStart", "()V");
    jmethodID onStreamStopMethod = java.Env->GetMethodID(cls, "onStreamStop", "()V");

    auto deadline = std::chrono::steady_clock::now();
    auto motionVec = std::vector<AlvrDeviceMotion>();

    while (CTX.running) {
        if (CTX.streaming) {
            motionVec.clear();

            // Predict head pose using Qiyu's recommended display-time lead.
            float predictMs = qiyu_PredictDisplayTime();
            uint64_t targetTimestampNs = getTimestampNs() + (uint64_t)(predictMs * 1e6);

            qiyu_HeadPoseState headTracking = qiyu_PredictHeadPose(predictMs);
            AlvrDeviceMotion headMotion = {};
            headMotion.device_id = HEAD_ID;
            headMotion.pose.orientation.x = headTracking.pose.rotation.x;
            headMotion.pose.orientation.y = headTracking.pose.rotation.y;
            headMotion.pose.orientation.z = headTracking.pose.rotation.z;
            headMotion.pose.orientation.w = -headTracking.pose.rotation.w;
            headMotion.pose.position[0] = -headTracking.pose.position.x;
            headMotion.pose.position[1] = -headTracking.pose.position.y - g_fTrackingOffset;
            headMotion.pose.position[2] = -headTracking.pose.position.z;
            motionVec.push_back(headMotion);

            {
                std::lock_guard<std::mutex> lock(CTX.trackingFrameMutex);
                CTX.trackingFrameMap.push_front({targetTimestampNs, headTracking});
                if (CTX.trackingFrameMap.size() > MAXIMUM_TRACKING_FRAMES) {
                    CTX.trackingFrameMap.pop_back();
                }
            }

            updateButtons();

            if(qiyu_IsControllerInit()) {
                qiyu_ControllerData left;
                qiyu_ControllerData right;
                qiyu_GetControllerData(&left, &right);

                // Right-handed -> left-handed coordinate conversion for velocities.
                left.velocity.z = -left.velocity.z;
                right.velocity.z = -right.velocity.z;
                left.acceleration.z = -left.acceleration.z;
                right.acceleration.z = -right.acceleration.z;
                left.angVelocity.z = -left.angVelocity.z;
                right.angVelocity.z = -right.angVelocity.z;
                left.angAcceleration.z = -left.angAcceleration.z;
                right.angAcceleration.z = -right.angAcceleration.z;

                if (left.isConnect) {
                    handPositionf leftHand;
                    float predictedPosition[3];
                    for (int i = 0; i < 3; i++) {
                        leftHand.Position = *(&left.position.x + i);
                        leftHand.LinearVelocity = *(&left.velocity.x + i);
                        leftHand.LinearAcceleration = *(&left.acceleration.x + i);
                        leftHandJerkEstimation[i].rtU.acceleration = leftHand.LinearAcceleration;
                        leftHandJerkEstimation[i].rtU.tor = 1.0f / CTX.refreshRate / 3;
                        leftHandJerkEstimation[i].step();
                        leftHand.LinearJerk = leftHandJerkEstimation[i].rtY.jerk;
                        leftHand.LinearSnap = 0.f;
                        leftHand.LinearCrackle = 0.f;
                        predictedPosition[i] = handTrajectoryPrediction(leftHand, CONTROLLER_PREDICTION_HORIZON_S);
                    }

                    AlvrDeviceMotion motion = {};
                    motion.device_id = LEFT_HAND_ID;
                    memcpy(&motion.pose.orientation, &left.rotation, 4 * sizeof(float));
                    memcpy(motion.pose.position, predictedPosition, 3 * sizeof(float));
                    memcpy(motion.linear_velocity, &left.velocity, 3 * sizeof(float));
                    memcpy(motion.angular_velocity, &left.angVelocity, 3 * sizeof(float));
                    motion.pose.position[1] -= g_fTrackingOffset;
                    motionVec.push_back(motion);
                }

                if (right.isConnect) {
                    handPositionf rightHand;
                    float predictedPosition[3];
                    for (int i = 0; i < 3; i++) {
                        rightHand.Position = *(&right.position.x + i);
                        rightHand.LinearVelocity = *(&right.velocity.x + i);
                        rightHand.LinearAcceleration = *(&right.acceleration.x + i);
                        rightHandJerkEstimation[i].rtU.acceleration = rightHand.LinearAcceleration;
                        rightHandJerkEstimation[i].rtU.tor = 1.0f / CTX.refreshRate / 3;
                        rightHandJerkEstimation[i].step();
                        rightHand.LinearJerk = rightHandJerkEstimation[i].rtY.jerk;
                        rightHand.LinearSnap = 0.f;
                        rightHand.LinearCrackle = 0.f;
                        predictedPosition[i] = handTrajectoryPrediction(rightHand, CONTROLLER_PREDICTION_HORIZON_S);
                    }

                    AlvrDeviceMotion motion = {};
                    motion.device_id = RIGHT_HAND_ID;
                    memcpy(&motion.pose.orientation, &right.rotation, 4 * sizeof(float));
                    memcpy(motion.pose.position, predictedPosition, 3 * sizeof(float));
                    memcpy(motion.linear_velocity, &right.velocity, 3 * sizeof(float));
                    memcpy(motion.angular_velocity, &right.angVelocity, 3 * sizeof(float));
                    motion.pose.position[1] -= g_fTrackingOffset;
                    motionVec.push_back(motion);
                }
            }

            // hand_skeletons and combined_eye_gaze are null for Qiyu.
            alvr_send_tracking(targetTimestampNs, motionVec.data(), motionVec.size(), nullptr, nullptr);

            // Periodically refresh view params (fov/ipd) so foveated encoding tracks IPD changes.
            qiyu_DeviceInfo di = qiyu_GetDeviceInfo();
            auto newLeftFov = getFov(&di, 0);
            auto newRightFov = getFov(&di, 1);
            float newIpd = getInterpupillaryDistance(&di);

            if (abs(newIpd - CTX.lastIpd) > IPD_EPS ||
                abs(newLeftFov.left - CTX.lastFov.left) > IPD_EPS ||
                abs(newLeftFov.up - CTX.lastFov.up) > IPD_EPS) {
                AlvrViewParams viewParams[2];
                fillViewParams(&di, viewParams);
                alvr_send_view_params(viewParams);
                CTX.lastIpd = newIpd;
                CTX.lastFov = newLeftFov;
            }

            uint8_t leftBattery = getControllerBattery(0);
            if (leftBattery != CTX.lastLeftControllerBattery) {
                alvr_send_battery(LEFT_HAND_ID, (float) leftBattery / 100.f, false);
                CTX.lastLeftControllerBattery = leftBattery;
            }
            uint8_t rightBattery = getControllerBattery(1);
            if (rightBattery != CTX.lastRightControllerBattery) {
                alvr_send_battery(RIGHT_HAND_ID, (float) rightBattery / 100.f, false);
                CTX.lastRightControllerBattery = rightBattery;
            }
        }

        AlvrEvent event;
        while (alvr_poll_event(&event)) {
            if (event.tag == ALVR_EVENT_HAPTICS) {
                auto haptics = event.HAPTICS;
                int curHandIndex = (haptics.device_id == RIGHT_CONTROLLER_HAPTICS_ID ? 0 : 1);
                auto &s = CTX.hapticsState[curHandIndex];
                s.startUs = 0;
                s.endUs = (uint64_t) (haptics.duration_s * 1000'000);
                s.amplitude = (haptics.amplitude > 0.2f) ? haptics.amplitude : 0.2f;
                s.frequency = haptics.frequency;
                s.fresh = true;
                s.buffered = false;
            } else if (event.tag == ALVR_EVENT_STREAMING_STARTED) {
                CTX.streamConfig.view_width = event.STREAMING_STARTED.view_width;
                CTX.streamConfig.view_height = event.STREAMING_STARTED.view_height;
                CTX.streamConfig.refresh_rate_hint = event.STREAMING_STARTED.refresh_rate_hint;
                CTX.streamConfig.enable_foveated_encoding = event.STREAMING_STARTED.enable_foveated_encoding;
                java.Env->CallVoidMethod(java.ActivityObject, onStreamStartMethod);
            } else if (event.tag == ALVR_EVENT_STREAMING_STOPPED) {
                java.Env->CallVoidMethod(java.ActivityObject, onStreamStopMethod);
            } else if (event.tag == ALVR_EVENT_DECODER_CONFIG) {
                // ALVR can retransmit decoder configuration packets while the
                // connection is being established. The v20 C API explicitly
                // requires clients to ignore all configs after the first one
                // until the next reconnection; recreating MediaCodec leaks its
                // ImageReader and quickly exhausts Dream's hardware decoder.
                if (CTX.decoderConfigured) {
                    info("Ignoring duplicate decoder configuration");
                    continue;
                }

                // The server told us the codec + decoder config NAL; create the decoder.
                uint64_t size = alvr_get_decoder_config(nullptr);
                if (size > 0) {
                    std::vector<uint8_t> buffer(size);
                    alvr_get_decoder_config((char *) buffer.data());

                    AlvrDecoderConfig cfg{};
                    cfg.codec = event.DECODER_CONFIG.codec;
                    cfg.force_software_decoder = false;
                    // Zero is not a valid value here: the v20 Android decoder
                    // drops its queue front whenever its measured buffering is
                    // greater than this value. With 0, every decoded frame is
                    // discarded before alvr_get_frame() can return it, leaving
                    // Qiyu with no frame to submit. Match ALVR's v20 defaults.
                    cfg.max_buffering_frames = 2.0f;
                    cfg.buffering_history_weight = 0.90f;
                    cfg.options = nullptr;
                    cfg.options_count = 0;
                    cfg.config_buffer = buffer.data();
                    cfg.config_buffer_size = size;
                    alvr_create_decoder(cfg);
                    CTX.decoderConfigured = true;
                    info("Decoder created (codec %d)", (int) cfg.codec);
                }
            }
        }

        deadline += std::chrono::nanoseconds((uint64_t) (1e9 / CTX.refreshRate / 3));
        std::this_thread::sleep_until(deadline);
    }
}

static void CreateLayout_(float centerX, float centerY, float radiusX, float radiusY, qiyu_RenderLayer_ScreenPosUV* pLayout) {
	float lowerLeftPos[4] = { centerX - radiusX, centerY - radiusY, 0.0f, 1.0f };
	float lowerRightPos[4] = { centerX + radiusX, centerY - radiusY, 0.0f, 1.0f };
	float upperLeftPos[4] = { centerX - radiusX, centerY + radiusY, 0.0f, 1.0f };
	float upperRightPos[4] = { centerX + radiusX, centerY + radiusY, 0.0f, 1.0f };
	float lowerUVs[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
	float upperUVs[4] = { 0.0f, 1.0f, 1.0f, 1.0f };
	memcpy(pLayout->LowerLeftPos, lowerLeftPos, sizeof(lowerLeftPos));
	memcpy(pLayout->LowerRightPos, lowerRightPos, sizeof(lowerRightPos));
	memcpy(pLayout->UpperLeftPos, upperLeftPos, sizeof(upperLeftPos));
	memcpy(pLayout->UpperRightPos, upperRightPos, sizeof(upperRightPos));
	memcpy(pLayout->LowerUVs, lowerUVs, sizeof(lowerUVs));
	memcpy(pLayout->UpperUVs, upperUVs, sizeof(upperUVs));
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_initializeNative(JNIEnv *env, jobject context) {
    env->GetJavaVM(&CTX.vm);
    CTX.context = env->NewGlobalRef(context);

    auto java = getOvrJava(true);

    alvr_initialize_logging();

    info("================ ALVR session start ================");
    info("[trace] calling eglInit");
    eglInit();
    info("[trace] eglInit done");

    memset(CTX.hapticsState, 0, sizeof(CTX.hapticsState));
    QY_GL_EXT::InitFunction_Foveation();
    info("[trace] calling qiyu_Init");
    qiyu_Init(java.ActivityObject,
              java.Vm,
              qiyu_GraphicsApi::GA_OpenGLES,
              qiyu_TrackingOriginMode::TM_Ground,
              false);
    info("[trace] qiyu_Init done");

    qiyu_DeviceInfo deviceInfo = qiyu_GetDeviceInfo();
    CTX.recommendedViewWidth = deviceInfo.iEyeTargetWidth;
    CTX.recommendedViewHeight = deviceInfo.iEyeTargetHeight;

    int refreshRatesCount = 2;
    auto refreshRatesBuffer = std::vector<float>(refreshRatesCount);
    refreshRatesBuffer[0] = 72.f;
    refreshRatesBuffer[1] = 90.f;

    // New ALVR ABI: register the Android context first, then initialize with capabilities.
    info("[trace] calling alvr_initialize_android_context");
    alvr_initialize_android_context((void *) CTX.vm, (void *) CTX.context);
    info("[trace] alvr_initialize_android_context done");

    AlvrClientCapabilities caps{};
    caps.default_view_width = CTX.recommendedViewWidth;
    caps.default_view_height = CTX.recommendedViewHeight;
    caps.refresh_rates = refreshRatesBuffer.data();
    caps.refresh_rates_count = refreshRatesCount;
    caps.foveated_encoding = true;
    caps.encoder_high_profile = false;
    caps.encoder_10_bits = false;
    caps.encoder_av1 = false;          // XR2 HW AV1 decode is uncommon; H264/HEVC are safe.
    caps.prefer_10bit = false;
    caps.preferred_encoding_gamma = 1.0f;
    caps.prefer_hdr = false;
    info("[trace] calling alvr_initialize");
    alvr_initialize(caps);
    info("[trace] alvr_initialize done");

    info("[trace] calling alvr_initialize_opengl");
    alvr_initialize_opengl();
    info("[trace] alvr_initialize_opengl done");
    qiyu_PostSetEyeBufferSize(CTX.recommendedViewWidth, CTX.recommendedViewHeight);

    QIYU_PROFILE_ID = alvr_path_string_to_id(QIYU_INTERACTION_PROFILE);
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_destroyNative(JNIEnv *_env, jobject _context) {
    info("=== ALVR session clean exit ===");
    qiyu_Release();
    alvr_destroy();
    alvr_destroy_opengl();
    eglDestroy();

    auto java = getOvrJava();
    java.Env->DeleteGlobalRef(CTX.context);
}

extern "C" JNIEXPORT void JNICALL Java_alvr_client_VRActivity_onResumeNative(
        JNIEnv *_env, jobject _context, jobject surface) {
    auto java = getOvrJava();

    CTX.window = ANativeWindow_fromSurface(java.Env, surface);

    info("Entering VR mode.");

    if (!qiyu_StartVR(CTX.window, PL_System, PL_System)) {
        error("qiyu_StartVR failed - aborting resume to avoid GPU hang");
        ANativeWindow_release(CTX.window);
        CTX.window = nullptr;
        return;
    }
    info("[trace] qiyu_StartVR succeeded");

    qiyu_SetTrackingOriginMode(qiyu_TrackingOriginMode::TM_Ground);

    bool isSupport_Foveation = QY_GL_EXT::IsSupport_Foveation();
    std::vector<int32_t> textureHandlesBuffer[2];
    for (int eye = 0; eye < 2; eye++) {
        for (int index = 0; index < NUM_EYE_BUFFERS_; index++) {
            CTX.lobbyBuffers[eye].eyeTarget[index].Init(
                false, GL_FOVEATION_ENABLE_BIT_QCOM | GL_FOVEATION_SCALED_BIN_METHOD_BIT_QCOM, false,
                CTX.recommendedViewWidth, CTX.recommendedViewHeight, 1, GL_RGBA8, false, false);
            auto handle = CTX.lobbyBuffers[eye].eyeTarget[index].GetColorAttachment();
            textureHandlesBuffer[eye].push_back(handle);
        }
        CTX.lobbyBuffers[eye].index = 0;
    }
    const int32_t *textureHandles[2] = {&textureHandlesBuffer[0][0], &textureHandlesBuffer[1][0]};
    qiyu_PostSetEyeBufferSize(CTX.recommendedViewWidth, CTX.recommendedViewHeight);

    CTX.running = true;
    info("[trace] starting events thread");
    CTX.eventsThread = std::thread(eventsThread);

    // swapchain textures are GL texture names (positive); cast to the u32** the ABI expects.
    info("[trace] calling alvr_resume_opengl");
    alvr_resume_opengl(CTX.recommendedViewWidth, CTX.recommendedViewHeight,
                       (const uint32_t **) textureHandles, (uint32_t) textureHandlesBuffer[0].size());
    info("[trace] calling alvr_resume");
    alvr_resume();
    info("[trace] alvr_resume done");
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_onStreamStartNative(JNIEnv *_env, jobject _context) {
    auto java = getOvrJava();

    CTX.refreshRate = CTX.streamConfig.refresh_rate_hint;
    CTX.decoderConfigured = false;

    // Build the eye swapchain texture handles for the stream buffers.
    std::vector<uint32_t> textureHandlesBuffer[2];
    for (int eye = 0; eye < 2; eye++) {
        for (int index = 0; index < NUM_EYE_BUFFERS_; index++) {
            CTX.streamBuffers[eye].eyeTarget[index].Init(
                false, GL_FOVEATION_ENABLE_BIT_QCOM | GL_FOVEATION_SCALED_BIN_METHOD_BIT_QCOM, false,
                CTX.streamConfig.view_width, CTX.streamConfig.view_height, 1, GL_RGBA8, false, false);
            auto handle = CTX.streamBuffers[eye].eyeTarget[index].GetColorAttachment();
            textureHandlesBuffer[eye].push_back((uint32_t) handle);
        }
        CTX.streamBuffers[eye].index = 0;
    }
    const uint32_t *textureHandles[2] = {textureHandlesBuffer[0].data(), textureHandlesBuffer[1].data()};
    qiyu_PostSetEyeBufferSize(CTX.streamConfig.view_width, CTX.streamConfig.view_height);

    // Configure Qiyu foveation from the server's foveated-encoding hint.
    if (CTX.streamConfig.enable_foveated_encoding) {
        qiyu_SetFoveation(FL_Medium);
    } else {
        qiyu_SetFoveation(FL_None);
    }

    // Tell ALVR where to render the decoded video (into the Qiyu eye textures).
    AlvrStreamConfig config{};
    config.view_resolution_width = CTX.streamConfig.view_width;
    config.view_resolution_height = CTX.streamConfig.view_height;
    config.swapchain_textures = (const uint32_t **) textureHandles;
    config.swapchain_length = (uint32_t) textureHandlesBuffer[0].size();
    config.enable_foveation = CTX.streamConfig.enable_foveated_encoding;
    config.foveation_center_size_x = 1.0f;
    config.foveation_center_size_y = 1.0f;
    config.foveation_center_shift_x = 0.0f;
    config.foveation_center_shift_y = 0.0f;
    config.foveation_edge_ratio_x = 1.0f;
    config.foveation_edge_ratio_y = 1.0f;
    config.enable_upscaling = false;
    config.upscaling_edge_direction = false;
    config.upscaling_edge_threshold = 0.0f;
    config.upscaling_edge_sharpness = 0.0f;
    config.upscale_factor = 1.0f;
    alvr_start_stream_opengl(config);

    // Send initial view params (fov/ipd) and battery/playspace to the server.
    qiyu_DeviceInfo di = qiyu_GetDeviceInfo();
    AlvrViewParams viewParams[2];
    fillViewParams(&di, viewParams);
    alvr_send_view_params(viewParams);

    alvr_send_battery(HEAD_ID, CTX.hmdBattery / 100.f, CTX.hmdPlugged);
    alvr_send_battery(LEFT_HAND_ID, getControllerBattery(0) / 100.f, false);
    alvr_send_battery(RIGHT_HAND_ID, getControllerBattery(1) / 100.f, false);

    float areaWidth, areaHeight;
    getPlayspaceArea(&areaWidth, &areaHeight);
    alvr_send_playspace(areaWidth, areaHeight);

    // Declare the controller interaction profile so the server maps our buttons.
    // (ALVR v20.14.1's API takes device_id + profile_id only.)
    alvr_send_active_interaction_profile(LEFT_HAND_ID, QIYU_PROFILE_ID);
    alvr_send_active_interaction_profile(RIGHT_HAND_ID, QIYU_PROFILE_ID);

    CTX.streaming = true;
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_onStreamStopNative(JNIEnv *_env, jobject _context) {
    CTX.streaming = false;
    CTX.decoderConfigured = false;

    alvr_destroy_decoder();

    for (int eye = 0; eye < 2; eye++) {
        for (int index = 0; index < NUM_EYE_BUFFERS_; index++) {
            CTX.streamBuffers[eye].eyeTarget[index].Release();
        }
    }
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_onPauseNative(JNIEnv *_env, jobject _context) {
    Java_alvr_client_VRActivity_onStreamStopNative(_env, _context);

    alvr_pause();
    alvr_pause_opengl();

    if (CTX.running) {
        CTX.running = false;
        CTX.eventsThread.join();
    }
    for (int eye = 0; eye < 2; eye++) {
        for (int index = 0; index < NUM_EYE_BUFFERS_; index++) {
            CTX.lobbyBuffers[eye].eyeTarget[index].Release();
        }
    }

    qiyu_EndVR();

    if (CTX.window != nullptr) {
        ANativeWindow_release(CTX.window);
    }
    CTX.window = nullptr;
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_renderNative(JNIEnv *_env, jobject _context) {
    if (!CTX.running || CTX.window == nullptr) {
        return;
    }
    double displayTime;
    qiyu_HeadPoseState tracking;
    qiyu_FrameParam frameParam;
    memset(&frameParam, 0, sizeof(frameParam));

    uint64_t currentUs = getTimestampUs();
    float tickSecond = (currentUs - CTX.lastFrameTimeUs) / 1000000.0f;
    qiyu_Update(tickSecond);
    CTX.lastFrameTimeUs = currentUs;

    if (CTX.streaming) {
        uint64_t timestampNs = 0;
        void *streamHardwareBuffer = nullptr;
        const bool hasDecodedFrame = alvr_get_frame(&timestampNs, &streamHardwareBuffer);

        // Keep submitting Qiyu frames while waiting for the first decoded image.
        // The stock v20 OpenXR client renders a null hardware buffer in this case
        // instead of returning early. Returning early leaves the Qiyu compositor
        // with no submitted layer at all ("No valid frame to warp") and can also
        // stall stream startup on runtimes that expect a continuous frame loop.
        if (hasDecodedFrame) {
            // Register this frame with the compositor and send predicted view params.
            AlvrViewParams predictedViewParams[2];
            alvr_report_compositor_start(timestampNs, predictedViewParams);
            alvr_send_view_params(predictedViewParams);
        }

        updateHapticsState();

        // Fall back to a fresh pose while the decoder has not yielded a frame.
        tracking = qiyu_PredictHeadPose(qiyu_PredictDisplayTime());
        {
            std::lock_guard<std::mutex> lock(CTX.trackingFrameMutex);
            for (auto &pair: CTX.trackingFrameMap) {
                if (hasDecodedFrame && pair.first <= timestampNs) {
                    tracking = pair.second;
                    break;
                }
            }
        }

        // Build stream view params (identity reprojection: Qiyu does its own ATW).
        AlvrStreamViewParams viewParams[2];
        qiyu_DeviceInfo di = qiyu_GetDeviceInfo();
        for (int eye = 0; eye < 2; eye++) {
            viewParams[eye].swapchain_index = CTX.streamBuffers[eye].index;
            viewParams[eye].reprojection_rotation.x = 0.f;
            viewParams[eye].reprojection_rotation.y = 0.f;
            viewParams[eye].reprojection_rotation.z = 0.f;
            viewParams[eye].reprojection_rotation.w = 1.f;
            viewParams[eye].fov = getFov(&di, eye);
        }

        int swapchainIndices[2] = {CTX.streamBuffers[0].index, CTX.streamBuffers[1].index};
        qiyu_StartEye(false, EYE_Left, TT_Texture);
        qiyu_StartEye(false, EYE_Right, TT_Texture);
        info("[trace] stream: before alvr_render_stream_opengl");
        alvr_render_stream_opengl(streamHardwareBuffer, viewParams);
        info("[trace] stream: after alvr_render_stream_opengl");
        qiyu_EndEye(false, EYE_Left, TT_Texture);
        qiyu_EndEye(false, EYE_Right, TT_Texture);

        if (hasDecodedFrame) {
            float vsyncQueueNs = qiyu_PredictDisplayTime() * 1e6f;
            alvr_report_submit(timestampNs, (uint64_t) vsyncQueueNs);
        }

        for (int eye = 0; eye < 2; eye++) {
            frameParam.renderLayers[eye].imageHandle = CTX.streamBuffers[eye].eyeTarget[CTX.streamBuffers[eye].index].GetColorAttachment();
            frameParam.renderLayers[eye].imageType = TT_Texture;
            CreateLayout_(0.0f, 0.0f, 1.0f, 1.0f, &frameParam.renderLayers[eye].imageCoords);
            frameParam.renderLayers[eye].eyeMask = eye ? RL_EyeMask_Right : RL_EyeMask_Left;
            CTX.streamBuffers[eye].index = (CTX.streamBuffers[eye].index + 1) % NUM_EYE_BUFFERS_;
        }
    } else {
        qiyu_DeviceInfo di = qiyu_GetDeviceInfo();
        float fPredictedTimeMs = qiyu_PredictDisplayTime();
        tracking = qiyu_PredictHeadPose(fPredictedTimeMs);

        qiyu_Quaternion leftEyeRot;
        leftEyeRot.x = di.frustumLeftEye.rotation.x;
        leftEyeRot.y = di.frustumLeftEye.rotation.y;
        leftEyeRot.z = di.frustumLeftEye.rotation.z;
        leftEyeRot.w = di.frustumLeftEye.rotation.w;
        qiyu_Quaternion rightEyeRot;
        rightEyeRot.x = di.frustumRightEye.rotation.x;
        rightEyeRot.y = di.frustumRightEye.rotation.y;
        rightEyeRot.z = di.frustumRightEye.rotation.z;
        rightEyeRot.w = di.frustumRightEye.rotation.w;
        qiyu_Matrix4 outEyeMatrix[2];
        qiyu_GetViewMatrix(outEyeMatrix[0], outEyeMatrix[1], g_fTrackingOffset, tracking, leftEyeRot, rightEyeRot);

        AlvrLobbyViewParams lobbyParams[2] = {};
        for (int eye = 0; eye < 2; eye++) {
            auto q = tracking.pose.rotation;
            auto v = ovrMatrix4f_Inverse((ovrMatrix4f*) &outEyeMatrix[eye]);

            lobbyParams[eye].swapchain_index = CTX.lobbyBuffers[eye].index;
            lobbyParams[eye].pose.orientation = AlvrQuat{q.x, q.y, q.z, -q.w};
            lobbyParams[eye].pose.position[0] = -v.M[0][3];
            lobbyParams[eye].pose.position[1] = -v.M[1][3] - g_fTrackingOffset;
            lobbyParams[eye].pose.position[2] = -v.M[2][3];
            lobbyParams[eye].fov = getFov(&di, eye);
        }

        qiyu_StartEye(false, EYE_Left, TT_Texture);
        qiyu_StartEye(false, EYE_Right, TT_Texture);
        info("[trace] lobby: before alvr_render_lobby_opengl");
        alvr_render_lobby_opengl(lobbyParams, true);
        info("[trace] lobby: after alvr_render_lobby_opengl");
        qiyu_EndEye(false, EYE_Left, TT_Texture);
        qiyu_EndEye(false, EYE_Right, TT_Texture);

        for (int eye = 0; eye < 2; eye++) {
            frameParam.renderLayers[eye].imageHandle = CTX.lobbyBuffers[eye].eyeTarget[CTX.lobbyBuffers[eye].index].GetColorAttachment();
            frameParam.renderLayers[eye].imageType = TT_Texture;
            CreateLayout_(0.0f, 0.0f, 1.0f, 1.0f, &frameParam.renderLayers[eye].imageCoords);
            frameParam.renderLayers[eye].eyeMask = eye ? RL_EyeMask_Right : RL_EyeMask_Left;
            CTX.lobbyBuffers[eye].index = (CTX.lobbyBuffers[eye].index + 1) % NUM_EYE_BUFFERS_;
        }
    }

    frameParam.minVsyncs = 1;
    frameParam.headPoseState = tracking;

    info("[trace] before qiyu_SubmitFrame");
    qiyu_SubmitFrame(frameParam);
    info("[trace] after qiyu_SubmitFrame");

    CTX.ovrFrameIndex++;
}

extern "C" JNIEXPORT void JNICALL Java_alvr_client_VRActivity_onBatteryChangedNative(
        JNIEnv *_env, jobject _context, jint battery, jboolean plugged) {
    alvr_send_battery(HEAD_ID, (float) battery / 100.f, (bool) plugged);
    CTX.hmdBattery = battery;
    CTX.hmdPlugged = plugged;
}
