// AddClipCommand.h
//
// Reference implementation of a "structural" command, as opposed to
// SetBrightnessCommand's "value" pattern. Shows the ID-lookup convention
// described in Command.h: Undo() finds the track by ID and removes the
// clip by ID, rather than holding a raw Track* that a prior/future command
// could invalidate.

#pragma once

#include <memory>

#include "command/Command.h"
#include "core/Clip.h"
#include "core/Project.h"

namespace nle {

class AddClipCommand : public Command {
public:
    AddClipCommand(TrackId trackId, MediaSourceId source, TimeUs timelineStart, TimeUs sourceIn, TimeUs sourceOut)
        : trackId_(trackId), source_(source), timelineStart_(timelineStart), sourceIn_(sourceIn), sourceOut_(sourceOut) {}

    void Do(Project& project) override {
        Track* track = project.GetTimeline().FindTrack(trackId_);
        if (!track) return;
        if (!clipId_.IsValid()) clipId_ = ClipId::Generate();
        auto clip = std::make_unique<Clip>(clipId_, source_, timelineStart_, sourceIn_, sourceOut_);
        track->AddClip(std::move(clip));
    }

    void Undo(Project& project) override {
        Track* track = project.GetTimeline().FindTrack(trackId_);
        if (!track) return;
        track->RemoveClip(clipId_);
    }

    std::string Description() const override { return "Add Clip"; }

    // Exposes the generated ClipId so the caller (EditorEngine::AddClip)
    // can hand it back across JNI for the UI to select the newly placed
    // clip -- valid only after Do() has run at least once.
    ClipId ResultClipId() const { return clipId_; }

private:
    TrackId trackId_;
    MediaSourceId source_;
    TimeUs timelineStart_;
    TimeUs sourceIn_;
    TimeUs sourceOut_;
    ClipId clipId_;  // assigned on first Do(), reused across Undo/Redo
};

}  // namespace nle
