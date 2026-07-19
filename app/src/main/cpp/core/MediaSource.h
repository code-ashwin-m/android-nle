// MediaSource.h
//
// A MediaSource is the engine's record of an imported file. The spec is
// explicit that "media library should reference imported assets rather than
// duplicating files" -- so a MediaSource holds a URI/path and probed
// metadata, never the decoded bytes. Clips on the timeline hold a
// MediaSourceId and an in/out range into it; many clips (e.g. the same
// b-roll used three times) share one MediaSource.
//
// Probing (reading duration/resolution/codec from the container) happens
// once at import time via Decoder's static Probe() helper, not on every
// playback, and the result is cached here.

#pragma once

#include <optional>
#include <string>

#include "nle/core/Types.h"

namespace nle {

struct MediaProbeInfo {
    TimeUs duration = 0;
    int width = 0;
    int height = 0;
    double frameRate = 0.0;   // 0 for audio-only / still images
    std::string videoCodecMime;  // e.g. "video/avc", "video/hevc"
    std::string audioCodecMime;
    int sampleRate = 0;
    int channelCount = 0;
    bool hasVideo = false;
    bool hasAudio = false;
};

class MediaSource {
public:
    MediaSource(MediaSourceId id, std::string uri, MediaType type)
        : id_(id), uri_(std::move(uri)), type_(type) {}

    MediaSourceId Id() const { return id_; }
    const std::string& Uri() const { return uri_; }
    MediaType Type() const { return type_; }

    void SetProbeInfo(MediaProbeInfo info) { probeInfo_ = std::move(info); }
    const std::optional<MediaProbeInfo>& ProbeInfo() const { return probeInfo_; }
    bool IsProbed() const { return probeInfo_.has_value(); }

private:
    MediaSourceId id_;
    std::string uri_;
    MediaType type_;
    std::optional<MediaProbeInfo> probeInfo_;
};

}  // namespace nle
