// Game/level_content.cpp - composes the visible level (textures + .inst props + .bsp world).
#include "Game/level_content.h"

#include "Game/checkpoint.h"

#include <algorithm>
#include <cctype>
#include <bit>
#include <iostream>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Assets/binary_reader.h"
#include "Assets/dff.h"
#include "Assets/txd.h"
#include "Renderer/light.h"
#include "Renderer/material.h"

namespace
{
    // Textures the original draws opaque even though their alpha channel contains cut-out pixels.
    // (Asm_growth and sl_brickpatch used to be listed too; they only looked wrong because the old
    // .bsp material-list scan bound the wrong texture to most world surfaces.)
    const std::vector<std::string> kForceOpaqueTextures = {"cj_plating2"};

    // RenderWare convention: materials whose texture starts with "inv_" are invisible geometry
    // (collision / trigger surfaces).
    bool isInvisibleMaterial(const std::string& textureName)
    {
        return Game::TextureSet::normalizeName(textureName).starts_with("inv_");
    }

    // Shadows painted into the level as blended decals: CJ_SHADOW (props), Bow_Van_Shadow,
    // Chu_Shadow_van1, Bow_Shadow_Skip, Est_TreeShadow2, ... An opaque texture with "shadow" in
    // its name (zoo_tarmacfloorSHADOW) is a floor, not a decal.
    bool isPaintedShadow(const std::string& textureName, const Renderer::Material& material)
    {
        return material.isFlag(Engine::RenderMode::BlendOn) &&
               Game::TextureSet::normalizeName(textureName).find("shadow") != std::string::npos;
    }

    // Static models: props, doors and lamps (a Light_Inst is a lamp model that also emits light).
    // Hunter_Inst are animated characters; Trigger/Player are logic-only.
    bool isVisualProp(const std::string& cls)
    {
        return cls == "Base_Inst" || cls == "Door_Inst" || cls == "Light_Inst";
    }

    // Light_Inst parameters, as CEntityLight's loader (0x4cce20) reads them: [0] type (0
    // directional, 1 spot, 2 soft spot, 3 point: RpLightCreate 1 / 0x81 / 0x82 / 0x80), [1] cone
    // angle (degrees), [2] radius, [3..5] colour (float), [6] / [7] whether it lights the atomics
    // / the world (their low byte; a directional one neither), [8..12] flags (bit 0: [10] lights
    // only while its sector is seen, 0x4cdb70), [13] size (float, at most 10), [14] intensity
    // (int, over 256 becomes 196). The light starts dark (0x4ccfa7) and each frame is lit by those
    // two flags (0x4cdb70); with neither it never shines.
    // In the original most lamps light nothing at runtime: Jury_Turf's have type 0, radius 0 and
    // no flags; their glow is the prelight plus corona / light-cone sprites. As dynamic lights
    // (Mixed / Dynamic lighting) those shine down as spots; range and cone are our choice, the
    // intensity is theirs (65 with a windowed inverse-square falloff gives about the brightness of
    // the prelit pools below). The few with flags (the scripts' Dynamic_light1_(L), the
    // searchlights, Mall's bar light) are RenderWare lights instead (addRwLight).
    constexpr float32 kLampRange = 18.0f;
    constexpr float32 kLampConeInner = 0.61f; // 35 degrees half-angle
    constexpr float32 kLampConeOuter = 1.13f; // 65 degrees
    constexpr float32 kLampIntensity = 65.0f;  // when the placement gives none
    constexpr float32 kLampBelowTop = 0.35f;   // the bulb sits this far below the lamp's top

} // namespace

