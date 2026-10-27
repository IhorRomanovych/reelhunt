// Audio/audio.h - threaded, priority-based OpenAL audio engine.
//
// 32 sources with priority-based allocation (a request may steal a lower-priority source). A
// worker thread polls source states and frees finished one-shots. A separate streaming source
// plays cutscene audio fed in chunks.
#pragma once
#include "engine_types.h"
#include "Math/vector.h"
#include <al.h>
#include <alc.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace Audio {

enum class SampleFormat : uint8 {
    Mono8,   // unsigned 8-bit
    Mono16,  // signed 16-bit little-endian (the SFX bank)
    Stereo16, // interleaved left/right signed 16-bit (plays 2D: OpenAL does not place stereo)
};

// The two volume buses of the original's audio settings (Game/settings.h): music, and the
// effects (everything else, the videos included).
enum class Bus : uint8 { Effects = 0, Music = 1 };

struct SoundRequest {
    const void* pcm = nullptr;
    size_t lengthBytes = 0u;
    SampleFormat format = SampleFormat::Mono16;
    uint32 sampleRate = 44100u;
    float priority = 0.5f;         // 0.0 (ambient) .. 1.0 (player action)
    bool loop = false;
    Math::Vector3 position{0.0f, 0.0f, 0.0f}; // world space (for 3D attenuation)
    float volume = 1.0f;
    bool positional = true;        // false: plays as global/2D (listener-relative, no attenuation)
    // A 2D sound's direction (degrees clockwise from ahead: 90 right, 270 left); < 0 centred.
    float pan = -1.0f;
    // Positional sounds fade linearly from full at 2 m to silence at this distance (metres).
    float maxDistance = 30.0f;
    Bus bus = Bus::Effects;
};

class AudioEngine {
public:
    static constexpr uint32 kMaxSources = 32u;
    static constexpr uint32 kNoChannel = UINT32_MAX;

    AudioEngine() = default;
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    [[nodiscard]] Engine::EngineError init() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] bool isInitialized() const noexcept { return initialized_.load(); }

    // Play a sound. Returns the channel (source index) or kNoChannel if it was dropped.
    [[nodiscard]] uint32 play(const SoundRequest& req) noexcept;

    void stop(uint32 channel) noexcept;
    void stopAll() noexcept;
    // Hold every playing sound (and the cut-scene stream) where it is, or let them go on: the
    // in-game menu.
    void setPaused(bool paused) noexcept;
    // A playing sound's own volume (0..1, under the master volume): the music's stems.
    void setChannelVolume(uint32 channel, float32 v) noexcept;
    // A playing positional sound's place (world space): the music's fire.
    void setChannelPosition(uint32 channel, const Math::Vector3& position) noexcept;

    // Master volume (0..1), applied on top of each sound's own volume.
    void setVolume(float32 v) noexcept;
    // A bus's volume (0..1), applied with the master volume (SFX / MUSIC VOLUME).
    void setBusVolume(Bus bus, float32 v) noexcept;
    [[nodiscard]] float32 volume() const noexcept { return masterGain_.load(); }

    // Listener position/orientation for 3D audio.
    void updateListener(const Math::Vector3& pos, const Math::Vector3& forward, const Math::Vector3& up) noexcept;

    // Gapless cutscene streaming on a dedicated source (isolated from the game sources). Chunks
    // are interleaved float-32 PCM, converted to int16 with `gain` applied.
    void cutsceneStart(uint32 channels, uint32 sampleRate, float gain) noexcept;
    void cutsceneFeed(const float* interleavedPcm, size_t numFrames) noexcept;
    void cutsceneStop() noexcept;

private:
    uint32 allocateSource(float requestedPriority) noexcept; // requires mutex_
    void workerLoop() noexcept;

    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;

    // Game sources. Everything below is guarded by mutex_ (the worker thread touches it too).
    std::mutex mutex_;
    std::array<ALuint, kMaxSources> sources_{};
    std::array<ALuint, kMaxSources> buffers_{};   // the buffer attached to each source (0 = none)
    std::array<float, kMaxSources> sourcePriority_{};
    std::array<float, kMaxSources> sourceVolume_{};
    std::array<Bus, kMaxSources> sourceBus_{};
    std::array<std::atomic<float>, 2> busGain_{1.0f, 1.0f};
    [[nodiscard]] float gainOf(uint32 ch) const noexcept
    {
        return masterGain_.load() * busGain_[static_cast<std::size_t>(sourceBus_[ch])].load() * sourceVolume_[ch];
    }

    // Cutscene stream (main thread only).
    ALuint csSource_ = 0u;
    std::vector<ALuint> csLiveBufs_;  // queued/playing buffers; deleted on stop
    std::vector<int16_t> csScratch_;
    uint32 csChannels_ = 0u;
    uint32 csRate_ = 0u;
    float csGain_ = 1.0f;

    std::atomic<float> masterGain_{1.0f};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace Audio
