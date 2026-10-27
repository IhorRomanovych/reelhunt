// Game/music_ambience.h - the level music's ambience channels 8..11 (exe 0x5a4b80's level
// handlers), by level.
//
// The level tick (0x5a4b80) runs one handler for channels 10 / 11 and one for channel 9 (and 8)
// by the level (levels.txt order, 1 up; Attic 20 and Weasel 24 run neither):
//   10 / 11  Scrap2 5 the crane (0x5a4000), Asylum 11 the electric chair (0x5a4500), Journo_Streets
//            15 the generator (0x5a3c30), Subway 16 a bed at 100 (0x5a3080), Estate_Ext 18 a bed
//            at 110 that a gunshot silences, back over 300 frames (0x5a32e0); the rest the rain
//            (0x5a2df0: 127 x the rain, over 500 ms).
//   9 (8)    Pharm_Wks 10 the lift (0x5a42c0), Asylum 11 the electric chair while it is off
//            (0x5a4940), Estate_Ext 18 the nearest fire within 30 m, and channel 8 within 15 m
//            (0x5a38f0); the rest a fire's crackle (0x5a3690: 90 within 16 m).
// The scripts start the machines with their script audio slots (PlayScriptAudioSlotOneShotFrom-
// Entity / FromPos, 0x5b39e0 / 0x5b3ba0): SFX_SCRIPT_SLOT_GENERATOR_START / _STOP (0x5a6080 /
// 0x5a6150), CRANE_START / _STOP (0x5a6240 / 0x5a6310), LIFT_START / _STOP (0x5a6400 / 0x5a6490)
// at the entity (followed) or the place, and TOGGLE_ELECTRO turns the chair over (0x5a6550 /
// 0x5a65c0; the chair stands at (134.5, 0.42, -110.69), 0x5a0ee7). A slot played with volume or
// range -1 takes the sample's own (0x447320: the generator 90 / 30 m, the crane 127 / 30, the
// lift 100 / 55, the chair 63 / 12).
// A sound "heard" is a line test from it to the camera (0x4df3b0), every 10 frames at a phase
// of its own. The positions 0x6c2aa0..0x6c2aac the beds and the rain subtract are never
// written (always 0).
#pragma once
#include "engine_types.h"

#include <array>
#include <functional>
#include <vector>

#include "Math/vector.h"

namespace Game {

class MusicAmbience {
public:
    // One channel's wish this frame.
    struct Channel {
        bool set = false;        // a new volume (else it keeps going where it was)
        int32 volume = 0;        // 0..127
        float32 seconds = 0.0f;  // to get there from where it is, or
        float32 speed = 0.0f;    // volume a second (when > 0)
        bool placed = false;     // played at `position` this frame
        Math::Vector3 position{0.0f, 0.0f, 0.0f};
    };
    using Frame = std::array<Channel, 4>; // channels 8, 9, 10, 11

    // What the handlers measure.
    struct Source {
        Math::Vector3 position{0.0f, 0.0f, 0.0f};
        bool live = true; // its effect still burns (the default fire alone asks)
    };
    struct Inputs {
        uint32 frame = 0;                       // 0x6c2b40
        float32 now = 0.0f;                     // seconds
        Math::Vector3 player{0.0f, 0.0f, 0.0f}; // distances are his (0x447950)
        float32 rain = 0.0f;                    // 0..1 (0x7b3254, less the shelter)
        std::vector<Source> fires;              // up to 40 (0x5a5b50)
        std::function<float32(const Math::Vector3&)> heard;    // 0..1 (0x4df3b0)
        std::function<bool(uint32, Math::Vector3&)> entityAt;  // a followed entity's place
    };

    // The level (levels.txt order, 1 up); everything off (0x5a0e3d). `chair`: Asylum's electric
    // chair (kChair) in the scene frame.
    void reset(int32 level, const Math::Vector3& chair);
    static constexpr float32 kChair[3] = {134.5f, 0.42f, -110.69f}; // the script frame (0x5a0ee7)
    [[nodiscard]] int32 level() const noexcept { return level_; }
    // Channels 10 / 11 played at a place (mono) rather than as the stereo pair.
    [[nodiscard]] bool placedPair() const noexcept { return level_ == 5 || level_ == 11 || level_ == 15; }

    // A script audio slot played `sample` (an SFX bank index) with `volume` / `range` (-1: the
    // sample's own) from entity `entity` (0 none) or at `at` (scene frame).
    void slotPlayed(int32 sample, int32 volume, int32 range, uint32 entity, const Math::Vector3* at, float32 now);
    // A gun went off (0x5a6e70, from the weapons' fire sounds): Estate_Ext's bed falls silent.
    void gunFired() noexcept { hush_ = 300.0f; }

    // One frame (only in normal play); `dt` counts the hush down.
    [[nodiscard]] Frame update(const Inputs& in, float32 dt);

    // The sample's own volume and range for the script slots (0x447320), {-1, -1} if not set.
    [[nodiscard]] static std::array<int32, 2> slotDefaults(int32 sample);
    // 0x452e70: `volume` within a tenth of `range`, falling on a half cosine to nothing at it.
    [[nodiscard]] static int32 falloff(int32 volume, float32 range, float32 distance);

    static constexpr int32 kGeneratorStart = 0x36e6, kGeneratorStop = 0x36e7, kLiftStart = 0x36ec, kLiftStop = 0x36ed,
                           kToggleElectro = 0x36ee, kCraneStart = 0x36f4, kCraneStop = 0x36f5;

private:
    // One machine: on, followed entity or place (y -1000: none), volume, range, when it last
    // changed, the delay before the change is heard and the fade in frames.
    struct Machine {
        bool on = false;
        uint32 entity = 0;
        Math::Vector3 at{0.0f, -1000.0f, 0.0f};
        int32 volume = 40, range = 30;
        float32 since = 0.0f;
        int32 delay = 0, fade = 10;
        float32 heard = 0.0f;
    };
    bool where(const Machine& m, const Inputs& in, Math::Vector3& at) const;
    void fadeFrames(Channel& c, int32 target, int32& fade, int32 current);

    int32 level_ = 0;
    Machine generator_, crane_, lift_, electro_;
    float32 fireHeard_ = 0.0f; // 0x364
    float32 hush_ = 0.0f;      // 0xd8, frames
    std::array<int32, 4> target_{}; // the volumes last asked of 8..11 (0x78c, 0x7b4, 0x7dc, 0x804)
};

} // namespace Game
