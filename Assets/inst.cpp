// Assets/inst.cpp - the game entity placement archive (.inst) parser. See inst.h.
#include "Assets/inst.h"

#include "Assets/binary_reader.h"

#include <utility>

namespace {

using Engine::EngineError;

// A record block that is too short simply ends early; the fields read so far are kept, which
// matches the toolkit (it reads what is present and stops at the block end).
Assets::InstPlacement parseRecord(std::span<const uint8_t> block) {
    Assets::InstPlacement p;
    Assets::ByteReader r(block);
    if (!r.readPaddedString(p.record) || !r.readPaddedString(p.internalName))
        return p;
    float32 px = 0.0f, py = 0.0f, pz = 0.0f;
    if (!r.readF32(px) || !r.readF32(py) || !r.readF32(pz))
        return p;
    p.position = Math::Vector3{px, py, pz};
    float32 qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 0.0f;
    if (!r.readF32(qx) || !r.readF32(qy) || !r.readF32(qz) || !r.readF32(qw))
        return p;
    p.rotation = Math::Vector4{qx, qy, qz, qw};
    if (!r.readPaddedString(p.entityClass))
        return p;
    int32 v = 0;
    while (r.readI32(v))
        p.params.push_back(v);
    return p;
}

} // namespace

namespace Assets {

std::expected<std::vector<InstPlacement>, Engine::EngineError> Inst::load(const std::string& path) {
    auto bytes = readFileBytes(path);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));

    ByteReader header(*bytes);
    uint32 count = 0;
    if (!header.readU32(count))
        return std::unexpected(EngineError::failure(EngineError::Code::Invalid, "inst too short for header"));
    // Each record needs at least a 4-byte size entry; reject absurd counts before allocating.
    if (count > header.remaining() / 4u)
        return std::unexpected(EngineError::failure(EngineError::Code::Invalid,
                                                    "inst record count " + std::to_string(count) + " exceeds file size"));

    std::vector<uint32> sizes(count);
    for (uint32& s : sizes)
        header.readU32(s); // cannot fail: checked above

    std::vector<InstPlacement> result;
    result.reserve(count);
    std::size_t cursor = header.offset();
    const std::span<const uint8_t> all(*bytes);
    for (uint32 i = 0; i < count; ++i) {
        if (sizes[i] > all.size() - cursor)
            return std::unexpected(EngineError::failure(EngineError::Code::Invalid,
                                                        "inst record " + std::to_string(i) + " runs past end of file"));
        result.push_back(parseRecord(all.subspan(cursor, sizes[i])));
        cursor += sizes[i];
    }
    return result;
}

} // namespace Assets
