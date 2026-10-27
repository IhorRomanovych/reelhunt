// Game/hunter.h - one hunter: its model, body, senses and behaviour.
//
// What it does follows the original's AI action (Game/ai/senses.h), driven by its senses and
// the level script's AI set-up (AiDirector):
//   not_on_alert  the subpack's goal (go to a node and idle / guard a direction) or, without
//                 one, its idle actions (patrol a path with stops, wander-search around home)
//   hunt          searches: walks (aware) between graph nodes within search_parameters
//                 RADIUS_m of where the player was last noticed
//   investigate   goes to what it saw or heard (investigate_action: run) and looks around
//   chase         runs at the player, or to where it last saw him
//   combat        closes in and fights by its melee traits (Game/ai/melee_traits.h): waits,
//                 swings quick combos or a hard blow, grabs or blocks as the exe's records weigh
//                 what the player is doing
// A gun hunter of an OPEN or COVER subpack fights from cover instead, fires blind at him in the
// shadows and, losing him, goes to the gunfire (Game/ai/gun_combat.h).
// Body states on top: a swing, a hit reaction, death (Die_*, then the script's OnDeath). The
// level script hears about sightings through OnMediumSightingOrAbove / OnHighSightingOrAbove.
#pragma once
#include "engine_types.h"

#include <array>
#include <functional>
#include <optional>
#include <memory>
#include <unordered_map>
#include <random>
#include <string>
#include <vector>

#include "Game/ai/ai_director.h"
#include "Game/ai/ai_tuning.h"
#include "Game/ai/melee_traits.h"
#include "Game/items.h"
#include "Game/ai/nav_graph.h"
#include "Game/ai/senses.h"
#include "Game/character.h"
#include "Game/gunplay.h"
#include "Game/player_field.h"

