// Game/level_music.h - a level's music, mixed by danger as the exe does (0x5a4b80 and below).
//
// audio/PC/MUSIC/<LEVEL>/<NAME>_M.RIB (stream 1 + the level index, table 0x6abae0) is one stream
// of 12 mono IMA ADPCM channels, interleaved in blocks of 0x10000 bytes per channel, 0x200-byte
// chunks (each chunk's predictor continues the last, and across blocks only 12 channels line up),
// at 22050 Hz (the exe's stream voices 0..11; 12 and up, the executions', are 44100: 0x458050).
// Channels 0..7 are four stereo stems (even left, odd right: pans 270 / 90).
//
// The exe keeps one stem up at volume 50 (of 127) by the danger it finds each frame:
//   0 calm                stem 0  (0x5a22a0(1))
//   1 a hunter after him  stem 1  (the player's nearest hunting hunter this frame; 0x5a2380)
//   2 he is spotted       stem 2  (0x799f74; 0x5a2450)
//   3 spotted, within 4 m stem 3  (0x5a2520)
// A state holds for 90 frames after its cause stops (0x7aa870 / 0x7aa874), and the stems
// crossfade in 10 frames of 33.3 ms (3: 0.33 s) going up, 150 coming down (0: 50 from a calm
// start). Ours: "after him" is a hunter hunting or investigating, "spotted" one chasing or
// fighting him.
//
// Channels 8..11 are ambience, only in normal play (0x91c), by the level (Game/music_ambience.h):
//   9      a fire's crackle at the nearest of up to 40 sources within 16 m (0x5a3690): the
//          scripts' CreateMHFX FLM001 / fxwater (0x57dc13) and CreateEffect of a slowburn copy
//          (0x47b440) register them (0x5a5b50). Placed there, at 90 x a cosine falloff (full to
//          1.6 m, none at 16; 0x452e70) x whether the camera hears it (a line test 0x4df3b0,
//          every 10 frames), moving at 0.38 a millisecond. Pharm_Wks' lift, Asylum's electric
//          chair, Estate_Ext's fires (with 8) instead.
//   10, 11 the rain (0x5a2df0), a stereo pair at 127 x the rain (0x7b3254: RAINY or
//          THUNDERSTORM weather, less the shelter above him and the camera; 0x5afa10), moving
//          over 500 ms. Subway's and Estate_Ext's beds instead, or two placed voices: Scrap2's
//          crane, Asylum's chair, Journo_Streets' generator.
//
// Two more streams of two channels each:
//   _D (stream 0x19 + the level) when he dies (0x5a4b80, then 0x5a59b0 / 0x5a25d0(1)): voices 14
//      and 15 (44100 Hz, 0x400-byte chunks), once, rising to 45 over 1 s while the stems fall to
//      nothing over 1 s and the ambience over 3 s.
//   _L (stream 0x31 + the level) over the scene's results (0x5a5a50, game state 5): voices 0 and
//      1 (22050 Hz, 0x200-byte chunks), once, at 50. Continuing stops it (0x5a14a0).
// The exe keeps each stream's position when it stops (0x5a1300 -> +0xdc) to start there again
// (0x5a1210), but 0x45a030 opens it at that position modulo one read (channels x 0x10000), and a
// position is always a whole number of reads: every stream starts from the top, as here.
#pragma once
#include "engine_types.h"

#include <array>
#include <string>
#include <vector>

#include "Audio/audio.h"
#include "Game/music_ambience.h"
#include "Math/vector.h"

namespace Game {

class LevelMusic {
public:
    // The music of the level with this folder name (levels.txt order); false if it has none.
    bool load(const std::string& gameRoot, const std::string& levelName);
    // Starts the four stems and the ambience looping, silent but the calm stem.
    void start(Audio::AudioEngine& audio);
    void stop(Audio::AudioEngine& audio);
    // One frame: the danger now (`hunted`: a hunter after him; `spotted`; `nearest`: metres to
    // the nearest hunter after him) and the ambience channels' wishes (LevelRuntime::musicAmbience).
    void update(Audio::AudioEngine& audio, float32 dt, bool hunted, bool spotted, float32 nearest,
                const MusicAmbience::Frame& ambience);
    // He died: the _D stream, the rest fading out. The stems no longer follow the danger.
    void playerDied(Audio::AudioEngine& audio);
    // The scene was won: the _L stream once (the rest is already stopped).
    void playLevelEnd(Audio::AudioEngine& audio);

    [[nodiscard]] int32 state() const noexcept { return state_; }
    // The stream path for a level and stream kind ('M', 'D', 'L') (testing).
    [[nodiscard]] static std::string path(const std::string& levelName, char kind = 'M');
    // The level's number (levels.txt order, 1 up), 0 if unknown.
    [[nodiscard]] static int32 levelNumber(const std::string& levelName);

private:
    // Channels 8, 9, then 10 / 11 as the stereo pair (kPair) or two placed voices (kPair, k11).
    enum Voice : std::size_t { kCalm, kHunted, kSpotted, kClose, k8, k9, kPair, k11, kDeath, kLevelEnd, kVoices };
    struct Track {
        std::vector<int16_t> pcm;
        bool stereo = true;
        uint32 sampleRate = 22050;
        uint32 channel = Audio::AudioEngine::kNoChannel;
        float32 volume = 0.0f, target = 0.0f, speed = 0.0f; // 0..127, per second
    };
    void play(Audio::AudioEngine& audio, Track& t, bool loop, float32 volume, bool positional = false);
    void fadeTo(Track& t, float32 target, float32 seconds);

    std::array<Track, kVoices> tracks_;
    bool placedPair_ = false; // channels 10 / 11 played at a place (MusicAmbience::placedPair)
    int32 state_ = -1;
    float32 huntedFor_ = 0.0f, spottedFor_ = 0.0f; // seconds the state still holds
    bool dead_ = false;
};

} // namespace Game
