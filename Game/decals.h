// Game/decals.h - marks projected onto the world (the exe's decals: init 0x482640, add 0x4829b0).
//
// The exe keeps a ring of 130 decals (0x71c9f4, 0x28 bytes each; the 131st replaces the first).
// Adding one (0x4829b0(position, direction, owner, size min / max, rotation min / max in degrees,
// r, g, b, a, life, type)) picks the size and rotation at random between their bounds (-1: the
// type's own), gathers the world triangles in a box of half the size about the point and clips a
// square of that size onto them (callbacks 0x4831f0 / 0x483200), tinted by the level's ambient.
//
// The types (0x484920, 0x18 bytes each at 0x71ca3c: texture from frontend_pc.txd, size min / max,
// alpha):
//   0 test, 1 blood, 2 blood2, 3 blood3, 4 blood_dead, 5 melee, 6 explos, 7 fluid (sizes from
//   the caller, alpha 255); 8 test, 9 bh_metal, 10 bh_stone (0.03..0.07 m), 11 bh_wood,
//   12 bh_metal, 13 bh_stone, 14 bh_metal, 15 bh_glass (0.2..0.26 m, alpha 128), 16 bh_stone
//   (0.03..0.07), 17 bh_metal, 18 bh_wood, 19 bh_fabric, 20 bh_stone, 21..25 bh_fabric (0.06..0.1
//   m, alpha 200 unless noted).
// Ours: the square is laid on the collision triangles facing its direction, as a mesh of its own
// (built once: a growing pool scales it about its centre), drawn blended with the level's
// prelight; a decal never fades (none of the callers give a life).
#pragma once
#include "engine_types.h"

#include <cstdint>
#include <deque>
#include <random>

#include "Math/vector.h"

namespace Renderer { class Mesh; class Scene; namespace Backend { class Vulkan; } }

namespace Game {

class Hud;
class LevelContent;

class Decals {
public:
    static constexpr std::size_t kRing = 130;
    static constexpr int32 kTypes = 26;

    // What to draw on and with (the level's collision triangles and prelight, the HUD's
    // frontend_pc.txd textures).
    void attach(Renderer::Scene& scene, Renderer::Backend::Vulkan* device, const LevelContent& level, const Hud& textures);
    // A decal of `type` at `at` laid against `normal` (the surface it marks faces it), `size`
    // metres across (< 0: the type's range) turned `rotation` degrees (< 0: at random). Returns its
    // id, or 0 when nothing took it.
    uint32 add(int32 type, const Math::Vector3& at, const Math::Vector3& normal, float32 size, float32 rotation,
               std::mt19937& rng);
    // A decal drawn at `scale` of its size (a corpse's pool growing, 0x4b30a0); false if it is gone.
    bool setScale(uint32 id, float32 scale);

private:
    struct Decal {
        uint32 id = 0;
        Renderer::Mesh* mesh = nullptr; // owned by the scene
        Math::Vector3 at{0.0f, 0.0f, 0.0f}, u{1.0f, 0.0f, 0.0f}, v{0.0f, 0.0f, 1.0f};
    };
    void drop(const Decal& d);

    Renderer::Scene* scene_ = nullptr;
    Renderer::Backend::Vulkan* device_ = nullptr;
    const LevelContent* level_ = nullptr;
    const Hud* textures_ = nullptr;
    std::deque<Decal> ring_;
    uint32 nextId_ = 1;
};

} // namespace Game
