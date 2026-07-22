// MathTypes.h
//
// Part of the Universal Property System (see docs/ARCHITECTURE.md, "Universal
// Property System"). Keyframe.h's generic Lerp<T> is: `a + (b - a) * t`. It
// was written once, for scalars, but it never assumed T was a scalar -- it
// only assumed T supports operator-, operator*(double), and operator+. That
// means Property<Vec2>, Property<Vec3>, and Property<Color> become fully
// keyframeable (Linear/Bezier/Ease*) the moment those three operators exist,
// with NO changes to Property.h or Keyframe.h. This file is exactly that:
// plain data + the three operators, nothing else.
//
// This is the concrete proof of the spec's "No property-specific code" rule:
// adding a new animatable type to the whole engine (every effect, every
// clip transform, every future mask/text property) is "add a struct with
// three operators here," never "teach the animation engine about a new
// type."
//
// Deliberately NOT included: quaternion rotation, arbitrary-dimension
// vectors, SIMD-optimized paths. Phase 2's animatable surface (clip
// transform, color correction, effect parameters) only needs 2D position/
// scale, 3D color adjustments, and RGBA color -- adding a Vec4 or Quat later
// is the same one-file, three-operator pattern, not a redesign.

#pragma once

#include <cmath>

namespace nle {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
};

// Straight (non-premultiplied) RGBA in [0, 1]. Effect/adjustment-layer
// parameters (tint, LUT strength tint color, etc.) animate this directly;
// Frame/Texture pixel data is unrelated and untouched by this type.
struct Color {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;

    Color operator+(const Color& o) const { return {r + o.r, g + o.g, b + o.b, a + o.a}; }
    Color operator-(const Color& o) const { return {r - o.r, g - o.g, b - o.b, a - o.a}; }
    Color operator*(double s) const { return {r * s, g * s, b * s, a * s}; }
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

// Row-major 4x4. Used by TransformNode as the *computed result* of a clip's
// Position/Rotation/Scale properties (see effects/nodes/TransformNode.h),
// not as something users keyframe directly -- component-wise lerp of a
// rotation matrix does not produce a correct intermediate rotation, which is
// exactly why Position/Rotation/Scale are kept as separate Vec2/double
// properties and only composed into a Matrix4 right before the vertex
// shader runs. Property<Matrix4> and the operators below exist so the
// *type* fits the same Property<T>/KeyframeCurve<T> machinery uniformly
// (per spec: "Property<Matrix>"), and so a future node that legitimately
// wants to hold/cache a matrix value doesn't need a special-cased container.
struct Matrix4 {
    double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Matrix4 Identity() { return Matrix4{}; }

    static Matrix4 Translation(double x, double y, double z = 0.0) {
        Matrix4 r;
        r.m[12] = x;
        r.m[13] = y;
        r.m[14] = z;
        return r;
    }

    static Matrix4 RotationZ(double radians) {
        Matrix4 r;
        double c = std::cos(radians), s = std::sin(radians);
        r.m[0] = c;
        r.m[1] = s;
        r.m[4] = -s;
        r.m[5] = c;
        return r;
    }

    static Matrix4 Scale(double sx, double sy, double sz = 1.0) {
        Matrix4 r;
        r.m[0] = sx;
        r.m[5] = sy;
        r.m[10] = sz;
        return r;
    }

    Matrix4 operator*(const Matrix4& o) const {
        Matrix4 r;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k) sum += m[k * 4 + row] * o.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }

    // Component-wise -- see the header comment above for why this is only
    // a machinery-completeness operator, not a recommended way to animate
    // rotation.
    Matrix4 operator+(const Matrix4& o) const {
        Matrix4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = m[i] + o.m[i];
        return r;
    }
    Matrix4 operator-(const Matrix4& o) const {
        Matrix4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = m[i] - o.m[i];
        return r;
    }
    Matrix4 operator*(double s) const {
        Matrix4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = m[i] * s;
        return r;
    }
};

}  // namespace nle
