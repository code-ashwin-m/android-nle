// ColorNode.h
//
// One of the "generic nodes" the spec asks for instead of hardcoded
// DreamyEffect.cpp/CyberpunkEffect.cpp files: exposure/contrast/saturation
// color correction, parameterized entirely through NodeParam<double> so any
// number of *different* effect packages can each reference a "ColorNode"
// instance with different parameter bindings (see
// assets/effects/Dreamy.effect/graph.json vs Cyberpunk.effect/graph.json)
// without this file changing at all.
//
// This is also the node the future Adjustment Layer track type will reuse
// unchanged -- an adjustment layer's "clip" is just an Effect (optionally
// package-backed) applied to the composite beneath it, and PlaybackEngine
// wires that composite in as this node's input exactly the way a normal
// clip's decoded frame is (see playback/PlaybackEngine.cpp, adjustment-track
// handling). Same node class, same shader, different upstream input -- no
// new C++ type needed for adjustment layers' color correction.
//
// Written in the same shape as BrightnessNode (core/rendergraph/nodes/) on
// purpose: OnAttach compiles one shader once, Process resolves each
// parameter for the current frame's time and skips the draw entirely when
// every parameter is at its neutral value, same GPU-cost discipline as
// Phase 1's hand-written nodes.

#pragma once

#include "core/PropertyBag.h"
#include "effects/EffectGraphDef.h"
#include "render/GraphicsDevice.h"
#include "render/TexturePool.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class ColorNode : public RenderNode {
public:
    ColorNode(const NodeInstanceDef& def, PropertyBag& bag)
        : exposure_(ResolveScalarParam(def, "exposure", bag, 0.0)),
          contrast_(ResolveScalarParam(def, "contrast", bag, 0.0)),
          saturation_(ResolveScalarParam(def, "saturation", bag, 0.0)) {}

    std::string Name() const override { return "Color"; }

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
            uniform float uExposure;    // stops, 0 = unchanged
            uniform float uContrast;    // -1..1, 0 = unchanged
            uniform float uSaturation;  // -1..1, 0 = unchanged
            in vec2 vTexCoord;
            out vec4 fragColor;
            void main() {
                vec4 src = texture(uSource, vTexCoord);
                vec3 c = src.rgb * pow(2.0, uExposure);
                c = (c - 0.5) * (1.0 + uContrast) + 0.5;
                float luma = dot(c, vec3(0.299, 0.587, 0.114));
                c = mix(vec3(luma), c, 1.0 + uSaturation);
                fragColor = vec4(clamp(c, 0.0, 1.0), src.a);
            }
        )";
        shader_ = context.graphicsDevice->CompileShader(kVertexSrc, kFragmentSrc);
    }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        if (inputs.empty() || !inputs[0].IsValid() || shader_ == kInvalidShader) {
            return inputs.empty() ? Frame{} : inputs[0];
        }
        const Frame& source = inputs[0];

        double exposure = exposure_.ValueAt(context.time);
        double contrast = contrast_.ValueAt(context.time);
        double saturation = saturation_.ValueAt(context.time);
        if (exposure == 0.0 && contrast == 0.0 && saturation == 0.0) return source;  // no-op: skip the GPU pass

        Texture* target = context.texturePool->Acquire(source.width, source.height, /*GL_RGBA8*/ 0x8058);
        context.graphicsDevice->BindRenderTarget(target);
        context.graphicsDevice->DrawFullscreenQuad(
            shader_, {TextureBinding{source.textureId, source.textureTarget, "uSource"}},
            {{"uExposure", static_cast<float>(exposure)},
             {"uContrast", static_cast<float>(contrast)},
             {"uSaturation", static_cast<float>(saturation)}});
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
    NodeParam<double> exposure_;
    NodeParam<double> contrast_;
    NodeParam<double> saturation_;
    ShaderHandle shader_ = kInvalidShader;
};

}  // namespace nle
