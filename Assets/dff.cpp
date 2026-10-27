#include "Assets/dff.h"
#include "Core/platform.h"

#include "Assets/binary_reader.h"
#include "Assets/librw_engine.h"

#include "rw.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace Details {

// rw::Matrix stores right/up/at as V3d rows plus a pos translation. Convert to a
// column-major float[16] (the transpose of RW's row layout) so a shader can do
// `mat4 * vec4(pos,1)` with a column vector. If the rendered model is mirrored we
// flip this to the non-transposed form.
void toColumnMajor(const rw::Matrix& m, float out[16]) {
    out[0] = m.right.x;  out[1] = m.right.y;  out[2] = m.right.z;  out[3] = 0.0f;
    out[4] = m.up.x;     out[5] = m.up.y;     out[6] = m.up.z;     out[7] = 0.0f;
    out[8] = m.at.x;     out[9] = m.at.y;     out[10] = m.at.z;    out[11] = 0.0f;
    out[12] = m.pos.x;   out[13] = m.pos.y;   out[14] = m.pos.z;   out[15] = 1.0f;
}

Assets::DffAtomic extractAtomic(rw::Atomic* atomic) {
    Assets::DffAtomic result;
    if (!atomic || !atomic->geometry)
        return result;

    const rw::Geometry* geo = atomic->geometry;
    const bool hasNormals =
        (geo->flags & rw::Geometry::NORMALS) != 0 &&
        geo->morphTargets != nullptr &&
        geo->numMorphTargets > 0 &&
        geo->morphTargets[0].normals != nullptr;
    const bool hasUv = geo->numTexCoordSets > 0 && geo->texCoords[0] != nullptr;
    result.prelit = (geo->flags & rw::Geometry::PRELIT) != 0 && geo->colors != nullptr;

    result.vertices.reserve(static_cast<size_t>(geo->numVertices));
    for (int32 i = 0; i < geo->numVertices; ++i) {
        const rw::V3d& v = geo->morphTargets[0].vertices[i];
        const rw::V3d& n = hasNormals ? geo->morphTargets[0].normals[i]
                                      : rw::V3d{0.0f, 0.0f, 1.0f};
        const rw::TexCoords& uv = hasUv ? geo->texCoords[0][i] : rw::TexCoords{0.0f, 0.0f};
        Math::Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
        if (result.prelit) {
            const rw::RGBA& c = geo->colors[i];
            color = {c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f, c.alpha / 255.0f};
        }
        result.vertices.push_back({
            Math::Vector3{v.x, v.y, v.z},
            Math::Vector3{n.x, n.y, n.z},
            Math::Vector2{uv.u, uv.v},
            color});
    }

    const uint32 numVerts = static_cast<uint32>(geo->numVertices);
    result.triangles.reserve(static_cast<size_t>(geo->numTriangles));
    for (int32 i = 0; i < geo->numTriangles; ++i) {
        const rw::Triangle& t = geo->triangles[i];
        if (t.v[0] >= numVerts || t.v[1] >= numVerts || t.v[2] >= numVerts)
            continue; // corrupt index: would read past the vertex array
        uint32 materialIndex = t.matId;
        if (materialIndex >= static_cast<uint32>(geo->matList.numMaterials))
            materialIndex = 0;
        result.triangles.push_back({t.v[0], t.v[1], t.v[2], materialIndex});
    }

    // One entry per material slot (a null slot keeps a default entry), so a triangle's
    // materialIndex always addresses its own material. Skipping null slots used to shift
    // every later material onto the wrong triangles.
    result.materials.reserve(static_cast<size_t>(geo->matList.numMaterials));
    for (int32 i = 0; i < geo->matList.numMaterials; ++i) {
        const rw::Material* mat = geo->matList.materials[i];
        if (!mat) {
            result.materials.emplace_back();
            continue;
        }
        Assets::DffMaterial m;
        m.color = Math::Vector4{
            static_cast<float32>(mat->color.red) / 255.0f,
            static_cast<float32>(mat->color.green) / 255.0f,
            static_cast<float32>(mat->color.blue) / 255.0f,
            static_cast<float32>(mat->color.alpha) / 255.0f};
        if (mat->texture)
            m.textureName = mat->texture->name;
        result.materials.push_back(std::move(m));
    }

    // Skinning: per-vertex 4 bone indices (uint8) + 4 weights. librw always stores 4 indices and
    // 4 weights per vertex (numWeights only says how many are non-zero), so the stride is 4 for
    // both. Reading weights with a stride of numWeights scrambled every skin with < 4 weights,
    // and the old early return left skinned=true with no per-vertex data.
    if (const rw::Skin* s = rw::Skin::get(geo)) {
        if (s->numBones > 0 && s->indices && s->weights && geo->numVertices > 0) {
            result.skinned = true;
            result.numBones = s->numBones;
            result.boneIndices.resize(static_cast<size_t>(geo->numVertices));
            result.weights.resize(static_cast<size_t>(geo->numVertices));
            for (int32 i = 0; i < geo->numVertices; ++i) {
                const uint8* idx = s->indices + static_cast<size_t>(i) * 4u;
                const float* w = s->weights + static_cast<size_t>(i) * 4u;
                result.boneIndices[i] = {idx[0], idx[1], idx[2], idx[3]};
                result.weights[i] = {w[0], w[1], w[2], w[3]};
            }
        }
    }

    return result;
}

