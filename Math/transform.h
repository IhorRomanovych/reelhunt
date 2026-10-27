// Math/transform.h - spatial transform (position + rotation + scale).
#pragma once
#include "engine_types.h"
#include "vector.h"
#include "quaternion.h"
#include "matrix.h"

namespace Math {
struct Transform {
    Vector3 position = Vector3{};
    Quaternion rotation = Quaternion::identity();
    Vector3 scale = Vector3{1.0f, 1.0f, 1.0f};

    [[nodiscard]] Matrix4x4 toMatrix() const noexcept {
        // T * R * S: scale in local space, then rotate, then translate. (The previous T * S * R
        // applied a non-uniform scale along world axes after rotating.)
        return Matrix4x4::translation(position) * rotation.toMatrix().make4x4() * Matrix4x4::scale(scale);
    }
    [[nodiscard]] Vector3 worldPosition(const Transform& parent) const noexcept {
        return parent.toMatrix().multiplyPoint(position);
    }
    void setTranslation(const Vector3& p) noexcept { position = p; }
    void setRotation(const Quaternion& q) noexcept { rotation = q.normalized(); }
    void setScale(float32 s) noexcept { scale = Vector3{s, s, s}; }
    void apply(const Transform& other) noexcept {
        rotation = other.rotation * rotation;
        position = other.rotation.toMatrix().make4x4().multiplyPoint(position) + other.position;
    }
};
} // namespace Math
