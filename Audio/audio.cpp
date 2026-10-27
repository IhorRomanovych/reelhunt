// Audio/audio.cpp - threaded, priority-based OpenAL audio engine.
#include "audio.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>

namespace Audio {

AudioEngine::~AudioEngine()
{
    shutdown();
}

Engine::EngineError AudioEngine::init() noexcept
{
    shutdown();

    device_ = alcOpenDevice(nullptr);
    if (!device_)
    {
        std::cerr << "[audio] failed to open OpenAL device\n";
        return Engine::EngineError::failure(Engine::EngineError::Code::Failed, "no audio device");
    }
    context_ = alcCreateContext(device_, nullptr);
    if (!context_)
    {
        alcCloseDevice(device_);
        device_ = nullptr;
        std::cerr << "[audio] failed to create OpenAL context\n";
        return Engine::EngineError::failure(Engine::EngineError::Code::Failed, "no audio context");
    }
    alcMakeContextCurrent(context_);
    // Linear fall-off clamped at each source's AL_MAX_DISTANCE: a sound beyond its range is
    // silent (the default inverse model never reaches zero, so every hunter's mumbling across
    // the level stayed faintly audible).
    alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);

    alGetError(); // clear
    alGenSources(kMaxSources, sources_.data());
    if (const ALenum err = alGetError(); err != AL_NO_ERROR)
    {
        std::cerr << "[audio] OpenAL init error 0x" << std::hex << err << std::dec << "\n";
        sources_.fill(0u);
        shutdown();
        return Engine::EngineError::failure(Engine::EngineError::Code::Failed, "OpenAL init error");
    }
    buffers_.fill(0u);
    sourcePriority_.fill(0.0f);
    sourceVolume_.fill(1.0f);

    initialized_.store(true);
    running_.store(true);
    worker_ = std::thread(&AudioEngine::workerLoop, this);

    std::cout << "[audio] OpenAL initialized (" << kMaxSources << " sources, threaded)\n";
    return Engine::okError;
}

void AudioEngine::shutdown() noexcept
{
    running_.store(false);
    if (worker_.joinable())
        worker_.join();

    if (context_)
    {
        // Delete everything while the context is still current (deleting after
        // alcMakeContextCurrent(nullptr) is a no-op and leaked the objects).
        cutsceneStop();
        std::lock_guard<std::mutex> lock(mutex_);
        for (ALuint s : sources_)
            if (s)
            {
                alSourceStop(s);
                alSourcei(s, AL_BUFFER, 0);
            }
        if (sources_[0])
            alDeleteSources(kMaxSources, sources_.data());
        for (ALuint b : buffers_)
            if (b)
                alDeleteBuffers(1, &b);
        sources_.fill(0u);
        buffers_.fill(0u);
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context_);
        context_ = nullptr;
    }
    if (device_)
    {
        alcCloseDevice(device_);
        device_ = nullptr;
    }
    initialized_.store(false);
}

uint32 AudioEngine::allocateSource(float requestedPriority) noexcept
{
    // First pass: a source that is not playing.
    for (uint32 i = 0; i < kMaxSources; ++i)
    {
        ALint state = 0;
        alGetSourcei(sources_[i], AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && state != AL_PAUSED)
            return i;
    }

    // Second pass: steal the lowest-priority source below the requester.
    uint32 best = kNoChannel;
    float bestPri = requestedPriority;
    for (uint32 i = 0; i < kMaxSources; ++i)
        if (sourcePriority_[i] < bestPri)
        {
            bestPri = sourcePriority_[i];
            best = i;
        }
    if (best == kNoChannel)
        std::cerr << "[audio] all sources busy, dropping sound (priority=" << requestedPriority << ")\n";
    return best;
}