// The frame name (node-name chunk 0x0253F2FE) of each atomic in a clump's body, in file order,
// the order librw keeps them in (Clump::addAtomic appends). "" for a frame without one.
std::vector<std::string> atomicFrameNames(const rw::uint8* body, std::size_t size) {
    auto header = [&](std::size_t at, rw::uint32& type, rw::uint32& len) {
        if (at + 12 > size)
            return false;
        std::memcpy(&type, body + at, 4);
        std::memcpy(&len, body + at + 4, 4);
        return len <= size - at - 12;
    };
    std::vector<std::string> frames, out;
    rw::uint32 type = 0, len = 0;
    for (std::size_t pos = 0; header(pos, type, len); pos += 12 + len) {
        const std::size_t inner = pos + 12;
        rw::uint32 t = 0, l = 0;
        if (type == rw::ID_FRAMELIST && header(inner, t, l) && t == rw::ID_STRUCT && l >= 4) {
            rw::uint32 count = 0;
            std::memcpy(&count, body + inner + 12, 4);
            std::size_t p = inner + 12 + l; // one extension per frame follows the struct
            for (rw::uint32 f = 0; f < count && header(p, t, l) && t == rw::ID_EXTENSION; ++f, p += 12 + l) {
                std::string name;
                rw::uint32 et = 0, el = 0;
                for (std::size_t q = p + 12; q < p + 12 + l && header(q, et, el); q += 12 + el)
                    if (et == 0x0253F2FE)
                        name.assign(reinterpret_cast<const char*>(body + q + 12), el);
                name.resize(std::strlen(name.c_str()));
                frames.push_back(std::move(name));
            }
        } else if (type == rw::ID_ATOMIC && header(inner, t, l) && t == rw::ID_STRUCT && l >= 4) {
            rw::uint32 frame = 0;
            std::memcpy(&frame, body + inner + 12, 4);
            out.push_back(frame < frames.size() ? frames[frame] : std::string());
        }
    }
    return out;
}

