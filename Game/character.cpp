// Game/character.cpp - generic skinned model load + IFP-driven skin upload.
#include "Game/character.h"

#include "Assets/binary_reader.h"
#include "Assets/dff.h"
#include "Assets/ifp/ifp.h"
#include "Assets/txd.h"
#include "Math/math.h"
#include "Math/matrix.h"
#include "Math/quaternion.h"
#include "Math/vector.h"
#include "Renderer/material.h"
#include "Renderer/mesh.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>

#include <glm/gtc/quaternion.hpp>

namespace
{
    // RenderWare stores matrices as row-vector (right, up, at, pos); our math is column-vector, so
    // the basis vectors become columns and the translation lands in column 3. The homogeneous row
    // is forced to (0,0,0,1): RW leaves flag words there.
    Math::Matrix4x4 fromColumnMajor(const float *cm) noexcept
    {
        Math::Matrix4x4 m;
        m.m00 = cm[0];  m.m10 = cm[1];  m.m20 = cm[2];  m.m30 = 0.0f;
        m.m01 = cm[4];  m.m11 = cm[5];  m.m21 = cm[6];  m.m31 = 0.0f;
        m.m02 = cm[8];  m.m12 = cm[9];  m.m22 = cm[10]; m.m32 = 0.0f;
        m.m03 = cm[12]; m.m13 = cm[13]; m.m23 = cm[14]; m.m33 = 1.0f;
        return m;
    }

    // Upload a skin matrix (transposed) into a float buffer, matching the mvp column-major convention.
    void writeBoneMatrix(float *dst, const Math::Matrix4x4 &m) noexcept
    {
        const Math::Matrix4x4 up = m.transpose();
        const float src[16] = {up.m00, up.m01, up.m02, up.m03, up.m10, up.m11, up.m12, up.m13,
                               up.m20, up.m21, up.m22, up.m23, up.m30, up.m31, up.m32, up.m33};
        std::copy(std::begin(src), std::end(src), dst);
    }

    // IFP quaternion (x,y,z,w) -> rotation block of L. The conjugate is the verified the game convention.
    void setRotation(Math::Matrix4x4 &L, const float *q) noexcept
    {
        const Math::Matrix4x4 rot = Math::Quaternion{q[3], -q[0], -q[1], -q[2]}.toMatrix().make4x4();
        L.m00 = rot.m00; L.m01 = rot.m01; L.m02 = rot.m02;
        L.m10 = rot.m10; L.m11 = rot.m11; L.m12 = rot.m12;
        L.m20 = rot.m20; L.m21 = rot.m21; L.m22 = rot.m22;
    }

    // Rigid blend of two local transforms: slerp the rotations, lerp the translations.
    Math::Matrix4x4 blendRigid(const Math::Matrix4x4 &a, const Math::Matrix4x4 &b, float32 w) noexcept
    {
        const glm::mat4 ga = a.toGlm(), gb = b.toGlm();
        const glm::quat qa = glm::quat_cast(glm::mat3(ga));
        glm::quat qb = glm::quat_cast(glm::mat3(gb));
        if (glm::dot(qa, qb) < 0.0f)
            qb = -qb; // shortest arc
        glm::mat4 out = glm::mat4_cast(glm::normalize(glm::slerp(qa, qb, w)));
        out[3] = glm::vec4(glm::mix(glm::vec3(ga[3]), glm::vec3(gb[3]), w), 1.0f);
        return Math::Matrix4x4::fromGlm(out);
    }

    // RenderWare HAnim hierarchy. Bones are stored depth-first with PUSH (0x2) / POP (0x1) flags:
    //   parent = current;  if PUSH: save the node's parent;  current = this node;  if POP: current = saved.
    std::vector<int32> parentsFromHAnimFlags(const std::vector<uint32> &flags)
    {
        std::vector<int32> parent(flags.size(), -1);
        std::vector<int32> stk;
        int32 cur = -1;
        for (size_t i = 0u; i < flags.size(); ++i)
        {
            parent[i] = cur;
            if (flags[i] & 0x2u) // PUSH
                stk.push_back(cur);
            cur = static_cast<int32>(i);
            if ((flags[i] & 0x1u) && !stk.empty()) // POP
            {
                cur = stk.back();
                stk.pop_back();
            }
        }
        return parent;
    }
} // namespace

namespace Game
{

    void Character::ClipState::advance(float32 dt) noexcept
    {
        if (!anim || !playing)
            return;
        time += dt * speed;
        if (loop)
        {
            time = std::fmod(time, duration);
            if (time < 0.0f)
                time += duration; // negative speed
        }
        else if (speed >= 0.0f ? time >= duration : time <= 0.0f)
        {
            time = std::clamp(time, 0.0f, duration);
            playing = false; // hold the last pose
        }
    }

    std::shared_ptr<const DffLibrary> DffLibrary::load(const std::string &path)
    {
        auto clumps = Assets::Dff::load(path);
        auto bytes = Assets::readFileBytes(path);
        if (!clumps.has_value() || clumps->empty() || !bytes)
            return nullptr;
        auto lib = std::make_shared<DffLibrary>();
        lib->path = path;
        lib->clumps = std::move(*clumps);
        lib->bytes = std::move(*bytes);
        return lib;
    }

