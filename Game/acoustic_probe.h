// Game/acoustic_probe.h - how open the camera's surroundings are (exe 0x447ac0, run by the
// audio manager each frame; 0x448ef0 / 0x4491c0 read it for the guns' tails).
//
// Every 10 frames (+0xd768 % 10 == 0) it takes the camera's matrix and finds its listening point:
// the camera, or the point straight below or above it at the player's head (his feet + 1.8 m,
// 0x6b29b0) when nothing is between, and the ceiling over that point: the distance to the first
// surface within 30 m straight up (0x6b29e8), 0 for none. Each frame it casts two of 20 rays
// round that point, 18 degrees apart (the frame's index in the ten and the one opposite), and
// turns its matrix 18 degrees on (0x6b29fc = -18: clockwise seen from above, so ray 5 is to the
// camera's right and ray 15 to its left). Each ray holds:
//   - the distance to the first surface within 10 m (0x6b29bc) along it, 0 for none;
//   - whether there is a ceiling within 30 m over the points 5 m (0x6b29b4) and 10 m out.
// Only surfaces whose material counts (table 0x7ac190, 0x5a6f20) are seen.
//
// Its two readings, per side (a ray's weight for the left is |((18 i + 90) mod 360) / 180 - 1|,
// for the right the same with 270; its share 0.8 x weight + 0.1, 0x6b2a10 / 0x6b2a14):
//   - shut (0x448ef0): 0 with no ceiling over the listening point; else 1, less 3 x share / 10
//     (0x6b2a1c) for each ray open to the sky: no wall within 5 m and no ceiling 5 m out, or no
//     wall at all and no ceiling 10 m out; at least 0.
//   - close (0x4491c0): the sum over the rays with a wall of max(0, (5 - its distance) / 5) x
//     share / 10, at most 1.
// A gun's tail on a side is heard at (1 - close / 1.25) x (1 - shut) of its level (0x44f620,
// 0x6b2a20): full outdoors in the open, none under a roof with no opening near.
// Not used: the reverb amount (0x447760, EAX), the surfaces' angles (+0xd5d4), and the ring's
// turning with the camera in his first-person mode (+0xd4ec).
#pragma once
#include "engine_types.h"

#include "Math/vector.h"

#include <array>
#include <functional>
#include <optional>

namespace Game {

class AcousticProbe {
public:
    static constexpr int32 kRays = 20;
    // The distance from a to the first surface that counts on the segment a -> b, if any.
    using Ray = std::function<std::optional<float32>(const Math::Vector3& a, const Math::Vector3& b)>;

    // One frame: the camera at `eye` facing `forward` (horizontal), the player's feet.
    void update(const Math::Vector3& eye, const Math::Vector3& forward, const Math::Vector3& playerFeet, const Ray& ray);
    void reset() noexcept { *this = AcousticProbe{}; }

    // A tail's share of its level on the left (side -1) or right (1).
    [[nodiscard]] float32 tail(int32 side) const noexcept;
    [[nodiscard]] float32 shut(int32 side) const noexcept;
    [[nodiscard]] float32 close(int32 side) const noexcept;
    [[nodiscard]] float32 ceiling() const noexcept { return ceiling_; }
    [[nodiscard]] const Math::Vector3& listener() const noexcept { return listener_; }

    // A ray's weight for the left (side -1) or right (1) side.
    [[nodiscard]] static float32 weight(int32 ray, int32 side) noexcept;

    struct Sample {
        float32 wall = 0.0f;   // distance to a wall along it (0: none within 10 m)
        bool roof5 = false;    // a ceiling over the point 5 m out
        bool roof10 = false;   // ... 10 m out
    };
    std::array<Sample, kRays> rays{};

private:
    uint32 frame_ = 0;
    Math::Vector3 listener_{0.0f, 0.0f, 0.0f};
    Math::Vector3 forward_{0.0f, 0.0f, -1.0f}; // the matrix's facing when it was last taken
    float32 ceiling_ = 0.0f;
};

} // namespace Game