// Every named frame of a clump's frame list and its origin in the clump's space: each frame is
// its rotation rows (right, up, at), its position and its parent's index (-1 for the root);
// a frame's origin is its position carried through its parents' frames.
std::vector<std::pair<std::string, Math::Vector3>> namedFramePoints(const rw::uint8* body, std::size_t size) {
    auto header = [&](std::size_t at, rw::uint32& type, rw::uint32& len) {
        if (at + 12 > size)
            return false;
        std::memcpy(&type, body + at, 4);
        std::memcpy(&len, body + at + 4, 4);
        return len <= size - at - 12;
    };
    std::vector<std::pair<std::string, Math::Vector3>> out;
    rw::uint32 type = 0, len = 0;
    for (std::size_t pos = 0; header(pos, type, len); pos += 12 + len) {
        const std::size_t inner = pos + 12;
        rw::uint32 t = 0, l = 0;
        if (type != rw::ID_FRAMELIST || !header(inner, t, l) || t != rw::ID_STRUCT || l < 4)
            continue;
        rw::uint32 count = 0;
        std::memcpy(&count, body + inner + 12, 4);
        if (l < 4 + static_cast<std::size_t>(count) * 56)
            break;
        struct F { float m[12]; int32 parent; Math::Vector3 axis[3]; Math::Vector3 at; };
        std::vector<F> frames(count);
        for (rw::uint32 f = 0; f < count; ++f) {
            std::memcpy(frames[f].m, body + inner + 16 + f * 56, 48);
            std::memcpy(&frames[f].parent, body + inner + 16 + f * 56 + 48, 4);
        }
        // World frames, parents first (a parent always precedes its children in the list).
        for (rw::uint32 f = 0; f < count; ++f) {
            F& fr = frames[f];
            const Math::Vector3 r{fr.m[0], fr.m[1], fr.m[2]}, u{fr.m[3], fr.m[4], fr.m[5]}, a{fr.m[6], fr.m[7], fr.m[8]};
            const Math::Vector3 p{fr.m[9], fr.m[10], fr.m[11]};
            if (fr.parent >= 0 && static_cast<rw::uint32>(fr.parent) < f) {
                const F& pa = frames[static_cast<std::size_t>(fr.parent)];
                auto carry = [&](const Math::Vector3& v) { return pa.axis[0] * v.x + pa.axis[1] * v.y + pa.axis[2] * v.z; };
                fr.axis[0] = carry(r);
                fr.axis[1] = carry(u);
                fr.axis[2] = carry(a);
                fr.at = carry(p) + pa.at;
            } else {
                fr.axis[0] = r;
                fr.axis[1] = u;
                fr.axis[2] = a;
                fr.at = p;
            }
        }
        std::size_t p = inner + 12 + l; // one extension per frame follows the struct
        for (rw::uint32 f = 0; f < count && header(p, t, l) && t == rw::ID_EXTENSION; ++f, p += 12 + l) {
            std::string name;
            rw::uint32 et = 0, el = 0;
            for (std::size_t q = p + 12; q < p + 12 + l && header(q, et, el); q += 12 + el)
                if (et == 0x0253F2FE)
                    name.assign(reinterpret_cast<const char*>(body + q + 12), el);
            name.resize(std::strlen(name.c_str()));
            if (!name.empty())
                out.emplace_back(std::move(name), frames[f].at);
        }
        break;
    }
    return out;
}

}  // namespace Details

}  // namespace anonymous