    const Assets::DffClump *DffLibrary::find(const std::string &name) const
    {
        if (name.empty())
            return clumps.empty() ? nullptr : &clumps.front();
        auto cached = found_.find(name);
        if (cached == found_.end())
        {
            const auto index = Assets::clumpIndexByName(bytes, {name});
            const auto it = index.find(name);
            cached = found_.emplace(name, it == index.end() ? -1 : it->second).first;
        }
        const int i = cached->second;
        return i < 0 || static_cast<std::size_t>(i) >= clumps.size() ? nullptr : &clumps[static_cast<std::size_t>(i)];
    }

    bool Character::load(const ModelDesc &desc, Renderer::Scene &scene)
    {
        if (loaded_)
        {
            std::cerr << "[model] load() was already called on this instance\n";
            return false;
        }

        // ---- 1. DFF -------------------------------------------------------------------------------------
        std::shared_ptr<const DffLibrary> owned;
        const DffLibrary *library = desc.library;
        if (!library)
        {
            owned = DffLibrary::load(desc.dffPath);
            library = owned.get();
        }
        if (!library)
        {
            std::cerr << "[model] DFF load failed: " << desc.dffPath << "\n";
            return false;
        }
        const Assets::DffClump *found = library->find(desc.clumpName);
        if (!found)
        {
            std::cerr << "[model] no clump '" << desc.clumpName << "' in " << desc.dffPath << "\n";
            return false;
        }
        const auto &clump = *found;

        // ---- 2. Bind-pose bounds -> placement ------------------------------------------------------------
        Renderer::BoundingBox bounds;
        for (const auto &a : clump.atomics)
            for (const auto &v : a.vertices)
                bounds.expand(v.position);
        if (bounds.empty())
        {
            std::cerr << "[model] DFF has no vertices: " << desc.dffPath << "\n";
            return false;
        }
        const Math::Vector3 center = bounds.center();
        const Math::Vector3 ext = bounds.size();
        float32 maxExt = std::max(ext.x, std::max(ext.y, ext.z));
        if (maxExt < 1e-6f)
            maxExt = 1.0f;
        const float32 s = (desc.targetSize > 0.0f) ? desc.targetSize / maxExt : desc.scale;
        const Math::Vector3 offset = desc.centerOnOrigin ? center * -1.0f : Math::Vector3{0.0f, 0.0f, 0.0f};

        // model space -> world; scale and centring are folded in so skin matrices stay in model units.
        // A standing character is tallest along its up axis: Cash's DFF is X-up, the level packs'
        // hunters are Y-up. Rotate that axis onto world +Y.
        upAxis_ = 1;
        if (desc.flipForVulkan)
            upAxis_ = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.z > ext.y ? 2 : 1);
        Math::Matrix4x4 base = Math::Matrix4x4::identity();
        if (upAxis_ == 0)
            base = base * Math::Matrix4x4::rotationZ(Math::pi / 2.0f); // +X -> +Y
        else if (upAxis_ == 2)
            base = base * Math::Matrix4x4::rotationX(-Math::pi / 2.0f); // +Z -> +Y
        base = base * Math::Matrix4x4::scale(s) * Math::Matrix4x4::translation(offset);
        baseTransform_ = base;
        scale_ = s;
        // desc.position is the feet; this basis is centred on the bind-pose box.
        rig_.placement = Math::Matrix4x4::translation(desc.position + Math::Vector3{0.0f, 0.5f * s * (upAxis_ == 0 ? ext.x : (upAxis_ == 2 ? ext.z : ext.y)), 0.0f}) * base;
        halfHeight_ = 0.5f * s * (upAxis_ == 0 ? ext.x : (upAxis_ == 2 ? ext.z : ext.y));

