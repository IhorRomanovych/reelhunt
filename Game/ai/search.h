// Game/ai/search.h - where a hunting hunter looks for the player (aiTypeData.ini
// search_parameters): graph nodes within RADIUS_m of where he was last noticed that are dark
// enough to hide in (DARKNODE_%); DENSITY_% of them, at least one. A node another hunter
// already searched is skipped unless DOUBLE_SEARCH_DELAY_s have passed and the
// DOUBLE_SEARCH_% chance comes up. (Searchable objects - lockers, bins - come with G3.)
#pragma once
#include "engine_types.h"

#include <random>
#include <unordered_map>
#include <vector>

#include "Game/ai/ai_tuning.h"
#include "Game/ai/nav_graph.h"

namespace Game {

// `nodeLight`: the baked light at each node (0..1; missing = lit). `searched`: node -> level
// time it was last searched. Falls back to the node nearest `around` when nothing qualifies.
std::vector<int32> planSearchNodes(const NavGraph& nav, const Math::Vector3& around, const AlertData& a,
                                   const std::vector<float32>* nodeLight,
                                   const std::unordered_map<int32, float32>* searched, float32 now, std::mt19937& rng);

} // namespace Game
