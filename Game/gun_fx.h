// Game/gun_fx.h - what a shot looks like: the muzzle flash, the tracer streak along each line,
// the shells a gun throws out and the nails the nail gun fires.
//
// From the data:
//   the streak   ShotTypeData STREAK_FX gunshot_streak (particleeffects.ini): a WIDTH 2.5 m by
//                HEIGHT 0.02 m strip of fx_streak (levels/GLOBAL/SFX/sfx_pc.txd) that lives
//                MIN_TIME .. MAX_TIME (0.1 .. 0.3 s)
//   shells       the gun's record (items.h GunLook): SHELL_MODEL from the level's modelspc.dff,
//                SHELL_NUMBER / _EJECT_POS / _EJECT_DELAY_FIRE / _RELOAD / _RAND_DELAY / _VELOCITY /
//                _VEL_RAND / _SPIN / _RADIUS; SHELLS_FIRED: the nail gun fires its nails
// Ours: the flash. Every gun names FLASH_MODEL Commando_Flash, which no PC model pack holds, and
// the shots' FLASH_FX (nailgun_muzFlash ...) have no particle records: a warm light and a
// fx_fire1 sprite at FLASH_POS for a moment stand in. Also ours: how fast the streak runs down
// the line, gravity and bounce for shells, the nails' speed, and how long shells and nails stay.
#pragma once
#include "engine_types.h"

#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "Game/character.h"
#include "Game/gunplay.h"
#include "Game/items.h"
#include "Game/texture_set.h"
#include "Math/matrix.h"
#include "Math/vector.h"

namespace Renderer { class Light; class Mesh; class Scene; }

namespace Game {

class PlayerField;

class GunEffects {
public:
    // `models` / `textures`: the level's modelspc.dff and its textures (shell and nail models).
    void load(Renderer::Scene& scene, const std::string& gameRoot, const DffLibrary* models, const TextureSet* textures);
    // A gun fired. `hand` is the shooter's hand bone in the scene (the gun model rides it as
    // authored: its +X down the barrel).
    // `feetY`: the shooter's feet (the lowest a shell falls).
    void fired(const GunLook& look, const Math::Matrix4x4& hand, const std::vector<ShotLine>& lines, float32 feetY,
               bool flashSprite = true);
    // A reload started: the guns that drop their spent shells then (revolver, sawn-off, shotgun).
    void reloaded(const GunLook& look, const Math::Matrix4x4& hand, float32 feetY);
    void update(float32 dt, const Math::Vector3& camera, const PlayerField& field);
    // A spent shell landed (0x59aa10 posts entity audio 0xe for the gun while it has bounced at
    // most twice before, with 1 - 0.5 x those bounces, times the surface's
    // MaterialTypes::shellLevel).
    struct Landing {
        int32 ct = 0;
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
        float32 value = 1.0f;
    };
    [[nodiscard]] std::vector<Landing> takeLandings() { return std::exchange(landings_, {}); }
    // The GPU texture handles of sfx_pc.txd (freed when the session ends).
    [[nodiscard]] std::vector<uint32> textureHandles() const { return fx_.handles(); }

private:
    struct Sprite {
        Renderer::Mesh* mesh = nullptr;
        bool streak = false;
        Math::Vector3 from{0.0f, 0.0f, 0.0f}, to{0.0f, 0.0f, 0.0f};
        float32 age = 0.0f, life = 0.0f;
        float32 size = 0.0f;
    };
    struct Flash {
        Renderer::Light* light = nullptr;
        float32 left = 0.0f;
    };
    struct Shell {
        std::string model;
        std::vector<Renderer::Mesh*> meshes;
        bool active = false;
        float32 delay = 0.0f;                       // seconds before it appears
        bool pending = false;                       // not thrown out yet
        Math::Matrix4x4 hand{};                     // the hand when it was thrown out
        Math::Vector3 localPos{0.0f, 0.0f, 0.0f};   // where on the gun
        Math::Vector3 pos{0.0f, 0.0f, 0.0f}, vel{0.0f, 0.0f, 0.0f};
        Math::Vector3 spin{0.0f, 0.0f, 0.0f}, angle{0.0f, 0.0f, 0.0f};
        Math::Vector3 localVel{0.0f, 0.0f, 0.0f};
        float32 radius = 0.01f;
        float32 feetY = -1e9f, floorY = -1e9f;      // the shooter's feet: the lowest floor (ours)
        float32 left = 0.0f;                        // seconds it stays once it lies still / sticks
        bool resting = false;
        bool nail = false;                          // fired: flies to `target`, then sticks
        Math::Vector3 target{0.0f, 0.0f, 0.0f};
        bool dropAtTarget = false;
        uint64 born = 0;
        int32 ct = 0;      // the gun's collectable type
        int32 bounces = 0; // off the floor so far
    };

    Sprite& sprite(bool streak);
    Shell* shell(const std::string& model);
    void throwShells(const GunLook& look, const Math::Matrix4x4& hand, float32 delay, float32 feetY);
    void place(Shell& s);

    Renderer::Scene* scene_ = nullptr;
    const DffLibrary* models_ = nullptr;
    const TextureSet* textures_ = nullptr;
    TextureSet fx_;
    const Renderer::Texture* streakTex_ = nullptr;
    const Renderer::Texture* fireTex_ = nullptr;
    std::vector<Sprite> sprites_;
    std::vector<Flash> flashes_;
    std::vector<Shell> shells_;
    std::vector<Landing> landings_;
    uint64 born_ = 0;
    std::mt19937 rng_{0x53484c4cu};
};

} // namespace Game
