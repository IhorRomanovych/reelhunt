// Game/hunter_speech.cpp - see hunter_speech.h.
#include "Game/hunter_speech.h"

#include <fstream>
#include <iterator>
#include <sstream>

namespace Game {

namespace {

// "SFX_H1V1_TAUNT_CHASE_3" -> "SFX_H1V1_TAUNT_CHASE".
std::string stemOf(const std::string& sample)
{
    const std::size_t u = sample.find_last_of('_');
    if (u == std::string::npos || u + 1 >= sample.size())
        return sample;
    for (std::size_t i = u + 1; i < sample.size(); ++i)
        if (sample[i] < '0' || sample[i] > '9')
            return sample;
    return sample.substr(0, u);
}

// A hunter's own voice line ("SFX_<letter><n>V<n>_..."): the ones the 60 s rule covers (the exe
// checks the vocal sample numbers 0x2d7 .. 0x1ac4).
bool vocal(const std::string& s)
{
    return s.size() > 9 && s.compare(0, 4, "SFX_") == 0 && s[4] >= 'A' && s[4] <= 'Z' && s[5] >= '0' && s[5] <= '9' &&
           s[6] == 'V' && s[7] >= '0' && s[7] <= '9' && s[8] == '_';
}

} // namespace

bool HunterSpeech::load(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream ss(line);
        std::string kind;
        ss >> kind;
        if (kind == "voice")
        {
            int32 v = -1;
            std::string prefix;
            if (ss >> v >> prefix)
                voices_[prefix] = v;
        }
        else if (kind == "line")
        {
            int32 v = -1, slot = -1;
            ss >> v >> slot;
            Lines l;
            for (std::string s; ss >> s;)
                l.samples.push_back(s);
            if (v < 0 || slot < 0 || l.samples.empty())
                continue;
            const int32 key = v * kContexts + slot;
            stems_.emplace(stemOf(l.samples.front()), key); // (a stem two slots share: the first)
            lines_[key] = std::move(l);
        }
        else if (kind == "specific")
        {
            int32 id = -1, n = -1;
            if (ss >> id >> n)
                specific_[id] = n;
        }
    }
    return !lines_.empty();
}

int32 HunterSpeech::voiceOf(const std::string& prefix) const
{
    const auto it = voices_.find(prefix);
    return it == voices_.end() ? -1 : it->second;
}

int32 HunterSpeech::specificSlot(int32 id) const
{
    const auto it = specific_.find(id);
    return it == specific_.end() ? -1 : kSpecificSlot + it->second;
}

int32 HunterSpeech::lineSlot(int32 line) noexcept
{
    // 0x527a50: the line -> its speech context (0x25 .. 0x5f); 0..7, 0x2e, 0x32: none.
    static constexpr int16 kContext[0x43] = {
        0,    0,    0,    0,    0,    0,    0,    0,    0x4e, 0x25, 0x28, 0x27, 0x26, 0x29, 0x2a, 0x2b, // 0x00
        0x2c, 0x2d, 0x2e, 0x2f, 0x56, 0x30, 0x31, 0x34, 0x33, 0x35, 0x36, 0x3d, 0x3e, 0x3f, 0x40, 0x41, // 0x10
        0x50, 0x4f, 0x37, 0x38, 0x39, 0x3a, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0,    0x4a, // 0x20
        0x4c, 0x49, 0,    0x4d, 0x51, 0x3b, 0x32, 0x3c, 0x54, 0x55, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, // 0x30
        0x5e, 0x5f, 0x57};
    if (line < 0 || line >= static_cast<int32>(std::size(kContext)) || kContext[line] == 0)
        return -1;
    return kContext[line] - 0x25;
}

bool HunterSpeech::find(const std::string& stem, int32& voice, int32& slot) const
{
    const auto it = stems_.find(stem);
    if (it == stems_.end())
        return false;
    voice = it->second / kContexts;
    slot = it->second % kContexts;
    return true;
}

std::optional<std::string> HunterSpeech::pick(int32 voice, int32 slot, float32 now, std::mt19937& rng)
{
    const auto it = lines_.find(voice * kContexts + slot);
    if (it == lines_.end())
        return std::nullopt;
    Lines& l = it->second;
    const std::size_t n = l.samples.size();
    std::size_t chosen = 0;
    if (n > 16)
        chosen = std::uniform_int_distribution<std::size_t>(0, n - 1)(rng);
    else
    {
        // One not said yet (0x44cf00); the last of them clears the mask, then counts as said.
        std::size_t unsaid = 0;
        for (std::size_t i = 0; i < n; ++i)
            unsaid += (l.said >> i & 1u) ? 0 : 1;
        if (unsaid == 0)
        {
            l.said = 0;
            unsaid = n;
        }
        std::size_t r = std::uniform_int_distribution<std::size_t>(0, unsaid - 1)(rng);
        for (chosen = 0; chosen < n; ++chosen)
            if (!(l.said >> chosen & 1u) && r-- == 0)
                break;
        if (unsaid == 1)
            l.said = 0;
        if (n > 1)
            l.said = static_cast<uint16>(l.said | (1u << chosen));
    }
    const std::string& sample = l.samples[chosen];
    if (vocal(sample))
    {
        // Said by anyone within the last 60 s: nothing now (the ten slots at 0x8202f0).
        for (const Recent& r : recent_)
            if (r.sample == sample && now < r.until)
                return std::nullopt;
        recent_[nextRecent_] = {sample, now + kRepeatGap};
        nextRecent_ = (nextRecent_ + 1) % recent_.size();
    }
    return sample;
}

} // namespace Game
