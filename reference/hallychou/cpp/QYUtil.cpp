/*******************************************************
Copyright (c) 2021 IQIYISMART, Inc. All Rights Reserved.
*******************************************************/

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "QYUtil.h"
// #include "JNIHelper.h"

namespace QY
{
	void CheckGlError(const char* file, int line)
	{
		for (GLint error = glGetError(); error; error = glGetError())
		{
			char *pError;
			switch (error)
			{
			case GL_NO_ERROR:                       pError = (char *)"GL_NO_ERROR";                         break;
			case GL_INVALID_ENUM:                   pError = (char *)"GL_INVALID_ENUM";                     break;
			case GL_INVALID_VALUE:                  pError = (char *)"GL_INVALID_VALUE";                    break;
			case GL_INVALID_OPERATION:              pError = (char *)"GL_INVALID_OPERATION";                break;
			case GL_OUT_OF_MEMORY:                  pError = (char *)"GL_OUT_OF_MEMORY";                    break;
			case GL_INVALID_FRAMEBUFFER_OPERATION:  pError = (char *)"GL_INVALID_FRAMEBUFFER_OPERATION";    break;

			default:
				// LOGE("@@CheckGlError, Error (0x%x) %s:%d\n", error, file, line);
				return;
			}

			// LOGE("@@CheckGlError, Error (%s) %s:%d\n", pError, file, line);
			return;
		}
		return;
	}

	void CheckEglError(const char* file, int line)
	{
		for (int i = 0; i < 10; i++)
		{
			const EGLint error = eglGetError();
			if (error == EGL_SUCCESS)
			{
				break;
			}

			char *pError;
			switch (error)
			{
			case EGL_SUCCESS:				pError = (char *)"EGL_SUCCESS"; break;
			case EGL_NOT_INITIALIZED:		pError = (char *)"EGL_NOT_INITIALIZED"; break;
			case EGL_BAD_ACCESS:			pError = (char *)"EGL_BAD_ACCESS"; break;
			case EGL_BAD_ALLOC:				pError = (char *)"EGL_BAD_ALLOC"; break;
			case EGL_BAD_ATTRIBUTE:			pError = (char *)"EGL_BAD_ATTRIBUTE"; break;
			case EGL_BAD_CONTEXT:			pError = (char *)"EGL_BAD_CONTEXT"; break;
			case EGL_BAD_CONFIG:			pError = (char *)"EGL_BAD_CONFIG"; break;
			case EGL_BAD_CURRENT_SURFACE:	pError = (char *)"EGL_BAD_CURRENT_SURFACE"; break;
			case EGL_BAD_DISPLAY:			pError = (char *)"EGL_BAD_DISPLAY"; break;
			case EGL_BAD_SURFACE:			pError = (char *)"EGL_BAD_SURFACE"; break;
			case EGL_BAD_MATCH:				pError = (char *)"EGL_BAD_MATCH"; break;
			case EGL_BAD_PARAMETER:			pError = (char *)"EGL_BAD_PARAMETER"; break;
			case EGL_BAD_NATIVE_PIXMAP:		pError = (char *)"EGL_BAD_NATIVE_PIXMAP"; break;
			case EGL_BAD_NATIVE_WINDOW:		pError = (char *)"EGL_BAD_NATIVE_WINDOW"; break;
			case EGL_CONTEXT_LOST:			pError = (char *)"EGL_CONTEXT_LOST"; break;
			default:
				// LOGE("@@CheckEglError, Error (0x%x) %s:%d\n", error, file, line);
				return;
			}
			// LOGE("@@CheckEglError, Error (%s) %s:%d\n", pError, file, line);
			return;
		}
		return;
	}
}
