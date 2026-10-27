// Game/player_field.h - level collision sphere field + the player capsule dropped into it.
#pragma once
#include "engine_types.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Math/vector.h"
#include "Physics/triangle_field.h"
#include "Renderer/scene.h"
#include "Game/level_content.h"

namespace Physics { class PhysicsEngine; class RigidBody; }
namespace Renderer { class Mesh; }

namespace Game {

class Doors;

// Builds the level's static collision: the world (.bsp) triangles, plus each placed prop's
// collisions.col entry (its triangle mesh into the same triangle field, its spheres into a
// sphere list). Drops the player onto it at the level's Player_Inst spawn (pre-simulated to rest)
// and resolves that body against it each frame.
//
// The player's shape is the "player" entry of collisions.col, as the original uses it: a body
// sphere (r 0.5, 1.15 m above the feet) that walls push against, and a leg line from 0.7 m down
// to the feet that finds the ground - floor within the leg's reach is stepped up onto.
// Owns the collision data; holds a non-owning pointer to the player RigidBody,
// which lives in the PhysicsEngine.
class PlayerField {
public:
    // Builds the field from `level`'s world triangles and `levelDir + "collisions.col"` (placed
    // with `level`'s placements + scene frame), creates the player body in `physics`, pre-simulates it to rest, and adds a hidden
    // debug marker mesh at the landing spot to `scene`.
    void load(const std::string& levelDir, const LevelContent& level,
              Physics::PhysicsEngine& physics, Renderer::Scene& scene);

    // The player's physics body (owned by the PhysicsEngine).
    [[nodiscard]] Physics::RigidBody* body() const noexcept { return playerBody_; }

    // Push the player's body sphere out of every overlapping triangle and prop sphere, then stand
    // it on the ground its leg reaches (stepping up to the leg's length, snapping down up to
    // kStepDown so slopes and stairs keep it grounded). Returns true if it is grounded.
    [[nodiscard]] bool resolveBody() const { return playerBody_ ? resolve(playerBody_) : false; }

    // The same body + leg resolve for any character whose body-sphere centre is `center` (bodyHeight()
    // above its feet): hunters move through it too. Returns true if it stands on something.
    bool resolveCharacter(Math::Vector3& center, Math::Vector3& velocity) const;
    // A prop loses its collision (NO_COL_ON_DAMAGE, the exe 0x4334e0): its triangles and
    // spheres, by its .inst placement index.
    void removeProp(int32 placement);
    // It collides again (EntityIgnoreCollisions(entity, FALSE)); nothing for a prop not removed.
    void restoreProp(int32 placement);
    // A mover (by its .inst placement index) now stands `offset` from where it was placed.
    void setMoverOffset(int32 placement, const Math::Vector3& offset);
    // A physics prop (Game/physics_props.h) that wakes takes its triangles out of the static
    // field into its own, collided through `transform` (rest world -> where it is now); false
    // when it has none. Its spheres go.
    bool makeDynamic(int32 placement);
    // A prop's own collision triangles where it was placed (empty: none).
    [[nodiscard]] std::vector<Physics::TriangleField::Triangle> propTriangles(int32 placement) const;
    void setDynamicTransform(int32 placement, const Math::Matrix4x4& transform);
    // The level's doors: characters collide with them where they have swung to.
    void setDoors(const Doors* doors) noexcept { doors_ = doors; }
    // True when no world or prop surface lies on the segment a -> b (sight lines).
    [[nodiscard]] bool lineOfSight(const Math::Vector3& a, const Math::Vector3& b) const;
    // How much of a sound at a is heard at b through the world (not the props): the product of
    // each crossed surface's MaterialTypes::soundThrough (0x4df3b0).
    [[nodiscard]] float32 soundThrough(const Math::Vector3& a, const Math::Vector3& b) const;
    // The distance from a to the first world surface (props aside) on the segment a -> b whose
    // material the audio manager's probe sees (MaterialTypes::stopsSound), if any.
    [[nodiscard]] std::optional<float32> soundHit(const Math::Vector3& a, const Math::Vector3& b) const;
    // True when no world surface (props aside) lies on the segment a -> b (the coronas' test).
    [[nodiscard]] bool worldClear(const Math::Vector3& a, const Math::Vector3& b) const;
    // The first world or prop surface on the segment a -> b, as a fraction of its length (shots).
    [[nodiscard]] std::optional<float32> raycast(const Math::Vector3& a, const Math::Vector3& b) const;
    // The same for a shot: where (fraction), the surface's normal (facing a) and its material
    // type (Game/material_types.h; props and unlisted textures: none).
    struct ShotHit {
        float32 fraction = 0.0f;
        Math::Vector3 normal{0.0f, 1.0f, 0.0f};
        uint8 material = 0;
    };
    [[nodiscard]] std::optional<ShotHit> shotHit(const Math::Vector3& a, const Math::Vector3& b) const;

