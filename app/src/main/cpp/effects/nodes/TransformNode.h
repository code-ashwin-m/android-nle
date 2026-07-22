// TransformNode.h
//
// Implements Clip Transform Animation / Clip Rotation Animation / Clip Scale
// Animation (see docs/ARCHITECTURE.md, "Demonstrating clip transform
// animation") as ONE generic node, not three. Position, rotation, and scale
// are three NodeParams read by the same shader in the same draw call,
// because they're not independent effects -- they're three properties of a
// single 2D transform, and treating them as separate nodes would mean
// composing three full-frame passes (and three texture-pool round trips)
// for something that's one small vertex-space computation.
//
// Approach: rather than moving/rotating geometry (this engine draws
// everything as a fullscreen quad -- see GraphicsDevice::DrawFullscreenQuad),
// the shader samples the source texture through the *inverse* transform at
// every output pixel. A pixel outside the transformed source's bounds
// samples nothing (alpha 0), which is exactly "clip moved/shrunk within the
// canvas, canvas shows through elsewhere" -- the correct compositing result
// once this feeds into CompositeNode.
//
// This node is also what a future Camera Animation feature reuses unchanged
// applied to the whole composite rather than one clip -- "camera move" and
// "clip transform" are the same 2D affine transform at different points in
// the graph, which is exactly the kind of duplication the spec's node-graph
// approach is meant to avoid.

#pragma once

#include <cmath>

#include "core/PropertyBag.h"
#include "effects/EffectGraphDef.h"
#include "render/GraphicsDevice.h"
#include "render/TexturePool.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class TransformNode : public RenderNode {
public:
    TransformNode(const NodeInstanceDef& def, PropertyBag& bag)
        : position_(ResolveVec2Param(def, "position", bag, Vec2{0.0, 0.0})),
          rotationDegrees_(ResolveScalarParam(def, "rotation", bag, 0.0)),
          scale_(ResolveVec2Param(def, "scale", bag, Vec2{1.0, 1.0})) {}

    // Direct-binding constructor for a clip's own intrinsic transform
    // properties (Clip::Position/Rotation/Scale -- core/Clip.h), which are
    // not package parameters and so have no NodeInstanceDef/PropertyBag to
    // resolve against. Used by PlaybackEngine::RebuildGraphIfNeeded to give
    // every clip transform animation for free, independent of whether it
    // has any packaged effect applied.
    TransformNode(NodeParam<Vec2> position, NodeParam<double> rotationDegrees, NodeParam<Vec2> scale)
        : position_(position), rotationDegrees_(rotationDegrees), scale_(scale) {}

    std::string Name() const override { return "Transform"; }

    // Repoints this node at a different set of NodeParams without
    // reconstructing it -- used by ClipTransformNode (rendergraph/nodes/) to
    // rebind to whichever clip is active on its track each frame, since
    // reconstructing would re-trigger OnAttach()/shader recompilation for
    // no reason (the shader itself never changes).
    void Rebind(NodeParam<Vec2> position, NodeParam<double> rotationDegrees, NodeParam<Vec2> scale) {
        position_ = position;
        rotationDegrees_ = rotationDegrees;
        scale_ = scale;
    }

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
        // Inverse-transform sampling: convert the output texcoord to
        // centered [-1,1] space, undo translate/rotate/scale in reverse
        // order (undo translate, then rotate, then scale, since the
        // forward order was scale -> rotate -> translate), then sample the
        // source at the result if it's still within [-1,1].
        static const char* kFragmentSrc = R"(#version 300 es
            precision mediump float;
            uniform sampler2D uSource;
            uniform float uPosX;
            uniform float uPosY;
            uniform float uRotationRad;
            uniform float uScaleX;
            uniform float uScaleY;
            in vec2 vTexCoord;
            out vec4 fragColor;
            void main() {
                vec2 c = vTexCoord * 2.0 - 1.0;
                c -= vec2(uPosX, uPosY);
                float cosR = cos(-uRotationRad);
                float sinR = sin(-uRotationRad);
                vec2 rotated = vec2(c.x * cosR - c.y * sinR, c.x * sinR + c.y * cosR);
                vec2 scaled = rotated / vec2(max(uScaleX, 0.0001), max(uScaleY, 0.0001));
                vec2 sampleCoord = scaled * 0.5 + 0.5;
                if (sampleCoord.x < 0.0 || sampleCoord.x > 1.0 || sampleCoord.y < 0.0 || sampleCoord.y > 1.0) {
                    fragColor = vec4(0.0);
                    return;
                }
                fragColor = texture(uSource, sampleCoord);
            }
        )";
        shader_ = context.graphicsDevice->CompileShader(kVertexSrc, kFragmentSrc);
    }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        if (inputs.empty() || !inputs[0].IsValid() || shader_ == kInvalidShader) {
            return inputs.empty() ? Frame{} : inputs[0];
        }
        const Frame& source = inputs[0];

        Vec2 position = position_.ValueAt(context.time);
        double rotationDeg = rotationDegrees_.ValueAt(context.time);
        Vec2 scale = scale_.ValueAt(context.time);
        bool identity = position.x == 0.0 && position.y == 0.0 && rotationDeg == 0.0 && scale.x == 1.0 &&
                         scale.y == 1.0;
        if (identity) return source;  // no-op: skip the draw entirely

        // Output at the composite canvas size (outputWidth/outputHeight),
        // not the source's native size -- a scaled-down/moved clip must
        // still occupy a full-canvas-sized frame so CompositeNode can blend
        // it against sibling tracks pixel-for-pixel.
        Texture* target = context.texturePool->Acquire(context.outputWidth, context.outputHeight, /*GL_RGBA8*/ 0x8058);
        context.graphicsDevice->BindRenderTarget(target);
        context.graphicsDevice->DrawFullscreenQuad(
            shader_, {TextureBinding{source.textureId, source.textureTarget, "uSource"}},
            {{"uPosX", static_cast<float>(position.x)},
             {"uPosY", static_cast<float>(position.y)},
             {"uRotationRad", static_cast<float>(rotationDeg * M_PI / 180.0)},
             {"uScaleX", static_cast<float>(scale.x)},
             {"uScaleY", static_cast<float>(scale.y)}});
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
    NodeParam<Vec2> position_;
    NodeParam<double> rotationDegrees_;
    NodeParam<Vec2> scale_;
    ShaderHandle shader_ = kInvalidShader;
};

}  // namespace nle
