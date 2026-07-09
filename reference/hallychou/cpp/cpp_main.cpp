// #include "VrApi.h"
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
#include "QiyuApi.h"
#include "Hand_Trajectory_Prediction/Hand_Trajectory_Prediction.h"
#include "Jerk_Estimation/Jerk_Estimation.h"
#include "QYRenderTarget.h"

 /**
  * Foveation
  * https://www.khronos.org/registry/OpenGL/extensions/QCOM/QCOM_framebuffer_foveated.txt
  * https://www.khronos.org/registry/OpenGL/extensions/QCOM/QCOM_texture_foveated.txt
  * https://www.khronos.org/registry/OpenGL/extensions/QCOM/QCOM_texture_foveated2.txt
  * https://www.khronos.org/registry/OpenGL/extensions/QCOM/QCOM_texture_foveated_subsampled_layout.txt
  */
#ifndef GL_EXT_framebuffer_foveated

#define GL_FOVEATION_ENABLE_BIT_QCOM                    0x0001
#define GL_FOVEATION_SCALED_BIN_METHOD_BIT_QCOM         0x0002
#define GL_FOVEATION_SUBSAMPLED_LAYOUT_METHOD_BIT_QCOM  0x0004
//#define GL_TEXTURE_PREVIOUS_SOURCE_TEXTURE_QCOM         0x8BE8	//
//#define GL_TEXTURE_FOVEATED_FRAME_OFFSET_QCOM           0x8BE9	//
#define GL_TEXTURE_FOVEATED_FEATURE_BITS_QCOM           0x8BFB
#define GL_TEXTURE_FOVEATED_MIN_PIXEL_DENSITY_QCOM      0x8BFC
//#define GL_TEXTURE_FOVEATED_FEATURE_QUERY_QCOM          0x8BFD
//#define GL_TEXTURE_FOVEATED_NUM_FOCAL_POINTS_QUERY_QCOM 0x8BFE
//#define GL_FRAMEBUFFER_INCOMPLETE_FOVEATION_QCOM        0x8BFF
//#define GL_MAX_SHADER_SUBSAMPLED_IMAGE_UNITS_QCOM       0x8FA1

#ifdef GL_GLEXT_PROTOTYPES
//GL_APICALL void GL_APIENTRY glFramebufferFoveationConfigQCOM(GLuint fbo, GLuint numLayers, GLuint focalPointsPerLayer, GLuint requiredFeatures, GLuint* gotFeatures);
//GL_APICALL void GL_APIENTRY glFramebufferFoveationParametersQCOM(GLuint fbo, GLuint layer, GLuint focalPoint, GLfloat focalX, GLfloat focalY, GLfloat gainX, GLfloat gainY, GLfloat foveaArea);
GL_APICALL void GL_APIENTRY glTextureFoveationParametersQCOM(GLuint texure, GLuint layer, GLuint focalPoint, GLfloat focalX, GLfloat focalY, GLfloat gainX, GLfloat gainY, GLfloat foveaArea);
#endif

#define GL_APIENTRYP GL_APIENTRY*
typedef void (GL_APIENTRYP PFNGLTEXTUREFOVEATIONPARAMETERSEXT)(GLuint texture, GLuint layer, GLuint focalPoint, GLfloat focalX, GLfloat focalY, GLfloat gainX, GLfloat gainY, GLfloat foveaArea);
PFNGLTEXTUREFOVEATIONPARAMETERSEXT glTextureFoveationParametersQCOM = NULL;
//
//typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERFOVEATIONCONFIGEXT)(GLuint fbo, GLuint numLayers, GLuint focalPointsPerLayer, GLuint requiredFeatures, GLuint* gotFeatures);
//PFNGLFRAMEBUFFERFOVEATIONCONFIGEXT glFramebufferFoveationConfigQCOM = NULL;
//
//typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERFOVEATIONPARAMETERSEXT)(GLuint fbo, GLuint layer, GLuint focalPoint, GLfloat focalX, GLfloat focalY, GLfloat gainX, GLfloat gainY, GLfloat foveaArea);
//PFNGLFRAMEBUFFERFOVEATIONPARAMETERSEXT glFramebufferFoveationParametersQCOM = NULL;
//bool glTextureFoveationFrameOffsetQCOM = false;

#endif//GL_EXT_framebuffer_foveated

#define NUM_EYE_BUFFERS_     3   //FIXME! //TODO!

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

    va_end(args);
}

#define error(...) log(ALVR_LOG_LEVEL_ERROR, __VA_ARGS__)
#define info(...) log(ALVR_LOG_LEVEL_INFO, __VA_ARGS__)

