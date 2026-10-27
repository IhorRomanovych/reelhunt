// Game/weapon_audio.h - the audio manager's weapon sounds (exe 0x44efe0, the class 0x3001
// handler) by the weapon's collectable type (its type record +0x164).
//
// A weapon posts entity audio events (0x456150) and the handler plays samples for them:
//   0xb  used (0x4c9ac0): a gun's shot (value 0), the end of an automatic's burst (value 1,
//        0x4b6e50), or a melee swing (value 1 early in a combo blow, else 0) - 0x44f620;
//   0xc  the trigger pulled on an empty gun (0x4cb0d0) - 0x450330;
//   0xd  a reload (0x4503d0: SFX_WEAPON_RELOAD_METAL; no code posts it);
//   0xe  a spent shell landing (0x59aa10, its first two bounces) - 0x450430; its value scales the
//        level: 1 - 0.5 x the bounces before, times the struck surface's share (MaterialTypes).
// One event can play several samples (0x44f620 is called again while its state is not back to 0):
// a gun within 10 m of the camera (0x6b29d8 = 100, squared) is heard as its stereo pair - the
// _LEFT sample panned left (pan 270, 0x276 - 360) and the _RIGHT one right (90) - and further away
// as the _LEFT sample at its place. Guns with a tail first play the tail's pair, always panned, its
// level scaled by how open the camera's surroundings are on that side (Game/acoustic_probe.h).
// Each sample is heard at its level (Game/sample_levels.h) times the voice's share, falling off
// over its range from the camera (0x452e70) and through the world (0x4df3b0); a panned one at
// 0.707 of that (0x6b2ae4). Its rate is the sample's times the voice's, jittered by up to a
// sixteenth (0x453080) except for the empty click.
#pragma once
#include "engine_types.h"

#include <vector>

namespace Game::WeaponAudio {

struct Voice {
    int32 sample = -1;   // bank index (sfx.lst order)
    float32 rate = 1.0f; // times the sample's own rate
    int32 pan = -1;      // -1 at its place; else panned: 270 left, 90 right
    int32 tail = 0;      // a tail: -1 the left one, 1 the right one (its level x the side's openness)
};

// Event 0xb for a weapon of collectable type `ct`: what plays, in order. `value` is the event's
// (rounded), `near` whether the camera is within 10 m.
[[nodiscard]] std::vector<Voice> used(int32 ct, int32 value, bool near);
// Event 0xc: the empty click's sample, or -1.
[[nodiscard]] int32 empty(int32 ct);
// Event 0xe: the landing shell's sample, or -1.
[[nodiscard]] int32 shell(int32 ct);

} // namespace Game::WeaponAudio
