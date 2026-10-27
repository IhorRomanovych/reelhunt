// Assets/world.cpp - native RWWorld .bsp parser (the game level geometry). See world.h.
#include "Assets/world.h"

#include "Assets/binary_reader.h"

#include <iostream>
#include <span>
#include <utility>

namespace Assets {
namespace {

using Engine::EngineError;

constexpr uint32 kChunkStruct = 1u;
constexpr uint32 kChunkString = 2u;
constexpr uint32 kChunkTexture = 6u;
constexpr uint32 kChunkMaterial = 7u;
constexpr uint32 kChunkMatList = 8u;
constexpr uint32 kChunkLeaf = 9u;
constexpr uint32 kChunkNode = 10u;
constexpr uint32 kChunkWorld = 11u;

struct Chunk {
    uint32 type = 0;
    uint32 version = 0; // raw RW version stamp
    std::span<const uint8_t> body;
};

// RW chunk version stamp -> library id (e.g. 0x1803FFFF -> 0x36003, 0x1005FFFF -> 0x34005).
uint32 libraryId(uint32 stamp) {
    if (stamp & 0xFFFF0000u)
        return (((stamp >> 14) & 0x3FF00u) + 0x30000u) | ((stamp >> 16) & 0x3Fu);
    return stamp << 8; // pre-3.1 files store the plain version
}

// The RW chunks laid out back-to-back in `region` (12-byte header: type, size, version). Stops
// at the first header that is truncated or whose size overruns the region.
std::vector<Chunk> childChunks(std::span<const uint8_t> region) {
    std::vector<Chunk> out;
    std::size_t off = 0;
    while (region.size() - off >= 12u) {
        uint32 type = 0, size = 0, version = 0;
        readAt(region, off, type);
        readAt(region, off + 4u, size);
        readAt(region, off + 8u, version);
        if (size > region.size() - off - 12u)
            break;
        out.push_back({type, version, region.subspan(off + 12u, size)});
        off += 12u + size;
    }
    return out;
}

const Chunk* firstOfType(const std::vector<Chunk>& chunks, uint32 type) {
    for (const Chunk& c : chunks)
        if (c.type == type)
            return &c;
    return nullptr;
}

// MATERIAL chunk -> its texture name ("" when the material is untextured).
std::string materialTextureName(std::span<const uint8_t> materialBody) {
    const auto parts = childChunks(materialBody);
    const Chunk* tex = firstOfType(parts, kChunkTexture);
    if (!tex)
        return {};
    const auto texParts = childChunks(tex->body);
    const Chunk* name = firstOfType(texParts, kChunkString);
    if (!name)
        return {};
    std::size_t len = 0;
    while (len < name->body.size() && name->body[len] != 0)
        ++len;
    return std::string(reinterpret_cast<const char*>(name->body.data()), len);
}

// MATLIST = STRUCT{u32 count, i32 index[count]} followed by one MATERIAL chunk per index that is
// -1; an index >= 0 reuses an earlier material. Untextured materials keep their slot (as ""), so
// triangle materialIndex values line up with the returned list. (The old byte-pattern scan
// skipped untextured materials, shifting every later name by one, and only recognised one RW
// version + texture filter value, so it found nothing at all in some levels.)
std::vector<std::string> parseMatList(std::span<const uint8_t> body) {
    const auto parts = childChunks(body);
    if (parts.empty() || parts.front().type != kChunkStruct)
        return {};
    ByteReader hdr(parts.front().body);
    uint32 count = 0;
    if (!hdr.readU32(count) || count > hdr.remaining() / 4u)
        return {};

    std::vector<std::string> materials; // the MATERIAL chunks, in order
    for (std::size_t i = 1; i < parts.size(); ++i)
        if (parts[i].type == kChunkMaterial)
            materials.push_back(materialTextureName(parts[i].body));

    std::vector<std::string> names;
    names.reserve(count);
    std::size_t next = 0;
    for (uint32 i = 0; i < count; ++i) {
        int32 ref = -1;
        hdr.readI32(ref);
        if (ref < 0)
            names.push_back(next < materials.size() ? materials[next++] : std::string());
        else
            names.push_back(static_cast<std::size_t>(ref) < names.size() ? names[static_cast<std::size_t>(ref)]
                                                                         : std::string());
    }
    return names;
}

class Walker {
public:
    WorldData out;
    bool materialFirst = false; // triangle record order: (mat, a, b, c) instead of (a, b, c, mat)

