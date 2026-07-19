// Types.h
//
// Why this file exists:
// Every subsystem (timeline, decoder, render graph, encoder) needs to agree on
// the same units for time and the same identifier types. Defining them once,
// in one header with no dependencies, prevents the classic bug in NLE engines
// where one module treats time as milliseconds and another as microseconds,
// or where "TrackId" is an int in one file and a string in another.
//
// TimeUs is int64_t microseconds. Microseconds (not frames) are the engine's
// canonical time unit because:
//   - MediaCodec/MediaExtractor timestamps are already in microseconds.
//   - Frame-based time breaks the moment you mix a 30fps clip with a 24fps
//     clip on the same timeline, or support variable-speed playback.
//   - Frame number is always derivable from (TimeUs, frame rate) when needed
//     for UI display (timecode), but going the other direction is lossy.

#pragma once

#include <cstdint>
#include <string>

namespace nle {

using TimeUs = int64_t;

constexpr TimeUs kTimeInvalid = -1;

inline TimeUs SecondsToUs(double seconds) {
    return static_cast<TimeUs>(seconds * 1'000'000.0);
}

inline double UsToSeconds(TimeUs us) {
    return static_cast<double>(us) / 1'000'000.0;
}

// Strongly-typed, opaque IDs instead of raw ints. This stops a caller from
// accidentally passing a TrackId where a ClipId is expected -- a mistake
// that compiles fine with plain `using ClipId = int` but is caught here at
// compile time.
template <typename Tag>
struct Id {
    uint64_t value = 0;

    bool operator==(const Id& other) const { return value == other.value; }
    bool operator!=(const Id& other) const { return value != other.value; }
    bool operator<(const Id& other) const { return value < other.value; }
    bool IsValid() const { return value != 0; }

    static Id Generate() {
        static uint64_t counter = 1;
        return Id{counter++};
    }
};

struct ClipIdTag {};
struct TrackIdTag {};
struct MediaSourceIdTag {};
struct EffectIdTag {};
struct ProjectIdTag {};

using ClipId = Id<ClipIdTag>;
using TrackId = Id<TrackIdTag>;
using MediaSourceId = Id<MediaSourceIdTag>;
using EffectId = Id<EffectIdTag>;
using ProjectId = Id<ProjectIdTag>;

enum class TrackType {
    Video,
    Audio,
    Adjustment,
    Sticker,
    Text,
};

enum class MediaType {
    Video,
    Audio,
    Image,
};

// Pixel format flowing through the render graph. Kept intentionally small
// for Phase 1; new formats (e.g. P010 for HDR) are additive, not breaking.
enum class PixelFormat {
    RGBA8,
    NV12,   // typical MediaCodec decoder output surface format
};

}  // namespace nle
