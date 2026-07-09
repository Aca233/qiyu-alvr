/*******************************************************
Copyright (c) 2021 IQIYISMART, Inc. All Rights Reserved.
*******************************************************/

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include "QYRenderTarget.h"
#include "QYUtil.h"
#include <assert.h>
// #include "JNIHelper.h"

#if !defined( GL_TEXTURE_PROTECTED_EXT )
#define GL_TEXTURE_PROTECTED_EXT    0x8BFA
#endif

PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC glFramebufferTexture2DMultisampleEXT = NULL;


//! Multiview
typedef void(*PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVR)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVR glFramebufferTextureMultiviewOVR = NULL;
//
typedef void(*PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVR)(GLenum, GLenum, GLuint, GLint, GLsizei, GLint, GLsizei);
PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVR glFramebufferTextureMultisampleMultiviewOVR = NULL;
//
typedef void(*PFNGLTEXSTORAGE3DMULTISAMPLEOES)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean);
PFNGLTEXSTORAGE3DMULTISAMPLEOES glTexStorage3DMultisampleOES = NULL;

#ifndef GL_TEXTURE_2D_MULTISAMPLE_ARRAY
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY			0x9102
#endif//GL_TEXTURE_2D_MULTISAMPLE_ARRAY

#ifndef GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE	0x8D56
#endif//GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE


#define MULTIVIEW_SLICES_COUNT	2


namespace QY_GL_EXT
{
	bool InitFunction_MultiView()
	{
		assert(glFramebufferTextureMultiviewOVR == NULL);
		glFramebufferTextureMultiviewOVR = (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVR)eglGetProcAddress("glFramebufferTextureMultiviewOVR");
		if (!glFramebufferTextureMultiviewOVR)
		{
			// LOGE("@@QY_GL_EXT::InitFunction_MultiView, glFramebufferTextureMultiviewOVR is not supported, fail to get proc address!");
			return false;
		}
		//
		assert(glFramebufferTextureMultisampleMultiviewOVR == NULL);
		glFramebufferTextureMultisampleMultiviewOVR = (PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVR)eglGetProcAddress("glFramebufferTextureMultisampleMultiviewOVR");
		if (!glFramebufferTextureMultisampleMultiviewOVR)
		{
			// LOGE("@@QY_GL_EXT::InitFunction_MultiView, glFramebufferTextureMultisampleMultiviewOVR is not supported, fail to get proc address!");
			return false;
		}
		//
		assert(glTexStorage3DMultisampleOES == NULL);
		glTexStorage3DMultisampleOES = (PFNGLTEXSTORAGE3DMULTISAMPLEOES)eglGetProcAddress("glTexStorage3DMultisampleOES");
		if (!glTexStorage3DMultisampleOES)
		{
			// LOGE("@@QY_GL_EXT::InitFunction_MultiView, glTexStorage3DMultisampleOES is not supported, fail to get proc address!");
			return false;
		}
		return true;
	}
	bool IsSupport_MultiView()
	{
		return (glFramebufferTextureMultiviewOVR != NULL 
			&& glFramebufferTextureMultisampleMultiviewOVR != NULL 
			&& glTexStorage3DMultisampleOES != NULL);
	}
}


