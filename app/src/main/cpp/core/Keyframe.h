// Keyframe.h
//
// Design rationale:
// The spec requires every property (brightness today; contrast, opacity,
// scale, etc. later) to support keyframes with Linear / Bezier / Hold
// interpolation. Rather than hand-writing interpolation logic inside each
// property, we implement it once, generically, here. `Property<T>` (see
// Property.h) is a thin container that delegates all curve evaluation to
// this file. Adding a new interpolatable property type later means adding
// a `Lerp` overload for T, not rewriting interpolation logic.
//
// Time lookup is O(log n) via binary search on sorted keyframe times, which
// matters because properties are evaluated once per rendered frame -- at
// 60fps with a dozen animated properties, this runs thousands of times a
// minute during playback and must stay cheap.

#pragma once

#include <algorithm>
#include <vector>

#include "nle/core/Types.h"

namespace nle {

enum class InterpolationType {
    Hold,    // step function: value snaps at the next keyframe
    Linear,
    Bezier,     // cubic bezier through user-adjustable control handles
    EaseIn,     // fixed-handle presets equivalent to a Bezier keyframe whose
    EaseOut,    // handles are pinned to standard easing curves -- these exist
    EaseInOut,  // so the Properties Panel can offer one-tap easing without
                // the user ever touching a handle; they reuse CubicBezierEase
                // below with fixed control points rather than duplicating
                // interpolation math. Custom per-user curves remain possible
                // later purely as new fixed-handle presets, or by exposing
                // InterpolationType::Bezier with programmatically-set
                // handles -- neither needs a new InterpolationType value.
};

// Bezier control handle, expressed as (time offset, value offset) from the
// keyframe it belongs to -- this is the same representation After Effects /
// Resolve use, which keeps a future curve-editor UI straightforward to
// implement against this data without translation.
struct BezierHandle {
    double timeOffset = 0.0;
    double valueOffset = 0.0;
};

template <typename T>
struct Keyframe {
    TimeUs time = 0;
    T value{};
    InterpolationType interpolation = InterpolationType::Linear;
    BezierHandle outHandle;  // handle leaving this keyframe (toward the next)
    BezierHandle inHandle;   // handle arriving at this keyframe (from the previous)
};

// Generic linear interpolation. Specialize/overload for types that aren't
// naturally lerp-able (e.g. enums, discrete effect selections) if they ever
// need to become keyframeable -- for those, only Hold interpolation makes
// sense, which the evaluator below already handles independently of Lerp.
template <typename T>
T Lerp(const T& a, const T& b, double t) {
    return static_cast<T>(a + (b - a) * t);
}

// Cubic bezier easing evaluated in the time domain via De Casteljau, then
// used to interpolate the value domain. This mirrors how curve-based
// animation systems separate "easing shape" from "value range".
inline double CubicBezierEase(double p1x, double p1y, double p2x, double p2y, double t) {
    // Solve for the bezier parameter u such that the x-component equals t,
    // via a handful of Newton-Raphson iterations (converges fast for the
    // well-behaved, monotonic handles a video editor UI will produce).
    double u = t;
    for (int i = 0; i < 8; ++i) {
        double x = 3 * (1 - u) * (1 - u) * u * p1x + 3 * (1 - u) * u * u * p2x + u * u * u;
        double dx = 3 * (1 - u) * (1 - u) * p1x + 6 * (1 - u) * u * (p2x - p1x) +
                    3 * u * u * (1 - p2x);
        if (std::abs(dx) < 1e-6) break;
        u -= (x - t) / dx;
        u = std::clamp(u, 0.0, 1.0);
    }
    double y = 3 * (1 - u) * (1 - u) * u * p1y + 3 * (1 - u) * u * u * p2y + u * u * u;
    return y;
}

// Owns a sorted keyframe list for a single property track and evaluates it
// at an arbitrary TimeUs. This class knows nothing about *which* property it
// animates -- Property<T> supplies that context. Keeping curve math separate
// from property identity is what lets Property.h stay tiny.
template <typename T>
class KeyframeCurve {
public:
    void AddKeyframe(Keyframe<T> kf) {
        auto it = std::lower_bound(keyframes_.begin(), keyframes_.end(), kf.time,
                                    [](const Keyframe<T>& existing, TimeUs t) {
                                        return existing.time < t;
                                    });
        if (it != keyframes_.end() && it->time == kf.time) {
            *it = std::move(kf);  // replace existing keyframe at this time
        } else {
            keyframes_.insert(it, std::move(kf));
        }
    }

    void RemoveKeyframeAt(TimeUs time) {
        keyframes_.erase(std::remove_if(keyframes_.begin(), keyframes_.end(),
                                         [time](const Keyframe<T>& kf) { return kf.time == time; }),
                          keyframes_.end());
    }

    bool HasKeyframes() const { return !keyframes_.empty(); }
    size_t Count() const { return keyframes_.size(); }
    const std::vector<Keyframe<T>>& All() const { return keyframes_; }

    // Evaluates the curve at `time`. Callers with no keyframes at all should
    // not call this -- Property<T> falls back to a static value in that case
    // -- so this always assumes at least one keyframe exists.
    T Evaluate(TimeUs time) const {
        if (keyframes_.size() == 1) return keyframes_.front().value;

        if (time <= keyframes_.front().time) return keyframes_.front().value;
        if (time >= keyframes_.back().time) return keyframes_.back().value;

        // Binary search for the surrounding keyframe pair.
        auto it = std::upper_bound(keyframes_.begin(), keyframes_.end(), time,
                                    [](TimeUs t, const Keyframe<T>& kf) { return t < kf.time; });
        const Keyframe<T>& next = *it;
        const Keyframe<T>& prev = *(it - 1);

        double span = static_cast<double>(next.time - prev.time);
        double t = span > 0 ? static_cast<double>(time - prev.time) / span : 0.0;

        switch (prev.interpolation) {
            case InterpolationType::Hold:
                return prev.value;
            case InterpolationType::Linear:
                return Lerp(prev.value, next.value, t);
            case InterpolationType::Bezier: {
                double eased = CubicBezierEase(0.33 + prev.outHandle.timeOffset, prev.outHandle.valueOffset,
                                                0.66 + next.inHandle.timeOffset, next.inHandle.valueOffset, t);
                return Lerp(prev.value, next.value, eased);
            }
            // Standard fixed-handle presets (same curves as CSS/AE "Ease In" /
            // "Ease Out" / "Ease In Out"). Expressed as calls into the same
            // CubicBezierEase used by InterpolationType::Bezier above rather
            // than separate math, so any future refinement to the easing
            // shape only ever needs to change one function.
            case InterpolationType::EaseIn:
                return Lerp(prev.value, next.value, CubicBezierEase(0.42, 0.0, 1.0, 1.0, t));
            case InterpolationType::EaseOut:
                return Lerp(prev.value, next.value, CubicBezierEase(0.0, 0.0, 0.58, 1.0, t));
            case InterpolationType::EaseInOut:
                return Lerp(prev.value, next.value, CubicBezierEase(0.42, 0.0, 0.58, 1.0, t));
        }
        return prev.value;
    }

private:
    std::vector<Keyframe<T>> keyframes_;  // always kept sorted by time
};

}  // namespace nle
