/*******************************************************
Copyright (c) 2021 IQIYISMART, Inc. All Rights Reserved.
*******************************************************/
#ifndef _QIYUAPI_H_
#define _QIYUAPI_H_

#include <android/native_window_jni.h> // for native window JNI
#include "QiyuApiDef.h"


#if defined(QIYUAPI_ENABLE_EXPORT)
#define QIYUAPI_EXPORT __attribute__((__visibility__("default")))
#else
#define QIYUAPI_EXPORT
#endif


#define QIYU_SDK_VERSION_PRODUCT	1
#define QIYU_SDK_VERSION_MAJOR		0
#define QIYU_SDK_VERSION_MINOR		0
struct qiyu_SdkVersion
{
	int versionProduct;
	int versionMajor;
	int versionMinor;
};

enum class qiyu_GraphicsApi
{
	GA_OpenGLES,
	//GA_Vulkan
};


struct qiyu_Vector2
{
	float x, y;
};
struct qiyu_Vector3
{
	float x, y, z;
};
struct qiyu_Quaternion
{
	float x, y, z, w;
};
struct qiyu_Matrix4
{
	float M[4][4];
};

struct qiyu_ViewFrustum
{
	float 			left;		//left plane
	float 			right;		//right plane
	float 			top;		//top plane
	float 			bottom;		//bottom plane
	float 			near;		//near plane
	float 			far;		//far plane
	qiyu_Vector3    position;	//frustum position
	qiyu_Quaternion rotation;	//frustum rotation
};

enum qiyu_FoveationLevel//NOTE! @PerformanceTool, FL_None= -1, etc... to make value same with unity&ue sdk.
{
	FL_None = 0,						//Default value. Not use foveation.
	FL_Low,								//use foveationLow from qiyu_DeviceInfo.
	FL_Medium,							//use foveationMedium from qiyu_DeviceInfo.
	FL_High,							//use foveationHigh from qiyu_DeviceInfo.
	FL_COUNT_DeviceLevel,

	FL_Custom = FL_COUNT_DeviceLevel,	//use customFoveation.

	FL_COUNT
};
struct qiyu_FoveationParam
{
	qiyu_Vector2	gainRate;			//Foveation Gain Rate [1, ...]
	float			areaSize;			//Foveation Area Size [0, ...]
	float			minResolution;		//Foveation Minimum Resolution [1, 1/2, 1/4, ..., 1/16, 0]
};

struct qiyu_DeviceInfo
{
	qiyu_ViewFrustum 	frustumLeftEye;		//frustum information for left eye
	qiyu_ViewFrustum 	frustumRightEye;	//frustum information for right eye
	int32_t 			iEyeTargetWidth;	//eye buffer width
	int32_t 			iEyeTargetHeight;	//eye buffer height
	qiyu_FoveationParam	foveationLow;		//foveation low level values
	qiyu_FoveationParam	foveationMedium;	//foveation medium level values
	qiyu_FoveationParam	foveationHigh;		//foveation high level values
};

enum qiyu_Eye
{
	EYE_Left = 0,	//left eye
	EYE_Right,		//right eye
	EYE_COUNT		//eye count
};

struct qiyu_ControllerData
{
	int 			isConnect;			//connection state
	int 			button;				//button state. refer to qiyu_ButtonType
	int 			buttonTouch;		//touch state. refer to qiyu_ButtonType
	int 			batteryLevel;		//[0, 100] battery level
	float 			triggerForce;		//[0.0f, 1.0f] trigger force
	float 			gripForce;			//[0.0f, 1.0f] grip force
	int 			isShow;				//whether show controller
	qiyu_Vector2 	joyStickPos;		//[-1.0f, 1.0f] joystick position
	qiyu_Vector3 	position;			//controller position (meter)
	qiyu_Quaternion rotation;			//controller quaternion
	qiyu_Vector3 	velocity;			//linear velocity
	qiyu_Vector3 	acceleration;		//linear acceleration
	qiyu_Vector3 	angVelocity;		//angular velocity
	qiyu_Vector3 	angAcceleration;	//angular acceleration
};

