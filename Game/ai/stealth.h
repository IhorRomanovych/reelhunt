// Game/ai/stealth.h - hiding in the shadows, as the exe decides it.
//
// Every character's light is worked out each frame (exe 0x49a6f0): the brightness of
// the world under it, (r + g + b) / 3 of the baked light (0x479350 samples the triangle below).
// For the player there is one more rule: being dark only counts where the level designers put a
// dark spot - when none of the AI graph nodes around him is itself below SAFE_ZONE_% (node
// light < 6 %, 0x406c40), his light is raised to SAFE_ZONE_% + 1 (fully visible). Below
// SAFE_ZONE_% he is hidden: the HUD status icon turns blue and hunters' sight gets no LIGHT
// level (Game/ai/senses.h), so they cannot see his body at all.
// What the exe's AI does on top (not traced; from the level's own hint text, ours):
//   - "If you are seen entering the shadows you won't be able to hide there": a hunter that
//     sees him as he hides keeps seeing him while it chases or fights;
//   - "Hunters will not see you in the dark unless they are actively searching for you": a
//     searching or investigating hunter sees a hidden player within FACE_DEFINITE_m.
#pragma once
#include "engine_types.h"

#include <vector>

#include "Game/ai/nav_graph.h"
#include "Math/vector.h"

namespace Game {

// The light hunters see the player in (0..1+).
float32 effectivePlayerLight(float32 floorLight, bool nearDarkNode, float32 safeZone);

// Whether a graph node within `reach` of `feet` (or within the node's own radius) is a safe
// zone. `nodeLight` holds each node's baked light.
bool nearSafeZoneNode(const NavGraph& nav, const std::vector<float32>& nodeLight, const Math::Vector3& feet,
                      float32 safeZone, float32 reach = 2.0f);

} // namespace Game