namespace Game {

// The player as hunters perceive him this frame.
struct PlayerSnapshot {
    Math::Vector3 feet{0.0f, 0.0f, 0.0f};
    float32 yaw = 0.0f;           // where his face points
    float32 light = 0.0f;         // the light hunters see him in (Game/ai/stealth.h)
    bool hidden = false;          // in a safe zone (light below SAFE_ZONE_%)
    bool justHidden = false;      // ... since this frame
    const char* sound = nullptr;  // AISounds.ini name of his footsteps (nullptr: silent)
    bool swiped = false;          // he swung a punch this frame (MELEE_SWIPE)
    const char* noise = nullptr;  // another sound he makes this frame (an execution: EXECUTION_LOW ...)
    bool sneaking = false;
    bool alive = true;
    // His guard is up (he stands facing a fight without attacking): a blow from in front does
    // only the share of his weapon's BLOCK_DAMAGE (quick, hard).
    bool blocking = false;
    float32 blockQuick = 1.0f, blockHard = 1.0f;
    bool down = false; // knocked to the floor: hunters strike him there (ground attacks, stomps)
    // His swing, as hunters fighting him read it (Game/ai/melee_traits.h): seconds into it, when
    // its blow lands, when his weapon's hard blow lands, his quick blow's reach (SIZE); and
    // whether he reaches to grab one.
    bool swinging = false, swingHard = false;
    float32 swingTime = 0.0f, swingLands = 0.0f, hardLands = 0.0f, reach = 1.1f;
    bool grabbing = false;
    // For the scripts' IsPlayer* queries: a body on his shoulder, his back to a wall, and his gait
    // (0 still, 1 walking: a slow push, 2 running: the default pace, 3 sprinting: RUN held; ours).
    bool carryingBody = false, wallSquashed = false;
    int32 gait = 0;
    // His gun, as a hunter under fire reads it (exe 0x587c90 / 0x587ee0): where he aims
    // (the camera's yaw), the hunter he is locked on to ('' none), seconds since his last shot.
    float32 aimYaw = 0.0f;
    std::string lockOn;
    float32 shotAgo = 1e9f;
    bool gun = false; // a firearm in his hand: his brain's gun mode (ped +0x698 +0x34 bit 1, 0x56af90)
    bool executing = false; // an execution is under way (IsExecutionInProgress)
    // The pad buttons held (the scripts' PAD_ bits: TRIANGLE 1, CIRCLE 2, SQUARE 8; ours: Use is
    // TRIANGLE, attack SQUARE), and a full-body clip of his still playing (PlayerPlayFullBodyAnim).
    uint32 pad = 0;
    bool actionBusy = false;
};

// How a blow lands on a hunter (it picks the reaction). Obstruct: struck with a gun
// (OBSTRUCT_WEAPON), Damage_Obstruct_Hit.
enum class HitKind { Quick, Hard, Kick, Stomp, Grapple, Obstruct };

// What a hunter needs from the level.
// A sound for the hunters made somewhere (exe 0x520640: an AISounds.ini sound at an
// entity): its name and where.
struct WorldSound {
    std::string name;
    Math::Vector3 at{0.0f, 0.0f, 0.0f};
};

struct HunterWorld {
    const NavGraph& nav;
    const AiDirector& director;
    const PlayerField& field;
    const AiTuning& tuning;
    PlayerSnapshot player;
    std::mt19937& rng;
    std::function<void(const std::string& hunter, const char* event)> scriptEvent;
    // Its blow reaches him: `hard` (the hard swing) knocks him down unless he blocks it; `stem` is
    // its weapon's clip stem and `step` the swing's number in its combo (1..3; his reaction and
    // fall clips follow them).
    std::function<void(float32 damage, const Math::Vector3& from, bool hard, const std::string& stem, int32 step)> hitPlayer;
    // It fires `gun` at him: `hit` says whether the shot finds him (damage then applies).
    std::function<void(class Hunter& by, float32 damage, const Firearm& gun, bool hit)> shootPlayer;
    // It starts reloading its gun.
    std::function<void(class Hunter& by)> reloadGun;
    // It grabs the player (he stood blocking within reach): true if the grab is taken up.
    std::function<bool(class Hunter& by)> grabPlayer;
    // A sound for the session to play: an SFX bank name, or a category whose numbered
    // variants (<name>_1, _2, ...) are picked from at random.
    std::function<void(const std::string& sample, const Math::Vector3& at, float32 priority)> sound;
    // Its weapon (collectable type `ct`) posts entity audio `event` (Game/weapon_audio.h).
    std::function<void(int32 ct, uint16 event, float32 value, const Math::Vector3& at)> weaponSound;
    const std::vector<float32>* nodeLight = nullptr;       // baked light at each graph node
    std::unordered_map<int32, float32>* searched = nullptr; // node -> when a hunter last searched it
    float32 time = 0.0f;                                   // level time, seconds
    bool hardDifficulty = false;
    // Another hunter is mid-swing (one swings at him at a time: the exe 0x569bc0).
    std::function<bool(const class Hunter& self)> otherSwinging;
    // The melee-traits records' repeat counts, by record (Game/ai/melee_traits.h).
    std::array<MeleeRepeats, kMeleeTraitCount>* meleeRepeats = nullptr;
    bool debugLog = false; // --ai-log: its melee decisions on stderr
    // Sounds made elsewhere this frame (a thrown bottle landing ...): heard where they are, and
    // a hunter that takes one up goes to look there.
    const std::vector<struct WorldSound>* sounds = nullptr;
    // Where a named entity stands (the player, a hunter, a placed entity); nullopt when unknown.
    std::function<std::optional<Math::Vector3>(const std::string& name)> entityAt;
    // How many other hunters are engaged with him hand to hand (the melee cap, 0x588420).
    std::function<int32(const class Hunter& self)> meleeEngaged;
    // Someone on his feet (the player, another hunter) stands within `radius` of `at`: a hunter
    // lying there does not get up under him (0x4b59f0).
    std::function<bool(const class Hunter& self, const Math::Vector3& at, float32 radius)> standingOver;
    // Another hunter within `radius` of it: where it stands (Find Space spreads them, 0x563120).
    std::function<std::optional<Math::Vector3>(const class Hunter& self, float32 radius)> crowdedBy;
    // Its squad keeps to its territory (AISubpackStayInTerritory) and he is outside it: the
    // territory's node nearest him it can reach (0x4fd6b0); -2: none (it evades, 0x4fd830);
    // -1: no such rule now.
    std::function<int32(const class Hunter& self)> boundaryNode;
    // Another hunter its leader has as an enemy (the nearest), nullptr none: with `sight` one it
    // can see now, else any (a buddy's enemy list, 0x54e160).
    std::function<class Hunter*(const class Hunter& self, bool sight)> pickFoe;
};

// Stunned: a hard blow knocked it reeling (Stun_Melee_*), defenceless; Down: knocked to the floor
// (BT_FALL_LAUNCH / _LAND / _STUN / _GETUP: Fall_Launch_Melee_*, Fall_Land_Melee_*, lying KO_TIME,
// Getup_*); Held: a grapple drives it (its clips are played for it, no AI); Executed: being executed.
enum class HunterState { Normal, Attacking, Hurt, Stunned, Down, Held, Executed, Dead };

class Hunter {
public:
    // `size` is the body size letter of its model (S, M, B), which picks the walk clip variant.
    Hunter(std::string name, std::unique_ptr<Character> model, const Math::Vector3& feet, float32 yaw,
           const HunterTraits& traits, float32 health, char size, float32 bodyHeight, float32 radius);

    void update(float32 dt, HunterWorld& w);
    // A buddy whose friend is the player (ped +0x338 0x10, while its BeBuddy runs), and whether
    // it is following him (0x20).
    [[nodiscard]] bool playerBuddy() const noexcept { return buddyOfPlayer_; }
    [[nodiscard]] bool buddyFollowing() const noexcept { return buddyMode_ == 1; }
    // A blow from `from` (the attacker's feet).
    // A blow lands: quick blows from in front may meet its guard (BLOCK_DAMAGE share), a hard
    // blow it survives stuns it, a second hard blow while it reels knocks it down (falling by the
    // attacker's weapon `stem`), kicks and stomps have their own reactions; on the floor it takes
    // Damage_Lying_* / Damage_Stomp_* and dies Die_Lying_*.
    void hit(float32 damage, const Math::Vector3& from, HunterWorld& w, HitKind kind = HitKind::Quick,
             const std::string& stem = "Fist");

