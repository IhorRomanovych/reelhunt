// Game/cutscene_player.cpp - Bink cutscene playback (see cutscene_player.h).
#include "Game/cutscene_player.h"

#include "Assets/binary_reader.h"
#include "Audio/audio.h"
#include "Renderer/renderer.h"

#include <iostream>
#include <span>

namespace Game {

namespace {

constexpr std::size_t kVideoQueueCap = 8u;  // ~0.27 s of frames at 30 fps
constexpr std::size_t kAudioQueueCap = 32u; // ~1 s of audio
// Never queue more than this much audio ahead of the picture: if the video lags, the audio
// underruns and catches down, keeping A/V in lockstep.
constexpr double kAudioLookahead = 0.3;
// Peak normalisation measured on asylum.bik (its raw decoder output peaks around 585).
constexpr float kAudioGain = 0.6f / 584.7f;

} // namespace

CutscenePlayer::~CutscenePlayer()
{
    joinThreads();
}

bool CutscenePlayer::decodeVideoFrame(int index, Rgba& out)
{
    const std::uint8_t* data = nullptr;
    int size = 0;
    if (!container_.frame(index, &data, &size))
        return false;
    Codec::BinkFrame frame;
    if (!video_.decode(data, size, frame))
        return false;
    out.resize(static_cast<std::size_t>(frame.width) * frame.height * 4u);
    Codec::yuv420p_to_rgba(frame, out.data());
    return true;
}

bool CutscenePlayer::open(const std::string& path, Renderer::Renderer& renderer)
{
    auto bytes = Assets::readFileBytes(path);
    if (!bytes)
    {
        std::cerr << "cutscene: " << bytes.error().message << "\n";
        return false;
    }
    file_ = std::move(*bytes);
    if (!container_.open(file_.data(), file_.size()))
    {
        std::cerr << "cutscene: not a supported Bink file: " << path << "\n";
        return false;
    }
    video_.configure(container_.width(), container_.height(), container_.version(), container_.flags());
    if (!frameTex_.create(static_cast<uint32>(container_.width()), static_cast<uint32>(container_.height())))
        return false;

    // Frame 0 is a keyframe: decode it now so the picture is up at t = 0, and upload it to warm the
    // overlay path (the first upload allocates the GPU image).
    Rgba first;
    if (!decodeVideoFrame(0, first))
    {
        std::cerr << "cutscene: frame 0 failed to decode\n";
        return false;
    }
    frameTex_.setPixelsRGBA8(first);
    renderer.showVideo(frameTex_);
    videoQ_.push_back(std::move(first));
    videoHead_ = 0;

    hasAudio_ = container_.hasAudio() && audioDec_.configure(container_.audioSampleRate(), container_.audioFlags());
    if (hasAudio_)
    {
        const std::uint8_t* a = nullptr;
        int asz = 0;
        std::vector<float> prime; // frame 0's packet holds ~0.45 s of audio
        if (container_.audioFrame(0, &a, &asz) && audioDec_.decode(a, asz, prime) && !prime.empty())
            audioQ_.push_back(std::move(prime));
    }

    opened_ = true;
    finished_ = false;
    std::cerr << "cutscene: " << path << " " << container_.width() << "x" << container_.height()
              << " frames=" << container_.numFrames() << " fps=" << container_.fps()
              << " audio=" << (hasAudio_ ? "yes" : "no") << "\n";
    return true;
}

void CutscenePlayer::startThreads()
{
    stopThreads_.store(false);
    const int frames = container_.numFrames();

    // Video decode runs whether or not the file has an audio track.
    videoThread_ = std::thread([this, frames]() {
        Rgba px;
        for (int f = 1; f < frames && !stopThreads_.load(); ++f)
        {
            if (!decodeVideoFrame(f, px))
                px.clear(); // keep the index aligned: an empty entry repeats the previous picture
            std::unique_lock<std::mutex> lk(videoMutex_);
            videoCv_.wait(lk, [&] { return videoQ_.size() < kVideoQueueCap || stopThreads_.load(); });
            if (stopThreads_.load())
                break;
            videoQ_.push_back(std::move(px));
            px = Rgba{};
        }
    });

    if (hasAudio_)
        audioThread_ = std::thread([this, frames]() {
            for (int f = 1; f < frames && !stopThreads_.load(); ++f)
            {
                const std::uint8_t* a = nullptr;
                int asz = 0;
                std::vector<float> chunk;
                if (!container_.audioFrame(f, &a, &asz) || !audioDec_.decode(a, asz, chunk) || chunk.empty())
                    continue;
                std::unique_lock<std::mutex> lk(audioMutex_);
                audioCv_.wait(lk, [&] { return audioQ_.size() < kAudioQueueCap || stopThreads_.load(); });
                if (stopThreads_.load())
                    break;
                audioQ_.push_back(std::move(chunk));
            }
        });
}

void CutscenePlayer::joinThreads()
{
    stopThreads_.store(true);
    videoCv_.notify_all();
    audioCv_.notify_all();
    if (videoThread_.joinable())
        videoThread_.join();
    if (audioThread_.joinable())
        audioThread_.join();
}

void CutscenePlayer::feedAudio(Audio::AudioEngine& audio)
{
    std::vector<std::vector<float>> ready;
    {
        std::lock_guard<std::mutex> lk(audioMutex_);
        while (!audioQ_.empty() && audioFedSeconds_ < elapsed_ + kAudioLookahead)
        {
            const auto& c = audioQ_.front();
            audioFedSeconds_ += static_cast<double>(c.size()) / audioDec_.channels() / audioDec_.sampleRate();
            ready.push_back(std::move(audioQ_.front()));
            audioQ_.pop_front();
        }
    }
    audioCv_.notify_one(); // room in the bounded queue
    for (const auto& c : ready)
        audio.cutsceneFeed(c.data(), c.size() / static_cast<std::size_t>(audioDec_.channels()));
}

bool CutscenePlayer::update(float32 dt, Renderer::Renderer& renderer, Audio::AudioEngine& audio)
{
    if (!isActive())
        return false;

    if (!started_)
    {
        started_ = true;
        elapsed_ = 0.0;
        audioFedSeconds_ = 0.0;
        if (hasAudio_)
            audio.cutsceneStart(static_cast<uint32>(audioDec_.channels()), static_cast<uint32>(audioDec_.sampleRate()), kAudioGain);
        startThreads();
    }
    else
    {
        elapsed_ += dt;
    }

    if (hasAudio_)
        feedAudio(audio);

    const double position = elapsed_ * container_.fps(); // playback position in frames
    if (position >= container_.numFrames())
    {
        stop(renderer, audio);
        return false;
    }

    // Drop frames we have already passed; upload the target frame when it is ready, otherwise
    // hold the last picture instead of stalling the loop on a slow (key)frame decode.
    const int target = static_cast<int>(position);
    Rgba px;
    {
        std::lock_guard<std::mutex> lk(videoMutex_);
        while (videoHead_ < target && videoQ_.size() > 1u)
        {
            videoQ_.pop_front();
            ++videoHead_;
        }
        if (videoHead_ == target && !videoQ_.empty())
        {
            px = std::move(videoQ_.front());
            videoQ_.pop_front();
            ++videoHead_;
        }
    }
    videoCv_.notify_one();
    if (!px.empty() && frameTex_.setPixelsRGBA8(px))
        renderer.showVideo(frameTex_);
    return true;
}

void CutscenePlayer::stop(Renderer::Renderer& renderer, Audio::AudioEngine& audio)
{
    joinThreads();
    if (opened_ && !finished_)
    {
        renderer.hideVideo();
        audio.cutsceneStop();
    }
    finished_ = true;
}

} // namespace Game
