// Game/ai/stealth.cpp - hiding in the shadows. See stealth.h.
#include "Game/ai/stealth.h"

#include <algorithm>
#include <cmath>

namespace Game {

float32 effectivePlayerLight(float32 floorLight, bool nearDarkNode, float32 safeZone)
{
    if (floorLight < safeZone && !nearDarkNode)
        return safeZone + 1.0f; // dark, but not where a hiding place is
    return floorLight;
}

bool nearSafeZoneNode(const NavGraph& nav, const std::vector<float32>& nodeLight, const Math::Vector3& feet,
                      float32 safeZone, float32 reach)
{
    const int32 from = nav.nearest(feet);
    if (from < 0)
        return false;
    for (int32 n : nav.within(from, reach + 6.0f))
    {
        const std::size_t i = static_cast<std::size_t>(n);
        if (i >= nodeLight.size() || nodeLight[i] >= safeZone)
            continue;
        const Math::Vector3 p = nav.node(n).position;
        const float32 dx = p.x - feet.x, dz = p.z - feet.z;
        if (std::sqrt(dx * dx + dz * dz) <= std::max(reach, nav.node(n).radius) && std::fabs(p.y - feet.y) < 2.5f)
            return true;
    }
    return false;
}

} // namespace Game