    // A shot of the player's (Game/gunplay.h) lands on it: `damage` is already the hit zone's. It
    // reacts by the zone and the side it came from: Firearm_Torso_Damage_Fwd_Left / _Right / _Bkw
    // (arms too), Firearm_Leg_Damage_Left / _Right, a head shot it survives Firearm_Head_Damage_
    // Fwd_T / _Bkw_T over what it does; it dies Firearm_<Head|Torso|Leg>_Die_*. On the floor it
    // takes the shot as a blow there. Like a blow, it fights him afterwards.
    void shot(float32 damage, const Math::Vector3& from, const BodyHit& where, HunterWorld& w);
    // The hit points it started with (a head shot takes 1 / NUM_HEAD_SHOTS of them).
    [[nodiscard]] float32 fullHealth() const noexcept { return fullHealth_; }
    [[nodiscard]] bool hardHead() const noexcept { return traits_.hardHead; }
    // SetHunterHitAccuracy / SetHunterBlockAccuracy (per cent; block accuracy is kept, the melee
    // brain does not use it).
    void setHitAccuracy(float32 percent) noexcept { traits_.hitAccuracy = percent; }
    void setBlockAccuracy(float32 percent) noexcept { traits_.blockAccuracy = percent; }
    // SetHunterAimTarget(hunter, position): with nothing else to do it aims its gun there (the
    // snipers' markers on Ramirez); nullopt clears it.
    void setAimTarget(std::optional<Math::Vector3> at) noexcept { aimTarget_ = at; }
    // JoinMySquad(leader, it) / AbandonMySquad (0x53c090 -> 0x51dcc0, "Mimicker"): it mimics the
    // leader (AI entity +0x27c / +0x280, 0x40e510) - takes no patrol route of its own (0x532620)
    // and, 3 frames on, its ped follows the leader's (0x412560 -> 0x4ec5e0; the following itself,
    // the brain's +0x10, is not traced: ours walks after it to within 2.5 m). It ends when the
    // leader is gone (0x4123e0). nullptr: none.
    void setSquadLeader(const Hunter* leader) noexcept { squadLeader_ = leader; }
    // AISetEntityAim(hunter, on) (0x53bdd0 -> 0x40d030: AI entity +0x200, its ped's +0x688 +0x1bc):
    // with a gun it idles aiming - ChooseFullBodyIdleAnim takes the aim stance (2) and
    // ChooseTorsoIdleAnim none (0x527630 / 0x527740). AIModifyGoalAim does the same while it
    // pursues that goal (ours).
    void setIdleAim(bool on) noexcept { idleAim_ = on; }
    // SetHunterHideHealth(hunter, percent) (0x1ed -> AI entity +0x80): in a melee fight, at or under
    // that much of its health (0x41d120: health / full x 100) it breaks off and hides
    // (0x587f40 -> melee state 5, 0x56e4e0: the cover action 0x57f680, which flees through
    // CPedActEvade to 20..50 m and takes cover; ours: it evades, Hunter::evade, and fights back
    // cornered with him in reach). 0: never.
    void setHideHealth(int32 percent) noexcept { hideHealth_ = percent; }
    [[nodiscard]] const Hunter* squadLeader() const noexcept { return squadLeader_; }
    // The hunter it fights (an enemy of its leader's that is not the player), nullptr none.
    [[nodiscard]] Hunter* foe() const noexcept { return foe_; }
    // Killed by another hunter (it does not count among his kills).
    [[nodiscard]] bool killedByHunter() const noexcept { return killedByHunter_; }

    // Grapples drive it: hold() plays `clip` (no AI, it stays put) until release().
    void hold(const std::string& clip, const Math::Vector3& feet, float32 yaw);
    void release() noexcept;
    // Kills it while held (grapple damage), playing `dieClip`.
    void dieHeld(const std::string& dieClip, HunterWorld& w);

    // Executions: the player has it. It stands at `feet` facing `yaw` and plays `clip` (its half
    // of the kill, Damage_<Weapon>_Sneak_Attack<n>) with no senses, AI or physics; then
    // finishExecution plays `dieClip` and it is dead (OnDeath, as from a blow).
    void beginExecution(const Math::Vector3& feet, float32 yaw, const std::string& clip, HunterWorld& w);
    void finishExecution(const std::string& dieClip, HunterWorld& w);
    [[nodiscard]] bool beingExecuted() const noexcept { return state_ == HunterState::Executed; }
    // It died in an execution (not a kill the director counts: the exe 0x4ef3a0).
    [[nodiscard]] bool diedInExecution() const noexcept { return diedInExecution_; }

