// Game/save_restore.cpp - pure-data restore (no Game subsystem dependency; linkable into save_test).
#include "Game/save_restore.h"

#include <cstring>

namespace
{
    bool startsWith(const std::string& s, const char* pre)
    {
        const std::size_t n = std::char_traits<char>::length(pre);
        return s.size() >= n && s.compare(0, n, pre) == 0;
    }
    bool endsWith(const std::string& s, const char* suf)
    {
        const std::size_t n = std::char_traits<char>::length(suf);
        if (s.size() < n)
            return false;
        return s.compare(s.size() - n, n, suf) == 0;
    }
    std::uint32_t rdU32(const std::uint8_t* p)
    {
        return static_cast<std::uint32_t>(p[0]) |
               (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) |
               (static_cast<std::uint32_t>(p[3]) << 24);
    }
    float rdF32(const std::uint8_t* p)
    {
        std::uint32_t u = rdU32(p);
        float f;
        std::memcpy(&f, &u, sizeof f);
        return f;
    }
} // namespace

bool Game::isPlayer(const std::string& name)
{
    return name == "player";
}

bool Game::hasIntegrity(EntityKind kind)
{
    return kind == EntityKind::Player || kind == EntityKind::Breakable ||
           kind == EntityKind::Dynamic || kind == EntityKind::Corpse;
}

Game::EntityKind Game::classifyEntity(const std::string& name, std::size_t classDataSize)
{
    if (isPlayer(name))
        return EntityKind::Player;
    if (name == "CameraData")
        return EntityKind::CameraData;
    if (startsWith(name, "Gen_Save_Point"))
        return EntityKind::SavePoint;
    if (name == "Trigger")
        return EntityKind::Trigger;
    if (startsWith(name, "Hod_Bod"))
        return EntityKind::Corpse;
    if (endsWith(name, "(L)"))
        return EntityKind::Light;
    if (endsWith(name, "(D)"))
        return EntityKind::Dynamic;
    if (classDataSize == 4u)
        return EntityKind::Breakable;
    return EntityKind::Unknown;
}

Game::EntityState Game::parseClassData(const std::string& name,
                                       const std::vector<std::uint8_t>& classData)
{
    EntityState es;
    es.raw = classData;
    const std::size_t n = classData.size();
    es.kind = classifyEntity(name, n);
    const std::uint8_t* p = classData.data();
    // The kind comes from the name; each layout is only read when classData is long enough for
    // it (a short record keeps the defaults and stays verbatim in `raw`).
    switch (es.kind)
    {
        case EntityKind::Player:
        case EntityKind::Breakable:
        case EntityKind::Corpse:             // d0 (the corpse's rigid-body state stays in `raw`)
            if (n >= 4u)
                es.integrity = rdU32(p);
            break;
        case EntityKind::SavePoint:
        case EntityKind::Trigger:
            if (n >= 8u)
            {
                es.state = rdU32(p);
                es.stateFloat = rdF32(p + 4);
            }
            break;
        case EntityKind::Dynamic:
            if (n >= 12u)
            {
                es.integrity = rdU32(p);
                es.dynState0 = rdU32(p + 4);
                es.dynState1 = rdU32(p + 8);
            }
            break;
        case EntityKind::CameraData:
            if (n >= 20u)
            {
                es.camEnabled = rdU32(p);       // d0
                es.camFov = rdF32(p + 8);        // d2
                es.camWeight = rdF32(p + 12);    // d3
                es.camInterval = rdU32(p + 16);  // d4
            }
            break;
        case EntityKind::Light:
        default:
            // 60 B light (15 dwords) and any unrecognised layout: keep verbatim in `raw`.
            break;
    }
    return es;
}

std::vector<Game::RestoredEntity> Game::restoreEntities(const SaveFile& save)
{
    std::vector<RestoredEntity> out;
    out.reserve(save.entities.size());
    for (const SaveEntityRecord& e : save.entities)
    {
        RestoredEntity re;
        re.name = e.name;
        re.instance = e.instance;
        re.position = Math::Vector3{e.pos[0], e.pos[1], e.pos[2]};
        // The 4 plane floats are the orientation quaternion stored as (x, y, z, w).
        if (e.plane.size() == 4u)
            re.rotation = Math::Quaternion{e.plane[3], e.plane[0], e.plane[1], e.plane[2]};
        re.state = parseClassData(e.name, e.classData);
        out.push_back(std::move(re));
    }
    return out;
}

void Game::writeBack(SaveEntityRecord& rec, const RestoredEntity& re)
{
    rec.pos[0] = re.position.x;
    rec.pos[1] = re.position.y;
    rec.pos[2] = re.position.z;
    // The 4 plane floats are the orientation quaternion stored as (x, y, z, w).
    if (rec.plane.size() == 4u)
    {
        rec.plane[0] = re.rotation.x;
        rec.plane[1] = re.rotation.y;
        rec.plane[2] = re.rotation.z;
        rec.plane[3] = re.rotation.w;
    }
    // Write the live integrity back into the integrity-bearing kinds' first dword. The dynamic
    // kind's remaining state words and every non-integrity kind are preserved verbatim.
    const EntityKind kind = classifyEntity(rec.name, rec.classData.size());
    if (hasIntegrity(kind) && rec.classData.size() >= 4u)
    {
        const std::uint32_t hp = re.state.integrity;
        rec.classData[0] = static_cast<std::uint8_t>(hp & 0xffu);
        rec.classData[1] = static_cast<std::uint8_t>((hp >> 8) & 0xffu);
        rec.classData[2] = static_cast<std::uint8_t>((hp >> 16) & 0xffu);
        rec.classData[3] = static_cast<std::uint8_t>((hp >> 24) & 0xffu);
    }
}

std::uint32_t Game::typeForKind(EntityKind kind)
{
    switch (kind)
    {
        case EntityKind::Player:     return 1234u;
        case EntityKind::Breakable:  return 2001u;
        case EntityKind::SavePoint:  return 2002u;
        case EntityKind::Trigger:    return 2003u;
        case EntityKind::Dynamic:    return 2004u;
        case EntityKind::CameraData: return 2005u;
        case EntityKind::Corpse:     return 2006u;
        case EntityKind::Light:      return 2007u;
        case EntityKind::Unknown:
        default:                     return 2000u;
    }
}
