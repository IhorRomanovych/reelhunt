// Game/ped_anims.h - the ped clips' behaviours and the blend each is entered with.
//
// The exe names its ped clips in one table (0x6851c0, ids 0 .. 0x355) and groups them into
// behaviours (BT_*, table 0x68f210: each the first clip id of its range, the range running to the
// next behaviour's first) with the time a clip of that behaviour is blended in over (0.1, 0.2,
// 0.4 or 0.6 s: Sneak_* 0.4, the turns 0.2, Jump_Launch 0.1, DeadBody_Pickup_Fwd 0.6, ...; set by
// 0x41d970 / 0x41f320). Both are read from the player's exe at start-up (Game/exe_tables.h).
#pragma once
#include "engine_types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Game {

class PedAnims {
public:
    bool load(const std::string& path);
    [[nodiscard]] bool empty() const noexcept { return ids_.empty(); }
    // The clip's id (-1 unknown) and behaviour (-1 none).
    [[nodiscard]] int32 clipId(const std::string& clip) const;
    [[nodiscard]] int32 behaviourOf(const std::string& clip) const;
    // The blend the clip's behaviour is entered with, or `fallback` for a clip outside the table.
    [[nodiscard]] float32 blend(const std::string& clip, float32 fallback) const;
    // Going from clip `from` to `to`: the blend of `to`'s behaviour when it is another behaviour
    // (or `from` is none), else `fallback` (a clip change within one behaviour).
    [[nodiscard]] float32 blendInto(const std::string& from, const std::string& to, float32 fallback) const;

    // The table every ped uses (loaded once at start-up).
    static PedAnims& shared();

private:
    struct Behaviour {
        int32 first = 0;
        float32 blend = 0.2f;
    };
    std::unordered_map<std::string, int32> ids_; // lower-case name -> id
    std::vector<Behaviour> behaviours_;
};

} // namespace Game
