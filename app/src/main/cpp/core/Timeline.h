// Timeline.h
//
// Owns the ordered list of Tracks and is the one place that knows the
// bottom-to-top compositing order (track index 0 = bottom). RenderGraph
// asks the Timeline "give me every clip active at time T, in composite
// order" via ActiveClipsAt() rather than walking tracks itself -- this
// keeps compositing-order knowledge in exactly one place, so introducing
// adjustment layers or track reordering later only touches Timeline.

#pragma once

#include <memory>
#include <vector>

#include "core/Track.h"
#include "nle/core/Types.h"

namespace nle {

struct ActiveClipRef {
    TrackId trackId;
    TrackType trackType;
    Clip* clip;  // non-owning; lifetime is the Timeline's
};

class Timeline {
public:
    Track* AddTrack(TrackType type) {
        auto track = std::make_unique<Track>(TrackId::Generate(), type);
        Track* ptr = track.get();
        tracks_.push_back(std::move(track));
        return ptr;
    }

    void RemoveTrack(TrackId id) {
        tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                      [id](const std::unique_ptr<Track>& t) { return t->Id() == id; }),
                      tracks_.end());
    }

    Track* FindTrack(TrackId id) const {
        for (auto& t : tracks_) {
            if (t->Id() == id) return t.get();
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<Track>>& Tracks() const { return tracks_; }

    // Bottom-to-top order: index 0 first. CompositeNode consumes this
    // directly and blends in list order, so this ordering *is* the visual
    // stacking order -- no separate "z-index" concept needed for Phase 1.
    std::vector<ActiveClipRef> ActiveClipsAt(TimeUs time) const {
        std::vector<ActiveClipRef> result;
        for (auto& track : tracks_) {
            if (track->IsHidden()) continue;
            if (Clip* clip = track->ClipAt(time)) {
                result.push_back({track->Id(), track->Type(), clip});
            }
        }
        return result;
    }

    TimeUs Duration() const {
        TimeUs maxEnd = 0;
        for (auto& t : tracks_) maxEnd = std::max(maxEnd, t->DurationEnd());
        return maxEnd;
    }

private:
    std::vector<std::unique_ptr<Track>> tracks_;
};

}  // namespace nle
