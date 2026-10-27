// Game/entity_audio.cpp - the audio manager's entity sound events. See entity_audio.h.
#include "Game/entity_audio.h"
#include "Game/sample_levels.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace Game {

namespace {

// 0x6b52c6: rank by event, 0x00..0x61.
constexpr std::array<uint8_t, 0x62> kRank = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 5, 4, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,   100,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 100, 0, 0};

constexpr std::size_t kQueued = 4;
constexpr float32 kFullValue = 300.0f; // 0x6b2aec

// 0x450660: a swing door's sound class by its MATERIAL index - 0 wood (and the rest), 1 metal,
// 2 heavy metal, 3 mesh, 4 bars.
int32 doorClass(int32 material)
{
    switch (material)
    {
    case 3: case 0x27: case 0x28: return 1;           // METAL, METAL_SMALL / LARGE_SHUTTER
    case 7: case 0x15: case 0x2a: case 0x2c: return 2; // CAR, METALHARD, THICK_METAL_PLATE, METAL_CONTAINER
    case 8: case 0x29: return 3;                       // WIREMESH, METAL_CAGE_SHUTTER
    case 0x2b: case 0x3e: return 4;                    // METAL_POLE, IRON_BAR_DOOR
    default: return 0;
    }
}

} // namespace

int32 EntityAudio::rank(uint16 event)
{
    return event < kRank.size() ? kRank[event] : 0;
}

void EntityAudio::post(int32 entity, uint16 event, float32 value)
{
    if (event >= 0x61)
        return;
    auto q = std::find_if(queues_.begin(), queues_.end(), [entity](const Queue& x) { return x.entity == entity; });
    if (q == queues_.end())
    {
        queues_.push_back({entity, {}});
        q = std::prev(queues_.end());
    }
    std::vector<Posted>& events = q->events;
    const auto at = std::find_if(events.begin(), events.end(), [event](const Posted& p) { return rank(event) < rank(p.event); });
    if (at == events.end() && events.size() >= kQueued)
        return;
    events.insert(at, {entity, event, value});
    if (events.size() > kQueued)
        events.pop_back();
}

std::vector<EntityAudio::Posted> EntityAudio::drain()
{
    std::vector<Posted> out;
    for (const Queue& q : queues_)
        out.insert(out.end(), q.events.begin(), q.events.end());
    queues_.clear();
    return out;
}

EntityAudio::Sample EntityAudio::level(int32 id)
{
    // The audio manager's table (0x446ea0, Game/sample_levels.h).
    const SampleLevel l = sampleLevel(id);
    return {id, static_cast<float32>(l.level) / 127.0f, l.range};
}

EntityAudio::Sample EntityAudio::swingDoor(uint16 event, int32 material, float32 value, uint32 random)
{
    const int32 cls = doorClass(material);
    int32 id = -1;
    switch (event)
    {
    case DoorPush:
        // SFX_DOOR_OPEN_WOOD / _METAL; mesh and bars: sample 0 (a bank marker: nothing).
        id = cls == 0 ? 237 : cls <= 2 ? 238 : -1;
        break;
    case DoorLatch:
    {
        // Shut gently (value <= 300) or slammed: SFX_DOOR_CLOSE_WOOD / SLAM_WOOD, the metal
        // container's, the trash can's, the caged shutter's / the wire mesh's, the cage gate's.
        const bool slam = value > kFullValue;
        switch (cls)
        {
        case 0: id = slam ? 243 : 240; break;
        case 1: id = slam ? 54 : 53; break;
        case 2: id = slam ? 32 : 31; break;
        case 3: id = slam ? 26 + static_cast<int32>(random % 3) : 36 + static_cast<int32>(random & 1); break;
        default: id = 40; break;
        }
        break;
    }
    case DoorLimit:
        // SFX_DOOR_HIT_HINGE_LIMIT_WOOD, the metal container's, the trash can's, the wire mesh's,
        // the cage gate's. (Over 1000 it also makes AI event 0x1c, 0x520640: not done.)
        switch (cls)
        {
        case 0: id = 246; break;
        case 1: id = 53 + static_cast<int32>(random & 1); break;
        case 2: id = 32; break;
        case 3: id = 26 + static_cast<int32>(random % 3); break;
        default: id = 40; break;
        }
        break;
    default:
        return {};
    }
    if (id < 0)
        return {};
    Sample s = level(id);
    s.volume *= std::min(1.0f, value / kFullValue);
    return s;
}

EntityAudio::Sample EntityAudio::slideDoor(uint16 event)
{
    // SFX_DOOR_SLIDE_OPEN as it starts, SFX_DOOR_SLIDE_CLOSE as it gets there.
    if (event == SlideStart)
        return level(251);
    if (event == SlideStop)
        return level(252);
    return {};
}

} // namespace Game
