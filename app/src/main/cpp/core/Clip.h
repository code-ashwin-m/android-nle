// Clip.h
//
// A Clip is a placed instance of a MediaSource on the timeline: where it
// sits (timelineStart), how much of the source it uses (sourceIn/sourceOut),
// and its own effect stack (starting with Brightness in Phase 1). Splitting
// a clip produces two Clips referencing the same MediaSourceId with adjusted
// ranges -- no media duplication, no re-encoding, which is why "reference,
// don't duplicate" from the Media Panel requirement extends naturally here.
//
// Effects live on the Clip (not the Track) because Phase 1 has no adjustment
// layers yet; AdjustmentTrack (future phase) will apply an effect stack
// across every clip beneath it in the composite, reusing the same Effect
// type defined here unchanged.

#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "core/Property.h"
#include "core/PropertyBag.h"
#include "nle/core/Types.h"

namespace nle {

// A single effect instance attached to a clip, e.g. "Brightness" or a
// packaged effect like "Dreamy". Effects are intentionally generic
// containers of named properties rather than one hand-written C++ class per
// effect type -- the render graph node(s) (BrightnessNode; or, for
// EffectType::Packaged, whatever EffectRuntime builds from the package's
// graph.json) are what actually implement the visual effect. The Effect
// object here only ever carries *parameters*.
//
// EffectType::Packaged is additive, introduced alongside the Universal
// Property System (see docs/ARCHITECTURE.md): Brightness keeps using
// AddProperty/FindProperty/Properties() exactly as Phase 1 built it, with
// zero changes to that path or to BrightnessNode. A packaged effect instead
// carries a packageId (which .effect package it is) and a PropertyBag
// (its declared, possibly-animated parameters) -- see
// effects/EffectRuntime.h for how those two fields turn into real
// RenderNodes.
enum class EffectType {
    Brightness,
    Packaged,
    // Contrast, Exposure, Saturation, Temperature, Tint, Opacity, Scale,
    // Rotation, Crop, Mask -- either fold into Packaged (as a built-in
    // package shipped with the app) or get their own EffectType, whichever
    // a given feature's Properties Panel binding ends up wanting; both
    // paths already work end to end.
};

class Effect {
public:
    Effect(EffectId id, EffectType type) : id_(id), type_(type) {}

    EffectId Id() const { return id_; }
    EffectType Type() const { return type_; }

    ScalarProperty& AddProperty(const std::string& name, double defaultValue) {
        properties_.emplace_back(std::make_unique<ScalarProperty>(name, defaultValue));
        return *properties_.back();
    }

    ScalarProperty* FindProperty(const std::string& name) {
        for (auto& p : properties_) {
            if (p->Name() == name) return p.get();
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<ScalarProperty>>& Properties() const { return properties_; }

    // Packaged-effect identity. `packageId` matches an EffectGraphDef's
    // manifest.id (see effects/EffectGraphDef.h); `Parameters()` is where
    // EffectRuntime::Instantiate populates that package's declared,
    // animatable parameters -- see PlaybackEngine::RebuildGraphIfNeeded for
    // where instantiation actually happens (once, on structural rebuild,
    // same as every other graph-shape decision).
    void SetPackageId(std::string packageId) { packageId_ = std::move(packageId); }
    const std::string& PackageId() const { return packageId_; }
    bool IsPackaged() const { return !packageId_.empty(); }

    PropertyBag& Parameters() { return parameters_; }
    const PropertyBag& Parameters() const { return parameters_; }

private:
    EffectId id_;
    EffectType type_;
    std::vector<std::unique_ptr<ScalarProperty>> properties_;

    std::string packageId_;
    PropertyBag parameters_;
};

class Clip {
public:
    Clip(ClipId id, MediaSourceId source, TimeUs timelineStart, TimeUs sourceIn, TimeUs sourceOut)
        : id_(id), source_(source), timelineStart_(timelineStart), sourceIn_(sourceIn), sourceOut_(sourceOut) {}

    ClipId Id() const { return id_; }
    MediaSourceId Source() const { return source_; }

    TimeUs TimelineStart() const { return timelineStart_; }
    TimeUs TimelineEnd() const { return timelineStart_ + Duration(); }
    TimeUs Duration() const { return sourceOut_ - sourceIn_; }

    TimeUs SourceIn() const { return sourceIn_; }
    TimeUs SourceOut() const { return sourceOut_; }

    void MoveTo(TimeUs newTimelineStart) { timelineStart_ = newTimelineStart; }

    // Trims by adjusting the source range in place; timelineStart is only
    // touched when trimming the head, matching standard NLE trim semantics
    // (dragging the tail does not shift the clip's start).
    void TrimHead(TimeUs newSourceIn) {
        TimeUs delta = newSourceIn - sourceIn_;
        sourceIn_ = newSourceIn;
        timelineStart_ += delta;
    }
    void TrimTail(TimeUs newSourceOut) { sourceOut_ = newSourceOut; }

    // Converts a global timeline time into this clip's local source time.
    // Used by DecoderSourceNode to know which frame of the underlying media
    // to request for a given playhead position.
    TimeUs ToSourceTime(TimeUs timelineTime) const {
        return sourceIn_ + (timelineTime - timelineStart_);
    }

    bool ContainsTime(TimeUs timelineTime) const {
        return timelineTime >= timelineStart_ && timelineTime < TimelineEnd();
    }

    // Clip Transform Animation (spec): Position/Rotation/Scale live directly
    // on Clip, not as an Effect, because every clip has them intrinsically
    // -- they're not optional the way Brightness or a packaged effect is.
    // They use the exact same Property<T>/KeyframeCurve<T> machinery as
    // every Effect parameter; TransformNode (effects/nodes/TransformNode.h)
    // reads them the same way it reads a packaged effect's position
    // parameter, via NodeParam<Vec2>::Bound(). Default position (0,0) means
    // "centered, no offset"; default scale (1,1) means "100%, unchanged" --
    // see TransformNode's identity check, which skips the draw entirely
    // when a clip is at these defaults.
    Vec2Property& Position() { return position_; }
    const Vec2Property& Position() const { return position_; }
    ScalarProperty& Rotation() { return rotation_; }
    const ScalarProperty& Rotation() const { return rotation_; }
    Vec2Property& Scale() { return scale_; }
    const Vec2Property& Scale() const { return scale_; }

    Effect& AddEffect(EffectType type) {
        effects_.emplace_back(std::make_unique<Effect>(EffectId::Generate(), type));
        return *effects_.back();
    }

    void RemoveEffect(EffectId id) {
        effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                       [id](const std::unique_ptr<Effect>& e) { return e->Id() == id; }),
                       effects_.end());
    }

    const std::vector<std::unique_ptr<Effect>>& Effects() const { return effects_; }

private:
    ClipId id_;
    MediaSourceId source_;
    TimeUs timelineStart_;
    TimeUs sourceIn_;
    TimeUs sourceOut_;
    std::vector<std::unique_ptr<Effect>> effects_;

    Vec2Property position_{"position", Vec2{0.0, 0.0}};
    ScalarProperty rotation_{"rotation", 0.0};
    Vec2Property scale_{"scale", Vec2{1.0, 1.0}};
};

}  // namespace nle
