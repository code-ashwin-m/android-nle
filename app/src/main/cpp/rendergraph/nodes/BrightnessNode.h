// BrightnessNode.h
//
// The spec calls out Brightness as the Phase 1 effect and everything else
// (Contrast, Exposure, Saturation, Temperature, Tint, Opacity, Scale,
// Rotation, Crop, Mask, Blur) as "later". This node is deliberately written
// as the template those will copy: look up the active Clip's Effect for
// the current track/time, read its keyframed ScalarProperty via
// Property::ValueAt(context.time), and feed the result to a simple
// fragment shader as a uniform. A future ContrastNode is this file with
// the property name and shader body changed.
//
// Per-frame keyframe evaluation happens here, not by pre-baking a value
// curve into the render graph, specifically so that changing a keyframe
// value takes effect on the very next rendered frame with no graph rebuild
// -- graph *structure* rebuilds only on structural edits (add/remove
// effect), not on every keyframe tweak.

#pragma once

#include "core/Project.h"
#include "core/Track.h"
#include "render/GraphicsDevice.h"
#include "render/TexturePool.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class BrightnessNode : public RenderNode {
public:
    BrightnessNode(Project* project, TrackId trackId) : project_(project), trackId_(trackId) {}

    std::string Name() const override { return "Brightness"; }

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
        // uBrightness is in [-1, 1]; 0 = unchanged, matching the Properties
        // Panel slider range described for brightness.
        static const char* kFragmentSrc = R"(#version 300 es
            precision mediump float;
            uniform sampler2D uSource;
            uniform float uBrightness;
            in vec2 vTexCoord;
            out vec4 fragColor;
            void main() {
                vec4 c = texture(uSource, vTexCoord);
                fragColor = vec4(clamp(c.rgb + uBrightness, 0.0, 1.0), c.a);
            }
        )";
        shader_ = context.graphicsDevice->CompileShader(kVertexSrc, kFragmentSrc);
    }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        if (inputs.empty() || !inputs[0].IsValid() || shader_ == kInvalidShader) {
            return inputs.empty() ? Frame{} : inputs[0];
        }
        const Frame& source = inputs[0];

        double brightness = LookupBrightness(context.time);
        if (brightness == 0.0) return source;  // no-op: skip the draw entirely, saving a GPU pass

        Texture* target = context.texturePool->Acquire(source.width, source.height, /*GL_RGBA8*/ 0x8058);
        context.graphicsDevice->BindRenderTarget(target);
        context.graphicsDevice->DrawFullscreenQuad(
            shader_, {TextureBinding{source.textureId, source.textureTarget, "uSource"}},
            {{"uBrightness", static_cast<float>(brightness)}});
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
    double LookupBrightness(TimeUs time) const {
        Track* track = project_->GetTimeline().FindTrack(trackId_);
        if (!track) return 0.0;
        Clip* clip = track->ClipAt(time);
        if (!clip) return 0.0;
        for (auto& effect : clip->Effects()) {
            if (effect->Type() == EffectType::Brightness) {
                if (ScalarProperty* prop = effect->FindProperty("brightness")) {
                    return prop->ValueAt(time);
                }
            }
        }
        return 0.0;
    }

    Project* project_;
    TrackId trackId_;
    ShaderHandle shader_ = kInvalidShader;
};

}  // namespace nle