        // ---- 3. Skeleton ----------------------------------------------------------------------------------
        const uint32 nb = static_cast<uint32>(clump.bones.size());
        if (nb > Renderer::kMaxSkinBones)
            std::cerr << "[model] WARNING: " << nb << " bones; skinned vertices can address only "
                      << Renderer::kMaxSkinBones << "\n";
        rig_.numBones = nb;
        if (nb > 0u)
        {
            rig_.invBindMat.resize(nb);
            rig_.lRest.resize(nb);
            rig_.boneIds.resize(nb);

            std::vector<Math::Matrix4x4> wRest(nb); // bind-pose world = inverse of the skin's inverse-bind
            std::vector<uint32> flags(nb);
            for (uint32 i = 0u; i < nb; ++i)
            {
                rig_.invBindMat[i] = fromColumnMajor(clump.bones[i].inverseBind);
                wRest[i] = rig_.invBindMat[i].inverse();
                rig_.boneIds[i] = static_cast<int16>(clump.bones[i].id);
                flags[i] = clump.bones[i].nodeFlags;
            }

            rig_.parentIdx = parentsFromHAnimFlags(flags);
            const auto rootIt = std::find(rig_.boneIds.begin(), rig_.boneIds.end(), Assets::kRootMotionBoneId);
            rootMotionBone_ = rootIt == rig_.boneIds.end() ? -1 : static_cast<int32>(rootIt - rig_.boneIds.begin());
            for (const auto &ov : desc.parentOverrides)
            {
                const auto child = std::find(rig_.boneIds.begin(), rig_.boneIds.end(), ov.first);
                const auto parent = std::find(rig_.boneIds.begin(), rig_.boneIds.end(), ov.second);
                if (child == rig_.boneIds.end() || parent == rig_.boneIds.end() || parent >= child)
                {
                    std::cerr << "[model] parent override " << ov.first << "->" << ov.second << " ignored\n";
                    continue;
                }
                rig_.parentIdx[static_cast<size_t>(child - rig_.boneIds.begin())] =
                    static_cast<int32>(parent - rig_.boneIds.begin());
            }

            // Bind LOCAL (column-vector): L = inverse(W_parent) * W_self. Missing IFP channels fall back to it.
            for (uint32 i = 0u; i < nb; ++i)
            {
                const int32 p = rig_.parentIdx[i];
                rig_.lRest[i] = (p < 0) ? wRest[i] : wRest[static_cast<size_t>(p)].inverse() * wRest[i];
                if (desc.frameRest)
                    rig_.lRest[i] = fromColumnMajor(clump.bones[i].localRest);
            }

            // The skeleton's root dummy (Cash's 10000, a hunter's 2000), parent of the pelvis (1000),
            // is the frame the clips are authored in: Y-up, feet at the origin, facing -Z. In the game
            // it is the character's own world matrix, so it is posed by the placement, not its bind
            // matrix (which differs between models: Cash's is rotated, a hunter's is not).
            if (rootMotionBone_ >= 0)
            {
                const int32 dummy = rig_.parentIdx[static_cast<size_t>(rootMotionBone_)];
                if (dummy >= 0 && rig_.parentIdx[static_cast<size_t>(dummy)] < 0)
                {
                    placementBone_ = dummy;
                    base = Math::Matrix4x4::scale(s);
                    baseTransform_ = base;
                    rig_.placement = Math::Matrix4x4::translation(desc.position) * base;
                }
            }

            if (desc.verbose)
            {
                for (uint32 i = 0u; i < nb; ++i)
                {
                    const Math::Vector3 p = wRest[i].position();
                    std::cerr << "[model] b" << i << " id=" << rig_.boneIds[i] << " parent=" << rig_.parentIdx[i]
                              << " flags=" << flags[i] << " pos=(" << p.x << "," << p.y << "," << p.z << ")\n";
                }
            }

            fk_.resize(nb);
            pose_->matrices.resize(static_cast<size_t>(nb) * 16u);
        }

        // ---- 4. Textures ----------------------------------------------------------------------------------
        if (!desc.textures)
        {
            const std::string txdPath = desc.txdPath.empty()
                                            ? std::filesystem::path(desc.dffPath).replace_extension(".txd").string()
                                            : desc.txdPath;
            if (auto txd = Assets::Txd::load(txdPath))
                textures_.add(*txd);
            else
                std::cerr << "[model] TXD load failed: " << txd.error().message << "\n";
        }
        const TextureSet &textures = desc.textures ? *desc.textures : textures_;

        // Fallbacks for materials that carry no texture name: a "head"/"hed" texture and a body one.
        const Renderer::Texture *headTex = nullptr;
        const Renderer::Texture *bodyTex = nullptr;
        textures.forEach([&](const std::string &key, const Renderer::Texture *tex) {
            if (!tex->hasPixels())
                return;
            if (key.find("hed") != std::string::npos || key.find("head") != std::string::npos)
                headTex = headTex ? headTex : tex;
            else
                bodyTex = bodyTex ? bodyTex : tex;
        });

