// OutputNode.h
//
// This header is the literal embodiment of "Preview and export MUST use
// the exact same render graph. Only the output node changes." Both
// classes below do the same thing -- blit the final composited frame into
// whatever surface the *current* GLContext has bound as its default
// framebuffer -- and differ only in which GLContext that is:
//
//   PreviewOutputNode:  bound to the on-screen preview SurfaceView/TextureView.
//   EncoderOutputNode:  bound to the Surface obtained from
//                       AMediaCodec_createInputSurface() (see encode/Encoder.h).
//
// PlaybackEngine constructs the shared upstream graph once (decoder ->
// color convert -> brightness -> composite) and attaches whichever output
// node matches the current mode; RenderGraph itself has no branch for
// "am I previewing or exporting".

#pragma once

#include "render/GLContext.h"
#include "render/GraphicsDevice.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class PreviewOutputNode : public RenderNode {
public:
    explicit PreviewOutputNode(GLContext* previewContext) : previewContext_(previewContext) {}

    std::string Name() const override { return "PreviewOutput"; }

    void OnAttach(const RenderContext& context) override { shader_ = BuildBlitShader(context); }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        if (inputs.empty() || !inputs[0].IsValid()) return Frame{};
        previewContext_->MakeCurrent();
        context.graphicsDevice->BindRenderTarget(nullptr);  // default framebuffer = on-screen surface
        context.graphicsDevice->DrawFullscreenQuad(
            shader_, {TextureBinding{inputs[0].textureId, inputs[0].textureTarget, "uSource"}}, {});
        previewContext_->SwapBuffers();
        return inputs[0];  // pass-through so render stats can inspect the final frame if needed
    }

private:
    static ShaderHandle BuildBlitShader(const RenderContext& context) {
        static const char* kVertexSrc = R"(#version 300 es
            in vec2 aPosition;
            in vec2 aTexCoord;
            out vec2 vTexCoord;
            void main() { vTexCoord = aTexCoord; gl_Position = vec4(aPosition, 0.0, 1.0); }
        )";
        static const char* kFragmentSrc = R"(#version 300 es
            precision mediump float;
            uniform sampler2D uSource;
            in vec2 vTexCoord;
            out vec4 fragColor;
            void main() { fragColor = texture(uSource, vTexCoord); }
        )";
        return context.graphicsDevice->CompileShader(kVertexSrc, kFragmentSrc);
    }

    GLContext* previewContext_;
    ShaderHandle shader_ = kInvalidShader;
};

// EncoderOutputNode is structurally identical to PreviewOutputNode -- the
// comment block at the top of this file explains why that's the point, not
// duplication to clean up later. It's kept as a separate class (rather than
// a single node parameterized by "which GLContext") so preview-only
// concerns (e.g. render statistics overlays) and export-only concerns
// (e.g. signaling end-of-stream to the encoder) can each grow independently
// without an if/else branch inside one class.
class EncoderOutputNode : public RenderNode {
public:
    explicit EncoderOutputNode(GLContext* encoderContext) : encoderContext_(encoderContext) {}

    std::string Name() const override { return "EncoderOutput"; }

    void OnAttach(const RenderContext& context) override {
        static const char* kVertexSrc = R"(#version 300 es
            in vec2 aPosition;
            in vec2 aTexCoord;
            out vec2 vTexCoord;
            void main() { vTexCoord = aTexCoord; gl_Position = vec4(aPosition, 0.0, 1.0); }
        )";
        static const char* kFragmentSrc = R"(#version 300 es
            precision mediump float;
            uniform sampler2D uSource;
            in vec2 vTexCoord;
            out vec4 fragColor;
            void main() { fragColor = texture(uSource, vTexCoord); }
        )";
        shader_ = context.graphicsDevice->CompileShader(kVertexSrc, kFragmentSrc);
    }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        if (inputs.empty() || !inputs[0].IsValid()) return Frame{};
        encoderContext_->MakeCurrent();
        context.graphicsDevice->BindRenderTarget(nullptr);  // default framebuffer = encoder input surface
        context.graphicsDevice->DrawFullscreenQuad(
            shader_, {TextureBinding{inputs[0].textureId, inputs[0].textureTarget, "uSource"}}, {});
        // eglSwapBuffers here is what actually hands this frame to
        // MediaCodec's input surface with its presentation timestamp
        // already set via eglPresentationTimeANDROID (see encode/Encoder.cpp).
        encoderContext_->SwapBuffers();
        return inputs[0];
    }

private:
    GLContext* encoderContext_;
    ShaderHandle shader_ = kInvalidShader;
};

}  // namespace nle