namespace QY_GL_EXT
{
	bool InitFunction_Foveation()
	{
		glTextureFoveationParametersQCOM == NULL;
		glTextureFoveationParametersQCOM = (PFNGLTEXTUREFOVEATIONPARAMETERSEXT)eglGetProcAddress("glTextureFoveationParametersQCOM");
		if (glTextureFoveationParametersQCOM == NULL)
		{
			error("@@QY_GL_EXT::InitFunction_Foveation, glTextureFoveationParametersQCOM is not supported, fail to get proc address!");
			return false;
		}
		//assert(glFramebufferFoveationConfigQCOM == NULL);
		//glFramebufferFoveationConfigQCOM = (PFNGLFRAMEBUFFERFOVEATIONCONFIGEXT)eglGetProcAddress("glFramebufferFoveationConfigQCOM");
		//if (glFramebufferFoveationConfigQCOM == NULL)
		//{
		//	LOGE_("@@QY_GL_EXT::InitFunction_Foveation, glFramebufferFoveationConfigQCOM is not supported, fail to get proc address!");
		//	return false;
		//}
		//assert(glFramebufferFoveationParametersQCOM == NULL);
		//glFramebufferFoveationParametersQCOM = (PFNGLFRAMEBUFFERFOVEATIONPARAMETERSEXT)eglGetProcAddress("glFramebufferFoveationParametersQCOM");
		//if (glFramebufferFoveationParametersQCOM == NULL)
		//{
		//	LOGE_("@@QY_GL_EXT::InitFunction_Foveation, glFramebufferFoveationParametersQCOM is not supported, fail to get proc address!");
		//	return false;
		//}
		return true;
	}
	bool IsSupport_Foveation()
	{
		return glTextureFoveationParametersQCOM != NULL;
	}
}

uint64_t HEAD_ID = alvr_path_string_to_hash("/user/head");
uint64_t LEFT_HAND_ID = alvr_path_string_to_hash("/user/hand/left");
uint64_t RIGHT_HAND_ID = alvr_path_string_to_hash("/user/hand/right");
uint64_t LEFT_CONTROLLER_HAPTICS_ID = alvr_path_string_to_hash("/user/hand/left/output/haptic");
uint64_t RIGHT_CONTROLLER_HAPTICS_ID = alvr_path_string_to_hash("/user/hand/right/output/haptic");

uint64_t MENU_CLICK_ID = alvr_path_string_to_hash("/user/hand/left/input/menu/click");
uint64_t A_CLICK_ID = alvr_path_string_to_hash("/user/hand/right/input/a/click");
uint64_t A_TOUCH_ID = alvr_path_string_to_hash("/user/hand/right/input/a/touch");
uint64_t B_CLICK_ID = alvr_path_string_to_hash("/user/hand/right/input/b/click");
uint64_t B_TOUCH_ID = alvr_path_string_to_hash("/user/hand/right/input/b/touch");
uint64_t X_CLICK_ID = alvr_path_string_to_hash("/user/hand/left/input/x/click");
uint64_t X_TOUCH_ID = alvr_path_string_to_hash("/user/hand/left/input/x/touch");
uint64_t Y_CLICK_ID = alvr_path_string_to_hash("/user/hand/left/input/y/click");
uint64_t Y_TOUCH_ID = alvr_path_string_to_hash("/user/hand/left/input/y/touch");
uint64_t LEFT_SQUEEZE_CLICK_ID = alvr_path_string_to_hash("/user/hand/left/input/squeeze/click");
uint64_t LEFT_SQUEEZE_VALUE_ID = alvr_path_string_to_hash("/user/hand/left/input/squeeze/value");
uint64_t LEFT_TRIGGER_CLICK_ID = alvr_path_string_to_hash("/user/hand/left/input/trigger/click");
uint64_t LEFT_TRIGGER_VALUE_ID = alvr_path_string_to_hash("/user/hand/left/input/trigger/value");
uint64_t LEFT_TRIGGER_TOUCH_ID = alvr_path_string_to_hash("/user/hand/left/input/trigger/touch");
uint64_t LEFT_THUMBSTICK_X_ID = alvr_path_string_to_hash("/user/hand/left/input/thumbstick/x");
uint64_t LEFT_THUMBSTICK_Y_ID = alvr_path_string_to_hash("/user/hand/left/input/thumbstick/y");
uint64_t LEFT_THUMBSTICK_CLICK_ID = alvr_path_string_to_hash(
        "/user/hand/left/input/thumbstick/click");
uint64_t LEFT_THUMBSTICK_TOUCH_ID = alvr_path_string_to_hash(
        "/user/hand/left/input/thumbstick/touch");
uint64_t LEFT_THUMBREST_TOUCH_ID = alvr_path_string_to_hash(
        "/user/hand/left/input/thumbrest/touch");
