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
#include "core/MathTypes.h"

namespace nle {

// Optional, additive metadata for property-panel binding and future
// constraint enforcement. Deliberately NOT required at construction --
// most Property<T> instances (e.g. Brightness today) never set this and pay
// nothing for it beyond one small struct's worth of memory. This is the
// spec's "constraints" and part of "metadata" for Property<T>; "expressions
// (future)" is intentionally left out of this struct -- see the note below
// on why that's a separate, later addition rather than a field to guess the
// shape of now.
struct PropertyMetadata {
    std::string category;  // e.g. "Transform", "Color" -- Properties Panel grouping/section
    std::string uiHint;     // e.g. "slider", "angle", "color-wheel" -- widget selection
    double minValue = 0.0;
    double maxValue = 0.0;
    bool hasRange = false;  // false = unconstrained; true = clamp/slider-bound in the UI
};

template <typename T>
class Property {
public:
    Property(std::string name, T defaultValue)
        : name_(std::move(name)), staticValue_(defaultValue) {}

    Property(std::string name, T defaultValue, PropertyMetadata metadata)
        : name_(std::move(name)), staticValue_(defaultValue), metadata_(std::move(metadata)) {}

    const std::string& Name() const { return name_; }

    const PropertyMetadata& Metadata() const { return metadata_; }
    void SetMetadata(PropertyMetadata metadata) { metadata_ = std::move(metadata); }

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
    PropertyMetadata metadata_;
};

// Concrete instantiations. Every one of these shares the exact same
// Property<T>/KeyframeCurve<T> machinery -- adding ScaleProperty (Vec2) or
// TemperatureProperty (double) is not "a new kind of property," it's just
// another alias, because Property<T> was never scalar-specific to begin
// with. This is the proof that the spec's "no property-specific code" goal
// is already met by Phase 1's design, extended to non-scalar types.
using ScalarProperty = Property<double>;
using Vec2Property = Property<Vec2>;
using Vec3Property = Property<Vec3>;
using ColorProperty = Property<Color>;
using Matrix4Property = Property<Matrix4>;

}  // namespace nle
