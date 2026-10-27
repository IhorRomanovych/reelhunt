// Game/director_speech.cpp - director speech lines. See director_speech.h.
#include "Game/director_speech.h"

#include <string_view>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Game {

bool DirectorSpeech::load(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        return false;
    lines_.clear();
    parts_.clear();
    subtitles_.clear();
    std::string raw;
    while (std::getline(in, raw))
    {
        if (raw.empty() || raw[0] == '#')
            continue;
        std::istringstream l(raw);
        std::string kind;
        l >> kind;
        if (kind == "line")
        {
            int32 id = -1;
            Range r;
            if (l >> id >> r.first >> r.last)
                lines_[id] = r;
        }
        else if (kind == "parts")
        {
            std::string sample;
            int32 n = 1;
            if (l >> sample >> n)
                parts_[sample] = n;
        }
        else if (kind == "subtitle")
        {
            int32 id = -1;
            std::string key;
            if (l >> id >> key)
                subtitles_[id] = key;
        }
    }
    return !lines_.empty();
}

std::string DirectorSpeech::subtitleKey(int32 id, std::size_t part) const
{
    const auto it = subtitles_.find(id);
    return it == subtitles_.end() ? std::string() : it->second + "A" + std::to_string(part + 1);
}

std::vector<std::string> DirectorSpeech::samples(int32 id, const std::function<std::size_t(const std::string&)>& indexOf,
                                                 const std::function<std::string(std::size_t)>& nameAt,
                                                 std::mt19937& rng) const
{
    const auto it = lines_.find(id);
    if (it == lines_.end())
        return {};
    const std::size_t first = indexOf(it->second.first), last = indexOf(it->second.last);
    if (first == static_cast<std::size_t>(-1) || last == static_cast<std::size_t>(-1) || last < first)
        return {};
    auto partsOf = [&](std::size_t s) {
        const auto p = parts_.find(nameAt(s));
        return p == parts_.end() ? 1 : std::max(1, p->second);
    };
    // The lines in the range (each starts where the previous one's parts end); pick one.
    std::vector<std::size_t> starts;
    // A lone <NAME>_RANDOM marker (SFX_D1V1_SCRIPTED_EXEC_RANDOM, a placeholder sample after
    // EXEC1..EXEC20) stands for one of the numbered <NAME><n> lines before it (our reading).
    const std::string single = nameAt(first);
    constexpr std::string_view kRandom = "_RANDOM";
    if (first == last && single.size() > kRandom.size() && single.ends_with(kRandom))
    {
        const std::string prefix = single.substr(0, single.size() - kRandom.size());
        for (std::size_t s = first; s-- > 0;)
        {
            const std::string n = nameAt(s);
            if (n.size() <= prefix.size() || n.compare(0, prefix.size(), prefix) != 0 ||
                n.find_first_not_of("0123456789", prefix.size()) != std::string::npos)
                break;
            starts.push_back(s);
        }
    }
    if (starts.empty())
        for (std::size_t s = first; s <= last; s += static_cast<std::size_t>(partsOf(s)))
            starts.push_back(s);
    const std::size_t start = starts[std::uniform_int_distribution<std::size_t>(0, starts.size() - 1)(rng)];
    std::vector<std::string> out;
    for (int32 k = 0; k < partsOf(start); ++k)
        out.push_back(nameAt(start + static_cast<std::size_t>(k)));
    return out;
}

} // namespace Game
