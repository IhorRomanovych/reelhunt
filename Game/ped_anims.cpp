// Game/ped_anims.cpp - see ped_anims.h.
#include "Game/ped_anims.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace Game {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

PedAnims& PedAnims::shared()
{
    static PedAnims table;
    return table;
}

bool PedAnims::load(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        return false;
    ids_.clear();
    behaviours_.clear();
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream ss(line);
        std::string kind;
        ss >> kind;
        if (kind == "clip")
        {
            int32 id = -1;
            std::string name;
            if (ss >> id >> name)
                ids_[lower(name)] = id; // (44 names are listed twice: the later id, its own behaviour's)
        }
        else if (kind == "behaviour")
        {
            int32 bt = -1;
            Behaviour b;
            if (ss >> bt >> b.first >> b.blend && bt == static_cast<int32>(behaviours_.size()))
                behaviours_.push_back(b);
        }
    }
    return !ids_.empty() && !behaviours_.empty();
}

int32 PedAnims::clipId(const std::string& clip) const
{
    const auto it = ids_.find(lower(clip));
    return it == ids_.end() ? -1 : it->second;
}

int32 PedAnims::behaviourOf(const std::string& clip) const
{
    const int32 id = clipId(clip);
    if (id < 0)
        return -1;
    // The last behaviour whose range starts at or before it (twins share a start: the first).
    int32 found = -1;
    for (std::size_t i = 0; i < behaviours_.size(); ++i)
        if (behaviours_[i].first <= id && (found < 0 || behaviours_[i].first > behaviours_[static_cast<std::size_t>(found)].first))
            found = static_cast<int32>(i);
    return found;
}

float32 PedAnims::blendInto(const std::string& from, const std::string& to, float32 fallback) const
{
    const int32 bt = behaviourOf(to);
    if (bt < 0 || (!from.empty() && behaviourOf(from) == bt))
        return fallback;
    return behaviours_[static_cast<std::size_t>(bt)].blend;
}

float32 PedAnims::blend(const std::string& clip, float32 fallback) const
{
    const int32 bt = behaviourOf(clip);
    return bt < 0 ? fallback : behaviours_[static_cast<std::size_t>(bt)].blend;
}

} // namespace Game