    // The weapon in its hand (Hunter_Inst WEAPON / WEAPON2; nullptr = fists) and how it fights
    // with it (the WeaponTypeData record: damage, reach, <stem>_Stance_Fire_ clips).
    void setWeapon(const ItemType* item, const MeleeWeapon* melee) noexcept { weapon_ = item; melee_ = melee; }
    [[nodiscard]] const ItemType* weapon() const noexcept { return weapon_; }
    [[nodiscard]] const MeleeWeapon* melee() const noexcept { return melee_; }
    // A gun it fires at the player (nullptr: none, or one hunters do not fire).
    void setFirearm(const Firearm* gun) noexcept
    {
        gun_ = gun;
        ammo_ = gun ? gun->clipAmmo : 0;
    }
    [[nodiscard]] const Firearm* firearm() const noexcept { return gun_; }
    // On the floor (knocked down).
    [[nodiscard]] bool down() const noexcept { return state_ == HunterState::Down; }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] Math::Vector3 feet() const noexcept { return center_ - Math::Vector3{0.0f, bodyHeight_, 0.0f}; }
    [[nodiscard]] float32 yaw() const noexcept { return yaw_; }
    [[nodiscard]] float32 radius() const noexcept { return radius_; }
    [[nodiscard]] bool alive() const noexcept { return state_ != HunterState::Dead; }
    [[nodiscard]] HunterState state() const noexcept { return state_; }
    [[nodiscard]] AiAction action() const noexcept { return awareness_.action; }
    [[nodiscard]] SenseLevel alertLevel() const noexcept { return awareness_.alert; }
    [[nodiscard]] SenseLevel sight() const noexcept { return sight_; }
    [[nodiscard]] SenseLevel hearing() const noexcept { return heard_.interest; }
    [[nodiscard]] float32 health() const noexcept { return health_; }
    // Its kind by its body (ped stats +0xa8, gun::bodyKind): 1 a cop, 2 a Cerberus ...
    void setKind(int32 kind) noexcept { kind_ = kind; }
    [[nodiscard]] int32 kind() const noexcept { return kind_; }
    [[nodiscard]] Character& model() noexcept { return *model_; }
    [[nodiscard]] const Character& model() const noexcept { return *model_; }
    // Push the body sideways (separation from the player and other hunters).
    void nudge(const Math::Vector3& offset) noexcept { center_ += Math::Vector3{offset.x, 0.0f, offset.z}; }
    // Speech: the SFX bank prefix of its voice ("SFX_H3V1_", empty = mute) and whether the
    // script lets it talk (AISetSpeechTypes).
    void setVoice(std::string prefix) { voice_ = std::move(prefix); }
    void setSpeech(bool on) noexcept { speechOn_ = on; }
    [[nodiscard]] bool speechOn() const noexcept { return speechOn_; }
    // SetPedLockonable / SetHunterExecutable: whether he can lock on to it, and execute it.
    void setLockonable(bool on) noexcept { lockonable_ = on; }
    // KillEntity: it dies where it stands (Die_Bkw), OnDeath as from a blow.
    void killNow(HunterWorld& w);
    // Lifts it `metres` (testing falls: REELHUNT_HFALL).
    void raise(float32 metres) noexcept { center_.y += metres; }
    // SetEntityInvulnerable / SetDamage.
    void setInvulnerable(bool on) noexcept { invulnerable_ = on; }
    void setHealth(float32 hp) noexcept { if (state_ != HunterState::Dead) health_ = hp; }
    // DestroyEntity: it leaves the level - no death (nothing dropped, no OnDeath), not drawn, not
    // updated; its record in a checkpoint is "NUL".
    // Its body carried (BT_DEADCARRY_*): the carrier places it at his root and plays its half of
    // the paired clip (GenHunterAnims DeadBody_*); `down` ends that (it lies where it was put).
    void carry(const std::string& clip, const Math::Vector3& feet, float32 yaw, bool loop);
    // Carried on his shoulder: its model rides in `frame` (one of his bones), its feet at his.
    void carryIn(const std::string& clip, const Math::Matrix4x4& frame, const Math::Vector3& feet, bool loop = true);
    void putDown() noexcept { carried_ = false; dropped_ = true; inFrame_ = false; }
    [[nodiscard]] bool carried() const noexcept { return carried_; }
    // It lies on its front (fell forward: Die_Fwd ...), which picks the pickup clip (corpse +0x15c).
    [[nodiscard]] bool faceDown() const;
    void vanish() noexcept
    {
        gone_ = true;
        state_ = HunterState::Dead;
        weapon_ = nullptr;
        gun_ = nullptr;
        model_->setVisible(false);
    }
    [[nodiscard]] bool gone() const noexcept { return gone_; }
    // MoveEntity: stands it at `feet`.
    void placeAt(const Math::Vector3& feet) noexcept { center_ = feet + Math::Vector3{0.0f, bodyHeight_, 0.0f}; }
    void setYaw(float32 yaw) noexcept { yaw_ = yaw; }
    // EntityPlayAnim(hunter, clip, loop): once, or (looped) its pose while it stands.
    void playAnim(const std::string& clip, bool looped)
    {
        if (looped)
            poseClip_ = clip;
        else
            once(clip);
    }
    // AIEntityCancelAnim: its looped clip ends.
    void cancelAnim() noexcept { poseClip_.clear(); }
    // LockPed(name, on): it stays where it stands, doing nothing (the journalist in her hideout).
    void setLocked(bool on) noexcept { locked_ = on; }
    [[nodiscard]] bool lockonable() const noexcept { return lockonable_; }
    void setExecutable(bool on) noexcept { executable_ = on; }
    [[nodiscard]] bool executable() const noexcept { return executable_; }
    void turn(float32 radians) noexcept { yaw_ += radians; }
    [[nodiscard]] const std::string& voice() const noexcept { return voice_; }
    void setAmbient(const Math::Vector4& light) { model_->setAmbientLight(light); }
    // It knows where he is and fights him, as when struck (REELHUNT_PROVOKE: testing fights).
    void provoke(const Math::Vector3& at, const AlertData& alert);
    // Bodies (the squad coordinator's scan, 0x50cb80): how well it sees a body lying at `at` in
    // light `light` (its own sight with the range set to 18 m, 0x5192c0; kNoLevel: not at all),
    // and the squad's errand to check one (Check Body: to within 1 m of it, Idle_Body_Investigate,
    // then a search there). `finder`: it found the body (DEAD_BODY_SEEN).
    [[nodiscard]] SenseLevel seeBody(const Math::Vector3& at, float32 light, const HunterWorld& w) const;
    // `kind`: what it checks - a body (Check Body: to within 1 m, Idle_Body_Investigate), a head
    // (Check Head: to within 1.5 m, then its line, 0x50ad00), or a body it holds by while the squad
    // waits for backup (Wait for Backup: standing, facing it).
    // Body / Head: to it and a look; Wait: stand by the body; Backup: answering a backup request,
    // walk to within 3 m of the finder (`at`) and stand there.
    enum class Errand { Body, Head, Wait, Backup };
    void checkBody(const Math::Vector3& at, bool finder, HunterWorld& w, Errand kind = Errand::Body);
    [[nodiscard]] bool waitingAtBody() const noexcept { return bodyCheck_ && errand_ == Errand::Wait; }
    [[nodiscard]] bool answeringBackup() const noexcept { return bodyCheck_ && errand_ == Errand::Backup; }
    // Its body has been found (each is found once: AI entity +0xcc).
    [[nodiscard]] bool bodyFound() const noexcept { return bodyFound_; }
    void setBodyFound() noexcept { bodyFound_ = true; }
    // Its melee-traits record (an MTT_ value: SetHunterMeleeTraits, else by its character record).
    void setMeleeTraits(int32 index) noexcept { meleeTraits_ = index; }
    [[nodiscard]] int32 meleeTraits() const noexcept { return meleeTraits_; }
    // Mid-swing at him (a blow not over yet).
    [[nodiscard]] bool swinging() const noexcept { return state_ == HunterState::Attacking; }
    [[nodiscard]] bool blocking() const noexcept { return blocking_; }
    // Its route (--ai-log): the point it heads for and how many are left (0: none).
    [[nodiscard]] std::size_t routeLeft() const noexcept { return route_.size() - std::min(routeIndex_, route_.size()); }
    [[nodiscard]] Math::Vector3 routePoint() const noexcept
    {
        return routeIndex_ < route_.size() ? route_[routeIndex_] : feet();
    }

private:
    void sense(float32 dt, HunterWorld& w);
    void react(AiAction before, HunterWorld& w);
    void idle(float32 dt, HunterWorld& w);
    void hunt(float32 dt, HunterWorld& w);
    void investigate(float32 dt, HunterWorld& w);
    void chase(float32 dt, HunterWorld& w);
    void combat(float32 dt, HunterWorld& w);
    // Its subpack's goal while it is not hunting (AIDefineGoal*): false when it cannot follow it
    // (an unknown node or entity), and its idle actions run instead.
    bool pursueGoal(const struct GoalDef& g, float32 dt, HunterWorld& w);
    std::string goalName_; // the goal it last took up (--ai-log)
    // The melee brain: what the player does, the response its traits pick, the action under way.
    void meleeThink(HunterWorld& w, const MeleeWeapon& weapon, float32 dist, float32 facing);
    bool tryBlow(HunterWorld& w, const MeleeWeapon& weapon, float32 dist, float32 facing);
    void startBlock(const MeleeTraits& t, const MeleeWeapon& weapon, HunterWorld& w);
    void finishMeleeAction(HunterWorld& w);
    void dropMelee();
    // Its melee foe dead (CPedActMeleeCombat states 0xc / 0xd, 0x56e450 / 0x56e040 / 0x56e140):
    // it waits rand(0, 7) s (none under 3), then on a roll of rand(0, 10) >= 3 plays
    // Idle_Celebrate_01..04 three times over, rand(4, 7) s each (the clip and the rounds are
    // shared by every hunter, the exe's 0x8217e0 / 0x8217e4); then it waits rand(4, 10) s and
    // goes again.
    void celebrate(float32 dt, HunterWorld& w);
    void attacking(float32 dt, HunterWorld& w);
    void startSwing(HunterWorld& w);
    void startGroundAttack(HunterWorld& w);
    void knockDown(bool fromFront, const std::string& stem, HunterWorld& w);
    void updateDown(float32 dt, HunterWorld& w);
    // A gun in its hand, fighting him (true while this drives it): the squad's order, the cover
    // fight, blind fire and the gunfire search of Game/ai/gun_combat.h.
    bool gunCombat(float32 dt, HunterWorld& w);
    void coverFight(float32 dt, HunterWorld& w, float32 dist);
    // `away`: CoverSeek's mode 1 (the hidden point farthest from him, under fire), else mode 2.
    bool seekCover(float32 nearest, HunterWorld& w, bool away = false);
    // He aims at it and has just fired (0x587ee0).
    [[nodiscard]] bool underFire(const HunterWorld& w) const;
    // He aims at it (0x587c90, without underFire's last shot): locked on to it, else his aim
    // within 8 degrees of the line to it.
    [[nodiscard]] bool aimedAt(const HunterWorld& w) const;
    // No gun, he in the shadows, his place known: "Blind Melee" / "Get Closer" (0x56f570).
    void blindMelee(float32 dt, HunterWorld& w);
    // Its squad keeps to its territory and he is outside it (true while this drives it).
    bool holdBoundary(float32 dt, HunterWorld& w);
    // CPedActEvade (0x577510): away from him, then holding on the 25 / 15 / 5 m ring it got to.
    void evade(float32 dt, HunterWorld& w);
    // The hide action (0x57f680, update 0x57f950): to a dark graph node out of his sight, crouched
    // there, standing to look when he comes; with none, it evades.
    void hide(float32 dt, HunterWorld& w);
    // The dark node it would hide at now (0x5808d0), -1 none.
    [[nodiscard]] int32 findHideNode(HunterWorld& w);
    // He is close in front and in sight, or it was just struck (0x580b50).
    [[nodiscard]] bool hideDisturbed(const HunterWorld& w) const;
    // Its damage notice (0x410c60): the first time a blow takes it to its hide health, it shouts
    // for help (reaction 0x11).
    void hurtPastHide(bool blow, HunterWorld& w);
    void startFlee(HunterWorld& w);
    bool startPeek(float32 dist, HunterWorld& w);
    bool leaveCover(HunterWorld& w);
    void blindFight(float32 dt, HunterWorld& w);
    bool gotoGunfire(float32 dt, HunterWorld& w, const std::string& subpack);
    void enterGunState(int32 state, float32 wait, HunterWorld& w);
    void leaveGunCombat();
    void rosterUpdate(const std::string& subpack, const HunterWorld& w);
    [[nodiscard]] bool coverHidden(int32 node, const HunterWorld& w) const;
    bool aimAndFire(float32 dt, HunterWorld& w, float32 dist, const Math::Vector3* from = nullptr,
                    const Math::Vector3* at = nullptr);
    void startGunStrike(HunterWorld& w);
    void lowerGun();
    void say(const char* context, HunterWorld& w);
    // A line of the AI's speech orders (the Communications enum: HunterSpeech::lineSlot).
    void sayLine(int32 line, const HunterWorld& w);
    void chatter(float32 dt, HunterWorld& w);
    void planSearch(HunterWorld& w);

