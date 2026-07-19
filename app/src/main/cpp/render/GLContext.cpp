#include "render/GLContext.h"

#include <android/log.h>
#include <android/native_window.h>

#define LOG_TAG "GLContext"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace nle {

GLContext::~GLContext() { Destroy(); }

bool GLContext::Initialize(void* nativeWindow, const GLContext* sharedWith) {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }

    EGLint majorVersion, minorVersion;
    if (!eglInitialize(display_, &majorVersion, &minorVersion)) {
        LOGE("eglInitialize failed");
        return false;
    }

    // Requesting GLES 3.0 with an 8-bit RGBA config. Anything using this
    // context (preview compositing, export rendering) targets GLES 3.0
    // baseline; the GraphicsAPI abstraction (render/OpenGLRenderer.h) is
    // what will let a future Vulkan backend swap in without touching
    // callers of GLContext.
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };

    EGLint numConfigs = 0;
    if (!eglChooseConfig(display_, configAttribs, &config_, 1, &numConfigs) || numConfigs == 0) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext shareContext = sharedWith ? sharedWith->context_ : EGL_NO_CONTEXT;
    context_ = eglCreateContext(display_, config_, shareContext, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        return false;
    }

    surface_ = eglCreateWindowSurface(display_, config_, reinterpret_cast<EGLNativeWindowType>(nativeWindow), nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: 0x%x", eglGetError());
        return false;
    }

    return true;
}

void GLContext::Destroy() {
    if (display_ != EGL_NO_DISPLAY) {
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        eglTerminate(display_);
    }
    display_ = EGL_NO_DISPLAY;
    context_ = EGL_NO_CONTEXT;
    surface_ = EGL_NO_SURFACE;
}

bool GLContext::MakeCurrent() {
    return eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
}

bool GLContext::SwapBuffers() {
    return eglSwapBuffers(display_, surface_) == EGL_TRUE;
}

}  // namespace nle
