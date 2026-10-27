// Game/physics_props.h - props that fall, slide and crush: PC_RIGID_CUBOID / PC_RIGID_SPHERE
// physics objects (exe 0x48c580, vtable 0x724b58; base 0x48b660).
//
// An entity whose entityTypeData record names a PHYSICS record gets a physics object (entity
// +0xa8, made by 0x42e230 by class). Rigid ones (cuboid / sphere) are boxes on the model's
// collision bounds (type +0x38..+0x40), inertia m (b^2 + c^2) / 12 per axis (0x48d540), their
// centre of mass CENTRE_OF_MASS from the pivot. Asleep unless TURNED_ON (0x48b9c0), they wake when
// something moves them (0x431bf0). Each frame (ours: ticks of 1/30 s):
// - Gravity: 25 m/s^2 down (0x48bb80: -25 x the time step, 0x724890), unless NO_GRAVITY or
//   IMMOVABLE; the fall is held to 30 m/s (0x724a0c). A velocity under 1e-5 (squared) is zeroed.
// - Spin resistance: the turn (angular momentum) loses SPIN_RESISTANCE a frame (0x48c760); air
//   resistance likewise the speed (ours: 0x48b730 not traced).
// - Contacts (0x48ecb0): the normal impulse -(1 + BOUNCE) v.n / (1/m + n.((I^-1 (r x n)) x r)),
//   split between the frame's contacts; a body barely moving (|v|^2 under 2 (25 dt)^2) does not
//   bounce. Friction (0x48cfd0): PROPER_FRICTION takes out the sliding up to FRICTION x mass x dt
//   per frame; SIMPLE_FRICTION drags by that much of it. Ours: the box's corners against the
//   world's collision (a line from the centre of mass to each corner); with several contacts the
//   shares are sized on the mass alone, and a resting body loses what approach is left and a
//   fifth of its turn a frame; it sleeps after half a second barely moving.
// - Another body (0x48da40, both rigid): the impulse -(1 + (BOUNCE + BOUNCE') / 2) v.n /
//   (1/m + 1/m' + both turns' shares) on each, v the contact's relative speed; a sleeping one
//   wakes. Ours: the face of one most against the other's nearest face, clipped to it, gives
//   the contacts; four passes a tick over these and the world's (out of the surfaces on the
//   first only, the friction's budget shared), and lying flat on each other they slide as one
//   and the upper lies flush.
// - A character (0x48da40, the other a PC_CHARACTER): the body takes the impulse
//   -(1 + (BOUNCE + its 0) / 2) v.n / (1/m + its 1/m + the turn's share) at the contact, v the
//   point's speed less the character's. A MADEOFIRON body hurts it by its momentum into it,
//   -(v.n) x mass, when over 200 (0x724a3c); one dropping on it from above (falling faster than
//   1 m/s, more down than across: vy < -0.2 (|vx| + |vz|), the contact's normal over 0.75 up)
//   does 1000 (0x4a0c10 type 6). Ours: the character a vertical capsule its moves give a
//   velocity; it is not pushed itself; sleeping bodies are not touched.
// - Impact (0x48cfd0): a contact closing faster than 3 m/s (0x7248c8) plays its MATERIAL's sound
//   (0x5a7020: a random id of the type's range, table 0x7ab9b0) at speed x 40 / 6, at most 40
//   (0x7248cc / 0x7248d0), once per two frames, and a player line (0x520640 context 0x1b, 0x19
//   above 30; ours: not spoken).
#pragma once
#include "engine_types.h"

#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Game/ai/ai_tuning.h"
#include "Math/matrix.h"
#include "Math/quaternion.h"
#include "Math/vector.h"