uint64_t RIGHT_SQUEEZE_CLICK_ID = alvr_path_string_to_hash("/user/hand/right/input/squeeze/click");
uint64_t RIGHT_SQUEEZE_VALUE_ID = alvr_path_string_to_hash("/user/hand/right/input/squeeze/value");
uint64_t RIGHT_TRIGGER_CLICK_ID = alvr_path_string_to_hash("/user/hand/right/input/trigger/click");
uint64_t RIGHT_TRIGGER_VALUE_ID = alvr_path_string_to_hash("/user/hand/right/input/trigger/value");
uint64_t RIGHT_TRIGGER_TOUCH_ID = alvr_path_string_to_hash("/user/hand/right/input/trigger/touch");
uint64_t RIGHT_THUMBSTICK_X_ID = alvr_path_string_to_hash("/user/hand/right/input/thumbstick/x");
uint64_t RIGHT_THUMBSTICK_Y_ID = alvr_path_string_to_hash("/user/hand/right/input/thumbstick/y");
uint64_t RIGHT_THUMBSTICK_CLICK_ID = alvr_path_string_to_hash(
        "/user/hand/right/input/thumbstick/click");
uint64_t RIGHT_THUMBSTICK_TOUCH_ID = alvr_path_string_to_hash(
        "/user/hand/right/input/thumbstick/touch");
uint64_t RIGHT_THUMBREST_TOUCH_ID = alvr_path_string_to_hash(
        "/user/hand/right/input/thumbrest/touch");

const int MAXIMUM_TRACKING_FRAMES = 360;
// minimum change for a scalar button to be registered as a new value
const float BUTTON_EPS = 0.001;
const float IPD_EPS = 0.001; // minimum change of IPD to be registered as a new value

const GLenum SWAPCHAIN_FORMAT = GL_RGBA8;

static float g_fTrackingOffset = 0.f;//FIXME!

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

class NativeContext {
public:
    JavaVM *vm;
    jobject context;

    Render_EGL egl;

    ANativeWindow *window = nullptr;
    ovrMobile *ovrContext{};

    bool running = false;
    bool streaming = false;
    std::thread eventsThread;

    uint32_t recommendedViewWidth = 1;
    uint32_t recommendedViewHeight = 1;
    float refreshRate = 72.f;
    StreamingStarted_Body streamingConfig = {};

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

    float lastIpd;
    EyeFov lastFov;

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
        case EGL_SUCCESS:
            return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:
            return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:
            return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:
            return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:
            return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT:
            return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG:
            return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE:
            return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:
            return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE:
            return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH:
            return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER:
            return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP:
            return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:
            return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST:
            return "EGL_CONTEXT_LOST";
        default:
            return "unknown";
    }
}

void eglInit() {
    EGLint major, minor;

    CTX.egl.Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(CTX.egl.Display, &major, &minor);

    // Do NOT use eglChooseConfig, because the Android EGL code pushes in multisample
    // flags in eglChooseConfig if the user has selected the "force 4x MSAA" option in
    // settings, and that is completely wasted for our warp target.
    const int MAX_CONFIGS = 1024;
    EGLConfig configs[MAX_CONFIGS];
    EGLint numConfigs = 0;
    if (eglGetConfigs(CTX.egl.Display, configs, MAX_CONFIGS, &numConfigs) == EGL_FALSE) {
        error("        eglGetConfigs() failed: %s", EglErrorString(eglGetError()));
        return;
    }
    const EGLint configAttribs[] = {EGL_RED_SIZE,
                                    8,
                                    EGL_GREEN_SIZE,
                                    8,
                                    EGL_BLUE_SIZE,
                                    8,
                                    EGL_ALPHA_SIZE,
                                    8, // need alpha for the multi-pass timewarp compositor
                                    EGL_DEPTH_SIZE,
                                    0,
                                    EGL_STENCIL_SIZE,
                                    0,
                                    EGL_SAMPLES,
                                    0,
                                    EGL_NONE};
    CTX.egl.Config = 0;
    for (int i = 0; i < numConfigs; i++) {
        EGLint value = 0;

        eglGetConfigAttrib(CTX.egl.Display, configs[i], EGL_RENDERABLE_TYPE, &value);
        if ((value & EGL_OPENGL_ES3_BIT_KHR) != EGL_OPENGL_ES3_BIT_KHR) {
            continue;
        }

        // The pbuffer config also needs to be compatible with normal window rendering
        // so it can share textures with the window context.
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
        error("        eglMakeCurrent( Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT )");
        if (eglMakeCurrent(CTX.egl.Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) ==
            EGL_FALSE) {
            error("        eglMakeCurrent() failed: %s", EglErrorString(eglGetError()));
        }
    }
    if (CTX.egl.Context != EGL_NO_CONTEXT) {
        error("        eglDestroyContext( Display, Context )");
        if (eglDestroyContext(CTX.egl.Display, CTX.egl.Context) == EGL_FALSE) {
            error("        eglDestroyContext() failed: %s", EglErrorString(eglGetError()));
        }
        CTX.egl.Context = EGL_NO_CONTEXT;
    }
    if (CTX.egl.TinySurface != EGL_NO_SURFACE) {
        error("        eglDestroySurface( Display, TinySurface )");
        if (eglDestroySurface(CTX.egl.Display, CTX.egl.TinySurface) == EGL_FALSE) {
            error("        eglDestroySurface() failed: %s", EglErrorString(eglGetError()));
        }
        CTX.egl.TinySurface = EGL_NO_SURFACE;
    }
    if (CTX.egl.Display != 0) {
        error("        eglTerminate( Display )");
        if (eglTerminate(CTX.egl.Display) == EGL_FALSE) {
            error("        eglTerminate() failed: %s", EglErrorString(eglGetError()));
        }
        CTX.egl.Display = 0;
    }
}

