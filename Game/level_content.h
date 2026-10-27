// Game/level_content.h - composes the visible level (textures + .inst props + .bsp world).
#pragma once
#include "engine_types.h"

#include <array>
#include <memory>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

#include "Assets/inst.h"
#include "Assets/world.h"
#include "Game/level_frame.h"
#include "Game/material_types.h"
#include "Game/save_system.h"
#include "Game/texture_set.h"
#include "Physics/triangle_field.h"
#include "Renderer/light.h"
#include "Renderer/mesh.h"
#include "Renderer/scene.h"

namespace Game {

// Loads a level directory: binds modelspc.txd + scene1pc.txd, places .inst props from
// modelspc.dff, builds the RWWorld .bsp architecture into a Scene, adds the Light_Inst lamps as
// point lights and sets the scene's lighting to the level's baked light. Owns the textures the
// scene's meshes point at, so it must outlive those meshes (AssetManager guarantees that).
// What a floor sounds like underfoot: the SFX bank's SFX_FOOTSTEP_<NAME>_1..n sets.
enum class FootSurface : uint8 { Concrete, Grass, Gravel, Metal, Mud, Tile, Water, Wood };
[[nodiscard]] const char* footstepSample(FootSurface s) noexcept;
// The surface of a world texture, by keywords in its name (ours: the data gives no table;
// Bow_Bigpave -> concrete, Ind_Metalpanel -> metal, Est_2ndfloor_chipboard -> wood, AH_tile1 -> tile).
[[nodiscard]] FootSurface surfaceOfMaterial(const std::string& textureName);

class LevelContent {
public:
    // Loads all level content under `levelDir` (with a trailing slash) into `scene`.
    // `checkpoint`: a save point's entity block to build the level from instead of the .inst
    // files' (Game/checkpoint.h).
    void load(Renderer::Scene& scene, const std::string& levelDir, const std::vector<SaveEntityRecord>* checkpoint = nullptr);
    // How many placements the .inst files hold (the entity block's records, "NUL"s included).
    [[nodiscard]] int32 instCount() const noexcept { return instCount_; }

    // Shared level -> scene transform; the collision field uses it too.
    [[nodiscard]] const LevelFrame& frame() const noexcept { return frame_; }

    // The .inst placements (entity.inst followed by entity2.inst).
    [[nodiscard]] const std::vector<Assets::InstPlacement>& placements() const noexcept { return placements_; }

    // Every world (.bsp) triangle in the scene frame, visible or not ("inv_" surfaces are
    // collision-only); the static collision field is built from it.
    [[nodiscard]] const std::vector<Physics::TriangleField::Triangle>& collisionTriangles() const noexcept {
        return collisionTriangles_;
    }
    // The footstep surface of each world collisionTriangles() entry (FootSurface), from its
    // material's texture name.
    [[nodiscard]] const std::vector<uint8>& collisionSurfaces() const noexcept { return collisionSurfaces_; }
    // The material type of each collision triangle (Game/material_types.h, mat.bin).
    [[nodiscard]] const std::vector<uint8>& collisionMaterials() const noexcept { return collisionMaterials_; }
    // The baked light at the three corners of each collisionTriangles() entry.
    [[nodiscard]] const std::vector<std::array<Math::Vector4, 3>>& collisionLight() const noexcept {
        return collisionLight_;
    }

    // The level's textures (characters placed in the level, e.g. hunters, use them too).
    [[nodiscard]] const TextureSet& textures() const noexcept { return textures_; }

    // Ids of every entity added to the scene (meshes and lights, for level-scoped unload).
    [[nodiscard]] const std::vector<ObjectId>& entityIds() const noexcept { return entityIds_; }

    // A Door_Inst placement: its meshes (world-space vertices; a door swings them by their
    // transform about the vertical axis through the placement origin, the hinge) and the bounds
    // of its model in model space.
    struct DoorModel {
        const Assets::InstPlacement* placement = nullptr;
        std::vector<Renderer::Mesh*> meshes; // owned by the scene
        Math::Vector3 localMin{0.0f, 0.0f, 0.0f}, localMax{0.0f, 0.0f, 0.0f};
        bool slide = false; // an EC_SLIDEDOOR prop (a "_(SD)" record), not a Door_Inst
    };
    // A slide door's record (EC_SLIDEDOOR: the "_(SD)" models, Prison's cell doors and gates).
    [[nodiscard]] static bool isSlideDoor(const std::string& record);
    // A mover's record (EC_MOVER: the "_(M)" models, Pharm_Wks's lift, Subway's trains).
    [[nodiscard]] static bool isMover(const std::string& record);
    // A prop mesh's LOD ("_L<n>" frames; -1: every LOD, or not a prop mesh).
    [[nodiscard]] int32 lodOf(const Renderer::Mesh* mesh) const
    {
        const auto it = meshLod_.find(mesh);
        return it == meshLod_.end() ? -1 : it->second;
    }
    [[nodiscard]] const std::vector<DoorModel>& doors() const noexcept { return doors_; }

