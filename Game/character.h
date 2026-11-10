// Game/character.h - generic skinned model: DFF/TXD load + IFP animation playback.
#pragma once
#include "engine_types.h"
#include "Assets/dff.h"
#include "Assets/ifp/ifp.h"
#include "Game/texture_set.h"
#include "Math/matrix.h"
#include "Math/vector.h"
#include "Renderer/mesh.h"
#include "Renderer/scene.h"
#include "Renderer/skin_pose.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Game
{

    // A parsed multi-model .dff (the level's modelspc.dff), shared by every model taken from it so
    // the file is read and parsed once rather than once per model.
    struct DffLibrary
    {
        std::string path;
        std::vector<Assets::DffClump> clumps;
        std::vector<uint8_t> bytes; // the raw file, for finding a clump by model name

        static std::shared_ptr<const DffLibrary> load(const std::string &path);
        // The clump defining model `name` ("" = the first clump); nullptr if there is none.
        [[nodiscard]] const Assets::DffClump *find(const std::string &name) const;

    private:
        mutable std::unordered_map<std::string, int> found_; // name -> clump index (-1: none), as looked up
    };

    // How to load and place a model.
    struct ModelDesc
    {
        std::string dffPath; // required (with `library`: only named in messages)
        const DffLibrary *library = nullptr; // already parsed; dffPath is then not read again
        std::string clumpName; // the clump defining this model in a multi-model .dff ("" = the first)
        std::string txdPath;   // empty = same path as the DFF with a .txd extension
        // Textures already loaded elsewhere (e.g. the level's modelspc.txd); no TXD is loaded then.
        const TextureSet *textures = nullptr;
        Math::Vector3 position{0.0f, 0.0f, 0.0f}; // where the feet stand
        float32 targetSize = 0.0f;  // > 0: uniformly scale so the largest bind-pose extent equals this
        float32 scale = 1.0f;       // used when targetSize == 0
        bool centerOnOrigin = true; // move the bind-pose bounding-box centre to `position`
        bool flipForVulkan = true;  // stand the model up: its tallest bind-pose axis becomes scene +Y
        // Pose the bones' rest from the DFF's frames (RenderWare's own hierarchy) rather than from
        // the skin's inverse binds. They agree for characters and the bag; a weapon's do not (its
        // master bone's frame sits ~10 cm from the grip, W_Bat_MasterBone), and its execute clip
        // (Bat_Sneak_Attack4_W) is authored against the frames.
        bool frameRest = false;
        bool verbose = false;       // print hierarchy / bone diagnostics
        // Optional {boneId, newParentBoneId} fixes applied after the HAnim flag walk, for a
        // skeleton whose flags give a wrong parent. The new parent must come earlier in bone order.
        // Example (bun_pc thighs): md.parentOverrides = {{1023, 1045}, {1077, 1045}};
        std::vector<std::pair<int, int>> parentOverrides;
    };

    // A skinned (or static) model plus its animation playback. One instance per model.
    //   load()           - DFF + TXD -> scene meshes, bind pose, hierarchy
    //   loadAnimations() - any number of IFP files
    //   play()/crossfade() - an animation by name; tracks are matched to bones by bone id
    //   update()         - advance time, FK, write the skin pose and move attachments
    // Each instance owns its SkinPose, so any number of characters can be drawn at once.
    // The meshes of `clump` added to `scene` (one per material), textured from `textures`; the
    // scene owns them. `textureNames` (if given) receives each mesh's material texture name.
    std::vector<Renderer::Mesh *> addClumpMeshes(const Assets::DffClump &clump, Renderer::Scene &scene,
                                                 const TextureSet &textures, std::vector<std::string> *textureNames = nullptr);

    class Character
    {
    public:
        bool load(const ModelDesc &desc, Renderer::Scene &scene);

        bool loadAnimations(const std::string &ifpPath);
        // Share an animation file already parsed for another character.
        void loadAnimations(std::shared_ptr<const Assets::IfpFile> ifp);
        // The animation block its clips are looked up in (its record's ANIMATION_BLOCK: a hunter's
        // DefaultHunterAnims plays its own DeadBody_* / Die_* / ..., not the player's of that name).
        void setAnimationBlock(std::string block) { animBlock_ = std::move(block); }

        // Add the rigid model `clumpName` from `dffPath` as an attachment that follows bone
        // `boneId` (-1 = the highest bone, i.e. the head): hunters' heads, held weapons.
        bool attach(const std::string &dffPath, const std::string &clumpName, int16 boneId, Renderer::Scene &scene,
                    const TextureSet *textures = nullptr);
        bool attach(const DffLibrary &library, const std::string &clumpName, int16 boneId, Renderer::Scene &scene,
                    const TextureSet *textures = nullptr, const Math::Matrix4x4 *offset = nullptr);
        // Takes the attachment `clumpName` (on bone `boneId`, -1 = any) off again (its meshes are
        // hidden); false if there is none.
        bool detach(const std::string &clumpName, int16 boneId = -1);
        [[nodiscard]] bool hasAttachment(const std::string &clumpName, int16 boneId = -1) const;

        // Re-texture every mesh (attachments too): each material's original texture name has the
        // `swaps` applied (case-insensitive substring replace, e.g. {"A_128", "B_128"} picks the
        // B skin) and is looked up again in `textures`. Names that do not resolve keep their texture.
        void retexture(const TextureSet &textures, const std::vector<std::pair<std::string, std::string>> &swaps);

        // Names of loaded animations. onlyCompatible = only those whose tracks cover at least half the bones.
        std::vector<std::string> animationNames(bool onlyCompatible = false) const;

        // Returns false if the animation is missing or shares no bone ids with this model.
        bool play(const std::string &name, bool loop = true, float32 speed = 1.0f);

        // Blend from the current pose to `name` over `duration` seconds (the outgoing clip keeps
        // advancing during the blend). Returns false if the target is missing or incompatible,
        // leaving the current animation untouched. `inPlace` removes a one-shot's root travel too
        // (the caller moves the model: rootTravel()).
        bool crossfade(const std::string &name, float32 duration, bool loop = true, float32 speed = 1.0f,
                       bool inPlace = false);
        // How far a clip's root motion carries it over the ground, in scene units (0 if missing).
        [[nodiscard]] float32 rootTravel(const std::string &name) const;
        // How far it carries it up (down when negative), in scene units (0 if missing).
        [[nodiscard]] float32 rootRise(const std::string &name) const;

        void stop(); // back to bind pose

        // An overlay clip on top of the current one: the bones it drives follow it, the rest keep
        // playing the base clip (the "_T" upper-body clips, e.g. Melee_Hold_Execute_Sneak_Stand_T).
        // Returns false if the clip is missing or drives none of this model's bones.
        bool playOverlay(const std::string &name, bool loop = true);
        // A second looping clip blended into the current one by `weight` (0..1), kept on the
        // current clip's phase (diagonal movement: Run_Fwd mixed with Run_Left). The weight eases
        // towards its target; an empty name or a new play() / crossfade() clears it.
        void setMix(const std::string &name, float32 weight);
        void stopOverlay() noexcept;
        // How far into the overlay clip the playback is (seconds), and moving it there.
        [[nodiscard]] float32 overlayTime() const noexcept;
        void setOverlayTime(float32 t) noexcept;
        [[nodiscard]] const std::string &overlayClip() const noexcept;

        // Playback rate of the current clip (1 = as authored).
        void setSpeed(float32 speed) noexcept { clip_.speed = speed; }
        // Name of the current clip ("" when none) and how far into it the playback is (seconds).
        [[nodiscard]] const std::string &currentClip() const noexcept;
        [[nodiscard]] float32 clipTime() const noexcept { return clip_.time; }

        // Ground speed the clip's root motion covers when played at rate 1, in scene units per
        // second (0 if the clip is missing or stays in place). Looping clips are played in place
        // (their root drift is removed), so a controller moves the model at this speed instead.
        [[nodiscard]] float32 rootSpeed(const std::string &name) const;
        // Length of a clip in seconds (0 if missing).
        [[nodiscard]] float32 clipDuration(const std::string &name) const;
        // Show or hide every mesh of the model.
        void setVisible(bool visible) noexcept;

        // Reposition the model: `pos` is where its feet stand and `yaw` turns it about world Y
        // (0 = facing -Z, the clips' forward). Takes effect at the next update().
        void setPlacement(const Math::Vector3 &pos, float32 yaw) noexcept;

        // dt in seconds.
        void update(float32 dt) noexcept;

        // Ambient light on every mesh of this model (w = 0 returns to the scene's ambient).
        void setAmbientLight(const Math::Vector4 &rgbw) noexcept;

        // One bone as last posed by update(): its IFP id, its parent's (-1 for the root) and
        // where it is in the scene.
        struct BonePoint
        {
            int16 id = -1;
            int16 parentId = -1;
            Math::Vector3 position{0.0f, 0.0f, 0.0f};
        };
        [[nodiscard]] std::vector<BonePoint> bonePoints() const;
        // The scene transform of bone `boneId` as last posed (bone space -> scene); false if the
        // model has no such bone.
        bool boneTransform(int16 boneId, Math::Matrix4x4 &out) const;
        // Place the model by a full matrix (model space -> scene), e.g. another model's bone
        // transform for a model carried in that bone's space.
        void setPlacementMatrix(const Math::Matrix4x4 &world) noexcept { rig_.placement = world; }

        bool isLoaded() const noexcept { return loaded_; }
        // A skeleton for clips to pose.
        [[nodiscard]] bool hasSkeleton() const noexcept { return rig_.numBones > 0u; }
        // Half the model's placed bind-pose height (scene units): the lift from feet to its centre.
        float32 halfHeight() const noexcept { return halfHeight_; }
        // GPU texture handles of this model's textures (to free them when it is unloaded).
        std::vector<uint32> textureHandles() const { return textures_.handles(); }
        bool isSkinned() const noexcept { return rig_.active; }
        bool isPlaying() const noexcept { return clip_.anim != nullptr && clip_.playing; }

    private:
        struct Rig
        {
            bool active = false;
            uint32 numBones = 0u;
            Math::Matrix4x4 placement{};
            std::vector<Math::Matrix4x4> invBindMat; // skin inverse-bind per bone
            std::vector<Math::Matrix4x4> lRest;      // bind-pose local (parent-relative) per bone
            std::vector<int32> parentIdx;            // -1 = root
            std::vector<int16> boneIds;              // IFP bone id per bone
        };

        // One playing animation retargeted onto the rig.
        struct ClipState
        {
            const Assets::IfpAnimation *anim = nullptr;
            std::vector<const Assets::IfpBoneTrack *> tracks; // per bone, nullptr = keep bind pose
            float32 duration = 1.0f;
            float32 time = 0.0f;
            float32 speed = 1.0f;
            bool loop = true;
            bool playing = false;
            // In-place playback of a looping clip: the root track's straight-line drift from its
            // first to its last key, removed in proportion to time.
            float32 rootDrift[3] = {0.0f, 0.0f, 0.0f};
            float32 rootT0 = 0.0f, rootT1 = 0.0f;

            void advance(float32 dt) noexcept;
        };

        const Assets::IfpAnimation *findAnimation(const std::string &name) const;
        // Bind `anim` to the rig; returns the number of bones it drives.
        uint32 bindClip(const Assets::IfpAnimation &anim, bool loop, float32 speed, ClipState &out,
                        bool inPlace = false) const;
        // Local transform of bone i under `clip` (bind pose for undriven bones).
        Math::Matrix4x4 localPose(uint32 i, const ClipState &clip) const noexcept;
        int overlapOf(const Assets::IfpAnimation &an) const;

        // A rigid model riding on one bone.
        struct Attachment
        {
            std::vector<Renderer::Mesh *> meshes; // owned by the scene
            std::string clump;                    // the model it came from
            uint32 bone = 0u;                     // rig index
            Math::Matrix4x4 local{};              // attachment model -> bone space
        };

        bool loaded_ = false;
        Rig rig_;
        int32 rootMotionBone_ = -1; // rig index of the bone whose track carries root motion (id 1000)
        int32 placementBone_ = -1;  // rig index of the root dummy posed by the placement (clip space)
        float32 scale_ = 1.0f;      // model units -> scene units

        std::vector<std::shared_ptr<const Assets::IfpFile>> ifps_; // shared: stable addresses for the clip pointers
        std::string animBlock_; // its record's ANIMATION_BLOCK (empty: the first clip of the name anywhere)
        ClipState clip_;                   // current animation
        ClipState blendFrom_;              // outgoing animation during a crossfade
        ClipState overlay_;                // partial-body clip over clip_ (bones it drives only)
        ClipState mix_;                    // looping clip blended into clip_ (setMix)
        float32 mixWeight_ = 0.0f, mixTarget_ = 0.0f;
        float32 blendTime_ = 0.0f;
        float32 blendDuration_ = 0.0f;

        TextureSet textures_; // this model's own textures (empty when ModelDesc::textures was given)

        std::vector<Math::Matrix4x4> fk_; // bone -> model space this frame, sized at load
        std::shared_ptr<Renderer::SkinPose> pose_ = std::make_shared<Renderer::SkinPose>();
        std::vector<Renderer::Mesh *> meshes_; // every mesh of this model (owned by the scene)
        std::vector<std::string> meshTextures_; // each mesh's original material texture name
        std::vector<Attachment> attachments_;

        Math::Matrix4x4 baseTransform_{}; // flip * scale * centring offset (no translation), from load()
        Math::Matrix4x4 bakedInverse_{};  // a static model: inverse of the placement baked into its vertices
        float32 halfHeight_ = 0.0f;
        int upAxis_ = 1; // the DFF axis that is "up" (0 x, 1 y, 2 z)
    };

} // namespace Game
