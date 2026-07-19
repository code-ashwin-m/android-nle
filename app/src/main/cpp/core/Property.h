// Property.h
//
// Every editable parameter in the engine -- Brightness today, Contrast /
// Opacity / Scale / Rotation later -- is a Property<T>. This is the single
// type that:
//   1. Holds a static value when un-keyframed (the common case).
//   2. Transparently switches to keyframe evaluation the moment a keyframe
//      is added, with no API change for callers.
//   3. Reports whether it's animated, which RenderNodes use to decide
//      whether they need to re-fetch a value every frame or can cache it.
//
// This is intentionally a thin wrapper around KeyframeCurve<T> rather than
// merging the two: Property adds *identity* (a name, for property panel
// binding and serialization) and the static-value fast path; KeyframeCurve
// stays a pure, reusable curve-math primitive.

#pragma once

#include <string>

#include "core/Keyframe.h"

namespace nle {

template <typename T>
class Property {
public:
    Property(std::string name, T defaultValue)
        : name_(std::move(name)), staticValue_(defaultValue) {}

    const std::string& Name() const { return name_; }

    // Fast path: no keyframes, just return the static value. This is the
    // overwhelmingly common case (most properties on most clips are never
    // animated) so it must not pay for curve evaluation overhead.
    T ValueAt(TimeUs time) const {
        if (!curve_.HasKeyframes()) return staticValue_;
        return curve_.Evaluate(time);
    }

    // Sets the static value. If the property is already animated, this is
    // interpreted as "add/replace a keyframe at the current time" by the
    // caller (typically a Command) rather than silently discarding the
    // animation -- see command/commands/SetBrightnessCommand.
    void SetStatic(T value) { staticValue_ = value; }
    T StaticValue() const { return staticValue_; }

    void AddKeyframe(TimeUs time, T value, InterpolationType interp = InterpolationType::Linear) {
        curve_.AddKeyframe(Keyframe<T>{time, value, interp});
    }

    void RemoveKeyframe(TimeUs time) { curve_.RemoveKeyframeAt(time); }

    bool IsAnimated() const { return curve_.HasKeyframes(); }
    const KeyframeCurve<T>& Curve() const { return curve_; }

private:
    std::string name_;
    T staticValue_{};
    KeyframeCurve<T> curve_;
};

// Common concrete instantiation used throughout the effect system. Effects
// like Brightness, Contrast, Opacity, Scale are all scalar doubles; Crop and
// Position will use a Vec2 specialization added alongside those features.
using ScalarProperty = Property<double>;

}  // namespace nle
