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
#include <vector>

#include "core/Property.h"
#include "nle/core/Types.h"

namespace nle {

// A single effect instance attached to a clip, e.g. "Brightness". Effects
// are intentionally generic containers of named ScalarProperty entries
// rather than one hand-written C++ class per effect type, because the
// render graph node (BrightnessNode, future ContrastNode, etc.) is what
// actually implements the visual effect -- the Effect object here only
// carries the *parameters*, so adding "Contrast" later means adding a new
// EffectType enum value and a matching RenderNode, not a new Clip API.
enum class EffectType {
    Brightness,
    // Contrast, Exposure, Saturation, Temperature, Tint, Opacity, Scale,
    // Rotation, Crop, Mask, Blur -- added here as each RenderNode ships.
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

private:
    EffectId id_;
    EffectType type_;
    std::vector<std::unique_ptr<ScalarProperty>> properties_;
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
};

}  // namespace nle