static void GetFormatTypeFromSizedFormat(GLenum sizedFormat, GLenum& outFormat, GLenum& outType)
{
	switch (sizedFormat)
	{
	case GL_R8:
		outFormat = GL_RED;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_R8_SNORM:
		outFormat = GL_RED;
		outType = GL_BYTE;
		return;
	case GL_R16F:
		outFormat = GL_RED;
		outType = GL_HALF_FLOAT;
		return;
	case GL_R32F:
		outFormat = GL_RED;
		outType = GL_FLOAT;
		return;
	case GL_R8UI:
		outFormat = GL_RED_INTEGER;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_R8I:
		outFormat = GL_RED_INTEGER;
		outType = GL_BYTE;
		return;
	case GL_R16UI:
		outFormat = GL_RED_INTEGER;
		outType = GL_UNSIGNED_SHORT;
		return;
	case GL_R16I:
		outFormat = GL_RED_INTEGER;
		outType = GL_SHORT;
		return;
	case GL_R32UI:
		outFormat = GL_RED_INTEGER;
		outType = GL_UNSIGNED_INT;
		return;
	case GL_R32I:
		outFormat = GL_RED_INTEGER;
		outType = GL_INT;
		return;
	case GL_RG8:
		outFormat = GL_RG;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_RG8_SNORM:
		outFormat = GL_RG;
		outType = GL_BYTE;
		return;
	case GL_RG16F:
		outFormat = GL_RG;
		outType = GL_HALF_FLOAT;
		return;
	case GL_RG32F:
		outFormat = GL_RG;
		outType = GL_FLOAT;
		return;
	case GL_RG8UI:
		outFormat = GL_RG_INTEGER;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_RG8I:
		outFormat = GL_RG_INTEGER;
		outType = GL_BYTE;
		return;
	case GL_RG16UI:
		outFormat = GL_RG_INTEGER;
		outType = GL_UNSIGNED_SHORT;
		return;
	case GL_RG16I:
		outFormat = GL_RG_INTEGER;
		outType = GL_SHORT;
		return;
	case GL_RG32UI:
		outFormat = GL_RG_INTEGER;
		outType = GL_UNSIGNED_INT;
		return;
	case GL_RG32I:
		outFormat = GL_RG_INTEGER;
		outType = GL_INT;
		return;
	case GL_RGB8:
		outFormat = GL_RGB;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_SRGB8:
		outFormat = GL_RGB;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_RGB565:
		outFormat = GL_RGB;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_RGB8_SNORM:
		outFormat = GL_RGB;
		outType = GL_BYTE;
		return;
	case GL_R11F_G11F_B10F:
		outFormat = GL_RGB;
		outType = GL_HALF_FLOAT;
		return;
	case GL_RGB9_E5:
		outFormat = GL_RGB;
		outType = GL_HALF_FLOAT;
		return;
	case GL_RGB16F:
		outFormat = GL_RGB;
		outType = GL_HALF_FLOAT;
		return;
	case GL_RGB32F:
		outFormat = GL_RGB;
		outType = GL_FLOAT;
		return;
	case GL_RGB8UI:
		outFormat = GL_RGB_INTEGER;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_RGB8I:
		outFormat = GL_RGB_INTEGER;
		outType = GL_BYTE;
		return;
	case GL_RGB16UI:
		outFormat = GL_RGB_INTEGER;
		outType = GL_UNSIGNED_SHORT;
		return;
	case GL_RGB16I:
		outFormat = GL_RGB_INTEGER;
		outType = GL_SHORT;
		return;
	case GL_RGB32UI:
		outFormat = GL_RGB_INTEGER;
		outType = GL_UNSIGNED_INT;
		return;
	case GL_RGB32I:
		outFormat = GL_RGB_INTEGER;
		outType = GL_INT;
		return;
	case GL_RGBA8:
		outFormat = GL_RGBA;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_SRGB8_ALPHA8:
		outFormat = GL_RGBA;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_RGBA8_SNORM:
		outFormat = GL_RGBA;
		outType = GL_BYTE;
		return;
	case GL_RGB5_A1:
		outFormat = GL_RGBA;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_RGBA4:
		outFormat = GL_RGBA;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_RGB10_A2:
		outFormat = GL_RGBA;
		outType = GL_UNSIGNED_INT_2_10_10_10_REV;
		return;
	case GL_RGBA16F:
		outFormat = GL_RGBA;
		outType = GL_HALF_FLOAT;
		return;
	case GL_RGBA32F:
		outFormat = GL_RGBA;
		outType = GL_FLOAT;
		return;
	case GL_RGBA8UI:
		outFormat = GL_RGBA_INTEGER;
		outType = GL_UNSIGNED_BYTE;
		return;
	case GL_RGBA8I:
		outFormat = GL_RGBA_INTEGER;
		outType = GL_BYTE;
		return;
	case GL_RGB10_A2UI:
		outFormat = GL_RGBA_INTEGER;
		outType = GL_UNSIGNED_INT_2_10_10_10_REV;
		return;
	case GL_RGBA16UI:
		outFormat = GL_RGBA_INTEGER;
		outType = GL_UNSIGNED_SHORT;
		return;
	case GL_RGBA16I:
		outFormat = GL_RGBA_INTEGER;
		outType = GL_SHORT;
		return;
	case GL_RGBA32I:
		outFormat = GL_RGBA_INTEGER;
		outType = GL_INT;
		return;
	case GL_RGBA32UI:
		outFormat = GL_RGBA_INTEGER;
		outType = GL_UNSIGNED_INT;
		return;
	}
}

QYRenderTarget::QYRenderTarget()
	: m_bInit(false)
	, m_nWidth(0)
	, m_nHeight(0)
	, m_nSamples(0)
	, m_bIsProtectedContent(false)
	, m_bRequireDepth(true)
	, m_bIsMultiView(false)
	, m_colorSizedFormat(GL_RGBA8)
	, m_format(0)
	, m_type(0)
	, m_idColor(0)
	, m_idDepth(0)
	, m_idFBO(0)
{
}

