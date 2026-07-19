// GLContext.h
//
// Wraps EGL display/context/surface setup. There are exactly two call
// sites that need a GL context: the render thread driving the preview
// SurfaceView, and the encoder path rendering into the MediaCodec input
// Surface for export. Both go through this one class (constructed twice,
// once per use) rather than duplicating EGL boilerplate, which is easy to
// get subtly wrong (e.g. mismatched EGL configs causing preview to work
// but export to silently produce black frames).
//
// A shared EGL context (via `sharedWith`) is used so textures created
// while rendering preview (e.g. decoded frame textures cached across
// calls) are visible to the export context too, avoiding a texture upload
// per context when scrubbing while an export-quality proxy is warming up.

#pragma once

#include <EGL/egl.h>

namespace nle {

class GLContext {
public:
    GLContext() = default;
    ~GLContext();

    // surface may be an ANativeWindow* (preview SurfaceView / TextureView)
    // or an encoder input Surface obtained from AMediaCodec. `sharedWith`
    // is nullable; pass another GLContext to share its texture namespace.
    bool Initialize(void* nativeWindow, const GLContext* sharedWith = nullptr);
    void Destroy();

    bool MakeCurrent();
    bool SwapBuffers();

    bool IsValid() const { return context_ != EGL_NO_CONTEXT; }

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLConfig config_ = nullptr;
};

}  // namespace nle
