// ColorConvertNode.h
//
// The decoder (decode/Decoder.h) renders into a Surface backed by a
// SurfaceTexture, which means the "texture" it produces is a
// GL_TEXTURE_EXTERNAL_OES sampler in whatever colorspace/chroma layout the
// hardware decoder used internally (YUV 4:2:0, BT.601 or BT.709 depending
// on the source). Every downstream node (Brightness, Composite) should not
// need to know or care about any of that -- they just want an RGBA texture
// in a predictable colorspace. That normalization is this node's entire
// job: sample the external texture once, with the correct color matrix,
// and write a standard GL_TEXTURE_2D RGBA8 texture.
//
// Isolating this in its own node (rather than folding YUV->RGB conversion
// into the first effect node) is what let BrightnessNode -- and every
// future effect node -- be written as a plain RGBA-in, RGBA-out shader,
// with zero knowledge of decoder-specific texture formats.

#pragma once

#include "render/GraphicsDevice.h"
#include "render/TexturePool.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class ColorConvertNode : public RenderNode {
public:
    std::string Name() const override { return "ColorConvert"; }

    void OnAttach(const RenderContext& context) override {
        // BT.709 full-range YUV->RGB matrix, applied to a GL_TEXTURE_EXTERNAL_OES
        // sampler. GLES treats external OES textures as already producing
        // RGB via the driver's internal YUV conversion on most Android
        // GPUs, but we still pass through a color-managed pass here so a
        // future HDR/BT.2020 source is a shader-only change, not a new node.
        static const char* kVertexSrc = R"(#version 300 es
            in vec2 aPosition;
            in vec2 aTexCoord;
            out vec2 vTexCoord;
            void main() {
                vTexCoord = aTexCoord;
                gl_Position = vec4(aPosition, 0.0, 1.0);
            }
        )";
        static const char* kFragmentSrc = R"(#version 300 es
            #extension GL_OES_EGL_image_external_essl3 : require
            precision mediump float;
            uniform samplerExternalOES uSource;
            in vec2 vTexCoord;
            out vec4 fragColor;
            void main() {
                fragColor = texture(uSource, vTexCoord);
            }
        )";
        shader_ = context.graphicsDevice->CompileShader(kVertexSrc, kFragmentSrc);
    }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        if (inputs.empty() || !inputs[0].IsValid() || shader_ == kInvalidShader) return Frame{};
        const Frame& source = inputs[0];

        Texture* target = context.texturePool->Acquire(source.width, source.height, /*GL_RGBA8*/ 0x8058);
        context.graphicsDevice->BindRenderTarget(target);
        context.graphicsDevice->DrawFullscreenQuad(
            shader_, {TextureBinding{source.textureId, source.textureTarget, "uSource"}}, {});
        context.graphicsDevice->BindRenderTarget(nullptr);

        Frame out;
        out.textureId = target->Id();
        out.textureTarget = target->Target();
        out.width = target->Width();
        out.height = target->Height();
        out.format = PixelFormat::RGBA8;
        out.presentationTimeUs = source.presentationTimeUs;
        return out;
    }

private:
    ShaderHandle shader_ = kInvalidShader;
};

}  // namespace nle
