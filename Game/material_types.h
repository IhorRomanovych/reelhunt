// Game/material_types.h - the surface material of each world texture (levels/GLOBAL/DATA/mat.bin).
//
// mat.bin is the compiled material script ("Compiling material script", loaded by 0x5a7060 ->
// 0x5a70c0): an int count of types (63), an int per type with how many texture names it lists,
// then the names, 200 bytes each (0x32 ints), type by type. Each world material takes the type
// whose list holds its texture name (0x5a7360 walks the world, callback 0x5a71c0; the names a list
// misses are counted, "Num Materials Missing = %d"). Type 1 holds the bulk of the brick and
// concrete, 22 / 23 grass and mud, 33..38 wood, 39..44 metal, 53 guts.
//
// What a type does comes from tables in the exe, by type: the hit effect (0x5a6f90, table
// 0x7ac544) is 1..11, which 0x5a6fb0 names: none, FXBWOOD, FXBSTON, FXBTMET2, FXBTMET, FXBTURF,
// FXBPLAS, FXBPAPE, FXBMUD, FXBCERM, FXP003 (blood); a struck surface plays it at the hit,
// its matrix's at-axis along the surface normal (0x4f3500 after 0x4f45ec). Its bullet hole is the
// decal type in table 0x7ac448 (0x5a6f80; Game/decals.h), left only where two flags of the type
// allow it (0x5a6f30 / 0x5a6f50: tables 0x7ac2cc and 0x7ac389). How much sound gets through it is
// table 0x7abf98 (0x5a6f10): the music's placed sounds are heard through the world by the product
// over every surface the line crosses (0x4df3b0, callback 0x4df2f0).
#pragma once
#include "engine_types.h"

#include <string>
#include <unordered_map>

namespace Game {

class MaterialTypes {
public:
    static constexpr uint8 kNone = 0;
    static constexpr uint8 kTypes = 63;

    // Reads mat.bin; false if it is missing or malformed.
    bool load(const std::string& path);
    // The type of a world texture (case ignored), kNone if no list holds it.
    [[nodiscard]] uint8 typeOf(const std::string& texture) const;
    // The FX project system a bullet striking this type plays, or nullptr.
    [[nodiscard]] static const char* hitEffect(uint8 type) noexcept;
    // The decal type a bullet leaves on this type, or -1.
    [[nodiscard]] static int32 bulletHole(uint8 type) noexcept;
    // The share of a sound that gets through this type (0..1).
    [[nodiscard]] static float32 soundThrough(uint8 type) noexcept;
    // Whether the audio manager's probe of the surroundings sees this type (0x5a6f20, table
    // 0x7ac190; Game/acoustic_probe.h).
    [[nodiscard]] static bool stopsSound(uint8 type) noexcept;
    // A spent shell landing on this type is heard at this share of its level (0x5a6f70, table
    // 0x7ac1d0).
    [[nodiscard]] static float32 shellLevel(uint8 type) noexcept;
    // The sound a bullet makes off this type (0x446750, by the struck surface's type): the
    // SFX_WEAPON_BULLET_ category (RICOCHET_STONE / _WOOD / _METAL, IMPACT_METAL; one of three
    // at random) and its volume (the unlisted ones: wood at 0.672).
    struct Ricochet {
        const char* sample;
        float32 volume;
    };
    [[nodiscard]] static Ricochet ricochet(uint8 type) noexcept;
    // The footstep samples of this type (0x5a6ed0 / 0x5a6ee0, table 0x7abda0): the first and last
    // bank index of its SFX_FOOTSTEP_ range (concrete, grass, gravel, water, wood, metal, mud,
    // tile; wire mesh SFX_COL_METAL_WIRE_MESH, guts SFX_MISC_BRAIN_SPLAT), or {-1, -1} for none.
    struct Steps {
        int32 first;
        int32 last;
    };
    [[nodiscard]] static Steps footsteps(uint8 type) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return types_.size(); }

private:
    std::unordered_map<std::string, uint8> types_; // lower-case texture name -> type (first list wins)
};

} // namespace Game