enum qiyu_ButtonType
{
	BT_None			= 0,
	BT_Trigger		= 0x01,
	BT_Grip			= 0x02,
	BT_A_X			= 0x10,
	BT_B_Y			= 0x20,
	BT_Home_Menu	= 0x40,
	BT_JoyStick 	= 0x80
};

enum class qiyu_ControllerMask
{
	CM_Left		= 0x01,		//left controller
	CM_Right	= 0x02,		//right controller
	CM_Both		= 0x03		//left and right controllers
};

enum qiyu_ControllerIndex
{
	CI_Left = 0,	//left controller
	CI_Right,		//right controller
	CI_COUNT
};

enum qiyu_PerfLevel
{
	PL_System 	= 0,	//default system level
	PL_Minimum 	= 1,	//min level
	PL_Medium 	= 2,	//medium level
	PL_Maximum 	= 3,	//max level
	PL_COUNT
};

struct qiyu_HeadPose
{
	qiyu_Quaternion   rotation;	//rotation quaternion
	qiyu_Vector3      position;	//position
};
struct qiyu_HeadPoseState
{
	qiyu_HeadPose 	pose;					//head pose
	int32_t 		poseStatus;				//Bit field (sxrTrackingMode) indicating pose status
	uint64_t 		poseTimeStampNs;		//Time stamp in which the head pose was generated (nanoseconds)
	uint64_t 		poseFetchTimeNs;		//Time stamp when this pose was retrieved
	uint64_t 		expectedDisplayTimeNs;	//Expected time when this pose should be on screen (nanoseconds)
};

enum class qiyu_TrackingOriginMode
{
	TM_Device,	//device mode. default mode.
	TM_Ground	//ground mode. 
};

enum qiyu_TextureType
{
	TT_Texture = 0,			//Standard texture
	TT_TextureArray,		//Standard texture array (Left eye is first layer, right eye is second layer)
	TT_Image,				//EGL Image texture
	TT_EquiRectTexture,		//Equirectangular texture
	TT_EquiRectImage,		//Equirectangular Image texture
	TT_CubemapTexture,		//Cubemap texture (Not supporting cubemap image)
	//TT_Vulkan,				//Vulkan texture
	//TT_Camera,				//Camera texture
	//TT_VulkanTextureArray	//Vulkan texture array
};

struct qiyu_RenderLayer_ScreenPosUV//
{
	float LowerLeftPos[4];		//0 = X-Position; 1 = Y-Position; 2 = Z-Position; 3 = W-Component
	float LowerRightPos[4];		//0 = X-Position; 1 = Y-Position; 2 = Z-Position; 3 = W-Component
	float UpperLeftPos[4];		//0 = X-Position; 1 = Y-Position; 2 = Z-Position; 3 = W-Component
	float UpperRightPos[4];		//0 = X-Position; 1 = Y-Position; 2 = Z-Position; 3 = W-Component
	float LowerUVs[4];			//[0,1] = Lower Left UV values; [2,3] = Lower Right UV values
	float UpperUVs[4];			//[0,1] = Upper Left UV values; [2,3] = Upper Right UV values
	float TransformMatrix[16];	//Column major uv transform matrix data. Applies to video textures (see SurfaceTexture::getTransformMatrix())		//FIXME! //FIXME!
};

enum qiyu_RenderLayer_EyeMask//
{
	RL_EyeMask_Left		= 0x00000001,
	RL_EyeMask_Right	= 0x00000002,
	RL_EyeMask_Both		= 0x00000003
};

//struct qiyu_RenderLayer_VulkanTexInfo//
//{
//	uint32_t memSize;
//	uint32_t width;
//	uint32_t height;
//	uint32_t numMips;
//	uint32_t bytesPerPixel;
//	uint32_t renderSemaphore;
//};

