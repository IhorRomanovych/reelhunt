// Game/gunplay.h - the player's firearms: lock-on, where a shot lands, damage by hit zone,
// recoil and ammo. Pure math (no scene, no SDL) so it is unit-tested.
//
// From the data (WeaponTypeData.ini WC_AMMO records, ShotTypeData.ini, the player record; see
// Game/ai/ai_tuning.h Firearm / PlayerAim):
//   lock-on      the nearest enemy within AIM_LOCKON_DIST and the AIM_LOCKON_ANGLES cone of the
//                camera's view; LOCKON_TARGET_RANGES: the head is aimed at by itself nearer than
//                the first, and may be picked by the player nearer than the second
//   the shot     SP_LINE of SIZE metres; MAX_SHOTS lines spread over SHOT_SPREAD degrees
//   damage       DAMAGE x TORSO / ARM / LEG_DAMAGE_MULTIPLIER; a head shot takes 1 / NUM_HEAD_SHOTS
//                of the victim's hit points
//   recoil       RECOIL_DIST, RECOIL_TIME, RECOIL_VERT_ACCUMULATOR, MAX_RECOIL_VERT_DIST, the
//                player's CROUCH_RECOIL
// From the exe (the ammo weapon's methods, 0x4f8b40 on, and the ammo box handler 0x463ed0):
//   ammo         each gun holds its own rounds: a clip of CLIP_AMMO and spare rounds, at most
//                MAX_CLIPS x CLIP_AMMO in all; boxes add a fixed count to a matching gun he carries
// Ours: the radii of the body parts shots meet (the parts follow the hunter's posed bones).
#pragma once
#include "engine_types.h"

#include <array>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Game/ai/ai_tuning.h"
#include "Math/vector.h"

