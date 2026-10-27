// Game/level_music.cpp - a level's music. See level_music.h.
#include "Game/level_music.h"
#include "Core/platform.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>

#include "Assets/rib.h"

namespace Game {

namespace {

constexpr float32 kFrame = 1.0f / 30.0f;
constexpr float32 kHold = 90.0f * kFrame;   // a state outlives its cause by 90 frames
constexpr float32 kStemVolume = 50.0f;      // of 127
constexpr float32 kCloseMetres = 4.0f;      // metres (the double at 0x7a9ea0)
constexpr float32 kDeathVolume = 45.0f;     // 0x2d
constexpr float32 kDeathSeconds = 1.0f;     // 0x7a9e2c: the stems out, _D up
constexpr float32 kAmbienceOutSeconds = 3.0f; // 0x7a9e34
constexpr float32 kLevelEndVolume = 50.0f;

// Folder and file stem of each level's streams, by the levels.txt order (stream 1 + index).
struct StreamFiles {
    const char* level;
    const char* file;
};
constexpr StreamFiles kTracks[] = {
    {"jury_turf", "JURY_TUR/JURY_T"}, {"derelict", "DERELICT/DERELI"}, {"der2", "DERELIC2/DEREL2"},
    {"scrap", "SCRAP/SCRAP"},         {"scrap2", "SCRAP2/SCRAP2"},     {"zoo", "ZOO/ZOO"},
    {"zoo2", "ZOO2/ZOO2"},            {"mall", "MALL/MALL"},           {"church2", "CHURCH2/CHURC2"},
    {"pharm_wks", "PHARM_WK/PHARM"},  {"asylum", "ASYLUM/ASYLUM"},     {"cellblock", "CELLBLOC/CELLBL"},
    {"prison", "PRISON/PRISON"},      {"ramirez", "RAMIREZ/RAMIRE"},   {"journo_streets", "JOURNO_S/JOURNO"},
    {"subway", "SUBWAY/SUBWAY"},      {"trainyard", "TRAINYAR/TRAINY"}, {"estate_ext", "ESTATE_E/ESTATE"},
    {"mansion_int", "MANSION_/MANSIO"}, {"attic", "ATTIC/ATTIC"},      {"bonus1", "DERELIC2/DEREL2"},
    {"bonus3", "JURY_TUR/JURY_T"},    {"bonus2", "ZOO2/ZOO2"},         {"weasel", "WEASEL/WEASEL"},
};

std::string lowerCase(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream f(Engine::Platform::resolvePath(path), std::ios::binary);
    if (!f)
        return {};
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

} // namespace

std::string LevelMusic::path(const std::string& levelName, char kind)
{
    const std::string l = lowerCase(levelName);
    for (const StreamFiles& t : kTracks)
        if (l == t.level)
            return std::string("audio/PC/MUSIC/") + t.file + "_" + kind + ".RIB";
    return {};
}

int32 LevelMusic::levelNumber(const std::string& levelName)
{
    const std::string l = lowerCase(levelName);
    for (std::size_t i = 0; i < std::size(kTracks); ++i)
        if (l == kTracks[i].level)
            return static_cast<int32>(i + 1);
    return 0;
}

bool LevelMusic::load(const std::string& gameRoot, const std::string& levelName)
{
    const std::string rel = path(levelName);
    if (rel.empty())
        return false;
    const std::vector<uint8_t> bytes = readFile(gameRoot + rel);
    if (bytes.empty())
    {
        std::cerr << "music: cannot read " << rel << "\n";
        return false;
    }
    const Assets::RibAudio all = Assets::decodeRib(bytes, 12, 0x200, 22050);
    const std::size_t frames = all.samples.size() / 12;
    // Channels 0..7 the stems, 8 and 9 mono, 10 / 11 the pair or two mono voices.
    auto pick = [&](Track& t, std::size_t left, std::size_t right) {
        t.stereo = left != right;
        t.sampleRate = all.sampleRate;
        t.pcm.resize(frames * (t.stereo ? 2 : 1));
        for (std::size_t i = 0; i < frames; ++i)
        {
            if (t.stereo)
            {
                t.pcm[2 * i] = all.samples[i * 12 + left];
                t.pcm[2 * i + 1] = all.samples[i * 12 + right];
            }
            else
                t.pcm[i] = all.samples[i * 12 + left];
        }
    };
    for (std::size_t s = 0; s < 4; ++s)
        pick(tracks_[s], 2 * s, 2 * s + 1);
    pick(tracks_[k8], 8, 8);
    pick(tracks_[k9], 9, 9);
    MusicAmbience probe;
    probe.reset(levelNumber(levelName), {});
    placedPair_ = probe.placedPair();
    if (placedPair_)
    {
        pick(tracks_[kPair], 10, 10);
        pick(tracks_[k11], 11, 11);
    }
    else
    {
        pick(tracks_[kPair], 10, 11);
        tracks_[k11].pcm.clear();
    }
    // _D on the 44100 Hz voices (0x400 chunks, as the executions), _L on the 22050 Hz ones.
    auto stream = [&](Track& t, char kind, uint32 chunk, uint32 rate) {
        const std::vector<uint8_t> b = readFile(gameRoot + path(levelName, kind));
        if (b.empty())
            return;
        Assets::RibAudio a = Assets::decodeRib(b, 2, chunk, rate);
        t.pcm = std::move(a.samples);
        t.stereo = true;
        t.sampleRate = rate;
    };
    stream(tracks_[kDeath], 'D', 0x400, 44100);
    stream(tracks_[kLevelEnd], 'L', 0x200, 22050);
    std::cerr << "music: " << rel << " " << static_cast<float32>(frames) / static_cast<float32>(all.sampleRate)
              << " s, 4 stems, 8, 9, " << (placedPair_ ? "10, 11 placed" : "10 / 11") << "; _D " << static_cast<float32>(tracks_[kDeath].pcm.size() / 2) / 44100.0f << " s, _L "
              << static_cast<float32>(tracks_[kLevelEnd].pcm.size() / 2) / 22050.0f << " s\n";
    return frames > 0;
}

void LevelMusic::play(Audio::AudioEngine& audio, Track& t, bool loop, float32 volume, bool positional)
{
    if (t.pcm.empty())
        return;
    if (t.channel != Audio::AudioEngine::kNoChannel)
        audio.stop(t.channel);
    Audio::SoundRequest req;
    req.pcm = t.pcm.data();
    req.lengthBytes = t.pcm.size() * sizeof(int16_t);
    req.format = t.stereo ? Audio::SampleFormat::Stereo16 : Audio::SampleFormat::Mono16;
    req.sampleRate = t.sampleRate;
    req.priority = 1.0f; // never given up for an effect
    req.loop = loop;
    req.positional = positional;
    req.maxDistance = 1000.0f; // the handlers' own falloff sets the volume
    req.volume = volume / 127.0f;
    req.bus = Audio::Bus::Music;
    t.channel = audio.play(req);
    t.volume = t.target = volume;
    t.speed = 0.0f;
}

void LevelMusic::fadeTo(Track& t, float32 target, float32 seconds)
{
    t.target = target;
    t.speed = std::fabs(target - t.volume) / std::max(seconds, 1e-3f);
}

void LevelMusic::start(Audio::AudioEngine& audio)
{
    for (std::size_t s = 0; s < 4; ++s)
        play(audio, tracks_[s], true, 0.0f);
    play(audio, tracks_[k8], true, 0.0f, true);
    play(audio, tracks_[k9], true, 0.0f, true);
    play(audio, tracks_[kPair], true, 0.0f, placedPair_);
    if (placedPair_)
        play(audio, tracks_[k11], true, 0.0f, true);
    state_ = -1;
    dead_ = false;
    update(audio, 0.0f, false, false, 1e9f, MusicAmbience::Frame{});
}

void LevelMusic::stop(Audio::AudioEngine& audio)
{
    for (Track& t : tracks_)
        if (t.channel != Audio::AudioEngine::kNoChannel)
        {
            audio.stop(t.channel);
            t.channel = Audio::AudioEngine::kNoChannel;
        }
}

void LevelMusic::update(Audio::AudioEngine& audio, float32 dt, bool hunted, bool spotted, float32 nearest, const MusicAmbience::Frame& ambience)
{
    if (!dead_)
    {
        if (hunted)
            huntedFor_ = kHold;
        if (spotted)
            spottedFor_ = kHold;
        int32 state = 0;
        if (huntedFor_ > 0.0f)
            state = 1;
        if (spottedFor_ > 0.0f)
            state = nearest < kCloseMetres ? 3 : 2;
        huntedFor_ = std::max(0.0f, huntedFor_ - dt);
        spottedFor_ = std::max(0.0f, spottedFor_ - dt);
        if (state != state_)
        {
            // Up: 10 frames (stem 3: a third of a second); down: 150 (calm from the start: 50).
            float32 seconds = state > state_ ? 10.0f * kFrame : 150.0f * kFrame;
            if (state == 3)
                seconds = 1.0f / 3.0f;
            if (state == 0 && state_ < 1)
                seconds = 50.0f * kFrame;
            for (std::size_t s = 0; s < 4; ++s)
                fadeTo(tracks_[s], static_cast<int32>(s) == state ? kStemVolume : 0.0f, seconds);
            state_ = state;
        }

        // The ambience: channels 8, 9, 10 and 11 (the pair follows 10).
        const std::array<Track*, 4> amb{&tracks_[k8], &tracks_[k9], &tracks_[kPair], placedPair_ ? &tracks_[k11] : nullptr};
        for (std::size_t i = 0; i < amb.size(); ++i)
        {
            Track* t = amb[i];
            const MusicAmbience::Channel& c = ambience[i];
            if (!t)
                continue;
            if (c.set)
            {
                if (c.speed > 0.0f)
                {
                    t->target = static_cast<float32>(c.volume);
                    t->speed = c.speed;
                }
                else
                    fadeTo(*t, static_cast<float32>(c.volume), c.seconds);
            }
            if (c.placed && t->channel != Audio::AudioEngine::kNoChannel && (i < 2 || placedPair_))
                audio.setChannelPosition(t->channel, c.position);
        }
    }

    for (Track& t : tracks_)
    {
        const float32 step = t.speed * dt;
        t.volume = t.volume < t.target ? std::min(t.target, t.volume + step) : std::max(t.target, t.volume - step);
        if (t.channel != Audio::AudioEngine::kNoChannel)
            audio.setChannelVolume(t.channel, t.volume / 127.0f);
    }
}

void LevelMusic::playerDied(Audio::AudioEngine& audio)
{
    if (dead_)
        return;
    dead_ = true;
    for (std::size_t s = 0; s < 4; ++s)
        fadeTo(tracks_[s], 0.0f, kDeathSeconds);
    for (Voice v : {k8, k9, kPair, k11})
        fadeTo(tracks_[v], 0.0f, kAmbienceOutSeconds);
    play(audio, tracks_[kDeath], false, 0.0f);
    fadeTo(tracks_[kDeath], kDeathVolume, kDeathSeconds);
}

void LevelMusic::playLevelEnd(Audio::AudioEngine& audio)
{
    for (Track& t : tracks_)
        t.channel = Audio::AudioEngine::kNoChannel; // (stopped with everything else)
    play(audio, tracks_[kLevelEnd], false, kLevelEndVolume);
}

} // namespace Game