uint32 AudioEngine::play(const SoundRequest& req) noexcept
{
    if (!initialized_.load() || !req.pcm || req.lengthBytes == 0u)
        return kNoChannel;

    std::lock_guard<std::mutex> lock(mutex_);
    const uint32 ch = allocateSource(req.priority);
    if (ch == kNoChannel)
        return kNoChannel;
    const ALuint src = sources_[ch];

    alGetError(); // clear any stale error so the check below reports this call
    // Detach the old buffer before deleting it: deleting a buffer still attached to a source fails
    // (AL_INVALID_OPERATION), which leaked it and made this play() report an error.
    alSourceStop(src);
    alSourcei(src, AL_BUFFER, 0);
    if (buffers_[ch])
        alDeleteBuffers(1, &buffers_[ch]);
    alGenBuffers(1, &buffers_[ch]);

    const ALenum format = req.format == SampleFormat::Mono8    ? AL_FORMAT_MONO8
                        : req.format == SampleFormat::Stereo16 ? AL_FORMAT_STEREO16
                                                               : AL_FORMAT_MONO16;
    alBufferData(buffers_[ch], format, req.pcm, static_cast<ALsizei>(req.lengthBytes),
                 static_cast<ALsizei>(req.sampleRate));
    alSourcei(src, AL_BUFFER, static_cast<ALint>(buffers_[ch]));
    alSourcei(src, AL_LOOPING, req.loop ? AL_TRUE : AL_FALSE);
    sourceVolume_[ch] = req.volume;
    sourceBus_[ch] = req.bus;
    alSourcef(src, AL_GAIN, gainOf(ch));
    if (req.positional)
    {
        alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
        alSourcef(src, AL_ROLLOFF_FACTOR, 1.0f);
        alSourcef(src, AL_REFERENCE_DISTANCE, 2.0f);
        alSourcef(src, AL_MAX_DISTANCE, std::max(2.5f, req.maxDistance));
        alSource3f(src, AL_POSITION, req.position.x, req.position.y, req.position.z);
    }
    else
    {
        // Listener-relative: at the listener (or a step off it toward its pan), no distance
        // attenuation.
        alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcef(src, AL_ROLLOFF_FACTOR, 0.0f);
        if (req.pan >= 0.0f)
        {
            const float a = req.pan * 3.14159265f / 180.0f;
            alSource3f(src, AL_POSITION, std::sin(a), 0.0f, -std::cos(a));
        }
        else
            alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
    }
    alSource3f(src, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcePlay(src);

    if (const ALenum err = alGetError(); err != AL_NO_ERROR)
    {
        std::cerr << "[audio] play error 0x" << std::hex << err << std::dec << "\n";
        sourcePriority_[ch] = 0.0f;
        return kNoChannel;
    }
    sourcePriority_[ch] = req.priority;
    sourceVolume_[ch] = req.volume;
    return ch;
}

void AudioEngine::stop(uint32 channel) noexcept
{
    if (!initialized_.load() || channel >= kMaxSources)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    alSourceStop(sources_[channel]);
    sourcePriority_[channel] = 0.0f;
}

void AudioEngine::setChannelVolume(uint32 channel, float32 v) noexcept
{
    if (!initialized_.load() || channel >= kMaxSources)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    sourceVolume_[channel] = v;
    alSourcef(sources_[channel], AL_GAIN, gainOf(channel));
}

void AudioEngine::setChannelPosition(uint32 channel, const Math::Vector3& position) noexcept
{
    if (!initialized_.load() || channel >= kMaxSources)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    alSource3f(sources_[channel], AL_POSITION, position.x, position.y, position.z);
}

void AudioEngine::stopAll() noexcept
{
    if (!initialized_.load())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32 i = 0; i < kMaxSources; ++i)
    {
        alSourceStop(sources_[i]);
        sourcePriority_[i] = 0.0f;
    }
}

void AudioEngine::setPaused(bool paused) noexcept
{
    if (!initialized_.load())
        return;
    auto apply = [paused](ALuint src) {
        ALint state = 0;
        alGetSourcei(src, AL_SOURCE_STATE, &state);
        if (paused && state == AL_PLAYING)
            alSourcePause(src);
        else if (!paused && state == AL_PAUSED)
            alSourcePlay(src);
    };
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32 i = 0; i < kMaxSources; ++i)
            apply(sources_[i]);
    }
    if (csSource_)
        apply(csSource_);
}

void AudioEngine::setVolume(float32 v) noexcept
{
    masterGain_.store(v);
    if (!initialized_.load())
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32 i = 0; i < kMaxSources; ++i)
            alSourcef(sources_[i], AL_GAIN, gainOf(i));
    }
    if (csSource_)
        alSourcef(csSource_, AL_GAIN, v * busGain_[0].load());
}

