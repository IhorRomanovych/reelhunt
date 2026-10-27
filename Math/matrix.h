// Math/matrix.h - 3x3 and 4x4 matrices.
// Storage keeps the historical row-major mRC fields (mRC = row R, col C) so all
// call sites / brace-init are untouched; arithmetic is delegated to glm.
#pragma once
#include <cmath>
#include "engine_types.h"
#include "vector.h"
#include <glm/glm.hpp>

namespace Math {
struct Matrix4x4; // forward declaration for make4x4
struct Matrix3x3 {
    float32 m00 = 1.0f, m01 = 0.0f, m02 = 0.0f;
    float32 m10 = 0.0f, m11 = 1.0f, m12 = 0.0f;
    float32 m20 = 0.0f, m21 = 0.0f, m22 = 1.0f;

    // column c of this matrix == {m0c, m1c, m2c}
    [[nodiscard]] glm::mat3 toGlm() const noexcept {
        return {{m00, m10, m20}, {m01, m11, m21}, {m02, m12, m22}};
    }

    [[nodiscard]] Vector3 multiply(const Vector3& v) const noexcept {
        glm::vec3 r = toGlm() * glm::vec3(v.x, v.y, v.z);
        return {r[0], r[1], r[2]};
    }
    [[nodiscard]] Matrix3x3 transpose() const noexcept {
        glm::mat3 g = glm::transpose(toGlm());
        return {g[0][0], g[1][0], g[2][0],
                g[0][1], g[1][1], g[2][1],
                g[0][2], g[1][2], g[2][2]};
    }
    [[nodiscard]] Matrix3x3 inverse() const noexcept {
        glm::mat3 g = glm::inverse(toGlm());
        return {g[0][0], g[1][0], g[2][0],
                g[0][1], g[1][1], g[2][1],
                g[0][2], g[1][2], g[2][2]};
    }
    [[nodiscard]] float32 determinant() const noexcept {
        return glm::determinant(toGlm());
    }
    [[nodiscard]] Matrix4x4 make4x4() const noexcept; // defined below
};

struct Matrix4x4 {
    float32 m00 = 1.0f, m01 = 0.0f, m02 = 0.0f, m03 = 0.0f;
    float32 m10 = 0.0f, m11 = 1.0f, m12 = 0.0f, m13 = 0.0f;
    float32 m20 = 0.0f, m21 = 0.0f, m22 = 1.0f, m23 = 0.0f;
    float32 m30 = 0.0f, m31 = 0.0f, m32 = 0.0f, m33 = 1.0f;

    // column c of this matrix == {m0c, m1c, m2c, m3c}
    [[nodiscard]] glm::mat4 toGlm() const noexcept {
        return {{m00, m10, m20, m30},
                {m01, m11, m21, m31},
                {m02, m12, m22, m32},
                {m03, m13, m23, m33}};
    }

    static Matrix4x4 identity() noexcept { return Matrix4x4{}; }
    static Matrix4x4 fromGlm(const glm::mat4& g) noexcept {
        Matrix4x4 r;
        r.m00 = g[0][0]; r.m01 = g[1][0]; r.m02 = g[2][0]; r.m03 = g[3][0];
        r.m10 = g[0][1]; r.m11 = g[1][1]; r.m12 = g[2][1]; r.m13 = g[3][1];
        r.m20 = g[0][2]; r.m21 = g[1][2]; r.m22 = g[2][2]; r.m23 = g[3][2];
        r.m30 = g[0][3]; r.m31 = g[1][3]; r.m32 = g[2][3]; r.m33 = g[3][3];
        return r;
    }
    static Matrix4x4 translation(const Vector3& t) noexcept {
        Matrix4x4 m = identity();
        m.m03 = t.x; m.m13 = t.y; m.m23 = t.z;
        return m;
    }
    static Matrix4x4 scale(float32 s) noexcept {
        Matrix4x4 m = identity();
        m.m00 = s; m.m11 = s; m.m22 = s;
        return m;
    }
    static Matrix4x4 scale(const Vector3& s) noexcept {
        Matrix4x4 m = identity();
        m.m00 = s.x; m.m11 = s.y; m.m22 = s.z;
        return m;
    }
    static Matrix4x4 rotationX(float32 a) noexcept {
        float32 c = std::cos(a), s = std::sin(a);
        Matrix4x4 m = identity();
        m.m11 = c; m.m12 = -s;
        m.m21 = s;  m.m22 = c;
        return m;
    }
    static Matrix4x4 rotationY(float32 a) noexcept {
        float32 c = std::cos(a), s = std::sin(a);
        Matrix4x4 m = identity();
        m.m00 = c; m.m02 = s;
        m.m20 = -s; m.m22 = c;
        return m;
    }
    static Matrix4x4 rotationZ(float32 a) noexcept {
        float32 c = std::cos(a), s = std::sin(a);
        Matrix4x4 m = identity();
        m.m00 = c; m.m01 = -s;
        m.m10 = s;  m.m11 = c;
        return m;
    }
    Matrix4x4 operator*(const Matrix4x4& o) const noexcept { return fromGlm(toGlm() * o.toGlm()); }
    [[nodiscard]] Vector4 multiply(const Vector4& v) const noexcept {
        glm::vec4 r = toGlm() * glm::vec4(v.x, v.y, v.z, v.w);
        return {r[0], r[1], r[2], r[3]};
    }
    [[nodiscard]] Vector3 multiplyPoint(const Vector3& p) const noexcept {
        glm::vec4 r = toGlm() * glm::vec4(p.x, p.y, p.z, 1.0f);
        float32 w = r[3];
        if (w == 0.0f) return {r[0], r[1], r[2]};
        return {r[0] / w, r[1] / w, r[2] / w};
    }
    [[nodiscard]] Matrix4x4 transpose() const noexcept { return fromGlm(glm::transpose(toGlm())); }
    // General 4x4 inverse.
    [[nodiscard]] Matrix4x4 inverse() const noexcept { return fromGlm(glm::inverse(toGlm())); }
    [[nodiscard]] Vector3 scale() const noexcept {
        glm::mat4 g = toGlm();
        return {glm::length(glm::vec3(g[0])),
                glm::length(glm::vec3(g[1])),
                glm::length(glm::vec3(g[2]))};
    }
    [[nodiscard]] Vector3 position() const noexcept { return {m03, m13, m23}; }
};

[[nodiscard]] inline Matrix4x4 Matrix3x3::make4x4() const noexcept {
    Matrix4x4 m;
    m.m00 = m00; m.m01 = m01; m.m02 = m02; m.m03 = 0.0f;
    m.m10 = m10; m.m11 = m11; m.m12 = m12; m.m13 = 0.0f;
    m.m20 = m20; m.m21 = m21; m.m22 = m22; m.m23 = 0.0f;
    m.m30 = 0.0f; m.m31 = 0.0f; m.m32 = 0.0f; m.m33 = 1.0f;
    return m;
}
} // namespace Math
