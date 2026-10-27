// Assets/col.cpp - the game collision archive (.col) parser. See col.h.
#include "Assets/col.h"

#include "Assets/binary_reader.h"

#include <utility>

namespace {

using Assets::ByteReader;

bool readVec3(ByteReader& r, Math::Vector3& v) {
    return r.readF32(v.x) && r.readF32(v.y) && r.readF32(v.z);
}

// Reads a u32 element count and checks that `count * elemSize` bytes can still follow, so a
// corrupt count fails cleanly instead of reserving gigabytes.
bool readCount(ByteReader& r, std::size_t elemSize, uint32& n) {
    return r.readU32(n) && n <= r.remaining() / elemSize;
}

bool readEntry(ByteReader& r, Assets::ColEntry& e) {
    if (!r.readPaddedString(e.name) || !readVec3(r, e.center) || !r.readF32(e.radius) ||
        !readVec3(r, e.min) || !readVec3(r, e.max))
        return false;

    uint32 n = 0;
    if (!readCount(r, 16u, n))
        return false;
    e.spheres.resize(n);
    for (Assets::ColSphere& s : e.spheres)
        if (!readVec3(r, s.center) || !r.readF32(s.radius) || !r.readU8(s.material) ||
            !r.readU8(s.flag) || !r.readU8(s.brightness) || !r.readU8(s.light))
            return false;

    if (!readCount(r, 24u, n))
        return false;
    e.lines.resize(n);
    for (Assets::ColLine& ln : e.lines)
        if (!readVec3(r, ln.a) || !readVec3(r, ln.b))
            return false;

    // The format reserves a box block, but the toolkit has no decoder for it and the shipped
    // levels store none. Refuse rather than mis-parse data of unknown size.
    if (!r.readU32(e.boxCount) || e.boxCount > 0)
        return false;

    if (!readCount(r, 12u, n))
        return false;
    e.verticals.resize(n);
    for (Math::Vector3& v : e.verticals)
        if (!readVec3(r, v))
            return false;

    if (!readCount(r, 12u, n))
        return false;
    e.faces.resize(n);
    for (Assets::ColFace& f : e.faces)
        if (!r.readI32(f.i0) || !r.readI32(f.i1) || !r.readI32(f.i2))
            return false;
    return true;
}

} // namespace

namespace Assets {

std::expected<ColData, Engine::EngineError> Col::load(const std::string& path) {
    using Engine::EngineError;
    auto bytes = readFileBytes(path);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));

    ByteReader r(*bytes);
    uint32 count = 0;
    if (!r.readU32(count))
        return std::unexpected(EngineError::failure(EngineError::Code::Invalid, "col too short for header"));

    ColData data;
    for (uint32 i = 0; i < count; ++i) {
        const std::size_t entryStart = r.offset();
        ColEntry e;
        if (!readEntry(r, e))
            return std::unexpected(EngineError::failure(
                EngineError::Code::Invalid,
                "col entry parse failed at index " + std::to_string(i) + " (offset " + std::to_string(entryStart) + ")"));
        data.totalSpheres += static_cast<uint32>(e.spheres.size());
        data.totalLines += static_cast<uint32>(e.lines.size());
        data.totalBoxes += e.boxCount;
        data.totalVerticals += static_cast<uint32>(e.verticals.size());
        data.totalFaces += static_cast<uint32>(e.faces.size());
        data.entries.push_back(std::move(e));
    }

    // Validation: a well-formed file is consumed exactly (no trailing bytes).
    if (!r.eof())
        return std::unexpected(EngineError::failure(
            EngineError::Code::Invalid, "col has " + std::to_string(r.remaining()) + " trailing byte(s)"));
    return data;
}

} // namespace Assets
