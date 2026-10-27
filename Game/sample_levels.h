// Game/sample_levels.h - the audio manager's level and range for every bank sample.
//
// At start-up (0x446ea0) the audio manager gives each SFX bank sample (sfx.lst order) a level
// (0..127) and a range in metres (0x447410): all 127 / 40 (0x447080), then by range of samples -
// the collisions 1..77 100 / 60 with 38..39 30 / 30 (0x447150), the footsteps 78..117 25 / 20
// (0x447130), 118..135 (0x4470a0), 136..217 (0x447190), 222..300 (0x446ef0 / 0x447050) - every
// sample from 315 on (0x6c2b48: the voices, the player's cries, the scripts' slots) 80 / 40
// (0x4472f0), and last a few of the script slots and phones (0x447320). A sound played through the
// audio manager is heard at its level falling off over its range (0x452e70) through the world
// (0x4df3b0).
#pragma once
#include "engine_types.h"

namespace Game {

struct SampleLevel {
    int32 level = 127;
    float32 range = 40.0f;
};

// The sample's level and range (127 / 40 for an index outside the bank).
[[nodiscard]] SampleLevel sampleLevel(int32 index) noexcept;

} // namespace Game
