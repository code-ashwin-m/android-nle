// SetBrightnessCommand.h
//
// This is the command the Properties Panel's brightness slider fires. Two
// things it demonstrates that AddClipCommand doesn't:
//
// 1. CanMergeWith/MergeWith: dragging a slider fires SetBrightness on every
//    onValueChange callback -- potentially 60+ times a second. Without
//    merging, one drag gesture would create 60+ undo steps, so a single
//    Ctrl+Z would barely move the slider back. By merging consecutive edits
//    to the *same clip's* brightness into one Command, one drag = one undo
//    step, matching what a user expects from "undo".
//
// 2. Keyframe-aware writes: if the property is already animated, SetStatic
//    would silently do nothing useful (Property::ValueAt ignores the static
//    value once keyframed). This command instead adds/replaces a keyframe
//    at the engine's current playhead time when animated, and falls back to
//    the plain static value otherwise -- so the same UI slider works
//    correctly whether or not the user has keyframed the property.

#pragma once

#include "command/Command.h"
#include "core/Project.h"

namespace nle {

class SetBrightnessCommand : public Command {
public:
    SetBrightnessCommand(TrackId trackId, ClipId clipId, EffectId effectId, TimeUs currentTime, double newValue)
        : trackId_(trackId), clipId_(clipId), effectId_(effectId), time_(currentTime), newValue_(newValue) {}

    void Do(Project& project) override {
        ScalarProperty* prop = FindBrightnessProperty(project);
        if (!prop) return;
        previousValue_ = prop->IsAnimated() ? prop->ValueAt(time_) : prop->StaticValue();
        if (prop->IsAnimated()) {
            prop->AddKeyframe(time_, newValue_);
        } else {
            prop->SetStatic(newValue_);
        }
    }

    void Undo(Project& project) override {
        ScalarProperty* prop = FindBrightnessProperty(project);
        if (!prop) return;
        if (prop->IsAnimated()) {
            prop->AddKeyframe(time_, previousValue_);
        } else {
            prop->SetStatic(previousValue_);
        }
    }

    std::string Description() const override { return "Adjust Brightness"; }

    bool CanMergeWith(const Command& next) const override {
        const auto* other = dynamic_cast<const SetBrightnessCommand*>(&next);
        return other && other->clipId_ == clipId_ && other->effectId_ == effectId_;
    }

    void MergeWith(const Command& next) override {
        const auto& other = static_cast<const SetBrightnessCommand&>(next);
        newValue_ = other.newValue_;
        time_ = other.time_;
        // previousValue_ intentionally untouched: it still holds the value
        // from *before the drag started*, which is what Undo should restore.
    }

private:
    ScalarProperty* FindBrightnessProperty(Project& project) {
        Track* track = project.GetTimeline().FindTrack(trackId_);
        if (!track) return nullptr;
        Clip* clip = track->FindClip(clipId_);
        if (!clip) return nullptr;
        for (auto& effect : clip->Effects()) {
            if (effect->Id() == effectId_) return effect->FindProperty("brightness");
        }
        return nullptr;
    }

    TrackId trackId_;
    ClipId clipId_;
    EffectId effectId_;
    TimeUs time_;
    double newValue_;
    double previousValue_ = 0.0;
};

}  // namespace nle