namespace Game {

void LevelContent::load(Renderer::Scene& scene, const std::string& levelDir, const std::vector<SaveEntityRecord>* checkpoint)
{
    loadTextures(levelDir);
    loadPlacements(levelDir);
    if (!materialTypes_.load(levelDir + "../GLOBAL/DATA/mat.bin"))
        std::cerr << "level: no material types (levels/GLOBAL/DATA/mat.bin)\n";
    if (checkpoint)
        Checkpoint::apply(placements_, *checkpoint); // 0x437650(0): the level as the save has it

    // The world bounding box anchors the shared scene transform for world, props and collision.
    auto world = Assets::World::load(levelDir + "scene1.bsp");
    if (world)
    {
        Math::Vector3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        for (const auto& m : world->meshes)
        {
            mn = {std::min(mn.x, m.bboxMin.x), std::min(mn.y, m.bboxMin.y), std::min(mn.z, m.bboxMin.z)};
            mx = {std::max(mx.x, m.bboxMax.x), std::max(mx.y, m.bboxMax.y), std::max(mx.z, m.bboxMax.z)};
        }
        // Recentre on the world's bounding box (keeps coordinates small for float precision) but
        // keep RenderWare's natural scale: 1 unit = 1 metre, the unit the character, the player
        // capsule and the controller tuning use. (The level used to be shrunk so its largest
        // extent spanned 34 units, which made characters several times too large for it.)
        if (!world->meshes.empty())
        {
            frame_.center = (mn + mx) * 0.5f;
            frame_.scale = 1.0f;
        }
        std::cerr << "level: world rw=0x" << std::hex << world->rwLibraryId << std::dec
                  << " leaves=" << world->leafCount << " layoutOk=" << world->layoutInvariantOk
                  << " verts=" << world->totalVerts << " tris=" << world->totalTris
                  << " droppedTris=" << world->droppedTriangles
                  << " materials=" << world->materialNames.size() << "\n";
    }
    else
    {
        std::cerr << "level: world load failed: " << world.error().message << "\n";
    }

    placeProps(scene, levelDir);
    if (world)
        buildWorld(scene, *world, false);
    // scene2.bsp is the level's decal layer, drawn over scene1: court markings, graffiti, blood
    // and stains on the floor, the painted shadows under vans and skips (Jury_Turf: 29 KB).
    if (auto decals = Assets::World::load(levelDir + "scene2.bsp"))
        buildWorld(scene, *decals, true);

    // The level is lit by its baked prelight; characters get the average of it as ambient
    // until they sample the ground under them.
    Renderer::SceneLighting lighting;
    lighting.mode = Renderer::LightingMode::Baked;
    lighting.ambient = meanPrelight_;
    scene.setLighting(lighting);
}

void LevelContent::loadTextures(const std::string& levelDir)
{
    for (const char* name : {"pak/modelspc.txd", "pak/scene1pc.txd"})
    {
        // Bound (made current) so DFF material texture names resolve against the dictionary.
        auto txd = Assets::Txd::loadAndBind(levelDir + name);
        if (!txd)
        {
            std::cerr << "level: " << txd.error().message << "\n";
            continue;
        }
        const std::size_t before = textures_.size();
        textures_.add(*txd);
        std::cerr << "level: " << name << " textures=" << (textures_.size() - before) << "\n";
    }
    textures_.setForceOpaque(kForceOpaqueTextures);
}

void LevelContent::loadPlacements(const std::string& levelDir)
{
    for (const char* name : {"entity.inst", "entity2.inst"})
    {
        auto inst = Assets::Inst::load(levelDir + name);
        if (!inst)
        {
            std::cerr << "level: " << name << ": " << inst.error().message << "\n";
            continue;
        }
        for (Assets::InstPlacement& p : *inst)
            p.index = instCount_++;
        placements_.insert(placements_.end(), inst->begin(), inst->end());
    }
}

void LevelContent::addMesh(Renderer::Scene& scene, std::unique_ptr<Renderer::Mesh> mesh)
{
    mesh->computeBoundingBox();
    if (mesh->paintedShadow())
        ++paintedShadows_;
    entityIds_.push_back(mesh->id());
    scene.addEntity(std::move(mesh));
}

void LevelContent::placeProps(Renderer::Scene& scene, const std::string& levelDir)
{
    const std::string dffPath = levelDir + "pak/modelspc.dff";
    auto clumps = Assets::Dff::load(dffPath);
    auto dffBytes = Assets::readFileBytes(dffPath);
    if (!clumps || !dffBytes)
    {
        std::cerr << "level: props skipped: " << (clumps ? dffBytes.error().message : clumps.error().message) << "\n";
        return;
    }

    std::vector<std::string> records;
    for (const auto& p : placements_)
        if (isVisualProp(p.entityClass))
            records.push_back(p.record);
    const auto propMap = Assets::clumpIndexByName(*dffBytes, records);

    uint32 placed = 0, skippedNoModel = 0, meshes = 0, untextured = 0, prelit = 0;
    std::vector<std::pair<const Assets::InstPlacement*, float32>> lamps; // with the lamp's top height
    for (const auto& p : placements_)
        if (p.entityClass == "Light_Inst")
            addRwLight(scene, p); // (one without a model too: Dynamic_light1_(L))
    for (const auto& p : placements_)
    {
        if (!isVisualProp(p.entityClass))
            continue;
        const auto it = propMap.find(p.record);
        if (it == propMap.end() || it->second >= static_cast<int>(clumps->size()))
        {
            ++skippedNoModel; // no model for this record (e.g. 'searchable' logic entities)
            continue;
        }
        ++placed;

        // .inst positions are where the model ORIGIN goes (RW semantics), at natural scale.
        const Math::Vector3 origin = frame_.placementOrigin(p.position);
        const Math::Matrix4x4 rotation = LevelFrame::placementRotation(p.rotation);
        float32 top = origin.y;
        DoorModel* door = nullptr;
        if (p.entityClass == "Door_Inst" || isSlideDoor(p.record))
        {
            door = &doors_.emplace_back();
            door->placement = &p;
            door->slide = p.entityClass != "Door_Inst";
            door->localMin = {1e30f, 1e30f, 1e30f};
            door->localMax = {-1e30f, -1e30f, -1e30f};
        }

        for (const auto& atomic : (*clumps)[static_cast<size_t>(it->second)].atomics)
        {
            if (atomic.prelit)
                ++prelit;
            // Its LOD: the frame name's "_L<n>" (none: every LOD).
            int32 lod = -1;
            if (const std::size_t at = atomic.frameName.rfind("_L"); at != std::string::npos && at + 2 < atomic.frameName.size() &&
                std::all_of(atomic.frameName.begin() + static_cast<std::ptrdiff_t>(at + 2), atomic.frameName.end(),
                            [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                lod = std::stoi(atomic.frameName.substr(at + 2));
            std::map<uint32, std::vector<const Assets::DffTriangle*>> byMaterial;
            for (const auto& t : atomic.triangles)
                byMaterial[t.materialIndex].push_back(&t);

            for (const auto& [mi, tris] : byMaterial)
            {
                const Assets::DffMaterial* dmat = mi < atomic.materials.size() ? &atomic.materials[mi] : nullptr;
                if (dmat && isInvisibleMaterial(dmat->textureName))
                    continue;

                auto mesh = std::make_unique<Renderer::Mesh>();
                std::unordered_map<uint32, uint32> remap;
                auto vertex = [&](uint32 vi) {
                    const auto [pos, inserted] = remap.try_emplace(vi, mesh->vertexCount());
                    if (inserted)
                    {
                        const auto& v = atomic.vertices[vi];
                        const Math::Vector3 world = frame_.placedPoint(rotation, origin, v.position);
                        if (door)
                        {
                            const Math::Vector3& l = v.position;
                            door->localMin = {std::min(door->localMin.x, l.x), std::min(door->localMin.y, l.y), std::min(door->localMin.z, l.z)};
                            door->localMax = {std::max(door->localMax.x, l.x), std::max(door->localMax.y, l.y), std::max(door->localMax.z, l.z)};
                        }
                        top = std::max(top, world.y);
                        mesh->addVertex(world, LevelFrame::placedNormal(rotation, v.normal), v.uv, v.color);
                    }
                    return pos->second;
                };
                for (const auto* t : tris)
                    mesh->addTriangle(vertex(t->a), vertex(t->b), vertex(t->c));

                auto material = Renderer::Material::defaultMaterial();
                material.setFlag(Engine::RenderMode::BackCull, false);
                if (dmat)
                    material.setDiffuse(Math::Vector4{dmat->color.x, dmat->color.y, dmat->color.z, 1.0f});
                const Renderer::Texture* tex = dmat ? textures_.find(dmat->textureName) : nullptr;
                if (tex)
                    textures_.apply(material, tex);
                else
                    ++untextured; // renders as the RW material colour
                mesh->setMaterial(material);
                mesh->setPaintedShadow(dmat && isPaintedShadow(dmat->textureName, material));
                // Prelit props carry the level's baked light; the rest are lit like characters.
                mesh->setLighting(atomic.prelit ? Renderer::MeshLighting::Baked : Renderer::MeshLighting::Dynamic);
                if (p.entityClass == "Light_Inst" || mesh->paintedShadow())
                    mesh->setCastsShadow(false); // the lamp head would shadow its own light
                propMeshes_.emplace_back(p.record, mesh.get());
                placementMeshes_[&p].push_back(mesh.get());
                meshLod_[mesh.get()] = lod;
                if (lod > 0)
                    mesh->setVisible(false);
                if (door)
                {
                    mesh->setMovingCaster(true); // it swings: redrawn into the lamp shadows every frame
                    door->meshes.push_back(mesh.get());
                }
                addMesh(scene, std::move(mesh));
                ++meshes;
            }
        }
        if (p.entityClass == "Light_Inst" && !lights_.contains(p.index))
            lamps.emplace_back(&p, top);
    }
    std::cerr << "level: props placed=" << placed << " skippedNoModel=" << skippedNoModel
              << " meshes=" << meshes << " untextured=" << untextured << " prelitAtomics=" << prelit << "\n";
    addLights(scene, lamps);
}

const std::vector<Renderer::Mesh*>& LevelContent::meshesOf(const Assets::InstPlacement& p) const
{
    static const std::vector<Renderer::Mesh*> kNone;
    const auto it = placementMeshes_.find(&p);
    return it == placementMeshes_.end() ? kNone : it->second;
}

void LevelContent::setPropLod(const Assets::InstPlacement& p, int32 lod) const
{
    for (Renderer::Mesh* m : meshesOf(p))
    {
        const auto it = meshLod_.find(m);
        const int32 own = it == meshLod_.end() ? -1 : it->second;
        m->setVisible(own < 0 || own == lod);
    }
}

void LevelContent::setShadowCasters(const std::vector<std::string>& records)
{
    uint32 marked = 0;
    for (const auto& [record, mesh] : propMeshes_)
    {
        const bool caster = std::any_of(records.begin(), records.end(), [&](const std::string& r) {
            return TextureSet::normalizeName(r) == TextureSet::normalizeName(record);
        });
        if (caster && !mesh->paintedShadow())
        {
            mesh->setCastsShadow(true);
            mesh->setMovingCaster(true);
            ++marked;
        }
    }
    std::cerr << "level: shadow-casting prop meshes=" << marked << " painted shadow meshes=" << paintedShadows_ << "\n";
}

const LevelContent::LevelLight* LevelContent::lightOf(int32 placement) const
{
    const auto it = lights_.find(placement);
    return it == lights_.end() ? nullptr : &it->second;
}

bool LevelContent::addRwLight(Renderer::Scene& scene, const Assets::InstPlacement& p)
{
    const auto param = [&](std::size_t i) { return i < p.params.size() ? p.params[i] : 0u; };
    const uint32 type = param(0);
    uint8 flags = 0;
    if (type != 0)
        flags = static_cast<uint8>(((param(6) & 0xffu) ? 1 : 0) | ((param(7) & 0xffu) ? 2 : 0));
    if (flags == 0 || type > 3)
        return false;
    auto light = std::make_unique<Renderer::Light>();
    const Math::Vector3 origin = frame_.placementOrigin(p.position);
    light->setPosition(origin); // the entity's frame
    if (type == 3)
        light->setLightType(Renderer::LightType::Point);
    else
    {
        // A spot shines along its frame's at axis: the level's +Z turned by the placement (the
        // model's up, placementRotation). Ours: a soft spot fades from half its cone.
        light->setLightType(Renderer::LightType::Spot);
        light->setDirection(LevelFrame::placedNormal(LevelFrame::placementRotation(p.rotation), {0.0f, 1.0f, 0.0f}));
        const float32 cone = std::clamp(Math::radians(static_cast<float32>(static_cast<int32>(param(1)))), 0.01f, 1.55f);
        light->setCone(type == 2 ? cone * 0.5f : cone * 0.95f, cone);
    }
    light->setColor({std::bit_cast<float32>(param(3)), std::bit_cast<float32>(param(4)), std::bit_cast<float32>(param(5)), 1.0f});
    light->setRadius(std::max(0.01f, static_cast<float32>(static_cast<int32>(param(2)))));
    light->setIntensity(1.0f);
    light->setRwFlags(flags);
    lights_[p.index] = {light.get(), 1.0f, origin};
    entityIds_.push_back(light->id());
    scene.addEntity(std::move(light));
    std::cerr << "level: light " << p.internalName << " type " << type << " flags " << int(flags) << " at (" << origin.x << ", "
              << origin.y << ", " << origin.z << ")\n";
    return true;
}

void LevelContent::addLights(Renderer::Scene& scene, const std::vector<std::pair<const Assets::InstPlacement*, float32>>& lamps)
{
    for (const auto& [p, top] : lamps)
    {
        auto light = std::make_unique<Renderer::Light>();
        const Math::Vector3 origin = frame_.placementOrigin(p->position);
        light->setLightType(Renderer::LightType::Spot);
        light->setPosition(Math::Vector3{origin.x, top - kLampBelowTop, origin.z}); // the lamp head
        light->setDirection(Math::Vector3{0.0f, -1.0f, 0.0f});
        light->setCone(kLampConeInner, kLampConeOuter);
        light->setCastingShadow(true);
        Math::Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
        if (p->params.size() > 5)
            color = {std::bit_cast<float32>(p->params[3]), std::bit_cast<float32>(p->params[4]),
                     std::bit_cast<float32>(p->params[5]), 1.0f};
        light->setColor(color);
        light->setRadius(kLampRange);
        const int32 intensity = p->params.size() > 14 ? static_cast<int32>(p->params[14]) : 0;
        light->setIntensity(intensity > 0 ? static_cast<float32>(intensity) : kLampIntensity);
        entityIds_.push_back(light->id());
        scene.addEntity(std::move(light));
    }
    std::cerr << "level: lights=" << lamps.size();
    for (const auto& [p, top] : lamps)
    {
        const Math::Vector3 o = frame_.placementOrigin(p->position);
        std::cerr << " (" << o.x << ", " << o.z << ")";
    }
    std::cerr << "\n";
}

const char* footstepSample(FootSurface s) noexcept
{
    switch (s)
    {
    case FootSurface::Grass: return "SFX_FOOTSTEP_GRASS";
    case FootSurface::Gravel: return "SFX_FOOTSTEP_GRAVEL";
    case FootSurface::Metal: return "SFX_FOOTSTEP_METAL";
    case FootSurface::Mud: return "SFX_FOOTSTEP_MUD";
    case FootSurface::Tile: return "SFX_FOOTSTEP_TILE";
    case FootSurface::Water: return "SFX_FOOTSTEP_WATER";
    case FootSurface::Wood: return "SFX_FOOTSTEP_WOOD";
    case FootSurface::Concrete: break;
    }
    return "SFX_FOOTSTEP_CONCRETE";
}

FootSurface surfaceOfMaterial(const std::string& textureName)
{
    std::string n = textureName;
    for (char& c : n)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto has = [&](const char* word) { return n.find(word) != std::string::npos; };
    if (has("grass") || has("lawn") || has("turf"))
        return FootSurface::Grass;
    if (has("gravel") || has("shingle"))
        return FootSurface::Gravel;
    if (has("water") || has("puddle"))
        return FootSurface::Water;
    if (has("mud"))
        return FootSurface::Mud;
    if (has("metal") || has("steel") || has("grate") || has("grill") || has("iron"))
        return FootSurface::Metal;
    if (has("wood") || has("plywood") || has("chipboard") || has("plank") || has("board"))
        return FootSurface::Wood;
    if (has("tile"))
        return FootSurface::Tile;
    return FootSurface::Concrete;
}

void LevelContent::buildWorld(Renderer::Scene& scene, const Assets::WorldData& world, bool decals)
{
    uint32 meshes = 0, tris = 0, textured = 0, hidden = 0;
    std::vector<std::string> missing;
    Math::Vector3 lightSum{0.0f, 0.0f, 0.0f};
    std::size_t lightCount = 0;
    std::set<std::array<std::array<int64, 3>, 3>> drawnTriangles; // corner positions in cm, sorted
    uint32 duplicates = 0;
    for (const auto& leaf : world.meshes)
    {
        // Every triangle collides, including invisible "inv_" collision surfaces.
        const bool hasLight = leaf.prelight.size() == leaf.positions.size();
        for (const auto& t : leaf.triangles)
        {
            if (decals)
                break; // decals are not solid
            collisionTriangles_.push_back({frame_.worldPoint(leaf.positions[t.a]), frame_.worldPoint(leaf.positions[t.b]),
                                           frame_.worldPoint(leaf.positions[t.c])});
            const Math::Vector4 white{1.0f, 1.0f, 1.0f, 1.0f};
            collisionLight_.push_back({hasLight ? leaf.prelight[t.a] : white, hasLight ? leaf.prelight[t.b] : white,
                                       hasLight ? leaf.prelight[t.c] : white});
            const std::string& texture = t.materialIndex < world.materialNames.size() ? world.materialNames[t.materialIndex] : std::string();
            collisionSurfaces_.push_back(static_cast<uint8>(surfaceOfMaterial(texture)));
            collisionMaterials_.push_back(materialTypes_.typeOf(texture));
        }
        if (hasLight && !decals)
            for (const Math::Vector4& c : leaf.prelight)
            {
                lightSum += Math::Vector3{c.x, c.y, c.z};
                ++lightCount;
            }

        if (leaf.normals.size() != leaf.positions.size() || leaf.uvs.size() != leaf.positions.size())
            continue;

        // One sub-mesh per material so every face carries its own texture. A triangle stored a
        // second time (204 in Jury_Turf's scene1.bsp, often with different prelight) is drawn
        // once: two coplanar copies z-fight into bright speckles in the baked lighting.
        std::map<uint32, std::vector<const Assets::WorldTriangle*>> byMaterial;
        for (const auto& t : leaf.triangles)
        {
            std::array<std::array<int64, 3>, 3> v{};
            const uint32 corner[3] = {t.a, t.b, t.c};
            for (int k = 0; k < 3; ++k)
                v[static_cast<std::size_t>(k)] = {std::llround(leaf.positions[corner[k]].x * 100.0f),
                                                  std::llround(leaf.positions[corner[k]].y * 100.0f),
                                                  std::llround(leaf.positions[corner[k]].z * 100.0f)};
            std::sort(v.begin(), v.end());
            if (!drawnTriangles.insert(v).second)
            {
                ++duplicates;
                continue;
            }
            byMaterial[t.materialIndex].push_back(&t);
        }

        for (const auto& [mi, group] : byMaterial)
        {
            const std::string& texName = mi < world.materialNames.size() ? world.materialNames[mi] : std::string();
            if (isInvisibleMaterial(texName))
            {
                hidden += static_cast<uint32>(group.size());
                continue;
            }

            auto mesh = std::make_unique<Renderer::Mesh>();
            std::unordered_map<uint32, uint32> remap;
            auto vertex = [&](uint32 vi) {
                const auto [pos, inserted] = remap.try_emplace(vi, mesh->vertexCount());
                if (inserted)
                    // UVs pass through: raster row 0 is the image top and maps to v = 0, as in D3D.
                    mesh->addVertex(frame_.worldPoint(leaf.positions[vi]), LevelFrame::worldNormal(leaf.normals[vi]),
                                    leaf.uvs[vi], hasLight ? leaf.prelight[vi] : Math::Vector4{1.0f, 1.0f, 1.0f, 1.0f});
                return pos->second;
            };
            for (const auto* t : group)
                mesh->addTriangle(vertex(t->a), vertex(t->b), vertex(t->c));

            Renderer::Material material = Renderer::Material::defaultMaterial();
            if (const Renderer::Texture* tex = textures_.find(texName))
            {
                textures_.apply(material, tex);
                ++textured;
            }
            else if (!texName.empty())
            {
                missing.push_back(texName);
            }
            if (decals)
            {
                // Laid over the main world: alpha-blended in the forward pass (with its decal depth
                // bias, so a decal on the floor is not lost to the floor), casting no shadow.
                material.setFlag(Engine::RenderMode::AlphaTest, false);
                material.setFlag(Engine::RenderMode::BlendOn, true);
                mesh->setCastsShadow(false);
            }
            mesh->setMaterial(material);
            mesh->setPaintedShadow(isPaintedShadow(texName, material));
            mesh->setLighting(hasLight ? Renderer::MeshLighting::Baked : Renderer::MeshLighting::Dynamic);
            tris += mesh->triangleCount();
            addMesh(scene, std::move(mesh));
            ++meshes;
        }
    }
    if (lightCount > 0 && !decals)
        meanPrelight_ = lightSum * (1.0f / static_cast<float32>(lightCount));
    std::sort(missing.begin(), missing.end());
    missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
    std::cerr << (decals ? "level: decal layer meshes=" : "level: world meshes=") << meshes << " tris=" << tris << " textured=" << textured
              << " hiddenInvTris=" << hidden << " duplicateTris=" << duplicates << " missingTextures=" << missing.size()
              << " collisionTris=" << collisionTriangles_.size() << " typed="
              << std::count_if(collisionMaterials_.begin(), collisionMaterials_.end(), [](uint8 m) { return m != 0; })
              << " scale=" << frame_.scale << " meanPrelight=("
              << meanPrelight_.x << ", " << meanPrelight_.y << ", " << meanPrelight_.z << ")\n";
    for (const auto& name : missing)
        std::cerr << "level:   [missingtex] " << name << "\n";
}

bool LevelContent::isSlideDoor(const std::string& record)
{
    std::string r(record);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return r.find("(SD)") != std::string::npos;
}

bool LevelContent::isMover(const std::string& record)
{
    std::string r(record);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return r.find("_(M)") != std::string::npos;
}

} // namespace Game