QYRenderTarget::~QYRenderTarget()
{
	Release();
}

void QYRenderTarget::Init_genColorDepth()
{
	GLenum target_ = m_bIsMultiView ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;

	QY_GL(glGenTextures(1, &m_idColor));
	QY_GL(glBindTexture(target_, m_idColor));
	QY_GL(glTexParameteri(target_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	QY_GL(glTexParameteri(target_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	QY_GL(glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	QY_GL(glTexParameteri(target_, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	if (m_bIsMultiView)
	{
		QY_GL(glTexStorage3D(target_, 1, m_colorSizedFormat, m_nWidth, m_nHeight, MULTIVIEW_SLICES_COUNT));
	}
	else
	{
		if (m_bIsProtectedContent)
		{
			QY_GL(glTexParameteri(target_, GL_TEXTURE_PROTECTED_EXT, GL_TRUE));
			QY_GL(glTexStorage2D(target_, 1, m_colorSizedFormat, m_nWidth, m_nHeight));
		}
		else
		{
			QY_GL(glTexImage2D(target_, 0, m_colorSizedFormat, m_nWidth, m_nHeight, 0, m_format, m_type, NULL));
		}
	}
	QY_GL(glBindTexture(target_, 0));

	if (m_bRequireDepth)
	{
		QY_GL(glGenTextures(1, &m_idDepth));
		QY_GL(glBindTexture(target_, m_idDepth));
		if (m_bIsMultiView)
		{
            QY_GL(glTexStorage3D(target_, 1, GL_DEPTH_COMPONENT24, m_nWidth, m_nHeight, MULTIVIEW_SLICES_COUNT));
		}
		else
		{
			if (m_bIsProtectedContent)
			{
				QY_GL(glTexParameteri(target_, GL_TEXTURE_PROTECTED_EXT, GL_TRUE));
				QY_GL(glTexStorage2D(target_, 1, GL_DEPTH_COMPONENT32F, m_nWidth, m_nHeight));
			}
			else
			{
				QY_GL(glTexImage2D(target_, 0, GL_DEPTH_COMPONENT, m_nWidth, m_nHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL));
			}
		}
		QY_GL(glBindTexture(target_, 0));
	}
}

bool QYRenderTarget::Init_MultiView()
{
	if (!QY_GL_EXT::IsSupport_MultiView())
	{
		// LOGE("@@QYRenderTarget::Init_MultiView, fail to QY_GL_EXT::IsSupport_MultiView");
		return false;
	}

	Init_genColorDepth();

	QY_GL(glGenFramebuffers(1, &m_idFBO));
	QY_GL(glBindFramebuffer(GL_FRAMEBUFFER, m_idFBO));//
	//
	if (m_nSamples > 1)
	{
		QY_GL(glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_idColor, 0, m_nSamples, 0, MULTIVIEW_SLICES_COUNT));//
		if (m_bRequireDepth)
		{
			QY_GL(glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_idDepth, 0, m_nSamples, 0, MULTIVIEW_SLICES_COUNT));//
		}
	}
	else
	{
		QY_GL(glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_idColor, 0, 0, MULTIVIEW_SLICES_COUNT));//
		if (m_bRequireDepth)
		{
			QY_GL(glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_idDepth, 0, 0, MULTIVIEW_SLICES_COUNT));//
		}
	}
	//////////////////////////////////////////////
    // Verify the frame buffer was created
    GLenum eResult = glCheckFramebufferStatus(GL_FRAMEBUFFER);//
    if (eResult != GL_FRAMEBUFFER_COMPLETE)
    {
        const char *pPrefix = "MultiView Framebuffer";
        switch (eResult)
        {
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            // LOGE("@@QYRenderTarget::Init_MultiView, %s => Error (GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT) setting up FBO", pPrefix);
            break;
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            // LOGE("@@QYRenderTarget::Init_MultiView, %s => Error (GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) setting up FBO", pPrefix);
            break;
            case GL_FRAMEBUFFER_UNSUPPORTED:
            // LOGE("@@QYRenderTarget::Init_MultiView, %s => Error (GL_FRAMEBUFFER_UNSUPPORTED) setting up FBO", pPrefix);
            break;
            case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            // LOGE("@@QYRenderTarget::Init_MultiView, %s => Error (GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE) setting up FBO", pPrefix);
            break;
            default:
            // LOGE("@@QYRenderTarget::Init_MultiView, %s => Error (0x%X) setting up FBO", pPrefix, eResult);
            break;
        }
		return false;
    }
	//////////////////////////////////////////////
	//
	QY_GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));//
	return true;
}

bool QYRenderTarget::Init_SingleSample()
{
	Init_genColorDepth();

	QY_GL(glGenFramebuffers(1, &m_idFBO));
	QY_GL(glBindFramebuffer(GL_FRAMEBUFFER, m_idFBO));
	//
	QY_GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_idColor, 0));
	if (m_bRequireDepth)
	{
		QY_GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_idDepth, 0));
	}
	//
	QY_GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	return true;
}

