// Math/math.h - math namespace entry point and constants.
#pragma once
#include <cmath>
#include <cstdint>
#include "engine_types.h"
#include <glm/glm.hpp>

namespace Math {
namespace Engine = ::Engine;

constexpr float32 pi = 3.14159265358979323846f;
constexpr float32 tau = 2.0f * pi;
constexpr float32 invPi = 1.0f / pi;
constexpr float32 invTau = 1.0f / tau;

constexpr float32 maxFloat = 3.40282346638557e38f;
constexpr float32 minPositive = 1.1754944e-38f;

// Degree/radian conversion — delegate to glm (no hand-rolled magic numbers).
[[nodiscard]] inline float32 radians(float32 deg) noexcept { return glm::radians(deg); }
[[nodiscard]] inline float32 degrees(float32 rad) noexcept { return glm::degrees(rad); }

// Scalar helpers — delegate to glm.
[[nodiscard]] inline float32 lerp(float32 a, float32 b, float32 t) noexcept { return glm::mix(a, b, t); }
[[nodiscard]] inline float32 clamp(float32 v, float32 lo, float32 hi) noexcept { return glm::clamp(v, lo, hi); }
constexpr float32 abs(float32 v) { return v < 0 ? -v : v; }
constexpr double64 abs(double64 v) { return v < 0 ? -v : v; }
} // namespace Math
