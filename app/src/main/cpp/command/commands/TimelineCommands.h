// TimelineCommands.h
//
// Delete, Split, and Trim are grouped in one file (unlike AddClipCommand
// and SetBrightnessCommand, each in their own) because all three share the
// same shape: they mutate an existing Clip's range/existence rather than
// introducing new kinds of state, and reviewing them side by side makes it
// easy to see they all follow the same "capture enough to reverse exactly"
// discipline described in Command.h.

#pragma once

#include <memory>

#include "command/Command.h"
#include "core/Project.h"

namespace nle {

// Toolbar "Delete": removes a clip, leaving a gap. "Ripple Delete" is the
// same command with rippleFollowingClips=true, which additionally shifts
// every later clip on the track left to close the gap -- both are
// expressed here rather than as two classes because ripple is a pure
// post-step on top of plain delete, not a different removal semantic.
class DeleteClipCommand : public Command {
public:
    DeleteClipCommand(TrackId trackId, ClipId clipId, bool ripple) : trackId_(trackId), clipId_(clipId), ripple_(ripple) {}

    void Do(Project& project) override {
        Track* track = project.GetTimeline().FindTrack(trackId_);
        if (!track) return;
        Clip* clip = track->FindClip(clipId_);
        if (!clip) return;

        removedStart_ = clip->TimelineStart();
        removedSource_ = clip->Source();
        removedSourceIn_ = clip->SourceIn();
        removedSourceOut_ = clip->SourceOut();
        TimeUs duration = clip->Duration();

        track->RemoveClip(clipId_);

        if (ripple_) {
            for (auto& other : track->Clips()) {
                if (other->TimelineStart() > removedStart_) {
                    shiftedClipIds_.push_back(other->Id());
                    other->MoveTo(other->TimelineStart() - duration);
                }
            }
            track->SortClips();
            rippleShiftAmount_ = duration;
        }
    }

    void Undo(Project& project) override {
        Track* track = project.GetTimeline().FindTrack(trackId_);
        if (!track) return;

        if (ripple_) {
            for (auto& other : track->Clips()) {
                for (ClipId shiftedId : shiftedClipIds_) {
                    if (other->Id() == shiftedId) other->MoveTo(other->TimelineStart() + rippleShiftAmount_);
                }
            }
        }

        auto clip = std::make_unique<Clip>(clipId_, removedSource_, removedStart_, removedSourceIn_, removedSourceOut_);
        track->AddClip(std::move(clip));
    }

    std::string Description() const override { return ripple_ ? "Ripple Delete" : "Delete Clip"; }

private:
    TrackId trackId_;
    ClipId clipId_;
    bool ripple_;

    TimeUs removedStart_ = 0;
    MediaSourceId removedSource_;
    TimeUs removedSourceIn_ = 0;
    TimeUs removedSourceOut_ = 0;
    std::vector<ClipId> shiftedClipIds_;
    TimeUs rippleShiftAmount_ = 0;
};

// Toolbar "Split": cuts one clip into two at `atTime`, both referencing the
// same MediaSource with adjusted source ranges -- no re-encode, matching
// the "reference rather than duplicate" principle extended to editing.
class SplitClipCommand : public Command {
public:
    SplitClipCommand(TrackId trackId, ClipId clipId, TimeUs atTime) : trackId_(trackId), clipId_(clipId), atTime_(atTime) {}

    void Do(Project& project) override {
        Track* track = project.GetTimeline().FindTrack(trackId_);
        if (!track) return;
        Clip* clip = track->FindClip(clipId_);
        if (!clip || !clip->ContainsTime(atTime_)) return;

        originalSourceOut_ = clip->SourceOut();
        TimeUs splitSourceTime = clip->ToSourceTime(atTime_);

        if (!secondClipId_.IsValid()) secondClipId_ = ClipId::Generate();
        auto secondHalf = std::make_unique<Clip>(secondClipId_, clip->Source(), atTime_, splitSourceTime, originalSourceOut_);
        clip->TrimTail(splitSourceTime);
        track->AddClip(std::move(secondHalf));
    }