namespace Game {

enum class HitZone { None, Head, Torso, Arm, Leg };
const char* hitZoneName(HitZone z);

// The fallback body (a character without the biped bones; ours): a cylinder of kBodyRadius from
// the feet to kNeckHeight (legs below kHipHeight, arms further than kShoulderHalfWidth to either
// side of the spine above it), then the head, a narrower cylinder up to kHeadTop.
inline constexpr float32 kBodyRadius = 0.3f;
inline constexpr float32 kHipHeight = 0.9f;
inline constexpr float32 kNeckHeight = 1.5f;
inline constexpr float32 kHeadRadius = 0.15f;
inline constexpr float32 kHeadTop = 1.85f;
inline constexpr float32 kShoulderHalfWidth = 0.18f;

struct BodyHit {
    HitZone zone = HitZone::None;
    float32 distance = 0.0f;              // along the shot
    Math::Vector3 point{0.0f, 0.0f, 0.0f};
    bool left = false;                    // on the victim's left side
    bool fromFront = true;                // the shot came from in front of it
};

// One line of a shot, from the gun to where it stopped; `body`: it ended on a body.
struct ShotLine {
    Math::Vector3 from{0.0f, 0.0f, 0.0f}, to{0.0f, 0.0f, 0.0f};
    bool body = false;
};

// One part of a posed body: a capsule round the bone segment a -> b.
struct BodyPart {
    Math::Vector3 a{0.0f, 0.0f, 0.0f}, b{0.0f, 0.0f, 0.0f};
    float32 radius = 0.1f;
    HitZone zone = HitZone::Torso;
    bool left = false;
};

// The parts of a game biped posed as `bones` (IFP bone id -> scene position): head (neck 1040
// to head 1001, carried on to the crown), torso (pelvis 1045 to neck), clavicles, upper and lower
// arms and the hands (left 1003-1039-1020-1021-1006, right 1057-1093-1074-1075-1060), thighs,
// calves and feet (left 1023-1002-1019-1024, right 1077-1056-1073-1078). The ids and sides are
// read off the hunters' skeleton; the radii are ours. Empty if a bone is missing.
std::vector<BodyPart> bodyParts(const std::unordered_map<int16, Math::Vector3>& bones);

// Where the line origin + dir * t (dir of unit length, 0 <= t <= range) first meets `parts`
// (fromFront from the character's `feet` and `yaw`). zone None: it misses.
BodyHit shotHitsParts(const std::vector<BodyPart>& parts, const Math::Vector3& feet, float32 yaw,
                      const Math::Vector3& origin, const Math::Vector3& dir, float32 range);
// The same against the fallback cylinders of a character standing at `feet` facing `yaw`.
BodyHit shotHitsBody(const Math::Vector3& feet, float32 yaw, const Math::Vector3& origin, const Math::Vector3& dir,
                     float32 range);

// The point a lock-on aims at: the middle of the head, or the chest.
Math::Vector3 aimPoint(const Math::Vector3& feet, bool head);
// Whether a lock at `distance` metres aims at the head: always nearer than LOCKON_TARGET_RANGES[0],
// when the player asks for it (`wantHead`) nearer than [1].
bool lockOnHead(const Firearm& gun, float32 distance, bool wantHead);
// Whether `target` lies within AIM_LOCKON_DIST of `eye` and inside the AIM_LOCKON_ANGLES cone about
// the view (yaw, pitch; radians, pitch up positive).
bool inLockonCone(const PlayerAim& aim, const Math::Vector3& eye, float32 yaw, float32 pitch, const Math::Vector3& target);

// The damage a shot of `gun` does hitting `zone` of a victim that started with `fullHealth`.
// `hardHead`: a HARDHEAD victim (Ramirez) and the player take head shots as the torso multiplier
// times DAMAGE, not 1 / NUM_HEAD_SHOTS of their health (exe 0x4a035b).
float32 zoneDamage(const Firearm& gun, HitZone zone, float32 fullHealth, bool hardHead = false);

// One pellet's direction: `dir` turned by a random angle up to half of `spreadDegrees`.
Math::Vector3 spreadDirection(const Math::Vector3& dir, float32 spreadDegrees, std::mt19937& rng);

// The camera kick of each shot. It jumps up by RECOIL_DIST (times `scale`: CROUCH_RECOIL when
// crouched; sideways by the second value to a random side), then comes back to rest over
// RECOIL_TIME. A shot while it is still up climbs RECOIL_VERT_ACCUMULATOR higher, up to
// MAX_RECOIL_VERT_DIST (guns without a cap do not climb: ours).
class Recoil {
public:
    void kick(const Firearm& gun, float32 scale, bool right);
    void update(float32 dt);
    void reset() noexcept { *this = Recoil{}; }
    // The camera's offset now, radians (pitch up, yaw left positive).
    [[nodiscard]] float32 pitch() const noexcept;
    [[nodiscard]] float32 yaw() const noexcept;

private:
    float32 up_ = 0.0f, side_ = 0.0f;         // degrees
    float32 upRate_ = 0.0f, sideRate_ = 0.0f; // degrees per second back to rest
};

// The rounds of the guns he carries, by WeaponTypeData record (lower case). As the exe keeps
// them (the ammo weapon at 0x4f8b40 on: +0xa0 the clip, +0x98 the spare rounds):
class GunAmmo {
public:
    [[nodiscard]] int32 loaded(const std::string& weapon) const;
    [[nodiscard]] int32 spare(const std::string& weapon) const;
    // 0x4f8b40: `rounds` in all: the clip takes up to CLIP_AMMO, the rest is spare; at most
    // MAX_CLIPS x CLIP_AMMO are kept.
    void set(const Firearm& gun, const std::string& weapon, int32 rounds);
    // 0x4f8ba0: the spare rounds are full ((MAX_CLIPS - 1) x CLIP_AMMO).
    [[nodiscard]] bool full(const Firearm& gun, const std::string& weapon) const;
    // 0x4f8bc0 (walking over a gun of a kind he carries): its rounds join the spare rounds, up to
    // the cap; what does not fit is lost with it. False (and nothing changes) when already full.
    bool takeRounds(const Firearm& gun, const std::string& weapon, int32 rounds);
    // 0x4f8d60 (an ammo box): `rounds` more spare rounds, up to the cap; false when already full.
    bool addBox(const Firearm& gun, const std::string& weapon, int32 rounds) { return takeRounds(gun, weapon, rounds); }
    // One round out of the clip; false when it is empty.
    bool fire(const std::string& weapon);
    [[nodiscard]] bool canReload(const Firearm& gun, const std::string& weapon) const;
    // 0x4f9b76: fills the clip from the spare rounds. Returns the rounds moved.
    int32 reload(const Firearm& gun, const std::string& weapon);
    // One round from the spare into the clip (a RELOAD_LOOP gun's turn); false when the clip is
    // full or no spare round is left.
    bool reloadOne(const Firearm& gun, const std::string& weapon);
    void clear() { rounds_.clear(); }

private:
    std::unordered_map<std::string, std::pair<int32, int32>> rounds_; // weapon -> {clip, spare}
};

// The exe 0x463ed0: the ammo boxes (CT_AMMO_*), how many rounds each holds and the guns it
// loads, by eCollectableType name, in the order the exe looks for them in his inventory (the
// first one he carries takes them; with none the box stays where it lies).
struct AmmoBox {
    const char* collectable;
    int32 rounds;
    std::array<const char*, 3> guns; // nullptr: no more
};
const AmmoBox* ammoBox(const std::string& collectable);

} // namespace Game