    // Movement: route over the graph to `target`, then steer along it.
    void planRoute(const Math::Vector3& target, HunterWorld& w);
    bool followRoute(float32 speed, float32 dt, HunterWorld& w); // true on arrival
    bool steerTo(const Math::Vector3& target, float32 speed, float32 arriveRadius, float32 dt);
    void turnTowards(const Math::Vector3& dir, float32 dt);
    void stand(float32 dt);
    void lookAround(float32 dt);
    [[nodiscard]] float32 walkSpeed() const;
    [[nodiscard]] float32 runSpeed() const;

    // Animation.
    void loop(const std::string& clip, float32 groundSpeed, bool repeat = true);
    void once(const std::string& clip);
    [[nodiscard]] std::string walkClip() const;

    std::string name_;
    std::unique_ptr<Character> model_;
    HunterTraits traits_;
    char size_ = 'M';
    float32 bodyHeight_ = 1.15f;
    float32 radius_ = 0.5f;
    Math::Vector3 center_{0.0f, 0.0f, 0.0f}; // body sphere centre
    Math::Vector3 velocity_{0.0f, 0.0f, 0.0f};
    float32 yaw_ = 0.0f;
    bool locked_ = false;
    float32 speed_ = 0.0f;      // current ground speed (for the stride rate and the frustum)
    float32 health_ = 100.0f;
    bool lockonable_ = true, executable_ = true, invulnerable_ = false;
    float32 fullHealth_ = 100.0f;

