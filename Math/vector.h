// Math/vector.h - fixed-size vectors with inline operations.
// Keeps the historical x/y/z/w fields (brace-init {x,y,z} untouched); arithmetic is
// delegated to glm.
#pragma once
#include <array>
#include <cmath>
#include "engine_types.h"
#include <glm/glm.hpp>

namespace Math {
struct Vector2 {
    float32 x = 0.0f;
    float32 y = 0.0f;
    static constexpr Vector2 zero() noexcept { return {}; }
    static constexpr Vector2 unitX() noexcept { return {1.0f, 0.0f}; }
    static constexpr Vector2 unitY() noexcept { return {0.0f, 1.0f}; }
    Vector2 operator+(const Vector2& o) const noexcept { return {x + o.x, y + o.y}; }
    Vector2 operator-(const Vector2& o) const noexcept { return {x - o.x, y - o.y}; }
    Vector2 operator*(float32 s) const noexcept { return {x * s, y * s}; }
    Vector2 operator*(const Vector2& o) const noexcept { return {x * o.x, y * o.y}; }
    Vector2& operator+=(const Vector2& o) { x += o.x; y += o.y; return *this; }
    Vector2& operator-=(const Vector2& o) { x -= o.x; y -= o.y; return *this; }
    Vector2& operator*=(float32 s) { x *= s; y *= s; return *this; }
    [[nodiscard]] float32 dot(const Vector2& o) const noexcept {
        return glm::dot(glm::vec2(x, y), glm::vec2(o.x, o.y));
    }
    [[nodiscard]] float32 lengthSquared() const noexcept {
        return x * x + y * y;
    }
    [[nodiscard]] float32 length() const noexcept {
        return glm::length(glm::vec2(x, y));
    }
    [[nodiscard]] Vector2 normalized() const noexcept {
        if (length() == 0.0f) return Vector2{};
        glm::vec2 r = glm::normalize(glm::vec2(x, y));
        return {r[0], r[1]};
    }
};

struct Vector3 {
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
    static constexpr Vector3 zero() noexcept { return {}; }
    static constexpr Vector3 unitX() noexcept { return {1.0f, 0.0f, 0.0f}; }
    static constexpr Vector3 unitY() noexcept { return {0.0f, 1.0f, 0.0f}; }
    static constexpr Vector3 unitZ() noexcept { return {0.0f, 0.0f, 1.0f}; }
    Vector3 operator+(const Vector3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    Vector3 operator-(const Vector3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    Vector3 operator*(float32 s) const noexcept { return {x * s, y * s, z * s}; }
    Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vector3& operator*=(float32 s) { x *= s; y *= s; z *= s; return *this; }
    [[nodiscard]] float32 dot(const Vector3& o) const noexcept {
        return glm::dot(glm::vec3(x, y, z), glm::vec3(o.x, o.y, o.z));
    }
    [[nodiscard]] Vector3 cross(const Vector3& o) const noexcept {
        glm::vec3 r = glm::cross(glm::vec3(x, y, z), glm::vec3(o.x, o.y, o.z));
        return {r[0], r[1], r[2]};
    }
    [[nodiscard]] float32 lengthSquared() const noexcept {
        return x * x + y * y + z * z;
    }
    [[nodiscard]] float32 length() const noexcept {
        return glm::length(glm::vec3(x, y, z));
    }
    [[nodiscard]] Vector3 normalized() const noexcept {
        if (length() == 0.0f) return Vector3{};
        glm::vec3 r = glm::normalize(glm::vec3(x, y, z));
        return {r[0], r[1], r[2]};
    }
    [[nodiscard]] constexpr Vector2 toXY() const noexcept { return {x, y}; }
};

struct Vector4 {
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 z = 0.0f;
    float32 w = 0.0f;
    static constexpr Vector4 zero() noexcept { return {}; }
    static constexpr Vector4 one() noexcept { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    Vector4 operator+(const Vector4& o) const noexcept { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vector4 operator*(float32 s) const noexcept { return {x * s, y * s, z * s, w * s}; }
    [[nodiscard]] Vector3 to3() const noexcept { return {x, y, z}; }
};
} // namespace Math