        // ---- 5. Meshes (one sub-mesh per material) -------------------------------------------------------
        uint32 meshCount = 0u, triCount = 0u, skinnedMeshes = 0u;
        for (const auto &atomic : clump.atomics)
        {
            if (atomic.vertices.empty() || atomic.triangles.empty())
                continue;

            const bool useSkin = atomic.skinned && nb > 0u && atomic.boneIndices.size() == atomic.vertices.size();

            std::map<uint32, std::vector<size_t>> byMat;
            for (size_t ti = 0u; ti < atomic.triangles.size(); ++ti)
                byMat[atomic.triangles[ti].materialIndex].push_back(ti);

            // The material that reaches highest along the model's up axis is the head (used only for
            // the unnamed-material fallback).
            auto height = [&](const Math::Vector3 &p) { return upAxis_ == 0 ? p.x : (upAxis_ == 2 ? p.z : p.y); };
            uint32 headMi = 0u;
            float headMax = -1e30f;
            for (const auto &[mi, tris] : byMat)
                for (size_t ti : tris)
                {
                    const auto &t = atomic.triangles[ti];
                    const float h = std::max({height(atomic.vertices[t.a].position), height(atomic.vertices[t.b].position),
                                              height(atomic.vertices[t.c].position)});
                    if (h > headMax)
                    {
                        headMax = h;
                        headMi = mi;
                    }
                }

            // Static geometry has no skin matrices, so placement is baked into its vertices.
            const Math::Vector3 placedOrigin = rig_.placement.multiplyPoint(Math::Vector3{0.0f, 0.0f, 0.0f});

            for (const auto &[mi, tris] : byMat)
            {
                auto mesh = std::make_unique<Renderer::Mesh>();
                std::unordered_map<uint32, uint32> remap;
                std::vector<std::array<uint8_t, 4>> sIdx;
                std::vector<std::array<float, 4>> sWgt;

                auto addV = [&](uint32 vi) -> uint32
                {
                    const auto [it, inserted] = remap.try_emplace(vi, mesh->vertexCount());
                    if (!inserted)
                        return it->second;
                    const auto &v = atomic.vertices[vi];
                    if (useSkin)
                    {
                        mesh->addVertex(v.position, v.normal, v.uv); // model space; skin matrices carry placement
                        sIdx.push_back(atomic.boneIndices[vi]);
                        sWgt.push_back(atomic.weights[vi]);
                    }
                    else
                    {
                        const Math::Vector3 nrm = (rig_.placement.multiplyPoint(v.normal) - placedOrigin).normalized();
                        mesh->addVertex(rig_.placement.multiplyPoint(v.position), nrm, v.uv);
                    }
                    return it->second;
                };

                for (size_t ti : tris)
                {
                    const auto &t = atomic.triangles[ti];
                    mesh->addTriangle(addV(t.a), addV(t.b), addV(t.c));
                }
                if (mesh->triangleCount() == 0u)
                    continue;
                if (useSkin)
                    mesh->setSkinData(std::move(sIdx), std::move(sWgt));
                mesh->computeBoundingBox();

                auto material = Renderer::Material::defaultMaterial();
                material.setFlag(Engine::RenderMode::BackCull, false);

                const Renderer::Texture *tex = nullptr;
                if (mi < atomic.materials.size() && !atomic.materials[mi].textureName.empty())
                    tex = textures.findFuzzy(atomic.materials[mi].textureName);
                if (!tex) // unnamed or unmatched material
                    tex = (mi == headMi && headTex) ? headTex : bodyTex;
                if (tex)
                    textures.apply(material, tex);
                else
                    material.setDiffuse(Math::Vector4{1.0f, 0.38f, 0.16f, 1.0f}); // conspicuous "no texture" colour

                mesh->setMaterial(material);
                if (useSkin)
                    mesh->setSkinPose(pose_);
                meshes_.push_back(mesh.get());
                meshTextures_.push_back(mi < atomic.materials.size() ? atomic.materials[mi].textureName : std::string());
                scene.addEntity(std::move(mesh));
                ++meshCount;
                triCount += static_cast<uint32>(tris.size());
                if (useSkin)
                    ++skinnedMeshes;
            }
        }

        rig_.active = (skinnedMeshes > 0u);
        loaded_ = (meshCount > 0u);
        bakedInverse_ = rig_.placement.inverse();

