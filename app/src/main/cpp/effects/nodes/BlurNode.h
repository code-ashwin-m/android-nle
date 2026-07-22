// BlurNode.h
//
// A fixed 9-tap single-pass blur, radius-scaled by NodeParam<double>. This
// is intentionally not a true separable two-pass Gaussian (the quality
// upgrade -- horizontal pass into a temp texture, then vertical pass -- is a
// self-contained change to this one file's Process(), not an architecture
// change, whenever a real device profiling pass says it's worth the second
// texture-pool round trip). Phase 2's goal is proving the generic-node +
// data-driven-graph path end to end with a real, visible effect; the exact
// blur kernel is the kind of thing effect packages will want to iterate on
// without touching the C++ that hosts it, once shader overrides (referenced
// in the package format's `shaders/` directory) are wired up.

#pragma once

#include "core/PropertyBag.h"
#include "effects/EffectGraphDef.h"
#include "render/GraphicsDevice.h"
#include "render/TexturePool.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class BlurNode : public RenderNode {
public:
    BlurNode(const NodeInstanceDef& def, PropertyBag& bag) : radius_(ResolveScalarParam(def, "radius", bag, 0.0)) {}

    std::string Name() const override { return "Blur"; }

    void OnAttach(const RenderContext& context) override {
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
            precision mediump float;
            uniform sampler2D uSource;
            uniform float uRadiusPx;
            uniform float uTexelW;
            uniform float uTexelH;
            in vec2 vTexCoord;
            out vec4 fragColor;
            void main() {
                vec2 texel = vec2(uTexelW, uTexelH) * uRadiusPx;
                vec4 sum = vec4(0.0);
                sum += texture(uSource, vTexCoord) * 0.28;
                sum += texture(uSource, vTexCoord + vec2( texel.x, 0.0)) * 0.12;
                sum += texture(uSource, vTexCoord + vec2(-texel.x, 0.0)) * 0.12;
                sum += texture(uSource, vTexCoord + vec2(0.0,  texel.y)) * 0.12;
                sum += texture(uSource, vTexCoord + vec2(0.0, -texel.y)) * 0.12;
                sum += texture(uSource, vTexCoord + vec2( texel.x,  texel.y)) * 0.06;
                sum += texture(uSource, vTexCoord + vec2(-texel.x,  texel.y)) * 0.06;
                sum += texture(uSource, vTexCoord + vec2( texel.x, -texel.y)) * 0.06;
                sum += texture(uSource, vTexCoord + vec2(-texel.x, -texel.y)) * 0.06;
                fragColor = sum;
            }
        )";
        shader_ = context.graphicsDevice->CompileShader(kVertexSrc, kFragmentSrc);
    }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        if (inputs.empty() || !inputs[0].IsValid() || shader_ == kInvalidShader) {
            return inputs.empty() ? Frame{} : inputs[0];
        }
        const Frame& source = inputs[0];

        double radius = radius_.ValueAt(context.time);
        if (radius <= 0.0) return source;  // no-op: skip the draw entirely

        Texture* target = context.texturePool->Acquire(source.width, source.height, /*GL_RGBA8*/ 0x8058);
        context.graphicsDevice->BindRenderTarget(target);
        context.graphicsDevice->DrawFullscreenQuad(
            shader_, {TextureBinding{source.textureId, source.textureTarget, "uSource"}},
            {{"uRadiusPx", static_cast<float>(radius)},
             {"uTexelW", source.width > 0 ? 1.0f / static_cast<float>(source.width) : 0.0f},
             {"uTexelH", source.height > 0 ? 1.0f / static_cast<float>(source.height) : 0.0f}});
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
    NodeParam<double> radius_;
    ShaderHandle shader_ = kInvalidShader;
};

}  // namespace nle