struct qiyu_RenderLayer
{
	int32_t							imageHandle;	//Handle to the texture/image to be rendered
	qiyu_TextureType				imageType;		//Type of texture: Standard Texture or EGL Image
	qiyu_RenderLayer_ScreenPosUV	imageCoords;	//Layout of this layer on the screen
	qiyu_RenderLayer_EyeMask		eyeMask;		//Determines which eye[s] receive this render layer

	//qiyu_RenderLayer_VulkanTexInfo	vulkanInfo;		//Information about the data if it is a Vulkan texture

};

static const int qiyu_RenderLayerMaxCount = 16;//

struct qiyu_FrameParam//
{

	int32_t				minVsyncs;								//Minimum number of vysnc events before displaying the frame (1=display refresh, 2=half refresh, etc...)

	qiyu_RenderLayer	renderLayers[qiyu_RenderLayerMaxCount];	//Description of each render layer   //FIXME! use dynamicArray?

	qiyu_HeadPoseState	headPoseState;							//Head pose state used to generate the frame  

};



#if defined(__cplusplus)
extern "C"
{
#endif

	/**
	 * @brief Get SDK version.
	 * @return qiyu_SdkVersion.
	 */
	QIYUAPI_EXPORT qiyu_SdkVersion qiyu_GetSdkVersion();

	/**
	 * @brief Init QiyuNativeSDK. 'void android_main(struct android_app* app)' is the main entry point of a native application that is using android_native_app_glue.  It runs in its own thread, with its own event loop for receiving input events and doing other things.
	 * @param[in] clazz - The object handle of activity which is 'ANativeActivity' object instance that this 'android_app' is running in. eg.'android_app->activity->clazz;'
	 * @param[in] vm - The global handle on the process's Java VM in 'ANativeActivity' in 'android_app'. eg.'android_app->activity->vm;'
	 * @param[in] graphicsApi - Specify the graphics api. Please check 'enum class qiyu_GraphicsApi'.
	 * @param[in] trackMode - Specify the mode to be device or ground. Please check 'enum class qiyu_TrackingOriginMode'.
	 * @param[in] bLogVerbose - Output the verbose log. Note this will output many unuseful log, just open this to fix bug while debug.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_Init(jobject clazz,
									JavaVM* vm,
									qiyu_GraphicsApi graphicsApi = qiyu_GraphicsApi::GA_OpenGLES,
									qiyu_TrackingOriginMode trackMode = qiyu_TrackingOriginMode::TM_Device,
									bool bLogVerbose = false);

	/**
	 * @brief Release QiyuNativeSDK, normally called while APP_CMD_DESTROY/APP_CMD_TERM_WINDOW, etc. Please refer to the Samples.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_Release();

	/**
	 * @brief Start VR service, normally called in the main loop while APP_CMD_START/APP_CMD_RESUME/APP_CMD_INIT_WINDOW, etc. Please refer to the Samples.
	 * @param[in] nativeWindow - The window surface that the 'android_app' can draw in. eg.'android_app->window;'
	 * @param[in] cpuPerfLevel - CPU performance level.
	 * @param[in] gpuPerfLevel - GPU performance level.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_StartVR(ANativeWindow* nativeWindow, qiyu_PerfLevel cpuPerfLevel, qiyu_PerfLevel gpuPerfLevel);

	/**
	 * @brief Terminate VR service, normally called while APP_CMD_PAUSE, etc. Please refer to the Samples.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_EndVR();

	/**
	 * @brief Prepare rendering for one eye, called AFTER eye buffer is Bind but BEFORE rendering, normally called after renderState is set.
	 * @param[in] isMultiView - true use MultiView; false use MultiPass.
	 * @param[in] eyeType - Specify left/right eye buffer to be rendered on.
	 * @param[in] imageType - Specify Texture Type, refer to 'enum qiyu_TextureType'.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_StartEye(bool isMultiView, qiyu_Eye eyeType, qiyu_TextureType imageType);

	/**
	 * @brief Finish rendering for one eye, called AFTER eye buffer is rendered but BEFORE eye buffer is Unbind and submit frame.
	 * @param[in] isMultiView - true use MultiView; false use MultiPass.
	 * @param[in] eyeType - Specify left/right eye buffer was rendered on.
	 * @param[in] imageType - Specify Texture Type, refer to 'enum qiyu_TextureType'.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_EndEye(bool isMultiView, qiyu_Eye eyeType, qiyu_TextureType imageType);

	/**
	 * @brief Submit one frame, by default submit to ATW(asynchronous time warp), called after render finished.
	 * @param[in] frameParam - refer to qiyu_FrameParam.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_SubmitFrame(const qiyu_FrameParam& frameParam);

	/**
	 * @brief Update, normally called before render or in another thread other rendering-thread.
	 * @param[in] deltaTime(in seconds) - time since last frame.
	 */
	QIYUAPI_EXPORT void qiyu_Update(float deltaTime);

	/**
	 * @brief Get the device information.
	 * @return qiyu_DeviceInfo - Please check 'struct qiyu_DeviceInfo'.
	 */
	QIYUAPI_EXPORT qiyu_DeviceInfo qiyu_GetDeviceInfo();

	/**
	 * @brief Set foveation data.
	 * @param[in] foveatLevel - Please check 'enum qiyu_FoveationLevel'.
	 * @param[in] customFoveatParam - Only need when (foveatLevel==FL_Custom). Please check 'struct qiyu_FoveationParam'.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_SetFoveation(qiyu_FoveationLevel foveatLevel, const qiyu_FoveationParam* customFoveatParam = nullptr);

	/**
	 * @brief Get foveation data.
	 * @param[out] foveatLevel - Please check 'enum qiyu_FoveationLevel'.
	 * @param[out] foveatParam - Please check 'struct qiyu_FoveationParam'.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_GetFoveation(qiyu_FoveationLevel& foveatLevel, qiyu_FoveationParam& foveatParam);

	/**
	 * @brief Get updated view matrix for both eyes, called before rendering and submitFrame which means before qiyu_StartEye. Use the updated lastes IPD inside.
	 * @param[inout] outLeftEyeViewMatrix - Return the view matrix for left eye.
	 * @param[inout] outRightEyeViewMatrix - Return the view matrix for right eye.
	 * @param[inout] outTrackingOffset(in meters) - Return value of TrackingOffset which take care of HeadsetToGround and the height of recenter. HeadsetToGround is the distance from headset down to the ground so the value is minus. NOTE this value should be used to adjust the position of controllers.
	 * @param[in] poseState - Head pose state, normally use the value get from qiyu_PredictHeadPose.
	 * @param[in] eyeRotationLeft - Rotation offset for left eye.
	 * @param[in] eyeRotationRight - Rotation offset for right eye.
	 * @return true if succeed; false if fail.
	 */
	bool qiyu_GetViewMatrix(qiyu_Matrix4& outLeftEyeViewMatrix, qiyu_Matrix4& outRightEyeViewMatrix, float& outTrackingOffset,
							const qiyu_HeadPoseState& poseState, const qiyu_Quaternion& eyeRotationLeft, const qiyu_Quaternion& eyeRotationRight);

	/**
	 * @brief Get the predicted time when current frame be displayed.
	 * @return Predicted display time(in milliseconds).
	 */
	QIYUAPI_EXPORT float qiyu_PredictDisplayTime();

	/**
	 * @brief Predicte head pose based on the predicteDisplayTime pass in, normally called before qiyu_GetViewMatrix.
	 * @param[in] predicteDisplayTime(in milliseconds) - Time ahead of current time to predict the head pose.
	 * @return qiyu_HeadPoseState - Predicted head pose. Please check 'struct qiyu_HeadPoseState'.
	 */
	QIYUAPI_EXPORT qiyu_HeadPoseState qiyu_PredictHeadPose(float predicteDisplayTime);

	/**
	 * @brief Call this after everytime you set EyeBuffer size, if not, stats from "QIYU Performance Tool" will be wrong. NOTE! EyeBuffer is totoally managed by developer, this API is only for collect Performance stats.
	 * @param[in] width - EyeBuffer width.
	 * @param[in] height - EyeBuffer height.
	 */
	QIYUAPI_EXPORT void qiyu_PostSetEyeBufferSize(int width, int height);


	/**
	 * @brief Get graphics api, which is pass in qiyu_Init then can be got by qiyu_GetGraphicsApi.
	 * @return qiyu_GraphicsApi - Return the graphics api. Please check 'enum class qiyu_GraphicsApi'.
	 */
	QIYUAPI_EXPORT qiyu_GraphicsApi qiyu_GetGraphicsApi();

	/**
	 * @brief Set trackingOrigin mode, mode is pass in qiyu_Init then can be changed by this function.
	 * @param[in] trackMode - Specify the mode to be device or ground. Please check 'enum class qiyu_TrackingOriginMode'.
	 */
	QIYUAPI_EXPORT void qiyu_SetTrackingOriginMode(qiyu_TrackingOriginMode trackMode);

	/**
	 * @brief Get trackingOrigin mode, mode is pass in qiyu_Init then can be changed by qiyu_SetTrackingOriginMode.
	 * @return qiyu_TrackingOriginMode - Return the mode of device or ground. Please check 'enum class qiyu_TrackingOriginMode'.
	 */
	QIYUAPI_EXPORT qiyu_TrackingOriginMode qiyu_GetTrackingOriginMode();


	/**
	 * @brief Is controller module initialized, called and check the return value before all other controller related api was called.
	 * @return true if initialized; false if not initialized.
	 */
	QIYUAPI_EXPORT bool qiyu_IsControllerInit();

	/**
	 * @brief Get updated controller data. Please check 'struct qiyu_ControllerData'.
	 * @param[inout] left - Return data for left controller.
	 * @param[inout] right - Return data for right controller.
	 */
	QIYUAPI_EXPORT void qiyu_GetControllerData(qiyu_ControllerData* left, qiyu_ControllerData* right);

	/**
	 * @brief Start vibration for specified controller.
	 * @param[in] whichController - Start vibration for which controller. Please check 'enum class qiyu_ControllerMask'.
	 * @param[in] amplitude - [0.0f, 1.0f] Vibration amplitude.
	 * @param[in] duration(in seconds) - [0.0f, 4.0f] Vibration duration.
	 */
	QIYUAPI_EXPORT void qiyu_StartControllerVibration(qiyu_ControllerMask whichController, float amplitude, float duration);

	/**
	 * @brief Stop vibration for specified controller.
	 * @param[in] whichController - Start vibration for which controller. Please check 'enum class qiyu_ControllerMask'.
	 */
	QIYUAPI_EXPORT void qiyu_StopControllerVibration(qiyu_ControllerMask whichController);


	/**
	 * @brief Get geometry for vitual boundary.
	 * @param[inout] points - Return geometry points data.
	 * @return true if succeed; false if fail.
	 */
	QIYUAPI_EXPORT bool qiyu_GetBoundaryGeometry(std::vector<qiyu_Vector3>& points);

	/**
	 * @brief Get dimensions for vitual boundary.
	 * @return qiyu_Vector3 - the size for x/y/z dimensions.
	 */
	QIYUAPI_EXPORT qiyu_Vector3 qiyu_GetBoundaryDimensions();

	/**
	 * @brief Is boundary visible.
	 * @return true if visible; false if not visible.
	 */
	QIYUAPI_EXPORT bool qiyu_IsBoundaryVisible();

	/**
	 * @brief Is the boundary below head visible.
	 * @return true if visible; false if not visible.
	 */
	QIYUAPI_EXPORT bool qiyu_IsBoundaryBelowHeadVisible();


	/**
	 * @brief Init platform APIs. Must be called before all other Platform APIs.
	 * @param[in] app_id - Application ID.
	 * @param[in] app_secret - Application secret key.
	 * @param[in] callback - Callback.
	 */
	QIYUAPI_EXPORT void qiyu_Platform_Init(const char* app_id, const char* app_secret, PCallback_Init callback);

	/**
	 * @brief Is account login.
	 * @return true if is; false if is not.
	 */
	QIYUAPI_EXPORT bool qiyu_Platform_IsAccountLogin();

	/**
	 * @brief Get QiyuAccountInfo.
	 * @param[in] callback - Callback.
	 */
	QIYUAPI_EXPORT void qiyu_Platform_GetAccountInfo(PCallback_GetAccountInfo callback);

	/**
	 * @brief Launch Other App.
	 * @param[in] app_id - app id.
	 * @param[in] key - DeepLink key.
	 * @param[in] value - DeepLink value.
	 */
	QIYUAPI_EXPORT void qiyu_Platform_LaunchOtherApp(const char* app_id, const char* key, const char* value);

	/**
	 * @brief Get DeppLink.
	 * @param[in] callback - Callback.
	 */
	QIYUAPI_EXPORT void qiyu_Platform_GetDeepLink(PCallback_GetDeepLink callback);


	/**
	 * @brief Init user custom archive methods. Will pop up a window to ask for storage authority. Need external storage authority. Must be called before all other PlayerPrefs archive APIs.
	 */
	QIYUAPI_EXPORT void qiyu_Prefs_Init();

	/**
	 * @brief Get float for specific key in archive, return defaultValue if key not exist.
	 * @param[in] key - key.
	 * @param[in] defaultValue - default value.
	 * @return float value.
	 */
	QIYUAPI_EXPORT float qiyu_Prefs_GetFloat(const char* key, float defaultValue);

	/**
	 * @brief Get int for specific key in archive, return defaultValue if key not exist.
	 * @param[in] key - key.
	 * @param[in] defaultValue - default value.
	 * @return int value.
	 */
	QIYUAPI_EXPORT int qiyu_Prefs_GetInt(const char* key, int defaultValue);

	/**
	 * @brief Get string for specific key in archive, return defaultValue if key not exist.
	 * @param[in] key - key.
	 * @param[in] defaultValue - default value.
	 * @return string value.
	 */
	QIYUAPI_EXPORT const char* qiyu_Prefs_GetString(const char* key, const char* defaultValue);

	/**
	 * @brief If this key exist in archive.
	 * @param[in] key - key.
	 * @return true if exist; false if not.
	 */
	QIYUAPI_EXPORT bool qiyu_Prefs_HasKey(const char* key);

	/**
	 * @brief Save, write to disk, should be called after all modifications.
	 */
	QIYUAPI_EXPORT void qiyu_Prefs_Save();

	/**
	 * @brief Deltete all archive and clear.
	 */
	QIYUAPI_EXPORT void qiyu_Prefs_DeleteAll();

	/**
	 * @brief Delete key in archive.
	 * @param[in] key - key.
	 */
	QIYUAPI_EXPORT void qiyu_Prefs_DeleteKey(const char* key);

	/**
	 * @brief Set float for specific key in archive.
	 * @param[in] key - key.
	 * @param[in] value - value.
	 */
	QIYUAPI_EXPORT void qiyu_Prefs_SetFloat(const char* key, float value);

	/**
	 * @brief Set int for specific key in archive.
	 * @param[in] key - key.
	 * @param[in] value - value.
	 */
	QIYUAPI_EXPORT void qiyu_Prefs_SetInt(const char* key, int value);

	/**
	 * @brief Set string for specific key in archive.
	 * @param[in] key - key.
	 * @param[in] value - value.
	 */
	QIYUAPI_EXPORT void qiyu_Prefs_SetString(const char* key, const char* value);


#if defined(__cplusplus)
} //extern "C"
#endif


#endif//_QIYUAPI_H_