bool QYRenderTarget::Init_MultiSampleEXT()
{
	//PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC glFramebufferTexture2DMultisampleEXT = NULL;
	if (!glFramebufferTexture2DMultisampleEXT)
		glFramebufferTexture2DMultisampleEXT = (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC)eglGetProcAddress("glFramebufferTexture2DMultisampleEXT");
	if (!glFramebufferTexture2DMultisampleEXT)
	{
		// LOGE("@@QYRenderTarget::Init_MultiSampleEXT, ERROR: Couldn't get function pointer to glFramebufferTexture2DMultisampleEXT!");
		return false;
	}

	Init_genColorDepth();

	QY_GL(glGenFramebuffers(1, &m_idFBO));
	QY_GL(glBindFramebuffer(GL_FRAMEBUFFER, m_idFBO));
	//
	QY_GL(glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_idColor, 0, m_nSamples));
	if (m_bRequireDepth)
	{
		QY_GL(glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_idDepth, 0, m_nSamples));
	}
	//
	QY_GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	return true;
}

bool QYRenderTarget::Init(bool isSupport_Foveation, GLuint foveationFeatureBits, bool isMultiView, int width, int height, int samples, int colorSizedFormat, bool requireDepth, bool isProtectedContent)
{
	m_nWidth = width;
	m_nHeight = height;
	m_nSamples = samples;
	m_bIsProtectedContent = isProtectedContent;
	m_bRequireDepth = requireDepth;
	m_bIsMultiView = isMultiView;
	m_colorSizedFormat = colorSizedFormat;
	GLenum format;
	GLenum type;
	GetFormatTypeFromSizedFormat(m_colorSizedFormat, format, type);
	m_format = format;
	m_type = type;

	if (m_bIsMultiView)
	{
		m_bInit = Init_MultiView();
	}
	else
	{
		if (m_nSamples > 1)
		{
			m_bInit = Init_MultiSampleEXT();
		}
		else
		{
			m_bInit = Init_SingleSample();
		}
	}

	if (m_bInit)
	{
		if (isSupport_Foveation)
		{
			GLenum texType_ = m_bIsMultiView ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
			QY_GL(glBindTexture(texType_, m_idColor));
			QY_GL(glTexParameteri(texType_, GL_TEXTURE_FOVEATED_FEATURE_BITS_QCOM, foveationFeatureBits));
			////glTexParameteri(texType_, GL_TEXTURE_PREVIOUS_SOURCE_TEXTURE_QCOM, PreviousTexture);//FIXME! //TODO!
			//glTexParameterf(texType_, GL_TEXTURE_FOVEATED_MIN_PIXEL_DENSITY_QCOM, foveationParam.minResolution);//FIXME! better set when create; otherwise is just for TEST!
			QY_GL(glBindTexture(texType_, 0));
		}
	}
	else
	{
		Release();
	}

	return m_bInit;
}

void QYRenderTarget::Release()
{
	assert(m_bInit);
	QY_GL(glDeleteFramebuffers(1, &m_idFBO));
	QY_GL(glDeleteTextures(1, &m_idColor));
	if (m_idDepth != 0)
	{
		QY_GL(glDeleteTextures(1, &m_idDepth));
	}
}

bool QYRenderTarget::Bind()
{
	assert(m_bInit);
	if (!Ref())
	{
		// LOGE("@@QYRenderTarget::Bind, RenderTarget(Handle = %d) is being bound without being unbound. Bind count = %d", m_idFBO, m_uRefCount);
		return false;
	}
	QY_GL(glBindFramebuffer(GL_FRAMEBUFFER, m_idFBO));
	return true;
}

bool QYRenderTarget::UnBind()
{
	assert(m_bInit);
	if (!UnRef())
	{
		// LOGE("@@QYRenderTarget::UnBind, RenderTarget(Handle = %d) is being unbound without being bound.", m_idFBO);
		return false;
	}
	QY_GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	return true;
}
