// Game/doors.h - the level's swinging doors and gates (Door_Inst).
//
// A door hinges about the vertical axis through its placement origin. entityTypeData.ini gives
// it CLASS EC_DOOR and PHYSICS SWINGDOOR_PHYS (physicsTypeData.ini: PC_DOOR, MASS 20,
// MAX_OPEN_ANGLE_IN / _OUT 140, HINGE_BOUNCE 0.2, SWING_DOOR 0, HINGE_SPRING_STRENGTH 0.15,
// HINGE_SPRING_DAMP 0.08). The level script sets each one up in OnCreate: UnFreezeEntity,
// SetDoorOpenAngleIn / Out (degrees), LockEntity; later UnLockEntity opens the way and
// SetDoorOverrideAngle swings a door to an angle by itself (Jury_Turf's chute door, -90, when the
// last hunter dies).
//
// The door is a rigid cuboid (0x490450 on 0x48c580) that only turns about its hinge: its pushes
// are made level (0x4907b0, unless HANGER) and its own movement is dropped. Its inertia is the
// cuboid's on its bounds, m (w^2 + d^2) / 12 (0x48d540). θ is the signed angle between its closed
// and current width (0x4908e0); + is "in".
// - Pushed by a character (0x48da40), it takes the impulse -v.n / (1/m + 1/M + its turn's share),
//   v the contact's speed less the character's, M the character's mass (HUNTER_PHYS 80,
//   PLAYER_PHYS 65); the character is not moved by it.
// - Awake, the hinge spring turns it back (0x4909a0): the torque -(STRENGTH x MASS x θ +
//   DAMP x its turn).
// - SetDoorOverrideAngle drives it instead with 2.5 x (the angle - θ) until it is at least that
//   far open, where it stops dead and sleeps.
// - Crossing an open limit (0x490bd0, the entity's angles in / out), its turn reverses times
//   HINGE_BOUNCE. Not a SWING_DOOR and not driven, crossing shut it latches there: still, closed
//   and asleep.
// Its sounds are entity audio events (Game/entity_audio): 0x1b pushed from shut and 0x1c
// latched, each at its energy x 4, 0x20 at a limit at its energy (0x48cbe0: the turn's part,
// 0.75 x 90 x (L.w) / m); a slide door 0x21 as it starts to move and 0x22 as it gets there.
//
// A door that is frozen (never unfrozen) or locked does not move. Every character collides with
// the door's box (its model bounds) at its current angle.
#pragma once
#include "engine_types.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Game/level_content.h"
#include "Math/matrix.h"
#include "Math/vector.h"

namespace Game {

class Doors {
public:
    struct Door {
        std::string name;                         // the placement's instance name (lower case)
        Math::Vector3 hinge{0.0f, 0.0f, 0.0f};    // scene frame
        Math::Matrix4x4 toLocal;                  // scene direction -> model space, door closed
        Math::Vector3 localMin{0.0f, 0.0f, 0.0f}, localMax{0.0f, 0.0f, 0.0f};
        std::vector<Renderer::Mesh*> meshes;
        float32 angle = 0.0f;                     // radians about scene +Y; + = "in"
        float32 speed = 0.0f;                     // radians per second
        float32 lastAngle = 0.0f;                 // last frame's (the limits and the latch)
        bool limitArmed = true;                   // its next limit hit is heard (+0x188)
        bool awake = false;                       // its spring works (pushed, unlatched)
        bool driving = false;                     // SetDoorOverrideAngle's drive is on
        std::string record;                       // its entityTypeData record (lower case)
        int32 material = 0;                       // its record's MATERIAL index (sounds)
        bool sliding = false;                     // a slide door on its way
        float32 openIn = 2.443f, openOut = 2.443f; // 140 degrees (SWINGDOOR_PHYS)
        bool frozen = true;
        bool locked = false;
        std::optional<float32> overrideAngle;     // SetDoorOverrideAngle (radians)
        bool destroyed = false;                   // DestroyEntity: gone (no mesh, no collision)
        // A slide door (EC_SLIDEDOOR): it slides along its width instead of swinging.
        // SetSlideDoorAjarDistance(door, metres) is how far it opens, SetDoorState(DOOR_OPEN /
        // OPENING) opens it that far and DOOR_CLOSED / CLOSING shuts it, at SetSlideDoorSpeed
        // (metres a second). Ours: it slides toward its origin's side.
        bool slide = false;
        Math::Vector3 slideAxis{1.0f, 0.0f, 0.0f}; // scene direction of opening
        float32 offset = 0.0f, target = 0.0f, ajar = 2.0f, slideSpeed = 1.0f;
    };

    void build(const LevelContent& level);
    [[nodiscard]] Door& door(std::size_t i) { return doors_[i]; }
    // Moves its meshes to where it is now (a door set there at once: no sound).
    void place(Door& door) const;
    // A sound event of the last update (Game/entity_audio).
    struct Sound {
        std::size_t door = 0;
        uint16 event = 0;
        float32 value = 0.0f;
    };
    [[nodiscard]] std::vector<Sound> takeSounds() { return std::exchange(sounds_, {}); }
    // Its energy (0x48cbe0): its turn's, 0.75 x 90 x I w^2 / MASS (it has no movement).
    [[nodiscard]] static float32 energy(const Door& door);
    // A door made by hand (tests).
    void add(Door door) { doors_.push_back(std::move(door)); }
    // The hinge's inertia: the cuboid's on its bounds (0x48d540).
    [[nodiscard]] static float32 inertia(const Door& door);
    [[nodiscard]] std::size_t size() const noexcept { return doors_.size(); }
    [[nodiscard]] Door* find(const std::string& name);
    [[nodiscard]] const std::vector<Door>& doors() const noexcept { return doors_; }

    // A character body (sphere centre, radius) and how it moved this frame.
    struct Pusher {
        Math::Vector3 center{0.0f, 0.0f, 0.0f};
        Math::Vector3 velocity{0.0f, 0.0f, 0.0f};
        float32 radius = 0.5f;
        float32 mass = 80.0f;                     // its PHYSICS' (HUNTER_PHYS 80, PLAYER_PHYS 65)
    };
    // Swings the doors (pushes, overrides, damping, limits) and moves their meshes.
    void update(float32 dt, const std::vector<Pusher>& pushers);

    // The middle of a door's leaf when closed, and half its width (GraphModifyConnections).
    [[nodiscard]] static Math::Vector3 closedCentre(const Door& door);
    [[nodiscard]] static float32 halfWidth(const Door& door) { return (door.localMax.x - door.localMin.x) * 0.5f; }

    // Pushes a body sphere out of every door's box; returns true when it touched one.
    bool collide(Math::Vector3& center, float32 radius, Math::Vector3& velocity) const;

private:
    // The closest point of `door`'s box to `p` (scene frame), and the box's outward normal there.
    [[nodiscard]] Math::Vector3 closestPoint(const Door& door, const Math::Vector3& p) const;

    std::vector<Door> doors_;
    std::vector<Sound> sounds_;
};

} // namespace Game