        std::cerr << "[model] " << desc.dffPath << " meshes=" << meshCount << " tris=" << triCount
                  << " skinned=" << skinnedMeshes << " bones=" << nb << " scale=" << s
                  << " textures=" << textures_.size() << "\n";
        return loaded_;
    }

    bool Character::loadAnimations(const std::string &ifpPath)
    {
        auto res = Assets::loadIfp(ifpPath);
        if (!res)
        {
            std::cerr << "[model] IFP load failed: " << ifpPath << ": " << res.error().message << "\n";
            return false;
        }
        ifps_.push_back(std::make_shared<const Assets::IfpFile>(std::move(*res)));
        return true;
    }

    void Character::retexture(const TextureSet &textures, const std::vector<std::pair<std::string, std::string>> &swaps)
    {
        auto lowerOf = [](std::string s) {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        for (std::size_t i = 0; i < meshes_.size() && i < meshTextures_.size(); ++i)
        {
            if (meshTextures_[i].empty())
                continue;
            std::string name = lowerOf(meshTextures_[i]);
            for (const auto &[from, to] : swaps)
                if (const std::size_t at = name.find(lowerOf(from)); at != std::string::npos)
                    name.replace(at, from.size(), lowerOf(to));
            if (const Renderer::Texture *tex = textures.find(name))
            {
                Renderer::Material m = meshes_[i]->material();
                textures.apply(m, tex);
                meshes_[i]->setMaterial(m);
            }
        }
    }

    void Character::loadAnimations(std::shared_ptr<const Assets::IfpFile> ifp)
    {
        if (ifp)
            ifps_.push_back(std::move(ifp));
    }

    bool Character::attach(const std::string &dffPath, const std::string &clumpName, int16 boneId,
                           Renderer::Scene &scene, const TextureSet *textures)
    {
        if (!loaded_ || rig_.numBones == 0u)
            return false;
        const auto library = DffLibrary::load(dffPath);
        return library && attach(*library, clumpName, boneId, scene, textures);
    }

    std::vector<Renderer::Mesh *> addClumpMeshes(const Assets::DffClump &clump, Renderer::Scene &scene,
                                                 const TextureSet &textures, std::vector<std::string> *textureNames)
    {
        std::vector<Renderer::Mesh *> out;
        for (const auto &atomic : clump.atomics)
        {
            std::map<uint32, std::vector<const Assets::DffTriangle *>> byMat;
            for (const auto &t : atomic.triangles)
                byMat[t.materialIndex].push_back(&t);
            for (const auto &[mi, tris] : byMat)
            {
                auto mesh = std::make_unique<Renderer::Mesh>();
                for (const auto &v : atomic.vertices)
                    mesh->addVertex(v.position, v.normal, v.uv);
                for (const auto *t : tris)
                    mesh->addTriangle(t->a, t->b, t->c);
                mesh->computeBoundingBox();
                auto material = Renderer::Material::defaultMaterial();
                const Renderer::Texture *texture =
                    mi < atomic.materials.size() ? textures.findFuzzy(atomic.materials[mi].textureName) : nullptr;
                if (texture)
                    textures.apply(material, texture);
                mesh->setMaterial(material);
                out.push_back(mesh.get());
                if (textureNames)
                    textureNames->push_back(mi < atomic.materials.size() ? atomic.materials[mi].textureName : std::string());
                scene.addEntity(std::move(mesh));
            }
        }
        return out;
    }

    bool Character::attach(const DffLibrary &library, const std::string &clumpName, int16 boneId,
                           Renderer::Scene &scene, const TextureSet *textures, const Math::Matrix4x4 *offset)
    {
        if (!loaded_ || rig_.numBones == 0u)
            return false;
        const Assets::DffClump *found = library.find(clumpName);
        if (!found)
        {
            std::cerr << "[model] attach: no clump '" << clumpName << "' in " << library.path << "\n";
            return false;
        }
        const Assets::DffClump &clump = *found;

        // Which bone: the given id, or the highest bone of the bind pose (the head).
        Attachment att;
        att.clump = clumpName;
        int32 bone = -1;
        float32 best = -1e30f;
        for (uint32 i = 0u; i < rig_.numBones; ++i)
        {
            const Math::Matrix4x4 bind = rig_.invBindMat[i].inverse();
            const Math::Vector3 bp = bind.position();
            const float32 h = upAxis_ == 0 ? bp.x : (upAxis_ == 2 ? bp.z : bp.y);
            if (boneId >= 0 ? rig_.boneIds[i] == boneId : h > best)
            {
                best = h;
                bone = static_cast<int32>(i);
            }
        }
        if (bone < 0)
            return false;
        att.bone = static_cast<uint32>(bone);

        // A model authored around the origin rides on the bone as is; one authored in the
        // character's space (sitting where it belongs in the bind pose) first undoes the bind pose.
        Renderer::BoundingBox bounds;
        for (const auto &a : clump.atomics)
            for (const auto &v : a.vertices)
                bounds.expand(v.position);
        // (Only an unskinned one: a skinned weapon is placed by its own skeleton below, however far
        // its vertices reach from its origin - the 1 m Chainsaw_(CT) was taken for character space.)
        const bool characterSpace = clump.bones.empty() && !bounds.empty() && bounds.center().length() > 0.5f;
        att.local = characterSpace ? rig_.invBindMat[att.bone] : Math::Matrix4x4::identity();
        // A skinned clump is drawn through its skeleton as RenderWare does: frame * inverse bind
        // of the bone its vertices hang on. They cancel for heads and the bag; a weapon's master
        // bone (W_Bat_MasterBone, every vertex on it, inverse bind the identity) has its frame
        // ~10 cm out from the grip and a few degrees turned, which raw vertices leave out.
        if (!clump.bones.empty())
        {
            std::vector<uint32> votes(clump.bones.size(), 0u);
            for (const auto &a : clump.atomics)
                for (std::size_t v = 0; v < a.boneIndices.size() && v < a.weights.size(); ++v)
                {
                    const auto &w = a.weights[v];
                    const std::size_t j = static_cast<std::size_t>(std::max_element(w.begin(), w.end()) - w.begin());
                    if (a.boneIndices[v][j] < votes.size())
                        ++votes[a.boneIndices[v][j]];
                }
            const std::size_t b = static_cast<std::size_t>(std::max_element(votes.begin(), votes.end()) - votes.begin());
            if (votes[b] > 0u)
                att.local = att.local * fromColumnMajor(clump.bones[b].restLocal) * fromColumnMajor(clump.bones[b].inverseBind);
        }
        if (offset)
            att.local = *offset * att.local; // e.g. HOLSTER_TRANSLATION / _ROTATION on a belt slot

        att.meshes = addClumpMeshes(clump, scene, textures ? *textures : textures_, &meshTextures_);
        meshes_.insert(meshes_.end(), att.meshes.begin(), att.meshes.end());
        std::cerr << "[model] attached " << clumpName << " to bone " << rig_.boneIds[att.bone]
                  << (characterSpace ? " (character space)" : " (bone space)") << "\n";
        attachments_.push_back(std::move(att));
        return true;
    }

    bool Character::detach(const std::string &clumpName, int16 boneId)
    {
        const auto it = std::find_if(attachments_.begin(), attachments_.end(), [&](const Attachment &a) {
            return a.clump == clumpName && (boneId < 0 || rig_.boneIds[a.bone] == boneId);
        });
        if (it == attachments_.end())
            return false;
        for (Renderer::Mesh *mesh : it->meshes)
        {
            mesh->setVisible(false); // the scene still owns it; the level unload frees it
            if (const auto m = std::find(meshes_.begin(), meshes_.end(), mesh); m != meshes_.end())
            {
                meshTextures_.erase(meshTextures_.begin() + (m - meshes_.begin()));
                meshes_.erase(m);
            }
        }
        attachments_.erase(it);
        return true;
    }

    bool Character::hasAttachment(const std::string &clumpName, int16 boneId) const
    {
        return std::any_of(attachments_.begin(), attachments_.end(), [&](const Attachment &a) {
            return a.clump == clumpName && (boneId < 0 || rig_.boneIds[a.bone] == boneId);
        });
    }

    void Character::setMix(const std::string &name, float32 weight)
    {
        mixTarget_ = name.empty() ? 0.0f : std::clamp(weight, 0.0f, 1.0f);
        if (name.empty() || (mix_.anim && mix_.anim->name == name))
            return;
        const Assets::IfpAnimation *anim = findAnimation(name);
        ClipState next;
        if (!anim || bindClip(*anim, true, 1.0f, next) == 0u)
        {
            mixTarget_ = 0.0f;
            return;
        }
        mix_ = std::move(next);
    }

    bool Character::playOverlay(const std::string &name, bool loop)
    {
        const Assets::IfpAnimation *anim = findAnimation(name);
        if (!anim)
            return false;
        if (overlay_.anim == anim)
            return true;
        ClipState next;
        if (bindClip(*anim, loop, 1.0f, next) == 0u)
            return false;
        overlay_ = std::move(next);
        return true;
    }

    void Character::stopOverlay() noexcept
    {
        overlay_ = ClipState{};
    }

    const std::string &Character::overlayClip() const noexcept
    {
        static const std::string kNone;
        return overlay_.anim ? overlay_.anim->name : kNone;
    }

    float32 Character::overlayTime() const noexcept
    {
        return overlay_.time;
    }

    void Character::setOverlayTime(float32 t) noexcept
    {
        overlay_.time = t;
    }

    float32 Character::clipDuration(const std::string &name) const
    {
        const Assets::IfpAnimation *anim = findAnimation(name);
        return anim ? anim->duration : 0.0f;
    }

    void Character::setVisible(bool visible) noexcept
    {
        for (Renderer::Mesh *m : meshes_)
            m->setVisible(visible);
    }

    void Character::setAmbientLight(const Math::Vector4 &rgbw) noexcept
    {
        for (Renderer::Mesh *m : meshes_)
            m->setAmbientLight(rgbw);
    }

    int Character::overlapOf(const Assets::IfpAnimation &an) const
    {
        int o = 0;
        for (const auto &bt : an.bones)
            if (std::find(rig_.boneIds.begin(), rig_.boneIds.end(), bt.boneId) != rig_.boneIds.end())
                ++o;
        return o;
    }

    std::vector<std::string> Character::animationNames(bool onlyCompatible) const
    {
        std::vector<std::string> names;
        for (const auto &file : ifps_)
            for (const auto &bl : file->blocks)
                for (const auto &an : bl.animations)
                {
                    if (onlyCompatible && overlapOf(an) * 2 < static_cast<int>(rig_.numBones))
                        continue;
                    names.push_back(an.name);
                }
        return names;
    }

    const Assets::IfpAnimation *Character::findAnimation(const std::string &name) const
    {
        if (!animBlock_.empty())
            for (const auto &file : ifps_)
                if (const Assets::IfpAnimation *a = file->findAnimation(name, animBlock_))
                    return a;
        // (no block, or none in its chain has it: the first of that name anywhere: ours)
        for (const auto &file : ifps_)
            if (const Assets::IfpAnimation *a = file->findAnimation(name))
                return a;
        return nullptr;
    }

    uint32 Character::bindClip(const Assets::IfpAnimation &anim, bool loop, float32 speed, ClipState &out,
                               bool inPlace) const
    {
        out = ClipState{};
        out.anim = &anim;
        out.tracks.assign(rig_.numBones, nullptr);
        uint32 matched = 0u;
        for (uint32 i = 0u; i < rig_.numBones; ++i)
            if (const Assets::IfpBoneTrack *t = anim.findBone(rig_.boneIds[i]); t && !t->frames.empty())
            {
                out.tracks[i] = t;
                ++matched;
            }
        out.duration = (anim.duration > 1e-3f) ? anim.duration : 1.0f;
        out.speed = speed;
        out.loop = loop;
        out.playing = true;
        // A looping clip would walk away from its placement and snap back every cycle; play it in
        // place and let the controller supply the travel (rootSpeed()). One-shots keep their
        // authored root motion (falls, lunges) unless asked to play in place.
        if ((loop || inPlace) && rootMotionBone_ >= 0)
            if (const Assets::IfpBoneTrack *t = out.tracks[static_cast<size_t>(rootMotionBone_)])
                if (!Assets::trackTravel(*t, out.rootDrift, out.rootT0, out.rootT1))
                    out.rootT1 = out.rootT0 = 0.0f;
        return matched;
    }

    const std::string &Character::currentClip() const noexcept
    {
        static const std::string none;
        return clip_.anim ? clip_.anim->name : none;
    }

    float32 Character::rootSpeed(const std::string &name) const
    {
        const Assets::IfpAnimation *anim = findAnimation(name);
        return anim ? Assets::rootGroundSpeed(*anim) * scale_ : 0.0f;
    }

    bool Character::play(const std::string &name, bool loop, float32 speed)
    {
        if (!rig_.active)
            return false;
        const Assets::IfpAnimation *anim = findAnimation(name);
        if (!anim)
        {
            std::cerr << "[model] animation not found: " << name << "\n";
            return false;
        }
        ClipState next;
        const uint32 matched = bindClip(*anim, loop, speed, next);
        if (matched == 0u)
        {
            std::cerr << "[model] animation '" << name << "' shares no bone ids with this model\n";
            return false;
        }
        if (matched * 2u < rig_.numBones)
            std::cerr << "[model] WARNING: '" << name << "' drives only " << matched << "/" << rig_.numBones << " bones\n";
        clip_ = std::move(next);
        blendFrom_ = ClipState{};
        mix_ = ClipState{};
        mixWeight_ = mixTarget_ = 0.0f;
        return true;
    }

    float32 Character::rootTravel(const std::string &name) const
    {
        const Assets::IfpAnimation *anim = findAnimation(name);
        const Assets::IfpBoneTrack *t = anim ? anim->findBone(Assets::kRootMotionBoneId) : nullptr;
        float32 d[3], t0 = 0.0f, t1 = 0.0f;
        if (!t || !Assets::trackTravel(*t, d, t0, t1))
            return 0.0f;
        return std::sqrt(d[0] * d[0] + d[2] * d[2]) * scale_;
    }

    float32 Character::rootRise(const std::string &name) const
    {
        const Assets::IfpAnimation *anim = findAnimation(name);
        const Assets::IfpBoneTrack *t = anim ? anim->findBone(Assets::kRootMotionBoneId) : nullptr;
        float32 d[3], t0 = 0.0f, t1 = 0.0f;
        if (!t || !Assets::trackTravel(*t, d, t0, t1))
            return 0.0f;
        return d[1] * scale_;
    }

    bool Character::crossfade(const std::string &name, float32 duration, bool loop, float32 speed, bool inPlace)
    {
        if (!rig_.active || !clip_.anim)
        {
            if (!play(name, loop, speed))
                return false;
            if (inPlace && rootMotionBone_ >= 0)
                if (const Assets::IfpBoneTrack *t = clip_.tracks[static_cast<size_t>(rootMotionBone_)])
                    if (!Assets::trackTravel(*t, clip_.rootDrift, clip_.rootT0, clip_.rootT1))
                        clip_.rootT1 = clip_.rootT0 = 0.0f;
            return true;
        }

        const Assets::IfpAnimation *target = findAnimation(name);
        if (!target)
        {
            std::cerr << "[model] crossfade: animation not found: " << name << "\n";
            return false;
        }
        ClipState next;
        if (bindClip(*target, loop, speed, next, inPlace) == 0u)
        {
            std::cerr << "[model] crossfade: '" << name << "' shares no bone ids with this model\n";
            return false; // current animation left untouched
        }

        // The outgoing clip keeps its own time (and keeps advancing) during the blend; it used to
        // be sampled at the incoming clip's time, so it snapped to its own frame 0 first.
        blendFrom_ = std::move(clip_);
        blendFrom_.playing = blendFrom_.loop || blendFrom_.playing;
        clip_ = std::move(next);
        mix_ = ClipState{};
        mixWeight_ = mixTarget_ = 0.0f;
        blendTime_ = 0.0f;
        blendDuration_ = (duration > 1e-3f) ? duration : 0.25f;
        return true;
    }

    void Character::stop()
    {
        clip_ = ClipState{};
        blendFrom_ = ClipState{};
    }

    void Character::setPlacement(const Math::Vector3 &pos, float32 yaw) noexcept
    {
        if (!loaded_)
            return;
        // Feet position + world-Y yaw, folded into the basis captured at load(): clip space for an
        // animated skeleton (origin at the feet), else the bind-pose box centred on the origin.
        const Math::Vector3 origin = placementBone_ >= 0 ? pos : pos + Math::Vector3{0.0f, halfHeight_, 0.0f};
        rig_.placement = Math::Matrix4x4::translation(origin) * Math::Matrix4x4::rotationY(yaw) * baseTransform_;
    }

    Math::Matrix4x4 Character::localPose(uint32 i, const ClipState &clip) const noexcept
    {
        if (static_cast<int32>(i) == placementBone_)
            return Math::Matrix4x4::identity(); // posed by the placement
        Math::Matrix4x4 L = rig_.lRest[i];
        const Assets::IfpBoneTrack *track = (clip.anim && i < clip.tracks.size()) ? clip.tracks[i] : nullptr;
        if (!track)
            return L;
        // frameType 1 = rotation, 2 = rotation + translation, 3 = constant rotation + translation;
        // anything else keeps the bind pose.
        if (track->frameType < 1u || track->frameType > 3u)
            return L;
        float32 q[4], p[3];
        Assets::sampleTrack(*track, clip.time, q, p);
        setRotation(L, q);
        if (track->frameType >= 2u)
        {
            if (static_cast<int32>(i) == rootMotionBone_ && clip.rootT1 > clip.rootT0)
            {
                // Over the ground only: a clip that settles or rises (DeadBody_Pickup_*: 0.13 m
                // down) keeps that.
                const float32 u = (std::clamp(clip.time, clip.rootT0, clip.rootT1) - clip.rootT0) / (clip.rootT1 - clip.rootT0);
                p[0] -= clip.rootDrift[0] * u;
                p[2] -= clip.rootDrift[2] * u;
            }
            L.m03 = p[0];
            L.m13 = p[1];
            L.m23 = p[2];
        }
        return L;
    }

    std::vector<Character::BonePoint> Character::bonePoints() const
    {
        std::vector<BonePoint> out;
        if (!rig_.active)
            return out;
        out.reserve(rig_.numBones);
        for (uint32 i = 0u; i < rig_.numBones && i < fk_.size(); ++i)
        {
            BonePoint b;
            b.id = rig_.boneIds[i];
            const int32 par = rig_.parentIdx[i];
            b.parentId = par >= 0 ? rig_.boneIds[static_cast<size_t>(par)] : int16(-1);
            b.position = (rig_.placement * fk_[i]).position();
            out.push_back(b);
        }
        return out;
    }

    bool Character::boneTransform(int16 boneId, Math::Matrix4x4 &out) const
    {
        for (uint32 i = 0u; rig_.active && i < rig_.numBones && i < fk_.size(); ++i)
            if (rig_.boneIds[i] == boneId)
            {
                out = rig_.placement * fk_[i];
                return true;
            }
        return false;
    }

    void Character::update(float32 dt) noexcept
    {
        if (!rig_.active)
        {
            // A static model's vertices hold its load-time placement: a new one moves them whole.
            if (loaded_)
                for (Renderer::Mesh *mesh : meshes_)
                    mesh->setTransform(rig_.placement * bakedInverse_);
            return;
        }

        clip_.advance(dt);
        if (overlay_.anim)
            overlay_.advance(dt);
        // The mix clip follows the current clip's phase and eases to its weight (0.12 s: ours).
        mixWeight_ += std::clamp(mixTarget_ - mixWeight_, -dt * 8.0f, dt * 8.0f);
        if (mix_.anim && clip_.anim && clip_.duration > 1e-3f)
            mix_.time = std::fmod(std::max(0.0f, clip_.time) / clip_.duration, 1.0f) * mix_.duration;
        if (mixWeight_ <= 1e-3f && mixTarget_ <= 0.0f)
            mix_ = ClipState{};
        float32 blendW = 1.0f; // weight of clip_ against blendFrom_
        if (blendFrom_.anim)
        {
            blendFrom_.advance(dt);
            blendTime_ += dt;
            if (blendTime_ >= blendDuration_)
                blendFrom_ = ClipState{};
            else
                blendW = blendTime_ / blendDuration_;
        }

        // Skin matrix per bone (column-vector convention):
        //   M_i = placement * N_i * invBind_i,   N_i = N_parent * L_i
        for (uint32 i = 0u; i < rig_.numBones; ++i)
        {
            Math::Matrix4x4 L = localPose(i, clip_);
            if (blendFrom_.anim)
                L = blendRigid(localPose(i, blendFrom_), L, blendW);
            if (mix_.anim && mixWeight_ > 1e-3f)
                L = blendRigid(L, localPose(i, mix_), mixWeight_);
            // The overlay never moves the body itself (root and placement bones stay on clip_).
            if (overlay_.anim && overlay_.tracks[i] && static_cast<int32>(i) != rootMotionBone_ &&
                static_cast<int32>(i) != placementBone_)
                L = localPose(i, overlay_);
            const int32 par = rig_.parentIdx[i];
            fk_[i] = (par >= 0) ? fk_[static_cast<size_t>(par)] * L : L;
        }

        for (uint32 i = 0u; i < rig_.numBones; ++i)
            writeBoneMatrix(pose_->matrices.data() + static_cast<size_t>(i) * 16u, rig_.placement * fk_[i] * rig_.invBindMat[i]);
        for (const Attachment &a : attachments_)
        {
            const Math::Matrix4x4 m = rig_.placement * fk_[a.bone] * a.local;
            for (Renderer::Mesh *mesh : a.meshes)
                mesh->setTransform(m);
        }
    }

} // namespace Game