inline uint64_t getTimestampUs() {
    timeval tv;
    gettimeofday(&tv, nullptr);

    uint64_t Current = (uint64_t) tv.tv_sec * 1000 * 1000 + tv.tv_usec;
    return Current;
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

void updateBinary(uint64_t path, uint32_t flag) {
    auto value = flag != 0;
    auto *stateRef = &CTX.previousButtonsState[path];
    if (stateRef->binary != value) {
        stateRef->tag = ALVR_BUTTON_VALUE_BINARY;
        stateRef->binary = value;

        alvr_send_button(path, *stateRef);
    }
}

void updateScalar(uint64_t path, float value) {
    auto *stateRef = &CTX.previousButtonsState[path];
    if (abs(stateRef->scalar - value) > BUTTON_EPS) {
        stateRef->tag = ALVR_BUTTON_VALUE_SCALAR;
        stateRef->scalar = value;

        alvr_send_button(path, *stateRef);
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

// return fov in OpenXR convention
EyeFov getFov(qiyu_DeviceInfo* di, int eye) {
    // ovrTracking2 tracking = vrapi_GetPredictedTracking2(CTX.ovrContext, 0.0);

    qiyu_ViewFrustum* pFrust;

    if (eye == 0) {
        pFrust = &di->frustumLeftEye;
    } else {
        pFrust = &di->frustumRightEye;
    }

    EyeFov fov;

    fov.left = (float) atan(pFrust->left / pFrust->near);
    fov.right = (float) atan(pFrust->right / pFrust->near);
    fov.top = (float) atan(pFrust->top / pFrust->near);
    fov.bottom = (float) atan(pFrust->bottom / pFrust->near);

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
    qiyu_Vector3 bboxScale;
    // Theoretically pose (the 2nd parameter) could be nullptr, since we already have that, but
    // then this function gives us 0-size bounding box, so it has to be provided.
    bboxScale = qiyu_GetBoundaryDimensions();
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

    if (index == 0) {
        return (uint8_t) left.batteryLevel;
    } else {
        return (uint8_t) right.batteryLevel;
    }
}

auto& finishHapticsBuffer = qiyu_StopControllerVibration;

void updateHapticsState() {
    qiyu_ControllerMask curCaps;

    if(!qiyu_IsControllerInit()) {
        return;
    }

    for (uint32_t deviceIndex = 0;
         deviceIndex < CI_COUNT;
         deviceIndex++) {

        // if (!ctrlState.qyCtrlData[deviceIndex].isConnect)
        //     continue;

        curCaps = deviceIndex ? qiyu_ControllerMask::CM_Right : qiyu_ControllerMask::CM_Left;

        int curHandIndex =
                deviceIndex ? 0 : 1;
        auto &s = CTX.hapticsState[curHandIndex];

        uint64_t currentUs = getTimestampUs();

        if (s.fresh) {
            s.startUs = s.startUs + currentUs;
            s.endUs = s.startUs + s.endUs;
            s.fresh = false;
        }

        if (s.startUs <= 0) {
            // No requested haptics for this hand.
            if (s.buffered) {
                finishHapticsBuffer(curCaps);
                s.buffered = false;
            }
            continue;
        }

        if (currentUs >= s.endUs) {
            // No more haptics is needed.
            s.startUs = 0;
            if (s.buffered) {
                finishHapticsBuffer(curCaps);
                s.buffered = false;
            }
            continue;
        }

        qiyu_StartControllerVibration(
            curCaps, s.amplitude, (float) (s.endUs - currentUs) / 1e6);
        s.buffered = true;
    }
}

// low frequency events.
// This thread gets created after the creation of ovrContext and before its destruction
void eventsThread() {
    auto java = getOvrJava(true);

    jclass cls = java.Env->GetObjectClass(java.ActivityObject);
    jmethodID onStreamStartMethod = java.Env->GetMethodID(cls, "onStreamStart", "()V");
    jmethodID onStreamStopMethod = java.Env->GetMethodID(cls, "onStreamStop", "()V");

    auto deadline = std::chrono::steady_clock::now();
    auto motionVec = std::vector<AlvrDeviceMotion>();

    int recenterCount = 0;

    while (CTX.running) {
        if (CTX.streaming) {
            motionVec.clear();
            OculusHand leftHand = {false};
            OculusHand rightHand = {false};

            AlvrDeviceMotion headMotion = {};
            uint64_t targetTimestampNs =
                    getTimestampNs() + alvr_get_prediction_offset_ns();
            auto headTracking =
                    qiyu_PredictHeadPose((float) alvr_get_prediction_offset_ns() / 1e6);
            headMotion.device_id = HEAD_ID;
            headMotion.orientation.x = headTracking.pose.rotation.x;
            headMotion.orientation.y = headTracking.pose.rotation.y;
            headMotion.orientation.z = headTracking.pose.rotation.z;
            headMotion.orientation.w = -headTracking.pose.rotation.w;
            headMotion.position[0] = -headTracking.pose.position.x;
            headMotion.position[1] = -headTracking.pose.position.y - g_fTrackingOffset;
            headMotion.position[2] = -headTracking.pose.position.z;
            // Note: do not copy velocities. Avoid reprojection in SteamVR
            motionVec.push_back(headMotion);

            {
                std::lock_guard<std::mutex> lock(CTX.trackingFrameMutex);
                // Insert from the front: it will be searched first
                CTX.trackingFrameMap.push_front({targetTimestampNs, headTracking});
                if (CTX.trackingFrameMap.size() > MAXIMUM_TRACKING_FRAMES) {
                    CTX.trackingFrameMap.pop_back();
                }
            }

            updateButtons();

            double controllerDisplayTimeS =
                    (double) alvr_get_prediction_offset_ns() / 1e9 *
                    CTX.streamingConfig.controller_prediction_multiplier;

            if(qiyu_IsControllerInit()) {
                qiyu_ControllerData left;
                qiyu_ControllerData right;

                qiyu_GetControllerData(&left, &right);

                // From left-handed to right-handed
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
                        leftHandJerkEstimation[i].rtU.tor = 1.0 / CTX.refreshRate / 3;
                        leftHandJerkEstimation[i].step();
                        leftHand.LinearJerk = leftHandJerkEstimation[i].rtY.jerk;
                        leftHand.LinearSnap = 0.f;
                        leftHand.LinearCrackle = 0.f;
                        predictedPosition[i] = handTrajectoryPrediction(leftHand, controllerDisplayTimeS);
                    }

                    AlvrDeviceMotion motion = {};
                    motion.device_id = LEFT_HAND_ID;
                    memcpy(&motion.orientation, &left.rotation, 4 * 4);
                    memcpy(motion.position, predictedPosition, 4 * 3);
                    memcpy(motion.linear_velocity, &left.velocity, 4 * 3);
                    memcpy(motion.angular_velocity, &left.angVelocity, 4 * 3);
                    motion.position[1] -= g_fTrackingOffset;

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
                        rightHandJerkEstimation[i].rtU.tor = 1.0 / CTX.refreshRate / 3;
                        rightHandJerkEstimation[i].step();
                        rightHand.LinearJerk = rightHandJerkEstimation[i].rtY.jerk;
                        rightHand.LinearSnap = 0.f;
                        rightHand.LinearCrackle = 0.f;
                        predictedPosition[i] = handTrajectoryPrediction(rightHand, controllerDisplayTimeS);
                    }

                    AlvrDeviceMotion motion = {};
                    motion.device_id = RIGHT_HAND_ID;
                    memcpy(&motion.orientation, &right.rotation, 4 * 4);
                    memcpy(motion.position, predictedPosition, 4 * 3);
                    memcpy(motion.linear_velocity, &right.velocity, 4 * 3);
                    memcpy(motion.angular_velocity, &right.angVelocity, 4 * 3);
                    motion.position[1] -= g_fTrackingOffset;

                    motionVec.push_back(motion);
                }
            }

            alvr_send_tracking(targetTimestampNs, &motionVec[0], motionVec.size(), leftHand,
                               rightHand);

        }


        // there is no useful event in the oculus API, ignore
        // ovrEventHeader _eventHeader;
        // auto _res = vrapi_PollEvent(&_eventHeader);

        // int newRecenterCount = vrapi_GetSystemStatusInt(&java, VRAPI_SYS_STATUS_RECENTER_COUNT);
        // if (recenterCount != newRecenterCount) {
        //     float width, height;
        //     getPlayspaceArea(&width, &height);
        //     alvr_send_playspace(width, height);

        //     recenterCount = newRecenterCount;
        // }

        qiyu_DeviceInfo di = qiyu_GetDeviceInfo();
        auto newLeftFov = getFov(&di, 0);
        auto newRightFov = getFov(&di, 1);
        float newIpd = getInterpupillaryDistance(&di);

        if (abs(newIpd - CTX.lastIpd) > IPD_EPS ||
            abs(newLeftFov.left - CTX.lastFov.left) > IPD_EPS) {
            EyeFov fov[2] = {newLeftFov, newRightFov};
            alvr_send_views_config(fov, newIpd);
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

        AlvrEvent event;
        while (alvr_poll_event(&event)) {
            if (event.tag == ALVR_EVENT_HAPTICS) {
                auto haptics = event.HAPTICS;
                int curHandIndex = (haptics.device_id == RIGHT_CONTROLLER_HAPTICS_ID ? 0 : 1);
                auto &s = CTX.hapticsState[curHandIndex];
                s.startUs = 0;
                s.endUs = (uint64_t) (haptics.duration_s * 1000'000);
                s.amplitude = (haptics.amplitude > 0.2) ? haptics.amplitude : 0.2;
                s.frequency = haptics.frequency;
                s.fresh = true;
                s.buffered = false;
            } else if (event.tag == ALVR_EVENT_STREAMING_STARTED) {
                CTX.streamingConfig = event.STREAMING_STARTED;
                java.Env->CallVoidMethod(java.ActivityObject, onStreamStartMethod);
            } else if (event.tag == ALVR_EVENT_STREAMING_STOPPED) {
                java.Env->CallVoidMethod(java.ActivityObject, onStreamStopMethod);
            } else if (event.tag == ALVR_EVENT_NAL_READY) {
                // unused and unreachable
            }
        }

        deadline += std::chrono::nanoseconds((uint64_t) (1e9 / CTX.refreshRate / 3));
        std::this_thread::sleep_until(deadline);
    }
}

static void CreateLayout_(float centerX, float centerY, float radiusX, float radiusY, qiyu_RenderLayer_ScreenPosUV* pLayout)//FIXME! //TODO!
{
	// This is always in screen space so we want Z = 0 and W = 1
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

    eglInit();

    memset(CTX.hapticsState, 0, sizeof(CTX.hapticsState));
    QY_GL_EXT::InitFunction_Foveation();
    qiyu_Init(java.ActivityObject,
			  java.Vm,
			  qiyu_GraphicsApi::GA_OpenGLES,
			  qiyu_TrackingOriginMode::TM_Ground,
			  false);

    qiyu_DeviceInfo deviceInfo = qiyu_GetDeviceInfo();
    CTX.recommendedViewWidth =
            deviceInfo.iEyeTargetWidth;
    CTX.recommendedViewHeight =
            deviceInfo.iEyeTargetHeight;

    int refreshRatesCount =
            2;
    auto refreshRatesBuffer = std::vector<float>(refreshRatesCount);
    refreshRatesBuffer[0] = 72.f;
    refreshRatesBuffer[1] = 90.f;

    alvr_initialize((void *) CTX.vm,
                    (void *) CTX.context,
                    CTX.recommendedViewWidth,
                    CTX.recommendedViewHeight,
                    &refreshRatesBuffer[0],
                    refreshRatesCount,
                    false);
    alvr_initialize_opengl();
    qiyu_PostSetEyeBufferSize(CTX.recommendedViewWidth, CTX.recommendedViewHeight);
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_destroyNative(JNIEnv *_env, jobject _context) {
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
        error("Invalid ANativeWindow");
    }

    // set Color Space
    // ovrHmdColorDesc colorDesc{};
    // colorDesc.ColorSpace = VRAPI_COLORSPACE_RIFT_S;
    // vrapi_SetClientColorDesc(CTX.ovrContext, &colorDesc);

    // vrapi_SetPerfThread(CTX.ovrContext, VRAPI_PERF_THREAD_TYPE_MAIN, gettid());

    qiyu_SetTrackingOriginMode(qiyu_TrackingOriginMode::TM_Ground);

    std::vector<int32_t> textureHandlesBuffer[2];
    bool isSupport_Foveation = QY_GL_EXT::IsSupport_Foveation();
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
    CTX.eventsThread = std::thread(eventsThread);

    alvr_resume_opengl(CTX.recommendedViewWidth, CTX.recommendedViewHeight, textureHandles,
                       textureHandlesBuffer[0].size());
    alvr_resume();

    // vrapi_SetDisplayRefreshRate(CTX.ovrContext, CTX.refreshRate);
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_onStreamStartNative(JNIEnv *_env, jobject _context) {
    auto java = getOvrJava();

    CTX.refreshRate = CTX.streamingConfig.fps;

    std::vector<int32_t> textureHandlesBuffer[2];
    bool isSupport_Foveation = QY_GL_EXT::IsSupport_Foveation();
    for (int eye = 0; eye < 2; eye++) {
        for (int index = 0; index < NUM_EYE_BUFFERS_; index++) {
            CTX.streamBuffers[eye].eyeTarget[index].Init(
                false, GL_FOVEATION_ENABLE_BIT_QCOM | GL_FOVEATION_SCALED_BIN_METHOD_BIT_QCOM, false, 
                CTX.streamingConfig.view_width, CTX.streamingConfig.view_height, 1, GL_RGBA8, false, false);
            auto handle = CTX.streamBuffers[eye].eyeTarget[index].GetColorAttachment();
            textureHandlesBuffer[eye].push_back(handle);
        }

        CTX.streamBuffers[eye].index = 0;
    }
    const int32_t *textureHandles[2] = {&textureHandlesBuffer[0][0], &textureHandlesBuffer[1][0]};
    qiyu_PostSetEyeBufferSize(CTX.streamingConfig.view_width, CTX.streamingConfig.view_height);

    // On Oculus Quest, without ExtraLatencyMode frames passed to vrapi_SubmitFrame2 are sometimes
    // discarded from VrAPI(?). Which introduces stutter animation. I think the number of discarded
    // frames is shown as Stale in Logcat like following:
    //    I/VrApi:
    //    FPS=72,Prd=63ms,Tear=0,Early=0,Stale=8,VSnc=1,Lat=0,Fov=0,CPU4/GPU=3/3,1958/515MHz,OC=FF,TA=0/E0/0,SP=N/F/N,Mem=1804MHz,Free=989MB,PSM=0,PLS=0,Temp=36.0C/0.0C,TW=1.90ms,App=2.74ms,GD=0.00ms
    // After enabling ExtraLatencyMode:
    //    I/VrApi:
    //    FPS=71,Prd=76ms,Tear=0,Early=66,Stale=0,VSnc=1,Lat=1,Fov=0,CPU4/GPU=3/3,1958/515MHz,OC=FF,TA=0/E0/0,SP=N/N/N,Mem=1804MHz,Free=906MB,PSM=0,PLS=0,Temp=38.0C/0.0C,TW=1.93ms,App=1.46ms,GD=0.00ms
    // We need to set ExtraLatencyMode On to workaround for this issue.
    // vrapi_SetExtraLatencyMode(CTX.ovrContext,
    //                           (ovrExtraLatencyMode) CTX.streamingConfig.extra_latency);

    // ovrResult result = vrapi_SetDisplayRefreshRate(CTX.ovrContext, CTX.refreshRate);
    // if (result != ovrSuccess) {
    //     error("Failed to set refresh rate requested by the server: %d", result);
    // }

    if (CTX.streamingConfig.oculus_foveation_level == FL_Custom) {
        qiyu_FoveationParam customFoveationParam;
		customFoveationParam.gainRate.x = 8.0f;
		customFoveationParam.gainRate.y = 8.0f;
		customFoveationParam.areaSize = 1.0f;
		customFoveationParam.minResolution = 0.0625f;
		qiyu_SetFoveation(FL_Custom, &customFoveationParam);
    } else {
        qiyu_SetFoveation(static_cast<qiyu_FoveationLevel>(CTX.streamingConfig.oculus_foveation_level));
    }
    // vrapi_SetPropertyInt(
    //         &java, VRAPI_DYNAMIC_FOVEATION_ENABLED, CTX.streamingConfig.dynamic_oculus_foveation);

    qiyu_DeviceInfo di = qiyu_GetDeviceInfo();
    EyeFov fovArr[2] = {getFov(&di, 0), getFov(&di, 1)};
    float ipd = getInterpupillaryDistance(&di);
    alvr_send_views_config(fovArr, ipd);

    alvr_send_battery(HEAD_ID, CTX.hmdBattery, CTX.hmdPlugged);
    alvr_send_battery(LEFT_HAND_ID, getControllerBattery(0) / 100.f, false);
    alvr_send_battery(RIGHT_HAND_ID, getControllerBattery(1) / 100.f, false);

    float areaWidth, areaHeight;
    getPlayspaceArea(&areaWidth, &areaHeight);
    alvr_send_playspace(areaWidth, areaHeight);

    alvr_start_stream_opengl(textureHandles, textureHandlesBuffer[0].size());

    CTX.streaming = true;
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_onStreamStopNative(JNIEnv *_env, jobject _context) {
    CTX.streaming = false;

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

    CTX.ovrContext = nullptr;

    if (CTX.window != nullptr) {
        ANativeWindow_release(CTX.window);
    }
    CTX.window = nullptr;
}

extern "C" JNIEXPORT void JNICALL
Java_alvr_client_VRActivity_renderNative(JNIEnv *_env, jobject _context) {
    double displayTime;
    qiyu_HeadPoseState tracking;
    qiyu_FrameParam frameParam;
    memset(&frameParam, 0, sizeof(frameParam));

    uint64_t currentUs = getTimestampUs();
    float tickSecond = (currentUs - CTX.lastFrameTimeUs) / 1000000.0;
    qiyu_Update(tickSecond);
    CTX.lastFrameTimeUs = currentUs;

    if (CTX.streaming) {
        void *streamHardwareBuffer = nullptr;
        auto timestampNs = alvr_get_frame(&streamHardwareBuffer);
        displayTime = (double) timestampNs / 1e9;

        if (timestampNs == -1) {
            return;
        }

        updateHapticsState();

        {
            std::lock_guard<std::mutex> lock(CTX.trackingFrameMutex);

            // Take the frame with equal timestamp, or the next closest one.
            for (auto &pair: CTX.trackingFrameMap) {
                if (pair.first <= timestampNs) {
                    tracking = pair.second;
                    break;
                }
            }
        }

        int swapchainIndices[2] = {CTX.streamBuffers[0].index,
                                   CTX.streamBuffers[1].index};
        qiyu_StartEye(false, EYE_Left, TT_Texture);
        qiyu_StartEye(false, EYE_Right, TT_Texture);
        alvr_render_stream_opengl(streamHardwareBuffer, swapchainIndices);
        qiyu_EndEye(false, EYE_Left, TT_Texture);
        qiyu_EndEye(false, EYE_Right, TT_Texture);

        float vsyncQueueMs = qiyu_PredictDisplayTime();
        alvr_report_submit(timestampNs, vsyncQueueMs * 1e6);

        for (int eye = 0; eye < 2; eye++) {
            frameParam.renderLayers[eye].imageHandle = CTX.streamBuffers[eye].eyeTarget[CTX.streamBuffers[eye].index].GetColorAttachment();
            frameParam.renderLayers[eye].imageType = TT_Texture;
            CreateLayout_(0.0f, 0.0f, 1.0f, 1.0f, &frameParam.renderLayers[eye].imageCoords);//FIXME! //TODO!
            frameParam.renderLayers[eye].eyeMask = eye ? RL_EyeMask_Right : RL_EyeMask_Left;
            CTX.streamBuffers[eye].index = (CTX.streamBuffers[eye].index + 1) % NUM_EYE_BUFFERS_;
        }
    } else {
        qiyu_DeviceInfo di = qiyu_GetDeviceInfo();
        float fPredictedTimeMs = qiyu_PredictDisplayTime();
	    tracking = qiyu_PredictHeadPose(fPredictedTimeMs);

        qiyu_Quaternion leftEyeRot;// glm quat is (w)(xyz), BUT here is xyzw
        leftEyeRot.x = di.frustumLeftEye.rotation.x;
        leftEyeRot.y = di.frustumLeftEye.rotation.y;
        leftEyeRot.z = di.frustumLeftEye.rotation.z;
        leftEyeRot.w = di.frustumLeftEye.rotation.w;
        qiyu_Quaternion rightEyeRot;// glm quat is (w)(xyz), BUT here is xyzw
        rightEyeRot.x = di.frustumRightEye.rotation.x;
        rightEyeRot.y = di.frustumRightEye.rotation.y;
        rightEyeRot.z = di.frustumRightEye.rotation.z;
        rightEyeRot.w = di.frustumRightEye.rotation.w;
        qiyu_Matrix4 outEyeMatrix[2];
        qiyu_GetViewMatrix(outEyeMatrix[0], outEyeMatrix[1], g_fTrackingOffset, tracking, leftEyeRot, rightEyeRot);

        AlvrEyeInput eyeInputs[2] = {};
        int swapchainIndices[2] = {};
        for (int eye = 0; eye < 2; eye++) {
            auto q = tracking.pose.rotation;
            auto v = ovrMatrix4f_Inverse((ovrMatrix4f*) &outEyeMatrix[eye]);

            eyeInputs[eye].orientation = AlvrQuat{q.x, q.y, q.z, -q.w};
            eyeInputs[eye].position[0] = -v.M[0][3];
            eyeInputs[eye].position[1] = -v.M[1][3] - g_fTrackingOffset;
            eyeInputs[eye].position[2] = -v.M[2][3];
            eyeInputs[eye].fov = getFov(&di, eye);

            swapchainIndices[eye] = CTX.lobbyBuffers[eye].index;
        }
        qiyu_StartEye(false, EYE_Left, TT_Texture);
        qiyu_StartEye(false, EYE_Right, TT_Texture);
        alvr_render_lobby_opengl(eyeInputs, swapchainIndices);
        qiyu_EndEye(false, EYE_Left, TT_Texture);
        qiyu_EndEye(false, EYE_Right, TT_Texture);


        for (int eye = 0; eye < 2; eye++) {
            frameParam.renderLayers[eye].imageHandle = CTX.lobbyBuffers[eye].eyeTarget[CTX.lobbyBuffers[eye].index].GetColorAttachment();
            frameParam.renderLayers[eye].imageType = TT_Texture;
            CreateLayout_(0.0f, 0.0f, 1.0f, 1.0f, &frameParam.renderLayers[eye].imageCoords);//FIXME! //TODO!
            frameParam.renderLayers[eye].eyeMask = eye ? RL_EyeMask_Right : RL_EyeMask_Left;
            CTX.lobbyBuffers[eye].index = (CTX.lobbyBuffers[eye].index + 1) % NUM_EYE_BUFFERS_;
        }
    }

    frameParam.minVsyncs = 1;
    frameParam.headPoseState = tracking;

    qiyu_SubmitFrame(frameParam);

    CTX.ovrFrameIndex++;
}

extern "C" JNIEXPORT void JNICALL Java_alvr_client_VRActivity_onBatteryChangedNative(
        JNIEnv *_env, jobject _context, jint battery, jboolean plugged) {
    alvr_send_battery(HEAD_ID, (float) battery / 100.f, (bool) plugged);
    CTX.hmdBattery = battery;
    CTX.hmdPlugged = plugged;
}