namespace Game {

class PhysicsProps {
public:
    struct Hit {
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
        Math::Vector3 normal{0.0f, 1.0f, 0.0f};
    };
    struct Character {
        int32 id = -1;
        Math::Vector3 feet{0.0f, 0.0f, 0.0f};
        float32 height = 1.8f;
        float32 radius = 0.35f;
        float32 mass = 80.0f;   // HUNTER_PHYS 80, PLAYER_PHYS 65
        Math::Vector3 velocity{0.0f, 0.0f, 0.0f}; // (set by update from its moves)
    };
    struct World {
        // The first static surface on a -> b.
        std::function<std::optional<Hit>(const Math::Vector3&, const Math::Vector3&)> hit;
        std::vector<Character> characters;
    };
    struct Impact {
        int32 body = -1;
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
        int32 volume = 0; // 0..40 of 127
    };
    // A random sound id of material `material`'s range (0x5a7020), or -1 for none (14099).
    [[nodiscard]] static int32 impactSound(int32 material, uint32 random);
    struct Crush {
        int32 body = -1, character = -1;
        float32 damage = 0.0f;
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
    };
    struct Body {
        int32 id = -1;            // the prop's placement index
        PhysicsType type;
        bool iron = false;
        int32 material = 0;
        float32 soundAt = -1e9f;  // its last impact sound
        Math::Vector3 origin0{0.0f, 0.0f, 0.0f};  // its pivot as placed (scene)
        Math::Matrix3x3 rest;                     // model -> scene as placed
        Math::Vector3 boxCentre{0.0f, 0.0f, 0.0f}, half{0.5f, 0.5f, 0.5f}; // model frame
        Math::Vector3 cm{0.0f, 0.0f, 0.0f};       // centre of mass, model frame
        Math::Vector3 x{0.0f, 0.0f, 0.0f};        // centre of mass, scene
        Math::Quaternion turn = Math::Quaternion::identity(); // since placed (scene)
        Math::Vector3 v{0.0f, 0.0f, 0.0f}, w{0.0f, 0.0f, 0.0f};
        Math::Vector3 invInertia{1.0f, 1.0f, 1.0f}; // model axes
        bool awake = false;
        float32 still = 0.0f;                     // seconds it has barely moved
        int32 contacts = 0;                       // with the world or another body this tick
        std::unordered_map<int32, float32> crushedAt; // by character: when it last hurt it
        [[nodiscard]] Math::Matrix3x3 rotation() const;     // model -> scene now
    };

    // A body for a placed prop: its pivot and rotation as placed, its box in model units.
    int32 add(int32 id, const PhysicsType& type, bool iron, const Math::Vector3& origin, const Math::Matrix3x3& rest,
              const Math::Vector3& boxMin, const Math::Vector3& boxMax);
    [[nodiscard]] int32 find(int32 id) const;
    [[nodiscard]] std::size_t size() const noexcept { return bodies_.size(); }
    [[nodiscard]] Body& body(int32 i) { return bodies_[static_cast<std::size_t>(i)]; }
    [[nodiscard]] const Body& body(int32 i) const { return bodies_[static_cast<std::size_t>(i)]; }
    // Wakes it; true the first time (its collision becomes its own).
    bool wake(int32 i);
    // Where its pivot is now, and the transform from where its meshes were placed to now.
    [[nodiscard]] Math::Vector3 origin(int32 i) const;
    [[nodiscard]] Math::Matrix4x4 transform(int32 i) const;

    // Advance the awake bodies by `dt` seconds; what they crushed.
    std::vector<Crush> update(float32 dt, const World& w);
    // A character walking into the bodies (awake or not): the largest contact impulse,
    // -v.n / (1/M + the body's share), 0 for none (the character physics' +0x10, 0x48c0a0).
    [[nodiscard]] float32 bump(const Character& c) const;
    // The impact sounds of the last update.
    [[nodiscard]] std::vector<Impact> takeImpacts() { return std::exchange(impacts_, {}); }
    // The ids of bodies another body woke (first time) since the last call.
    [[nodiscard]] std::vector<int32> takeNewlyAwake() { return std::exchange(newlyAwake_, {}); }

private:
    void integrate(Body& b, float32 dt);
    // Against the world's collision; how many contacts.
    int32 solveWorld(Body& b, float32 dt, const World& w, float32 now, bool first);
    void settle(Body& b, float32 dt, const World& w, float32 now, std::vector<Crush>& crushed);
    // Two bodies against each other (0x48da40, both rigid): a face of one clipped to the other's.
    void collide(Body& a, Body& b, bool first);
    void applyImpulse(Body& b, const Math::Vector3& r, const Math::Vector3& j) const;
    [[nodiscard]] float32 effectiveMass(const Body& b, const Math::Vector3& r, const Math::Vector3& n) const;
    [[nodiscard]] Math::Vector3 worldInvInertia(const Body& b, const Math::Vector3& v) const;

    std::vector<Body> bodies_;
    std::vector<Impact> impacts_;
    std::vector<int32> newlyAwake_; // ids woken by another body (their collision must go dynamic)
    std::unordered_map<int32, Math::Vector3> lastFeet_; // by character
    std::vector<int32> woken_;
    float32 carry_ = 0.0f;
    float32 time_ = 0.0f;
};

} // namespace Game