    void Undo(Project& project) override {
        Track* track = project.GetTimeline().FindTrack(trackId_);
        if (!track) return;
        track->RemoveClip(secondClipId_);
        if (Clip* first = track->FindClip(clipId_)) {
            first->TrimTail(originalSourceOut_);
        }
    }

    std::string Description() const override { return "Split Clip"; }

private:
    TrackId trackId_;
    ClipId clipId_;
    TimeUs atTime_;
    ClipId secondClipId_;
    TimeUs originalSourceOut_ = 0;
};

// Toolbar "Trim": drag-adjusts one edge of a clip. Head and tail are one
// command with a flag rather than two classes, since Undo/Do are otherwise
// identical modulo which Clip method they call.
class TrimClipCommand : public Command {
public:
    enum class Edge { Head, Tail };

    TrimClipCommand(TrackId trackId, ClipId clipId, Edge edge, TimeUs newValue)
        : trackId_(trackId), clipId_(clipId), edge_(edge), newValue_(newValue) {}

    void Do(Project& project) override {
        Clip* clip = FindClip(project);
        if (!clip) return;
        previousValue_ = edge_ == Edge::Head ? clip->SourceIn() : clip->SourceOut();
        if (edge_ == Edge::Head) {
            clip->TrimHead(newValue_);
        } else {
            clip->TrimTail(newValue_);
        }
    }

    void Undo(Project& project) override {
        Clip* clip = FindClip(project);
        if (!clip) return;
        if (edge_ == Edge::Head) {
            clip->TrimHead(previousValue_);
        } else {
            clip->TrimTail(previousValue_);
        }
    }

    std::string Description() const override { return edge_ == Edge::Head ? "Trim Head" : "Trim Tail"; }

    // Dragging a trim handle fires many times per gesture, same rationale
    // as SetBrightnessCommand::CanMergeWith.
    bool CanMergeWith(const Command& next) const override {
        const auto* other = dynamic_cast<const TrimClipCommand*>(&next);
        return other && other->clipId_ == clipId_ && other->edge_ == edge_;
    }
    void MergeWith(const Command& next) override {
        newValue_ = static_cast<const TrimClipCommand&>(next).newValue_;
    }

private:
    Clip* FindClip(Project& project) {
        Track* track = project.GetTimeline().FindTrack(trackId_);
        return track ? track->FindClip(clipId_) : nullptr;
    }

    TrackId trackId_;
    ClipId clipId_;
    Edge edge_;
    TimeUs newValue_;
    TimeUs previousValue_ = 0;
};

// Properties Panel "add effect": attaches a new Effect (e.g. Brightness)
// to a clip. Kept in this file rather than beside SetBrightnessCommand
// because it's structural (creates/destroys an Effect) like the other
// three commands here, not a value edit.
class AddEffectCommand : public Command {
public:
    AddEffectCommand(TrackId trackId, ClipId clipId, EffectType type, double defaultValue)
        : trackId_(trackId), clipId_(clipId), type_(type), defaultValue_(defaultValue) {}

    void Do(Project& project) override {
        Clip* clip = FindClip(project);
        if (!clip) return;
        Effect& effect = clip->AddEffect(type_);
        effectId_ = effect.Id();
        // "brightness" is the well-known property name BrightnessNode and
        // SetBrightnessCommand look up by string -- see the EffectType
        // comment in core/Clip.h on why properties are named rather than
        // hand-typed per effect.
        effect.AddProperty("brightness", defaultValue_);
    }

    void Undo(Project& project) override {
        Clip* clip = FindClip(project);
        if (!clip) return;
        clip->RemoveEffect(effectId_);
    }

    std::string Description() const override { return "Add Effect"; }

    EffectId ResultEffectId() const { return effectId_; }

private:
    Clip* FindClip(Project& project) {
        Track* track = project.GetTimeline().FindTrack(trackId_);
        return track ? track->FindClip(clipId_) : nullptr;
    }

    TrackId trackId_;
    ClipId clipId_;
    EffectType type_;
    double defaultValue_;
    EffectId effectId_;
};

}  // namespace nle
