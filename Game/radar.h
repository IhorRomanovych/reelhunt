// Game/radar.h - the HUD radar (HUD_MAP), as the exe draws it (0x5dffa0 and below).
//
// Bottom left: a translucent grey disc (24 segments, (0x51, 0x51, 0x51) at alpha 0x80) centred at
// x 0.04 + 0.75 * 0.231 / 2, y 0.8365, 0.231 tall (0.75 * 0.231 wide: round on the 4:3 frontend),
// under the `radar` ring drawn black at (0.04 - 0.00375, 0.716), (0.18075 x 0.241). It turns
// with the camera (its view is up) and reaches 39 m (0x7c9e3c). The exe scales the world into
// the level's minimap picture first (levels.txt reference points, 0x5dfea0), but both the blips
// and the radar's 39 m go through that scale, so it cancels out.
//
// Blips (0x5e1510 -> 0x5e1670 -> 0x5e03f0), colours dimmed by 20 and their alpha held to 180:
//   the player: a grey (0xa4) dot (`mappos`, 3.2 m) in the middle, with his noise ring;
//   hunters the scripts keep on the radar (AISetHunterOnRadar) while they are after him: an
//     arrow (`mapdir`, 5.8 m) turned to their heading, by what they know of him - lost him
//     (150, 170, 0), heard him (150, 85, 0), seen him (110, 0, 0) - faded by how long ago the
//     player last saw them or they last noticed him; dead ones black. Past 39 m they are left
//     off. One above or below him (ours: 2 m) is a triangle (`maptri`) pointing up or down, alpha
//     at most 160;
//   script blips (RadarPositionSetEntity, 10 slots, 0x7ca4f0): a 4.8 m dot in their MAP_COLOR
//     (black, grey, red, orange, yellow, green, blue, cyan), at the rim (5.8 m) when out of range.
// Noise rings (0x5e1c50, 16 slots): a sound of radius r (at most 39 m) grows a disc from 0 to r
// in 39 ticks of 1/30 s, then fades from alpha 70 by 20 a tick, drawn with `mapnoiseent` in
// (a / 2, a / 5, a / 5), clipped to the radar disc. The player's own noise keeps raising one.
#pragma once
#include "engine_types.h"

#include <array>
#include <vector>

#include "Game/texture_set.h"
#include "Math/vector.h"
#include "Renderer/ui.h"

namespace Game {

struct RadarBlip {
    enum class Kind { Hunter, DeadHunter, Script };
    Kind kind = Kind::Hunter;
    Math::Vector3 position{0.0f, 0.0f, 0.0f};
    float32 yaw = 0.0f;     // its heading (hunters), our yaw convention
    int32 state = 0;        // hunters: 0 lost him, 1 heard him, 2 seen him
    float32 visibility = 1.0f; // hunters: 0..1 (faded)
    int32 colour = 0;       // script blips: MAP_COLOR 0..7
};

struct RadarView {
    Math::Vector3 centre{0.0f, 0.0f, 0.0f}; // the player's feet
    float32 viewYaw = 0.0f;                 // the camera's heading (up on the radar)
    std::vector<RadarBlip> blips;
};

class Radar {
public:
    static constexpr float32 kRange = 39.0f; // metres from the middle to the rim

    bool load(const TextureSet& frontend);
    // A sound of radius `radius` m at `at` (a hunter's, a script's): a ring on the radar.
    void addNoise(const Math::Vector3& at, float32 radius);
    // The player's own noise this frame (0 = silent): his ring is raised again when it ends.
    void update(float32 dt, const Math::Vector3& playerFeet, float32 playerNoise);
    void draw(Renderer::UiBatch& ui, const RadarView& view) const;
    void clear() noexcept { rings_ = {}; }

private:
    struct Ring {
        bool on = false;
        bool player = false;
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
        float32 radius = 0.0f, max = 0.0f, alpha = 0.0f;
    };
    std::array<Ring, 16> rings_{};
    const Renderer::Texture* ring_ = nullptr;
    const Renderer::Texture* dot_ = nullptr;
    const Renderer::Texture* arrow_ = nullptr;
    const Renderer::Texture* tri_ = nullptr;
    const Renderer::Texture* noise_ = nullptr;
};

} // namespace Game