    HunterState state_ = HunterState::Normal;
    Awareness awareness_;
    SenseLevel sight_ = kNoLevel;
    SenseLevel lastSight_ = kNoLevel; // last frame's
    HeardSound heard_;
    Math::Vector3 lastKnown_{0.0f, 0.0f, 0.0f}; // where the player was last seen or heard
    bool mediumSent_ = false, highSent_ = false;
    bool sawHide_ = false;      // it watched him slip into the shadows: they do not hide him from it

    Math::Vector3 home_{0.0f, 0.0f, 0.0f};
    float32 homeYaw_ = 0.0f;
    std::vector<Math::Vector3> route_;
    std::size_t routeIndex_ = 0;
    float32 stuckFor_ = 0.0f;   // seconds without getting closer to the route point
    float32 stuckBest_ = 1e9f;  // the closest it got to it
    float32 replanTimer_ = 0.0f;

    // Speech.
    std::string voice_;
    bool speechOn_ = true;
    float32 speechBusy_ = 0.0f;     // seconds until it may speak again
    float32 chatterIn_ = 8.0f;      // seconds to the next taunt / mumble
    bool tauntedDeath_ = false;

    // Idle and search behaviour.
    std::vector<int32> searchNodes_; // graph nodes still to search, nearest first
    std::size_t patrolIndex_ = 0;
    float32 wait_ = 0.0f;            // seconds left standing at a stop / search point
    float32 idleActionLeft_ = -1.0f; // seconds left in the current idle action (< 0: not started)
    std::size_t idleActionIndex_ = 0;
    float32 fidget_ = 6.0f;          // seconds to the next idle fidget clip

