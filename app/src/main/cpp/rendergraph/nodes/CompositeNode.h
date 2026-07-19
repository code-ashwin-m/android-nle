// CompositeNode.h
//
// Blends all active track outputs into a single frame, in the order given
// (see Timeline::ActiveClipsAt: index 0 = bottom, composited first). With
// Phase 1's single video track this node has exactly one input and is a
// pass-through; it exists from day one anyway because "multiple video
// tracks" is an explicit future-phase requirement, and retrofitting
// compositing into a graph that assumed one input everywhere would be
// exactly the kind of refactor the spec says to avoid.
//
// Standard "over" alpha blending is used per layer for Phase 1. Blend
// modes (multiply, screen, etc. -- listed as a future render graph node)
// slot in later as a per-input parameter this node reads from the Clip's
// track, not as a structural change to CompositeNode itself.

#pragma once

#include "render/GraphicsDevice.h"
#include "render/TexturePool.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class CompositeNode : public RenderNode {
public:
    std::string Name() const override { return "Composite"; }

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
            uniform sampler2D uBase;
            uniform sampler2D uOverlay;
            uniform float uHasOverlay;
            in vec2 vTexCoord;
            out vec4 fragColor;
            void main() {
                vec4 base = texture(uBase, vTexCoord);
                if (uHasOverlay < 0.5) { fragColor = base; return; }
                vec4 top = texture(uOverlay, vTexCoord);
                fragColor = mix(base, top, top.a);  // standard "over" blend
            }
        )";
        shader_ = context.graphicsDevice->CompileShader(kVertexSrc, kFragmentSrc);
    }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        std::vector<Frame> valid;
        for (auto& f : inputs) {
            if (f.IsValid()) valid.push_back(f);
        }
        if (valid.empty()) return Frame{};
        if (valid.size() == 1) return valid[0];  // nothing to blend

        Frame accumulator = valid[0];
        for (size_t i = 1; i < valid.size(); ++i) {
            Texture* target = context.texturePool->Acquire(context.outputWidth, context.outputHeight, 0x8058);
            context.graphicsDevice->BindRenderTarget(target);
            context.graphicsDevice->DrawFullscreenQuad(
                shader_,
                {TextureBinding{accumulator.textureId, accumulator.textureTarget, "uBase"},
                 TextureBinding{valid[i].textureId, valid[i].textureTarget, "uOverlay"}},
                {{"uHasOverlay", 1.0f}});
            context.graphicsDevice->BindRenderTarget(nullptr);

            accumulator.textureId = target->Id();
            accumulator.textureTarget = target->Target();
            accumulator.width = target->Width();
            accumulator.height = target->Height();
        }
        return accumulator;
    }

private:
    ShaderHandle shader_ = kInvalidShader;
};

}  // namespace nle
