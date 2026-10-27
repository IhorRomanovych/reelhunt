// Game/entity_audio.h - the audio manager's entity sound events (exe 0x4529f0 / 0x4530f0).
//
// An entity the audio manager knows (0x4527e0, up to 200) posts sound events through
// 0x456150(entity, event, value). It keeps up to four a frame, ordered by the event's rank (table
// 0x6b52c6, lower first: a new event goes before the first queued one of a higher rank, and the
// fifth falls off the end). Once a frame (0x4530f0) each entity's events are played by its class
// (0x44b4d0: EC_DOOR 0x60001 by 0x4507b0, the slide doors 0x200001 by 0x450be0, ...), then its
// queue empties.
//
// The played sample is a bank index (sfx.lst order) with the volume (0..127) and range (metres)
// set for it at start-up (0x446ea0, Game/sample_levels.h). A swing door's event value scales the
// volume by value / 300, at most 1 (0x4507b0).
// The other classes' events are played where they happen: the peds' steps, landings, pain and
// head bursts (0xf..0x16, GameSession / LevelRuntime::hunterSteps; their 0x17, SFX_MISC_HEAD_POP
// by 0x44e140, is never posted), the player's heartbeat and breath (0x18..0x1a,
// Game/player_audio.h), the body he carries (0x23) and his cries (0x14, 0x49, 0x4b), and the
// weapons' (0xb..0xe, Game/weapon_audio.h).
#pragma once
#include "engine_types.h"

#include <cstdint>
#include <vector>

namespace Game {

class EntityAudio {
public:
    enum Event : uint16 {
        DoorPush = 0x1b,   // pushed from shut (0x4cc3e0): its energy x 4
        DoorLatch = 0x1c,  // swung shut and latched (0x4cc3a0): its energy x 4
        DoorCreak = 0x1f,  // past one of its creak angles (0x4cc4b0): plays nothing
        DoorLimit = 0x20,  // hit an open limit (0x4cc430): its energy
        SlideStart = 0x21, // a slide door starts to open or close (0x4cfcb0)
        SlideStop = 0x22,  // a slide door gets there
    };
    struct Posted {
        int32 entity = 0;
        uint16 event = 0;
        float32 value = 0.0f;
    };
    struct Sample {
        int32 id = -1;          // bank index, -1 none
        float32 volume = 1.0f;  // 0..1
        float32 range = 40.0f;  // metres
    };

    // 0x4529f0: queue `event` (< 0x61) for `entity`.
    void post(int32 entity, uint16 event, float32 value);
    // 0x4530f0: every entity's events this frame, entity by entity in the order they first
    // posted, each in its queue's order; the queues empty.
    [[nodiscard]] std::vector<Posted> drain();

    // 0x6b52c6: an event's rank (lower plays first).
    [[nodiscard]] static int32 rank(uint16 event);
    // 0x447410's table for the samples used here: (volume 0..127, range m).
    [[nodiscard]] static Sample level(int32 id);
    // A swing door's sample (0x4504c0 push, 0x450540 latch, 0x4506b0 limit) by its MATERIAL
    // index; `random` picks among alternatives.
    [[nodiscard]] static Sample swingDoor(uint16 event, int32 material, float32 value, uint32 random);
    // A slide door's (0x450be0).
    [[nodiscard]] static Sample slideDoor(uint16 event);

private:
    struct Queue {
        int32 entity = 0;
        std::vector<Posted> events; // at most four, by rank
    };
    std::vector<Queue> queues_;
};

} // namespace Game
