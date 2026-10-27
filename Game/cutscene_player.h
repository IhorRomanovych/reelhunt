// Game/cutscene_player.h - Bink (.bik) cutscene playback: video + audio decoded on background
// threads, presented through the renderer's video overlay and the audio engine's stream source.
#pragma once
#include "engine_types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Codec/bink_audio.h"
#include "Codec/bink_container.h"
#include "Codec/bink_decoder.h"
#include "Renderer/texture.h"

namespace Audio { class AudioEngine; }
namespace Renderer { class Renderer; }

namespace Game {

class CutscenePlayer {
public:
    CutscenePlayer() = default;
    ~CutscenePlayer();
    CutscenePlayer(const CutscenePlayer&) = delete;
    CutscenePlayer& operator=(const CutscenePlayer&) = delete;

    // Load and validate the file, and decode frame 0 (video + audio) synchronously so the first
    // picture and sound are ready at t = 0. Shows frame 0 on the overlay, which also warms up
    // the GPU upload path during level load. Returns false if the file is unusable.
    bool open(const std::string& path, Renderer::Renderer& renderer);

    // Advance playback by dt seconds. The first call starts the audio stream and the decode
    // threads. Returns false once the cutscene has finished (or was never opened).
    bool update(float32 dt, Renderer::Renderer& renderer, Audio::AudioEngine& audio);

    // Stop early (skip) or clean up after the end: joins the threads, hides the overlay and
    // stops the audio stream. Safe to call at any time, including twice.
    void stop(Renderer::Renderer& renderer, Audio::AudioEngine& audio);

    [[nodiscard]] bool isActive() const noexcept { return opened_ && !finished_; }

private:
    using Rgba = std::vector<std::uint8_t>;

    bool decodeVideoFrame(int index, Rgba& out);
    void startThreads();
    void joinThreads();
    void feedAudio(Audio::AudioEngine& audio);

    std::vector<std::uint8_t> file_;
    Codec::BinkContainer container_;
    Codec::BinkDecoder video_;
    Codec::BinkAudioDecoder audioDec_;
    Renderer::Texture frameTex_;
    bool hasAudio_ = false;
    bool opened_ = false;
    bool started_ = false;
    bool finished_ = false;
    double elapsed_ = 0.0;        // playback clock (seconds since the first update)

    // Decoded video frames (RGBA8) in order; videoHead_ is the frame index of videoQ_.front().
    std::deque<Rgba> videoQ_;
    int videoHead_ = 0;
    std::mutex videoMutex_;
    std::condition_variable videoCv_;
    std::thread videoThread_;

    // Decoded audio chunks (interleaved float) in order.
    std::deque<std::vector<float>> audioQ_;
    std::mutex audioMutex_;
    std::condition_variable audioCv_;
    std::thread audioThread_;
    double audioFedSeconds_ = 0.0; // content seconds queued to the audio source

    std::atomic<bool> stopThreads_{false};
};

} // namespace Game
