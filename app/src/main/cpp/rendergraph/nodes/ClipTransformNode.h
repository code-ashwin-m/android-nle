// ClipTransformNode.h
//
// Written in exactly BrightnessNode's shape, for the same reason: a Track
// holds many Clips over time, and Phase 1's graph is rebuilt only on
// structural edits (add/remove clip/effect), not on every cut the playhead
// crosses during playback -- so this node cannot be bound to one specific
// Clip at construction time. Instead, like BrightnessNode, it looks up
// `Track::ClipAt(context.time)` fresh on every Process() call and reads
// *that* clip's Position/Rotation/Scale (core/Clip.h) for the current
// frame. One node instance per video track handles every clip on it,
// including clips with no transform applied (the identity check in
// TransformNode's Process() skips the draw entirely for those, same as
// Brightness's zero-value fast path).
//
// This node's actual transform math is TransformNode's -- see
// effects/nodes/TransformNode.h -- reused here via composition rather than
// duplicated, since the shader and inverse-sampling logic don't change
// based on *where* the position/rotation/scale values come from.

#pragma once

#include "core/Project.h"
#include "core/Track.h"
#include "effects/nodes/TransformNode.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class ClipTransformNode : public RenderNode {
public:
    ClipTransformNode(Project* project, TrackId trackId) : project_(project), trackId_(trackId) {}

    std::string Name() const override { return "ClipTransform"; }

    void OnAttach(const RenderContext& context) override {
        // TransformNode's shader is stateless w.r.t. which clip is active,
        // so one shared inner TransformNode -- constructed once, here, with
        // NodeParams that resolve the active clip fresh on every ValueAt()
        // call -- serves the whole track. Building the NodeParams requires
        // capturing `this` in small lambdas is tempting but NodeParam<T>
        // only supports "bound to a Property<T>*" or "fixed literal", not
        // an arbitrary callback -- so instead this node resolves the active
        // clip itself and forwards straight to Clip::Position()/Rotation()/
        // Scale(), bypassing NodeParam entirely. See Process() below.
        inner_.OnAttach(context);
    }
    void OnDetach() override { inner_.OnDetach(); }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        if (inputs.empty() || !inputs[0].IsValid()) return inputs.empty() ? Frame{} : inputs[0];

        Track* track = project_->GetTimeline().FindTrack(trackId_);
        Clip* clip = track ? track->ClipAt(context.time) : nullptr;
        if (!clip) return inputs[0];

        // Rebinding NodeParams to the current clip's properties on every
        // frame is cheap (three pointer assignments) next to a GPU draw
        // call, so there's no need to cache "last active clip" here the
        // way a heavier operation would.
        inner_.Rebind(NodeParam<Vec2>::Bound(&clip->Position()), NodeParam<double>::Bound(&clip->Rotation()),
                      NodeParam<Vec2>::Bound(&clip->Scale()));
        return inner_.Process(inputs, context);
    }

private:
    Project* project_;
    TrackId trackId_;
    TransformNode inner_{NodeParam<Vec2>{}, NodeParam<double>{}, NodeParam<Vec2>{}};
};

}  // namespace nle