    // Pull a camera at `desired` in toward `pivot` (the point it looks over) so the world geometry
    // between them does not hide the player; keeps `margin` clear of the hit surface.
    [[nodiscard]] Math::Vector3 clipCamera(const Math::Vector3& pivot, const Math::Vector3& desired,
                                           float32 margin = 0.25f) const;

    // The baked light of the floor under `p` (within a few metres), if that floor is level
    // geometry with prelight; characters use it as their ambient light.
    [[nodiscard]] std::optional<Math::Vector3> lightBelow(const Math::Vector3& p) const;
    // The footstep surface under `p` (concrete on props or off the world).
    [[nodiscard]] FootSurface surfaceBelow(const Math::Vector3& p) const;

    // The level's player start heading (yaw about scene +Y, 0 = facing -Z), from Player_Inst.
    [[nodiscard]] float32 spawnYaw() const noexcept { return spawnYaw_; }

    // Show/hide the debug spawn marker (off by default).
    void setMarkerVisible(bool v) const noexcept;

    [[nodiscard]] std::size_t sphereCount() const noexcept { return colSpheres_.size(); }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return worldTris_.size(); }
    [[nodiscard]] float32 playerRadius() const noexcept { return playerRadius_; }
    // Height of the body sphere's centre (the body's position) above the feet.
    [[nodiscard]] float32 bodyHeight() const noexcept { return bodyHeight_; }

private:
    struct WorldSphere { Math::Vector3 center; float32 radius; };

    bool resolve(Physics::RigidBody* b) const;
    void buildField(const std::string& levelDir, const LevelContent& level);
    // Also records spawnYaw_.
    [[nodiscard]] Math::Vector3 spawnPoint(const LevelContent& level);

    Physics::TriangleField worldTris_;
    std::vector<uint8> worldSurfaces_; // FootSurface per world triangle (props follow, unlisted)
    std::vector<uint8> worldMaterials_; // material type per world triangle (likewise)
    std::vector<std::array<Math::Vector4, 3>> worldLight_; // per world triangle (props have none)
    std::vector<WorldSphere> colSpheres_;
    // Each prop's triangles (in build order) and spheres, by placement index.
    struct PropShapes { uint32 firstTriangle = 0, triangles = 0; std::size_t firstSphere = 0, spheres = 0; };
    std::unordered_map<int32, PropShapes> propShapes_;
    // Props taken out by removeProp, with their spheres (for restoreProp).
    std::unordered_map<int32, std::pair<PropShapes, std::vector<WorldSphere>>> removedProps_;
    // Movers ("_(M)" props): each its own triangles, collided where it has moved to.
    struct MoverShape {
        Physics::TriangleField tris;
        Math::Vector3 offset{0.0f, 0.0f, 0.0f};
        bool rotated = false;            // a physics prop: through transform, not offset
        Math::Matrix4x4 transform, inverse;
    };
    std::unordered_map<int32, MoverShape> movers_;
    static constexpr float32 kStepDown = 0.3f; // metres the player is snapped down onto floor
    float32 playerRadius_ = 0.5f;  // body sphere radius
    const Doors* doors_ = nullptr; // the level's doors (LevelRuntime owns them)
    float32 bodyHeight_ = 1.15f;   // body sphere centre above the feet
    float32 legLength_ = 0.7f;     // the leg line's reach above the feet
    float32 spawnYaw_ = 0.0f;
    Physics::RigidBody* playerBody_ = nullptr;
    Renderer::Mesh* marker_ = nullptr; // non-owning; the scene keeps the marker mesh alive
};

} // namespace Game