namespace Assets {

std::expected<std::vector<DffClump>, Engine::EngineError> Dff::load(const std::string& path) {
    if (!Assets::ensureLibrwEngine()) {
        return std::unexpected(Engine::EngineError::failure(
            Engine::EngineError::Code::Failed, "librw engine not started"));
    }

    rw::uint32 len = 0;
    rw::uint8* data = rw::getFileContents(Engine::Platform::resolvePath(path).c_str(), &len);
    if (!data) {
        return std::unexpected(Engine::EngineError::failure(
            Engine::EngineError::Code::NotFound, "failed to open dff " + path));
    }

    rw::StreamMemory in;
    in.open(data, len);

    std::vector<DffClump> out;
    rw::uint32 clumpSize = 0;
    while (rw::findChunk(&in, rw::ID_CLUMP, &clumpSize, nullptr)) {
        const rw::uint32 bodyAt = in.tell();
        rw::Clump* rwclump = rw::Clump::streamRead(&in);
        if (!rwclump)
            break;

        DffClump clump;
        clump.atomics.reserve(static_cast<size_t>(rwclump->countAtomics()));
        FORLIST(lnk, rwclump->atomics) {
            rw::Atomic* atomic = rw::Atomic::fromClump(lnk);
            if (!atomic)
                continue;
            clump.atomics.push_back(Details::extractAtomic(atomic));
        }
        if (bodyAt <= len) {
            const auto names = Details::atomicFrameNames(data + bodyAt, std::min<std::size_t>(clumpSize, len - bodyAt));
            if (names.size() == clump.atomics.size())
                for (std::size_t k = 0; k < names.size(); ++k)
                    clump.atomics[k].frameName = names[k];
            clump.framePoints = Details::namedFramePoints(data + bodyAt, std::min<std::size_t>(clumpSize, len - bodyAt));
        }

        // Skeleton: HAnimHierarchy rest locals + the first skinned atomic's bind
        // inverses. Both stored column-major for direct UBO upload.
        if (rw::HAnimHierarchy* h = rw::HAnimHierarchy::get(rwclump)) {
            if (h && h->numNodes > 0) {
                // Link each node to its Frame by HAnim id (streamRead leaves nodeInfo[].frame
                // unset; without it every frame below reads as the identity).
                if (h->parentFrame)
                    h->attach();
                clump.localSpaceMatrices =
                    (h->flags & rw::HAnimHierarchy::LOCALSPACEMATRICES) != 0;
                // Compose world-space rest-pose matrices by walking the Frame tree.
                // Mirrors HAnimHierarchy::updateMatrices() but sources each node's
                // local transform from its Frame (the bind pose) instead of the
                // interpolator, which is not populated until an animation is applied.
                std::vector<rw::Matrix> world(static_cast<size_t>(h->numNodes));
                rw::Matrix rootMat;
                rootMat.setIdentity();
                std::vector<rw::Matrix*> stack; // PUSH/POP parent stack (balanced in valid files)
                rw::Matrix* parentMat = &rootMat;
                for (int32 i = 0; i < h->numNodes; ++i) {
                    const rw::Frame* f = h->nodeInfo ? h->nodeInfo[i].frame : nullptr;
                    if (!f) {
                        world[static_cast<size_t>(i)].setIdentity();
                    } else {
                        rw::Matrix::mult(&world[static_cast<size_t>(i)], &f->matrix, parentMat);
                    }
                    if (h->nodeInfo && (h->nodeInfo[i].flags & rw::HAnimHierarchy::PUSH))
                        stack.push_back(parentMat);
                    parentMat = &world[static_cast<size_t>(i)];
                    if (h->nodeInfo && (h->nodeInfo[i].flags & rw::HAnimHierarchy::POP)) {
                        parentMat = stack.empty() ? &rootMat : stack.back();
                        if (!stack.empty())
                            stack.pop_back();
                    }
                }

                const rw::Matrix* invMats = nullptr;
                int32 numInvMats = 0;
                FORLIST(lnk2, rwclump->atomics) {
                    rw::Atomic* a2 = rw::Atomic::fromClump(lnk2);
                    if (a2 && a2->geometry) {
                        if (const rw::Skin* s2 = rw::Skin::get(a2->geometry)) {
                            if (s2->numBones > 0 && s2->inverseMatrices) {
                                invMats = reinterpret_cast<const rw::Matrix*>(s2->inverseMatrices);
                                numInvMats = s2->numBones;
                                break;
                            }
                        }
                    }
                }
                clump.bones.resize(static_cast<size_t>(h->numNodes));
                for (int32 i = 0; i < h->numNodes; ++i) {
                    DffBone& b = clump.bones[i];
                    b.id = h->nodeInfo ? h->nodeInfo[i].id : i;
                    b.nodeFlags = h->nodeInfo ? static_cast<uint32>(h->nodeInfo[i].flags) : 0u;
                    const rw::Frame* fi = h->nodeInfo ? h->nodeInfo[i].frame : nullptr;
                    if (fi) {
                        Details::toColumnMajor(fi->matrix, b.localRest);   // bind-pose LOCAL transform
                    } else {
                        for (int k = 0; k < 16; ++k)
                            b.localRest[k] = (k % 5 == 0) ? 1.0f : 0.0f;
                    }
                    Details::toColumnMajor(world[static_cast<size_t>(i)], b.restLocal);
                    if (invMats && i < numInvMats) { // the skin may carry fewer bones than the hierarchy
                        Details::toColumnMajor(invMats[i], b.inverseBind);
                    } else {
                        for (int k = 0; k < 16; ++k)
                            b.inverseBind[k] = (k % 5 == 0) ? 1.0f : 0.0f;
                    }
                }
            }
        }

        out.push_back(std::move(clump));
        rwclump->destroy();
    }

    in.close();
    rwFree(data);
    return out;
}

// Record-name -> top-level DFF clump index. RW .dff stores per-object names as
// [u32 len][0x1803FFFF][name] entries nested in the stream; each record is located by
// whole-word byte search and the occurrence shallowest within its owning ID_CLUMP chunk wins
// (identity names sit near a clump's top, shared texture refs are deeper). Records with no
// model are absent from the map.
std::unordered_map<std::string, int> clumpIndexByName(const std::vector<uint8_t>& bytes,
                                                      const std::vector<std::string>& records)
{
    std::unordered_map<std::string, int> map;
    const std::string buf(bytes.begin(), bytes.end());

    struct Range { size_t start; size_t end; };
    std::vector<Range> clumps; // top-level ID_CLUMP chunks (12-byte header: type, size, version)
    for (size_t pos = 0; buf.size() - pos >= 12;)
    {
        uint32 type = 0, size = 0;
        readAt(std::span<const uint8_t>(bytes), pos, type);
        readAt(std::span<const uint8_t>(bytes), pos + 4, size);
        if (type != 0x10 || size > buf.size() - pos - 12)
            break;
        clumps.push_back({pos, pos + 12 + size});
        pos += 12 + size;
    }

    auto whichClump = [&](size_t off) -> int
    {
        for (size_t i = 0; i < clumps.size(); ++i)
            if (off >= clumps[i].start && off < clumps[i].end)
                return static_cast<int>(i);
        return -1;
    };
    auto isWordByte = [](uint8_t c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    };
    auto resolve = [&](const std::string& name, bool requireWholeWord) -> int
    {
        if (name.empty() || clumps.empty())
            return -1;
        int bestClump = -1;
        size_t bestDepth = 0;
        for (size_t i = buf.find(name); i != std::string::npos; i = buf.find(name, i + 1))
        {
            if (requireWholeWord && i + name.size() < buf.size() && isWordByte(static_cast<uint8_t>(buf[i + name.size()])))
                continue;
            const int ci = whichClump(i);
            if (ci < 0)
                continue;
            const size_t depth = i - clumps[static_cast<size_t>(ci)].start;
            if (bestClump < 0 || depth < bestDepth)
            {
                bestDepth = depth;
                bestClump = ci;
            }
        }
        return bestClump;
    };

    for (const auto& rec : records)
    {
        if (map.contains(rec))
            continue;
        int ci = resolve(rec, true);
        if (ci < 0) // doors/gates carry a "(D)"-style suffix the DFF name lacks: retry on the base
        {
            const size_t paren = rec.find_last_of('(');
            const std::string baseName = (paren != std::string::npos) ? rec.substr(0, paren) : rec;
            if (!baseName.empty())
                ci = resolve(baseName, false);
            // Some records also carry a trailing underscore ('Bow_Interior_PrisonGate_(D)').
            if (ci < 0 && baseName.size() > 1 && baseName.back() == '_')
                ci = resolve(baseName.substr(0, baseName.size() - 1), false);
        }
        if (ci >= 0)
            map[rec] = ci;
    }
    return map;
}

}  // namespace Assets