    // The meshes a placement's model was built into (empty when it has none). They live in the
    // scene; their vertices are already placed in the world.
    [[nodiscard]] const std::vector<Renderer::Mesh*>& meshesOf(const Assets::InstPlacement& p) const;
    // A prop's LOD (LOD_DATA; the exe 0x4336b0 sets it, vtable +0x38): of its model's atomics
    // named "<model>_L<n>" only LOD n's show; one without the suffix (a pane of glass) shows at
    // every LOD. Every prop starts at LOD 0 (CJ_BOX1_L0, Gen_Bin_Liner_L0; _L1 is the burst one).
    void setPropLod(const Assets::InstPlacement& p, int32 lod) const; // (the meshes live in the scene)

    // A Light_Inst's RenderWare light (CEntityLight, 0x4cc7f0): the scripts switch it
    // (SwitchLightOn / Off) and flicker it (SetLightFlicker); `intensity` is what it shines with
    // when on. Only those whose RpLight is lit by its flags have one.
    struct LevelLight {
        Renderer::Light* light = nullptr; // owned by the scene
        float32 intensity = 1.0f;
        Math::Vector3 origin{0.0f, 0.0f, 0.0f};
    };
    [[nodiscard]] const LevelLight* lightOf(int32 placement) const;

    // Props whose entityTypeData record is flagged SHADOW (LevelSetup::shadowCasters) cast a
    // real-time shadow as characters do: on baked surfaces too (Mixed lighting), not only into
    // the lamps' static shadow maps.
    void setShadowCasters(const std::vector<std::string>& records);

    // GPU texture handles owned by this level (for level-scoped unload).
    [[nodiscard]] std::vector<uint32> textureHandles() const { return textures_.handles(); }

private:
    void loadTextures(const std::string& levelDir);
    void loadPlacements(const std::string& levelDir);
    void placeProps(Renderer::Scene& scene, const std::string& levelDir);
    // The level's world geometry (scene1.bsp), or its decal layer (scene2.bsp: blended, not solid).
    void buildWorld(Renderer::Scene& scene, const Assets::WorldData& world, bool decals);
    void addMesh(Renderer::Scene& scene, std::unique_ptr<Renderer::Mesh> mesh);
    void addLights(Renderer::Scene& scene, const std::vector<std::pair<const Assets::InstPlacement*, float32>>& lamps);
    // The Light_Inst placements' own RenderWare lights (see lightOf); true when `p` has one.
    bool addRwLight(Renderer::Scene& scene, const Assets::InstPlacement& p);
    std::unordered_map<int32, LevelLight> lights_; // by placement index

    TextureSet textures_;
    LevelFrame frame_;
    std::vector<Assets::InstPlacement> placements_;
    int32 instCount_ = 0;
    std::vector<Physics::TriangleField::Triangle> collisionTriangles_;
    std::vector<std::array<Math::Vector4, 3>> collisionLight_;
    std::vector<uint8> collisionSurfaces_;
    std::vector<uint8> collisionMaterials_;
    MaterialTypes materialTypes_;
    std::vector<ObjectId> entityIds_;
    std::vector<std::pair<std::string, Renderer::Mesh*>> propMeshes_; // .inst record, mesh (owned by the scene)
    uint32 paintedShadows_ = 0;
    std::vector<DoorModel> doors_;
    std::unordered_map<const Assets::InstPlacement*, std::vector<Renderer::Mesh*>> placementMeshes_;
    std::unordered_map<const Renderer::Mesh*, int32> meshLod_; // a prop mesh's LOD (-1: every LOD)
    Math::Vector3 meanPrelight_{0.1f, 0.1f, 0.1f};
};

} // namespace Game
