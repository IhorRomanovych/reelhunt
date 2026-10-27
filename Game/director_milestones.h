// Game/director_milestones.h - the director's milestone commentary (AISPEECH 1..38).
//
// The exe keeps level stats in records at 0x7b7d74 (0x1c bytes each) and, every frame in
// scenes 1..13 (0x413eb0, scene index 0..12), compares four of them with per-level milestone
// bytes (0x7b7e34..0x7b7e41). When a count equals a milestone the director comments once:
//   X  executions (OnStartExecution, 0x5b62e0)   ids 1..9    (0x414090, category 0x5b6b80)
//   H  hidden > 5 s (safe zone, 0x5b5ef0)        ids 10..18  (0x4141c0, category 0x5b6c30)
//   S  spotted (OnPlayerSpotted, 0x5b59c0)       ids 19..29  (0x414300, category 0x5b6cd0)
//   KT kills (a hunter dies, 0x5b63a0)           ids 30..38  (0x414530, category 0x5b6bf0)
// Categories: 1 LOW, 4 MED, 5 HIGH (S in scenes 1..5 also 2, 3: SLOW2, SLOW3); the id set by
// scene (index <= 4, <= 8, else). The milestones are drawn at level start (0x5b6490), partly at
// random, and for X / KT scaled by the level's hunter count. See PLAN.md (2026-10-20).
#pragma once
#include "engine_types.h"

#include <array>
#include <random>

namespace Game {

class DirectorMilestones {
public:
    enum Stat { Executions, Hiding, Spotted, Kills, kStats };

    // What the frame needs from the game (0x413eb0's checks).
    struct Frame {
        float32 dt = 0.0f;
        bool hidden = false;    // the player is in a safe zone (light below SAFE_ZONE)
        bool seen = false;      // a hunter is after him this frame (0x799f74 / 0x53e740)
        bool cutscene = false;  // a scripted cut-scene holds the game (0x53e020)
        bool talking = false;   // the director is speaking (0x6c5244)
        bool inArena = false;   // his graph node lies in the area "aiarena" (0x414670)
    };

    // Level start (0x5b5cd0 -> 0x5b6490): the counts to zero and this scene's milestones.
    // `scene` is the 0-based levels.txt index (-1: no commentary), `hunters` the level's hunter
    // count (0x7917e4). The "gruesome execution done" flag (0x7b7e2c) is never reset by the exe.
    // `counts`: a resumed checkpoint's (0x5b5a60 puts the save point's copy back before 0x5b6490
    // draws the milestones, so those its counts already stand on are spent).
    void start(int32 scene, int32 hunters, std::mt19937& rng, const std::array<int32, kStats>& counts = {});
    [[nodiscard]] const std::array<int32, kStats>& counts() const noexcept { return counts_; }

    // The events that count.
    void execution(int32 level); // OnStartExecution; `level` 2..4 as ExecutionCharge (4 = gruesome)
    void kill();                 // a hunter died (not by another hunter)
    void spotted();              // OnPlayerSpotted

    // One frame: the spotted event and hiding spells from `f`, then the four handlers. Returns
    // the AISPEECH id to play, or -1.
    int32 update(const Frame& f);

    [[nodiscard]] int32 count(Stat s) const noexcept { return counts_[s]; }
    // Milestone byte i (0..13 = 0x7b7e34..0x7b7e41) as the exe compares it (signed: 0xff -> -1).
    [[nodiscard]] int32 milestone(int32 i) const noexcept { return ms_[static_cast<std::size_t>(i)]; }
    void setMilestone(int32 i, int32 v) noexcept { ms_[static_cast<std::size_t>(i)] = static_cast<int8>(v); }

    // The categories as 0x5b6b80 / 0x5b6c30 / 0x5b6cd0 / 0x5b6bf0 compute them (with their side
    // effects on the milestone bytes); 0 = none.
    int32 category(Stat s);
    // The AISPEECH id for category `cat` of `s` in `scene`, or -1 (X / H / KT categories 2, 3).
    [[nodiscard]] static int32 speechId(Stat s, int32 cat, int32 scene);

    // exe random helpers: 0x493b90 (an int in lo..hi, the ends half as likely) and 0x493c70.
    static int32 randomInt(int32 lo, int32 hi, std::mt19937& rng);
    static float32 randomFloat(float32 lo, float32 hi, std::mt19937& rng);

private:
    int32 handler(Stat s, const Frame& f, bool& talking);
    void hidingEnded();

    int32 scene_ = -1;
    std::array<int32, kStats> counts_{};
    std::array<int8, 14> ms_{};        // X 0..2, KT 3..5, H 6..8, S 9..13
    std::array<std::array<bool, 6>, kStats> played_{}; // per category (0x34 / 0x4c / 0x64 / 0x7c)
    bool gruesome_ = false;            // 0x7b7e2c
    bool executionHit_ = false;        // 0x7b7e30: an X milestone was just reached
    float32 time_ = 0.0f;
    float32 sDelay_ = -1.0f;           // the S line's 3 s wait (+0x94), < 0: not running
    // OnPlayerSpotted (0x541290): on the first frame he is seen, unless seen less than 10 s ago.
    bool wasSeen_ = false;
    float32 spottedGrace_ = -1.0f;
    // Hiding (0x49a98a..0x49aa37): open while he stays in a safe zone unseen.
    bool wasHidden_ = false;
    bool hideOpen_ = false;            // 0x727fac == 0
    float32 hideStart_ = 0.0f;         // 0x7b7e24
};

} // namespace Game