    // One-shots and combat.
    float32 oneShot_ = 0.0f;         // seconds left of a one-shot clip (no movement meanwhile)
    MeleeAttack swing_;
    const ItemType* weapon_ = nullptr;
    const MeleeWeapon* melee_ = nullptr;
    float32 stunLeft_ = 0.0f;   // seconds of Stunned left
    bool diedInExecution_ = false;
    enum class DownPhase { Launch, Land, Lying, Getup } downPhase_ = DownPhase::Launch;
    float32 downLeft_ = 0.0f;   // seconds left of the current Down phase
    bool downFwd_ = false;      // it fell forwards (face down)
    std::string downStem_;      // the weapon that felled it (Fall_*_Melee_*_<stem>)
    bool groundSwing_ = false;  // the swing is at him on the floor
    const Firearm* gun_ = nullptr;
    bool gone_ = false; // vanish()
    bool bodyFound_ = false;
    // Checking a body (checkBody): where it lies, whether it has looked yet, how long it keeps at it.
    bool bodyCheck_ = false, bodyLooked_ = false;
    Errand errand_ = Errand::Body;
    Math::Vector3 bodyAt_{0.0f, 0.0f, 0.0f};
    float32 bodyCheckLeft_ = 0.0f;
    bool carried_ = false, dropped_ = false; // carry() / putDown()
    bool inFrame_ = false;                   // carryIn(): placed by frame_
    Math::Matrix4x4 frame_{};
    int32 ammo_ = 0;
    float32 fireIn_ = 0.0f;     // seconds to its next shot
    float32 gunOverlay_ = 0.0f; // seconds the fire / reload clip still plays
    bool aiming_ = false;
    bool idleAim_ = false;       // AISetEntityAim
    int32 hideHealth_ = 0;       // SetHunterHideHealth (per cent)
    bool idleAimRaised_ = false; // its gun is up for that (not for a fight)
    // The cover fight (Game/ai/gun_combat.h): its state (GunState in hunter.cpp), the level time it
    // decides next, the cover point (graph node) it holds or heads for, and the pose it holds
    // there (crouched behind cover: Sneak_Stand).
    int32 gunState_ = 0;
    float32 gunDecide_ = 0.0f;
    int32 coverNode_ = -1;
    float32 coverStuck_ = 0.0f;  // seconds on the way to cover without getting anywhere
    Math::Vector3 coverFrom_{0.0f, 0.0f, 0.0f};
    float32 sawHimAt_ = -1e9f;   // level time it last saw him
    Math::Vector3 sawHimPos_{0.0f, 0.0f, 0.0f}; // where (his feet)
    std::string poseClip_;
    int32 kind_ = 0;
    // Leaning out (CPedActPeek): the side (-1 left, +1 right, 0 up over the cover), where its shot
    // leaves, when it started, whether it fired yet, and the level time it may peek again.
    float32 peekSide_ = 0.0f;
    Math::Vector3 peekFrom_{0.0f, 0.0f, 0.0f};
    float32 peekStart_ = 0.0f;
    bool peekFired_ = false;
    float32 peekReady_ = 0.0f;
    // Blind fire: the place it fires at and when it was last sent closer.
    Math::Vector3 blindPlace_{0.0f, 0.0f, 0.0f};
    float32 blindOrderAt_ = -1e9f;
    bool seekAway_ = false;            // its cover seek is the mode-1 one (away from him)
    float32 closeTo_ = 15.0f;          // the range it closes to (cover-fight state 8)
    std::optional<Math::Vector3> aimTarget_;
    const Hunter* squadLeader_ = nullptr;
    Math::Vector3 squadRouteTo_{0.0f, 0.0f, 0.0f}; // where its route to the leader was planned to
    // Fighting another hunter: whom, when it last looked for one, whom its swing is at, and who
    // strikes or shoots it now (set by the attacker around hit() / shot()).
    Hunter* foe_ = nullptr;
    float32 foeCheckAt_ = -1e9f;
    Hunter* swingFoe_ = nullptr;
    Hunter* incomingFrom_ = nullptr;
    bool killedByHunter_ = false;
    void fightFoe(float32 dt, HunterWorld& w);
    void strikeFoe(Hunter& foe, float32 damage, HitKind kind, HunterWorld& w);
    int8 buddyInside_ = -1;            // BeBuddy: its friend within its radius (-1 not known yet)
    // BeBuddy's state (cAI_ObjectiveBeBuddy +0xf4, 0x54c190): 0 none, 1 follow, 2 / 3 sneak to the
    // nodes by him, 4 hold there, 5 wait, 6 / 7 to a node to wait on, 8 fight, 9 shoot, 10 hurt.
    enum class Buddy : uint8 { None, Follow, SneakTo, SneakOn, Hold, Wait, WaitTo, WaitOn, Fight, Shoot, Hurt };
    Buddy buddy_ = Buddy::None;
    int32 buddyNode_ = -1;             // the node it goes to (states 2, 3, 6)
    float32 buddyCheckAt_ = 0.0f;      // Wait: when it next looks for its node (every 4 s)
    float32 friendFoughtAt_ = -1e9f;   // when the player last attacked (the window is 4 s)
    float32 buddyHurtAt_ = -1e9f;      // its last hit
    float32 buddyHealth_ = -1.0f;      // its health last frame
    int8 buddyFollowSet_ = -1;         // follow mode its hurt reaction switched on (-1: the script's)
    bool buddyOfPlayer_ = false;       // its BeBuddy ran this frame with him the friend
    // A fall (0x4a0c10): in the air flailing, landing, getting up; from its height when it began
    // (+0x354), a deadly one past 8 m.
    enum class FallPhase { None, Air, Land, Getup } fall_ = FallPhase::None;
    float32 fallTop_ = 0.0f, fallLeft_ = 0.0f;
    bool fallDeath_ = false;
    void updateFall(float32 dt, HunterWorld& w);
    bool buddyScripted_ = true;        // the script's follow mode last frame
    bool beBuddy(const GoalDef& g, float32 dt, HunterWorld& w);
    int32 goalCover_ = -1;             // SeekCoverBackwards / Hide: the node it holds
    float32 goalCoverAt_ = -1e9f;
    bool blindMelee_ = false, blindSwung_ = false;
    int32 boundaryNode_ = -1;          // the territory node it holds (holdBoundary)
    enum class EvadePhase { None, Flee, Hold, Cornered } evadePhase_ = EvadePhase::None;
    float32 evadeRing_ = 0.0f;
    // The hide action: its state (0 choose, 1 flee, 4 to the node, 5 settle, 6 watch, 7 look), the
    // node, when the state began, its last search (every 0.5 s) and pick, and the nodes it used
    // (each out of reach for 60 s).
    enum class HidePhase { Choose, Flee, ToCover, Settle, Watch, Look } hidePhase_ = HidePhase::Choose;
    int32 hideNode_ = -1, hidePick_ = -1;
    float32 hideAt_ = 0.0f, hideSearchAt_ = -1e9f, hideStuck_ = 0.0f;
    Math::Vector3 hideFrom_{0.0f, 0.0f, 0.0f};        // where its last progress was
    std::vector<std::pair<int32, float32>> hideUsed_; // (node, until)
    bool hiding_ = false;                              // hide() ran this frame
    float32 struckAt_ = -1e9f;                         // its last hit (ped +0x778)
    bool pastHide_ = false;                            // at or under its hide health (+0x1b4)
    // The gunfire search: the graph node it heads for, and whether it went there since it last
    // fought him (then it hunts).
    int32 gunfireSpot_ = -1;
    bool gunfireDone_ = false;
    bool gunStrike_ = false;    // the swing is a blow with the gun (OBSTRUCT_WEAPON)
    float32 guardLeft_ = 0.0f;  // seconds its block-hit reaction still plays
    float32 swingClip_ = 0.0f, swingElapsed_ = 0.0f;
    bool swingLanded_ = false;
    float32 swingPause_ = 0.0f;      // seconds until the next ground / gun swing may start
    enum class Celebrate { None, Wait, Play } celebratePhase_ = Celebrate::None;
    int32 role_ = 5;          // its squad role slot as last seen (gun::RoleSlots, 5 none)
    bool reseeking_ = false;  // the seek under way is a re-seek (0x586630 -> 0x5868b0)
    int8 buddyMode_ = -1;     // the buddy's follow mode as last spoken for (0x54ec80)
    float32 celebrateUntil_ = 0.0f;  // level time the wait or the round ends (0: no round)
    static inline int32 celebrateClip_ = 0, celebrateRounds_ = 0; // 0x8217e0 / 0x8217e4
    bool swingHard_ = false;         // the swing is a hard blow
    // The melee brain (Game/ai/melee_traits.h): its record, the action under way (Wait: none),
    // the blows still to swing (true: hard), the quick blows swung (Quick1..3), its block, the
    // level times its actions are ready again, and what it last decided on.
    int32 meleeTraits_ = kDefaultMeleeTraits;
    MeleeResponse meleeAction_ = MeleeResponse::Wait;
    std::vector<bool> combo_;
    int32 quickStep_ = 0;
    int32 swingStep_ = 1; // the swing's number in its combo (Quick1..3 / Hard1)
    bool blocking_ = false;
    float32 blockUntil_ = 0.0f;
    float32 blockFrom_ = 0.0f; // when its guard went up
    float32 quickReady_ = 0.0f, hardReady_ = 0.0f, grappleReady_ = 0.0f;
    float32 waitedAt_ = -1e9f;
    int32 lastSituation_ = -1, lastPhase_ = -1;
    // Its footwork (CPedActMeleeCombat 0x56bc80): closing in (state 0x11 / 0), holding back a
    // moment (10), or backing away to 2.2..3.2 m (7), and when it may reconsider (+0x74).
    enum class MeleeMove { Close, Pause, Back, Standoff, Circle, Ring, Cover, InCover } meleeMove_ = MeleeMove::Close;
    // The engage cycle (state 2, 0x56ccb0): when it next weighs taking cover from his gun, and the
    // cover point it went for (states 3 / 4, CoverSeek at +0xb0).
    float32 engageAt_ = -1.0f;
    int32 meleeCover_ = -1;
    float32 meleeCoverStuck_ = 0.0f;
    bool meleeCoverFaced_ = false; // turned to face him there
    Math::Vector3 meleeCoverFrom_{0.0f, 0.0f, 0.0f};
    MeleeMove afterPause_ = MeleeMove::Close;
    float32 pauseUntil_ = 0.0f, repositionAt_ = 0.0f, backTo_ = 2.7f, backSpeed_ = 0.75f;
    float32 moveUntil_ = 0.0f, flipAt_ = 0.0f;      // the stand-off's / circling's end, the circle's next turn
    float32 circleSide_ = 1.0f;                     // +1 to its right, -1 to its left
    float32 engagedDist_ = 1e9f, combatAt_ = -1e9f; // its distance to him and when it last fought
    void decideFootwork(HunterWorld& w, float32 dist); // the free state 0xf (0x56d840)
    bool positionMelee(float32 dt, HunterWorld& w, float32 dist, const Math::Vector3& to);

public:
    // In the fight hand to hand, closing in or at him (counts against the cap of two).
    [[nodiscard]] bool meleeEngaged(float32 now) const noexcept
    {
        return now - combatAt_ < 0.25f && meleeMove_ == MeleeMove::Close && engagedDist_ < 2.5f;
    }

private:
    std::string moveClip_; // the walk clip this frame's footwork asks for (Walk_Bkw_Aware ...)
    std::string loopClip_;
};

} // namespace Game