    void walkNode(std::span<const uint8_t> region, int depth) {
        if (depth > 64) // a real octree is far shallower; guards against corrupt self-nesting
            return;
        for (const Chunk& c : childChunks(region)) {
            if (c.type == kChunkNode)
                walkNode(c.body, depth + 1);
            else if (c.type == kChunkLeaf)
            {
                const auto leafParts = childChunks(c.body); // kept: firstOfType points into it
                if (const Chunk* mesh = firstOfType(leafParts, kChunkStruct))
                    parseMeshStruct(mesh->body);
            }
        }
    }

private:
    void parseMeshStruct(std::span<const uint8_t> m) {
        uint32 nt = 0, nv = 0;
        if (m.size() < 11u * 4u || !readAt(m, 4u, nt) || !readAt(m, 8u, nv))
            return;
        if (nv == 0u || nv > 65536u || nt > (1u << 20))
            return;

        ++out.leafCount;
        const std::size_t words = m.size() / 4u;
        if (words == 11u + 7u * static_cast<std::size_t>(nv) + 2u * static_cast<std::size_t>(nt))
            ++out.layoutInvariantOk;

        auto f32 = [&](std::size_t word) {
            float v = 0.0f;
            readAt(m, word * 4u, v);
            return v;
        };

        WorldMesh mesh;
        mesh.bboxMin = {f32(3), f32(4), f32(5)};
        mesh.bboxMax = {f32(6), f32(7), f32(8)};

        const std::size_t posWord = 11u;
        const std::size_t packedWord = posWord + 3u * nv;
        const std::size_t triWord = posWord + 7u * nv;
        if (triWord * 4u > m.size())
            return; // positions + per-vertex block do not fit: not a mesh we understand

        mesh.positions.resize(nv);
        mesh.normals.resize(nv);
        mesh.uvs.resize(nv);
        mesh.prelight.resize(nv);
        for (std::size_t i = 0; i < nv; ++i) {
            mesh.positions[i] = {f32(posWord + 3u * i), f32(posWord + 3u * i + 1u), f32(posWord + 3u * i + 2u)};

            const std::size_t nb = packedWord * 4u + i * 4u; // NV x (i8 x, y, z, pad)
            const Math::Vector3 n{static_cast<float>(static_cast<int8_t>(m[nb])),
                                  static_cast<float>(static_cast<int8_t>(m[nb + 1u])),
                                  static_cast<float>(static_cast<int8_t>(m[nb + 2u]))};
            const float len = n.length();
            mesh.normals[i] = (len > 1e-6f) ? n * (1.0f / len) : Math::Vector3{0.0f, 1.0f, 0.0f};

            const std::size_t cb = (packedWord + nv + i) * 4u; // NV x (u8 r, g, b, a)
            mesh.prelight[i] = {m[cb] / 255.0f, m[cb + 1u] / 255.0f, m[cb + 2u] / 255.0f, m[cb + 3u] / 255.0f};

            mesh.uvs[i] = {f32(packedWord + 2u * nv + 2u * i), f32(packedWord + 2u * nv + 2u * i + 1u)};
        }

        mesh.triangles.reserve(nt);
        for (std::size_t j = 0; j < nt; ++j) {
            uint16 rec[4]{};
            if (!readAt(m, triWord * 4u + j * 8u, rec))
                break;
            const WorldTriangle t = materialFirst ? WorldTriangle{rec[1], rec[2], rec[3], rec[0]}
                                                  : WorldTriangle{rec[0], rec[1], rec[2], rec[3]};
            if (t.a >= nv || t.b >= nv || t.c >= nv) {
                ++out.droppedTriangles; // would index past the vertex array when rendered
                continue;
            }
            mesh.triangles.push_back(t);
        }

        out.totalVerts += nv;
        out.totalTris += static_cast<uint32>(mesh.triangles.size());
        out.meshes.push_back(std::move(mesh));
    }
};

} // namespace

std::expected<WorldData, Engine::EngineError> World::load(const std::string& path) {
    auto bytes = readFileBytes(path);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));

    const auto top = childChunks(*bytes);
    if (top.empty() || top.front().type != kChunkWorld)
        return std::unexpected(EngineError::failure(EngineError::Code::Invalid, "bsp: expected a WORLD chunk"));

    Walker w;
    w.out.rwLibraryId = libraryId(top.front().version);
    w.materialFirst = w.out.rwLibraryId < 0x36000u;
    // The WORLD body is walked like a node: STRUCT/MATLIST/EXTENSION are skipped and the octree
    // root node(s) are recursed into.
    w.walkNode(top.front().body, 0);
    WorldData data = std::move(w.out);
    // (The chunk list is kept: a pointer into a temporary list would dangle.)
    const auto worldParts = childChunks(top.front().body);
    if (const Chunk* matList = firstOfType(worldParts, kChunkMatList))
        data.materialNames = parseMatList(matList->body);
    return data;
}

void World::dump(const std::string& path) {
    auto res = load(path);
    if (!res) {
        std::cerr << "world: dump failed: " << res.error().message << "\n";
        return;
    }
    const WorldData& d = *res;
    std::cerr << "world: file=" << path << " leaves=" << d.leafCount << " verts=" << d.totalVerts
              << " tris=" << d.totalTris << " materials=" << d.materialNames.size()
              << " rw=0x" << std::hex << d.rwLibraryId << std::dec
              << " layout invariant ok=" << d.layoutInvariantOk << "/" << d.leafCount
              << " droppedTris=" << d.droppedTriangles << "\n";
    for (std::size_t i = 0; i < d.meshes.size() && i < 12; ++i) {
        const auto& m = d.meshes[i];
        std::cerr << "  mesh[" << i << "] verts=" << m.positions.size() << " tris=" << m.triangles.size()
                  << " bmin=(" << m.bboxMin.x << "," << m.bboxMin.y << "," << m.bboxMin.z << ")"
                  << " bmax=(" << m.bboxMax.x << "," << m.bboxMax.y << "," << m.bboxMax.z << ")\n";
    }
}

}  // namespace Assets
