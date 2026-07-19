// Track.h
//
// Tracks are independent lanes, as required by the spec. "Independent"
// specifically means: a Track never reaches into another Track's clip list,
// and compositing order between tracks is a Timeline-level concern (bottom
// track composited first, higher tracks layered on top) rather than
// something a Track needs to know about its neighbors. This keeps Track
// trivially unit-testable in isolation.
//
// Clips within a track are kept sorted by timelineStart and are expected
// not to overlap (the UI/Timeline layer enforces this via ripple-aware
// commands); Track itself only stores and queries, it does not enforce
// non-overlap, so future features like multi-layer transitions (where two
// clips briefly overlap) aren't blocked by an assumption baked in here.

#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "core/Clip.h"
#include "nle/core/Types.h"

namespace nle {

class Track {
public:
    Track(TrackId id, TrackType type) : id_(id), type_(type) {}

    TrackId Id() const { return id_; }
    TrackType Type() const { return type_; }

    bool IsMuted() const { return muted_; }
    void SetMuted(bool muted) { muted_ = muted; }
    bool IsHidden() const { return hidden_; }
    void SetHidden(bool hidden) { hidden_ = hidden; }

    Clip* AddClip(std::unique_ptr<Clip> clip) {
        Clip* ptr = clip.get();
        clips_.push_back(std::move(clip));
        SortClips();
        return ptr;
    }

    void RemoveClip(ClipId id) {
        clips_.erase(std::remove_if(clips_.begin(), clips_.end(),
                                     [id](const std::unique_ptr<Clip>& c) { return c->Id() == id; }),
                     clips_.end());
    }

    Clip* FindClip(ClipId id) const {
        for (auto& c : clips_) {
            if (c->Id() == id) return c.get();
        }
        return nullptr;
    }

    // Returns the clip active at `time`, or nullptr if the track has a gap
    // there. Used once per track, per rendered frame, by RenderGraph.
    Clip* ClipAt(TimeUs time) const {
        for (auto& c : clips_) {
            if (c->ContainsTime(time)) return c.get();
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<Clip>>& Clips() const { return clips_; }

    TimeUs DurationEnd() const {
        TimeUs maxEnd = 0;
        for (auto& c : clips_) maxEnd = std::max(maxEnd, c->TimelineEnd());
        return maxEnd;
    }

    void SortClips() {
        std::sort(clips_.begin(), clips_.end(),
                  [](const auto& a, const auto& b) { return a->TimelineStart() < b->TimelineStart(); });
    }

private:
    TrackId id_;
    TrackType type_;
    bool muted_ = false;
    bool hidden_ = false;
    std::vector<std::unique_ptr<Clip>> clips_;
};

}  // namespace nle
