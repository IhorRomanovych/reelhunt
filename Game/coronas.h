// Game/coronas.h - the lamps' coronas (the exe's light flares: 0x59bc20, 0x59c050, 0x59c330, 0x59c3c0).
//
// At the level's start every EC_ENTITYLIGHT entity (type flags 0x401, 0x431880) registers one
// (0x59bc20 -> 0x59c050, at most 80) at its light's place: the world position of its model's
// "HOTSPOT" frame, else its own (0x4ccb00). From its Light_Inst (Type, Cone Angle, Radius, Red,
// Green, Blue, Flags 1..7, Size, Intensity: the toolkit's names) it takes the colour x 255, the
// range 60 m (0x7a7b4c, no further than the camera's far plane), the Intensity, and from the
// Size a far size 0.2 x Size (0x7a7b44) and a near size 6 x Size (0x7a7b40), the widths x 0.75
// (0x7d3458: 480 / 640) - and its flags: 1 | 4, Flags 5 0x20 (it flickers), Flags 6 0x40
// (streaks), Flags 7 0x80. (A Flags value is tagged: its low bit is the setting.)
//
// Each frame (0x59c330 -> 0x59c3c0) a corona within its range is drawn as a screen sprite of the
// "corona" texture (frontend_pc.txd, 0x5ea742), added to the frame:
//   - 0.55 m (0x7a7c94) nearer the camera, unless the world blocks the line between there and
//     the light; not at all when it blocks the line from the camera to the light (0x4de410);
//   - its size the near size / max(distance, 5) (0x7a7c9c), at least the far size (screen
//     fractions: width x 0.75);
//   - its brightness Intensity x (1 - distance / range), times 0.85 + rand(0..255) x 0.000586
//     (0x7a7ca4 / 0x7a7ca0) unless the game is paused, at least the minimum (0);
//   - its colour x brightness / 255; when that averages over 30 (0x7a7cac) it is scaled to
//     its brightest channel at 25 + rand % 5 (0x7a7cb0, 0x4baf20);
//   - a flickering one (0x20, 0x59e080) is hidden while (seed x 1900 + ms) & 0x1fff < 2000
//     and (frame + seed) & 3 != 0.
// The exe blends them ONE, ONE (0x5f5c90(2, 2)); its texture is white with the glow in its
// alpha (from "coronam"), so ours adds it by its alpha.
// A lamp with 0x40 also throws a lens flare while GAME DETAIL is on (0x59e190: settings 0 / 1
// none, 2 every other ghost, 3 all) and it is within 30 m: up to 15 ghosts of the texture on
// the line from it through the screen's middle, each at its place along it, its size and its
// red / green / blue share (tables 0x7a7b68 / 0x7a7ba4 / 0x7a7be0 / 0x7a7c1c / 0x7a7c58) of
// the lamp's colour x a strength: none until (its distance from the middle x its depth) reaches
// 1, full from 5, less past 20 m (to none at 30). Its sizes are the corona's, the larger at most
// half the screen.
// Its sizes are times 1 + the fog (0x7b3250: 1 in FOGGY weather, CWeather's Foggyness), and its
// brightness a quarter while the scripts hold the camera (0x5ea2c0).
//
// A torch's beam (flag 0x100 with the torch's matrix, 0x594400's second pass): instead of the
// corona, 13 sprites of the texture along the torch's x axis, within 40 m and unless the world
// blocks the line to it: the n-th at t = 0.077 + 0.075 + ... (each step 0.002 less, 0x7a7cb4), its
// size the corona's (here 1 x 1) x t x 0.274 (0x7a7cb8), each the next a further t x 0.274 x 0.1
// (0x7a7cbc) x its depth out along the axis, in the torch's colour as it is, added (blend
// DESTALPHA / ONE, 0x5f5ee0). (A black quad drawn before each, 0x5f8d20 with 0x7a5f34.., adds
// nothing: its alpha 0x7a5f54 is never set.)
// Not done: the z test (the exe's sprites are depth-tested; ours: the world line only). The 0x80
// pass (0x59da30) is never set off: no lamp or torch sets that flag.
#pragma once
#include "engine_types.h"

#include "Math/vector.h"

#include <functional>
#include <optional>
#include <random>
#include <vector>

namespace Game {

class Coronas {
public:
    static constexpr std::size_t kMax = 80;
    struct Light {
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
        int32 r = 255, g = 255, b = 255;
        // Its near and far sizes (screen fractions: at 5 m and its least) and range.
        float32 nearW = 4.5f, nearH = 6.0f, farW = 0.15f, farH = 0.2f;
        float32 range = 60.0f;
        float32 intensity = 0.0f;
        float32 minIntensity = 0.0f;
        uint32 flags = 5;
        uint16 seed = 0;
        Math::Vector3 axis{0.0f, 0.0f, 0.0f}; // a torch's beam (0x100): along its x axis
    };
    // A Light_Inst placement's corona at `at` (its params as the .inst stores them).
    [[nodiscard]] static Light fromParams(const std::vector<int32>& params, const Math::Vector3& at, uint16 seed);
    void clear() noexcept { lights_.clear(); }
    bool add(const Light& light);
    [[nodiscard]] std::size_t size() const noexcept { return lights_.size(); }

    struct View {
        Math::Vector3 eye{0.0f, 0.0f, 0.0f};
        float32 farClip = 1000.0f;
        // A world point on the screen: (x, y) in 0..1 from the top left, z its depth; none
        // behind the camera.
        std::function<std::optional<Math::Vector3>(const Math::Vector3&)> project;
        // True when the world does not block the line a -> b.
        std::function<bool(const Math::Vector3&, const Math::Vector3&)> clear;
        uint32 timeMs = 0;
        uint32 frame = 0;
        bool paused = false;
        int32 detail = 0; // GAME DETAIL's level (0x7a5f88): 0 none, 1 half the ghosts, 2 all
        float32 fog = 0.0f;    // 0x7b3250 (FOGGY weather: 1)
        bool scripted = false; // the scripts hold the camera (0x5ea2c0)
    };
    struct Sprite {
        float32 x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f; // screen fractions
        uint8 r = 0, g = 0, b = 0;
    };
    // This frame's sprites: the registered coronas', then any `extra` ones (the torches').
    [[nodiscard]] std::vector<Sprite> frame(const View& view, std::mt19937& rng, const std::vector<Light>* extra = nullptr) const;

private:
    std::vector<Light> lights_;
};

} // namespace Game
