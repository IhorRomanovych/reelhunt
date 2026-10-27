// Game/ai/melee_traits.h - how a hunter fights hand to hand: the exe's melee traits.
//
// Every hunter fighting the player hand to hand (CPedActMeleeCombat, its brain at 0x567b20) sorts
// what the player is doing into one of 12 situations (0x566a80) and picks a response by the
// weights its melee-traits record gives that situation (0x5a9a70):
//   0 wait   1 quick blow(s)   2 hard blow   3 grapple   4 block   5 taunt
// It re-decides when the situation (or the player's attack phase) changes, else 0.2 s after it
// last chose to wait; an action holds it until done. Each action has a cooldown in the record
// (quick 0x1ec, hard 0x1f0, grapple 0x1f8; a block lasts 0x1f4). A quick response is a combo
// of blows picked by weight (0x5a9e30 / 0x569f40).
// The records (0x7ae9ec + index * 0x210, built at 0x5ae450) are the scripts' MTT_ values:
// SetHunterMeleeTraits (0x57d7b2) sets one, else a hunter gets one by its character record
// (0x41c470: hoods 1-3, skinheads 4-6, innocentz 7-9, wardogs 10, smileys 11, Piggsy 12, cops
// 9, the rest 26 "default (balanced)"). On hard difficulty the gang records 1..12 differ.
// The record also counts how often the player repeats himself - blocking (situation 3),
// attacking (4), leaving openings (7-9), grabbing (11) - and past its limit reweights the
// responses (0x5a9bd0); the counts live in the record, so every hunter with it shares them.
#pragma once
#include "engine_types.h"

#include <array>
#include <random>
#include <string_view>
#include <vector>

namespace Game {

inline constexpr int32 kMeleeTraitCount = 33;
inline constexpr int32 kDefaultMeleeTraits = 26; // "default (balanced)"

enum class MeleeResponse : int32 { Wait = 0, Quick, Hard, Grapple, Block, Taunt };

struct MeleeTraits {
    const char* name;
    // Response weights (wait, quick, hard, grapple, block, taunt) for each situation:
    //   0 he is close (< 2.5 m), not in reach      1 in reach      2 close, in front of it (< 20 deg)
    //   3 he blocks     4 his blow is coming (> 0.11 s off)     5 his hard blow is coming at its guard
    //   6 (its guard broken: not used)     7, 8, 9 his blow has landed: an opening of 0.11 s, of
    //   its own quick blow's time, of its grab time     10 it guards or reels as his swing ends
    //   11 he reaches to grab it
    std::array<std::array<float32, 6>, 12> situations;
    // Weights of the quick combos by type (0x5a9e30); only 8..12 are used: 8 one quick blow, 9 two,
    // 10 quick then hard, 11 three quick, 12 two quick then hard.
    std::array<float32, 15> attackTypes;
    float32 quickPause;   // seconds after a quick combo before the next (0x1ec)
    float32 hardPause;    // after a hard blow (0x1f0)
    float32 blockHold;    // how long a block is held (0x1f4)
    float32 grapplePause; // after a grapple (0x1f8)
    std::array<uint8, 4> repeatLimit;   // 0x1cc..0x1cf: situation 4, 7-9, 11, 3
    std::array<float32, 4> repeatScale; // 0x1d0..0x1dc
};

// The record of index `index` (an MTT_ value; out of range: the default).
const MeleeTraits& meleeTraits(int32 index, bool hardDifficulty);

// Its footwork weights (record +0x164 .. +0x170, picked by 0x5a9ec0 in CPedActMeleeCombat's free
// state 0xf): 0 stand off (state 0xe), 1 circle (0x10), 2 hold (0 / a pause), 3 close in (0x11);
// and +0x1bc, the chance in 100 that after backing away it decides afresh (0x5a9fa0).
struct MeleeFootwork {
    std::array<float32, 4> moves;
    float32 redecide;
};
const MeleeFootwork& meleeFootwork(int32 index, bool hardDifficulty);
// One of the four moves by weight (0x5a9ec0); 3 (close in) when every weight is 0.
int32 pickFootwork(const MeleeFootwork& footwork, std::mt19937& rng);
// A hunter's record by its character record (Hod_BodS1 ...), when no script sets one.
int32 defaultMeleeTraits(std::string_view characterRecord);

// The player's repeats, per record (shared by every hunter with that record).
struct MeleeRepeats {
    std::array<uint8, 4> count{};
};

// What a hunter sees of the fight, for 0x566a80.
struct MeleeView {
    float32 distance = 1e9f; // flat, its feet to his
    float32 facing = 180.0f; // degrees between where it faces and him
    float32 reach = 1.4f;    // its quick blow's SIZE + 0.3 (0x56a1f0)
    bool playerBlocking = false;
    bool playerGrabbing = false;
    bool playerSwinging = false;
    bool swingHard = false;    // his hard blow
    float32 swingTime = 0.0f;  // seconds into his swing
    float32 swingLands = 0.0f; // when his swing's blow lands
    float32 hardLands = 0.0f;  // when his weapon's hard blow lands (COMBAT_DELAYS[1])
    bool selfBlocking = false;
    float32 selfBlockAge = 1e9f; // seconds since its guard went up
    bool selfHurt = false;
    float32 selfQuickLands = 0.4f; // its own quick blow's time (COMBAT_DELAYS[0])
    float32 selfGrabTime = 0.6f;   // its weapon's GRAB_TIME
};

// The situation (0..11; 13 he is beyond 2.5 m, 14 nothing to decide on).
int32 classifyMeleeSituation(const MeleeView& v);

// The response to `situation` (0x5a9a70): `noQuick` / `noHard` / `noGrapple` rule those out (out
// of reach, cooling down). Counts the repeat in `repeats`.
MeleeResponse chooseMeleeResponse(const MeleeTraits& traits, MeleeRepeats& repeats, int32 situation, bool noQuick,
                                  bool noHard, bool noGrapple, std::mt19937& rng);

// A plain weighted pick from `situation`'s row (0x5a9b40 -> 0x5a9b60: no rule-outs, no repeats).
MeleeResponse pickMeleeResponse(const MeleeTraits& traits, int32 situation, std::mt19937& rng);

// The blows of a quick response, in order (true: a hard blow).
std::vector<bool> chooseMeleeCombo(const MeleeTraits& traits, std::mt19937& rng);

} // namespace Game