void AudioEngine::setBusVolume(Bus bus, float32 v) noexcept
{
    busGain_[static_cast<std::size_t>(bus)].store(v);
    if (!initialized_.load())
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32 i = 0; i < kMaxSources; ++i)
            if (sourceBus_[i] == bus)
                alSourcef(sources_[i], AL_GAIN, gainOf(i));
    }
    // Videos play at the effects volume (exe 0x4c0c60: BinkSetVolume from SFX VOLUME).
    if (csSource_ && bus == Bus::Effects)
        alSourcef(csSource_, AL_GAIN, masterGain_.load() * v);
}

void AudioEngine::updateListener(const Math::Vector3& pos, const Math::Vector3& forward, const Math::Vector3& up) noexcept
{
    if (!initialized_.load())
        return;
    alListener3f(AL_POSITION, pos.x, pos.y, pos.z);
    const ALfloat orient[6] = {forward.x, forward.y, forward.z, up.x, up.y, up.z};
    alListenerfv(AL_ORIENTATION, orient);
}

void AudioEngine::cutsceneStart(uint32 channels, uint32 sampleRate, float gain) noexcept
{
    if (!initialized_.load() || channels == 0u || sampleRate == 0u)
        return;

    cutsceneStop(); // replace any in-flight cutscene
    csChannels_ = channels;
    csRate_ = sampleRate;
    csGain_ = gain;

    alGenSources(1, &csSource_);
    alSourcei(csSource_, AL_LOOPING, AL_FALSE);
    alSourcef(csSource_, AL_GAIN, masterGain_.load() * busGain_[0].load());
    alSourcei(csSource_, AL_SOURCE_RELATIVE, AL_TRUE); // dialogue: at the listener, no attenuation
    alSourcef(csSource_, AL_ROLLOFF_FACTOR, 0.0f);
    alSource3f(csSource_, AL_POSITION, 0.0f, 0.0f, 0.0f);
}

void AudioEngine::cutsceneFeed(const float* interleavedPcm, size_t numFrames) noexcept
{
    if (!csSource_ || !interleavedPcm || numFrames == 0u)
        return;

    const size_t n = numFrames * csChannels_;
    csScratch_.resize(n);
    for (size_t i = 0; i < n; ++i)
        csScratch_[i] = static_cast<int16_t>(std::clamp(interleavedPcm[i] * csGain_, -1.0f, 1.0f) * 32767.0f);

    // Reclaim finished buffers so the in-flight set stays bounded.
    ALint processed = 0;
    alGetSourcei(csSource_, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0)
    {
        ALuint done = 0u;
        alSourceUnqueueBuffers(csSource_, 1, &done);
        alDeleteBuffers(1, &done);
        std::erase(csLiveBufs_, done);
    }

    ALuint b = 0u;
    alGenBuffers(1, &b);
    const ALenum format = (csChannels_ >= 2u) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    alBufferData(b, format, csScratch_.data(), static_cast<ALsizei>(n * sizeof(int16_t)), static_cast<ALsizei>(csRate_));
    alSourceQueueBuffers(csSource_, 1, &b);
    csLiveBufs_.push_back(b);

    ALint state = 0;
    alGetSourcei(csSource_, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) // first chunk, or the stream underran
        alSourcePlay(csSource_);
}

void AudioEngine::cutsceneStop() noexcept
{
    if (csSource_)
    {
        alSourceStop(csSource_);
        alSourcei(csSource_, AL_BUFFER, 0); // unqueues everything
        alDeleteSources(1, &csSource_);
        csSource_ = 0u;
    }
    for (ALuint b : csLiveBufs_)
        alDeleteBuffers(1, &b);
    csLiveBufs_.clear();
}

void AudioEngine::workerLoop() noexcept
{
    while (running_.load())
    {
        {
            // Free the priority slot of one-shots that finished on their own.
            std::lock_guard<std::mutex> lock(mutex_);
            for (uint32 i = 0; i < kMaxSources; ++i)
            {
                if (sourcePriority_[i] <= 0.0f)
                    continue;
                ALint state = 0;
                alGetSourcei(sources_[i], AL_SOURCE_STATE, &state);
                if (state == AL_STOPPED)
                    sourcePriority_[i] = 0.0f;
            }
        }
        // ~64 Hz poll. OpenAL Soft mixes on its own thread.
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

} // namespace Audio
