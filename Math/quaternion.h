// Math/quaternion.h - unit quaternions for rotation.
// Keeps the historical w/x/y/z fields (brace-init {w,x,y,z} untouched); arithmetic
// is delegated to glm. Note: glm::quat constructor order is (w, x, y, z).
#pragma once
#include <cmath>
#include <algorithm>
#include "engine_types.h"
#include "math.h"
#include "vector.h"
#include "matrix.h"
#include <glm/gtc/quaternion.hpp>

namespace Math {
struct Quaternion {
    float32 w = 1.0f;
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;

    [[nodiscard]] glm::quat toGlm() const noexcept { return glm::quat(w, x, y, z); }

    static constexpr Quaternion identity() noexcept { return {1.0f, 0.0f, 0.0f, 0.0f}; }
    static Quaternion fromAxisAngle(const Vector3& axis, float32 angle) noexcept {
        float32 half = 0.5f * angle;
        float32 s = std::sin(half);
        return {std::cos(half), axis.x * s, axis.y * s, axis.z * s};
    }
    Quaternion operator*(const Quaternion& o) const noexcept {
        glm::quat r = toGlm() * o.toGlm();
        return {r.w, r.x, r.y, r.z};
    }
    Quaternion operator*(float32 s) const noexcept { return {w * s, x * s, y * s, z * s}; }
    Quaternion conjugate() const noexcept { return {w, -x, -y, -z}; }
    [[nodiscard]] float32 length() const noexcept {
        return glm::length(glm::vec4(x, y, z, w));
    }
    [[nodiscard]] Quaternion normalized() const noexcept {
        glm::quat r = glm::normalize(toGlm());
        if (glm::length(r) == 0.0f) return identity();
        return {r.w, r.x, r.y, r.z};
    }
    // Rotate v by this quaternion; routed through the rotation matrix so it is
    // guaranteed consistent with toMatrix().
    [[nodiscard]] Vector3 rotate(const Vector3& v) const noexcept {
        return toMatrix().multiply(v);
    }
    [[nodiscard]] Matrix3x3 toMatrix() const noexcept {
        glm::mat3 g = glm::mat3_cast(toGlm());
        return {g[0][0], g[1][0], g[2][0],
                g[0][1], g[1][1], g[2][1],
                g[0][2], g[1][2], g[2][2]};
    }
    [[nodiscard]] float32 angle() const noexcept {
        float32 c = Math::clamp(2.0f * w * w - 1.0f, -1.0f, 1.0f);
        return std::acos(c);
    }
};
} // namespace Math
