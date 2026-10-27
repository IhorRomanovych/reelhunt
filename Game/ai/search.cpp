// Game/ai/search.cpp - search planning. See search.h.
#include "Game/ai/search.h"

#include <algorithm>
#include <cmath>

namespace Game {

std::vector<int32> planSearchNodes(const NavGraph& nav, const Math::Vector3& around, const AlertData& a,
                                   const std::vector<float32>* nodeLight,
                                   const std::unordered_map<int32, float32>* searched, float32 now, std::mt19937& rng)
{
    const int32 from = nav.nearest(around);
    if (from < 0)
        return {};
    std::vector<int32> candidates;
    for (int32 n : nav.within(from, a.searchRadius))
    {
        const std::size_t i = static_cast<std::size_t>(n);
        if (nodeLight && i < nodeLight->size() && (*nodeLight)[i] > a.darkNode)
            continue;
        if (searched)
            if (const auto it = searched->find(n); it != searched->end())
            {
                const bool again = now - it->second >= a.doubleSearchDelay &&
                                   std::uniform_real_distribution<float32>(0.0f, 1.0f)(rng) < a.doubleSearch;
                if (!again)
                    continue;
            }
        candidates.push_back(n);
    }
    std::shuffle(candidates.begin(), candidates.end(), rng);
    std::size_t keep = static_cast<std::size_t>(std::ceil(static_cast<float32>(candidates.size()) * a.searchDensity));
    if (a.searchDensity > 0.0f && !candidates.empty())
        keep = std::max<std::size_t>(keep, 1);
    candidates.resize(std::min(keep, candidates.size()));
    if (candidates.empty())
        candidates.push_back(from); // nothing dark around: at least look where he was
    return candidates;
}

} // namespace Game
