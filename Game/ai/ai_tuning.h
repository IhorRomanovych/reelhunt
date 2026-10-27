// Game/ai/ai_tuning.h - the original game's AI and melee tuning, read from the tuning pak.
//
// The game keeps its AI numbers in data, not code (levels/GLOBAL/DATA/*.pak, see
// Assets/pak.h):
//   levels/global/data/aiTypeData.ini      vision_data, hearing_data, VOLUME_DISTANCES,
//                                          BSP_VOLUME_FILTERING, the alert/action record
//                                          "default", investigate_action, search_parameters
//   levels/global/data/AISounds.ini        INTERESTING_SOUNDS: volume, interest, confusion
//   levels/global/data/WeaponTypeData.ini  fists: COMBAT_DELAYS (when each blow lands)
//   levels/global/data/ShotTypeData.ini    fistShotQuick / fistShotHard: SIZE, DAMAGE, PLAYER_DAMAGE
//   levels/<level>/entityTypeData.ini      per model record: HIT_POINTS, SWING_PAUSE, RUN_TIME, ...
// Every default below is the value shipped in those files, so the AI behaves the same without
// the pak (unit tests). How the exe uses the numbers is in Game/ai/senses.h.
#pragma once
#include "engine_types.h"

#include "Math/vector.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "Game/stamina.h"

namespace Assets { class PakFile; }

namespace Game {

// Sense / alert levels as the files name them. The exe stores very_low 0 .. very_high 4 and
// "none" as 9; here "none" is -1 so that comparisons read naturally.
using SenseLevel = int32;
inline constexpr SenseLevel kNoLevel = -1;
inline constexpr SenseLevel kVeryLow = 0, kLow = 1, kMedium = 2, kHigh = 3, kVeryHigh = 4;
SenseLevel parseSenseLevel(const std::string& name); // "very_low" .. "very_high", else kNoLevel
const char* senseLevelName(SenseLevel l);

// Thresholds in file order: [0] very_high .. [4] very_low (the exe fills them by line order).
using LevelThresholds = std::array<float32, 5>;

// aiTypeData.ini RECORD vision_data. Angles in degrees, distances in metres, % as fractions.
struct VisionData {
    float32 safeZone = 0.06f;
    float32 widthStandEasy = 75.0f, widthWalkEasy = 75.0f, widthRunEasy = 50.0f;
    float32 widthStandHard = 95.0f, widthWalkHard = 95.0f, widthRunHard = 50.0f;
    float32 height = 60.0f;
    float32 range = 30.0f;                       // FRUSTUM_m
    float32 peripheralWidth = 170.0f, peripheralHeight = 60.0f;
    float32 peripheralRange = 3.0f;              // PERIPHERAL_FRUSTUM_m
    float32 checkPeripheral = 4.0f;              // CHECK_PERIPHERAL_m
    float32 face = 4.0f, faceDefinite = 2.0f;    // FACE_m, FACE_DEFINITE_m
    bool useFaceLight = true, useDistance = true, useLight = true, useVisible = true;
    LevelThresholds faceLight{0.10f, 0.08f, 0.06f, 0.04f, 0.02f};
    LevelThresholds distance{0.20f, 0.45f, 0.60f, 0.70f, 1.00f};
    LevelThresholds light{0.50f, 0.50f, 0.06f, 0.06f, 0.06f};
    LevelThresholds visible{0.90f, 0.75f, 0.55f, 0.35f, 0.10f};
};

// aiTypeData.ini VOLUME_DISTANCES / BSP_VOLUME_FILTERING / hearing_data.
struct HearingData {
    std::array<float32, 5> volumeDistance{5.0f, 15.0f, 30.0f, 60.0f, 200.0f}; // very_low .. very_high
    float32 bspGeneral = 15.0f, bspExecution = 8.0f, bspLure = 8.0f, alertExtension = 5.0f;
    float32 walkFraction = 0.5f, runFraction = 1.0f; // WALK_% / RUN_% of the player's full speed
    SenseLevel confusion = kMedium;
};

// AISounds.ini INTERESTING_SOUNDS.
struct SoundType {
    SenseLevel volume = kNoLevel;
    SenseLevel interest = kNoLevel;
    bool confusion = true;
};

// What a hunter is doing about the player, ranked (aiTypeData.ini).
enum class AiAction : int32 { NotOnAlert = 0, Hunt, Investigate, Chase, Combat };
inline constexpr int32 kAiActions = 5;
AiAction parseAiAction(const std::string& name, bool* ok = nullptr);
const char* aiActionName(AiAction a);

struct ActionTransition {
    bool valid = false;
    SenseLevel sight = kNoLevel;   // minimum sight level, kNoLevel = disregard sight
    SenseLevel hearing = kNoLevel; // minimum hearing interest, kNoLevel = disregard hearing
};

// aiTypeData.ini RECORD default (+ investigate_action, search_parameters).
struct AlertData {
    AlertData(); // the shipped table
    float32 degradeSeconds = 20.0f;                // ALERT_LEVEL_DEGRADE
    std::array<SenseLevel, kAiActions> setAlert{}; // SET_ALERT_LEVEL, per action
    std::array<float32, kAiActions> sustain{};     // SUSTAIN_ACTION seconds, per action
    std::array<std::array<ActionTransition, kAiActions>, kAiActions> transitions{}; // [from][to]
    bool investigateRuns = true;                   // investigate_action: run / walk
    float32 searchRadius = 35.0f;                  // search_parameters RADIUS_m
    float32 darkNode = 0.08f;                      // DARKNODE_%: a node this dark is worth searching
    float32 searchDensity = 0.85f;                 // DENSITY_%: share of the candidates searched
    float32 doubleSearch = 0.15f;                  // DOUBLE_SEARCH_%: chance another hunter re-searches
    float32 doubleSearchDelay = 10.0f;             // DOUBLE_SEARCH_DELAY_s

    [[nodiscard]] const ActionTransition& transition(AiAction from, AiAction to) const
    {
        return transitions[static_cast<std::size_t>(from)][static_cast<std::size_t>(to)];
    }
};

// One melee attack: WeaponTypeData COMBAT_DELAYS (seconds into the swing when it lands) and
// the ShotTypeData shot (an SP_SPHERE of radius SIZE; DAMAGE to peds, PLAYER_DAMAGE to the player).
// The combo timing is the fists record's CHAIN_POINTS / FIRE_WINDOW_STARTS / _LENGTHS
// (quick, hard) - see Game/melee_combo.h for how they are used.
struct MeleeAttack {
    float32 strikeAt = 0.4f;
    float32 damage = 7.5f;
    float32 playerDamage = 6.0f;
    float32 size = 1.1f;
    float32 chainPoint = 0.5f;        // the queued next punch starts this far into the swing
    float32 fireWindowStart = 0.24f;  // a press from here ...
    float32 fireWindowLength = 0.6f;  // ... for this long queues the next punch
};

struct MeleeWeapon {
    MeleeAttack quick{0.4f, 7.5f, 6.0f, 1.1f, 0.5f, 0.24f, 0.6f};      // COMBAT_DELAYS[0], SHOT_TYPE_1 (fistShotQuick)
    MeleeAttack hard{0.567f, 12.5f, 10.0f, 1.1f, 0.8f, 0.32f, 0.6f};   // COMBAT_DELAYS[1], SHOT_TYPE_2 (fistShotHard)
    std::array<float32, 3> comboPauses{0.0f, 0.5f, 0.6f};              // COMBO_PAUSES, after step 1 / 2 / 3
    // The clips, <stem>_Stance_Fire_Quick1..3 / _Hard1, <stem>_Block_T, <stem>_Grapple_Attack1..3,
    // ...: "Fist" for MELEE_CLASS FISTS, "Blade" for BLADE1 (the shard), "Beater" for BEATER1
    // (crowbar, small bat, machete ...), "Beater2" for BEATER2 (baseball bats, chainsaw).
    std::string clipStem = "Fist";
    // Blocking: BLOCK_SPEED (seconds to raise the guard) and BLOCK_DAMAGE (share of a quick blow,
    // of a hard blow and of a grapple that gets through the guard).
    float32 blockSpeed = 0.1f;
    std::array<float32, 3> blockDamage{0.4f, 0.7f, 0.0f};
    // Grappling: GRAB_TIME (the grab), then three blows: GRAPPLE_DAMAGE (to a hunter) /
    // _PLAYER (to the player) landing GRAPPLE_DAMAGE_TIMES into each; GRAPPLE_FIRE_WINDOW_STARTS /
    // _LENGTHS: when the next press counts.
    float32 grabTime = 0.6f;
    std::array<float32, 3> grappleDamage{10.5f, 10.5f, 10.5f};
    std::array<float32, 3> grappleDamagePlayer{8.7f, 8.7f, 8.7f};
    std::array<float32, 3> grappleDamageTimes{0.733f, 0.733f, 0.833f};
    float32 grappleWindowStart = 0.533f, grappleWindowLength = 5.0f;
};

// A kick weapon (WC_MELEE_KICK: kick_fists, kick_beater ...; an item's KICK_WEAPON): COMBAT_DELAYS
// "first is for kick, second for stomp", SHOT_TYPE_1 (kickFistsShot: SIZE 0.75, DAMAGE 5).
struct KickWeapon {
    float32 kickAt = 0.3f;
    float32 stompAt = 0.267f;
    MeleeAttack shot{0.3f, 5.0f, 5.0f, 0.75f, 0.0f, 0.0f, 0.0f};
};

// A thrown weapon (WC_THROWING: bottle, brickHalf, can, head ...; an item's WEAPON): its
// SHOT_TYPE_1 (bottleThrowShot: DAMAGE 20, PLAYER_DAMAGE 11; PROJECTILE throwerShot, an EC_SHOT
// whose PHYSICS is GRENADE_PHYS: a point mass of 5, BOUNCE 0.05, SIMPLE_FRICTION 12).
struct ThrowWeapon {
    float32 damage = 20.0f;
    float32 playerDamage = 11.0f;
};

// A firearm (WC_AMMO: nailGun, glock, shotgun ...). A hunter fires it at the player within
// MAX_FIRE_RANGE: a shot hits within MIN_FIRE_RANGE, with MAX_FIRE_RANGE_HIT_PROBABILITY at
// MAX_FIRE_RANGE (linear in between: ours), times the hunter's HIT_ACCURACY %. CLIP_AMMO shots,
// then a reload. The SHOT_TYPE_1 shot: DAMAGE (to peds), PLAYER_DAMAGE. Defaults: nailGun.
struct Firearm {
    // Its shot's effects (ShotTypeData): FLASH_FX at the muzzle (<gun>_muzFlash), CHARACTER_HIT_FX
    // on a body (FXP001 / FXP002), HARD_BSP_HIT_FX / SMOKE_BSP_HIT_FX where it strikes the world
    // (gunshot_spark, gunshot_smoke).
    std::string flashFx, bodyHitFx, hardHitFx, smokeHitFx;
    float32 minRange = 14.0f, maxRange = 16.0f, farHit = 0.125f;
    int32 clipAmmo = 30;
    // RELOAD_LOOP (WeaponTypeData, read into +0xa8 / +0xac / +0xb0 by 0x4424a5): the reload loads
    // a round at a time, its clip looping between these seconds once per round (the 6shooter
    // 1.067..1.267, the shotgun 0..0.367). end <= start: the clip fills at once.
    float32 reloadLoopStart = 0.0f, reloadLoopEnd = 0.0f;
    [[nodiscard]] bool reloadsRoundByRound() const noexcept { return reloadLoopEnd > reloadLoopStart; }
    // MAX_CLIPS: a gun holds at most MAX_CLIPS x CLIP_AMMO rounds, the clip included (the exe
    // 0x4f8b40). DROPPED_AMMO: the rounds in a gun lying in the level (0x4caea0) or dropped by a
    // hunter (0x4cabe0).
    int32 maxClips = 6;
    int32 droppedAmmo = 10;
    float32 damage = 14.5f, playerDamage = 9.5f;
    // The shot (ShotTypeData SC_INSTANT, PRIMITIVE SP_LINE): a line SIZE metres long; MAX_SHOTS
    // lines per trigger pull (shotgun pellets), SHOT_SPREAD degrees apart at most (the cone's width).
    // BODIES_HIT: how many bodies one line goes through (0 in the file: one, ours).
    float32 range = 16.0f;
    int32 pellets = 1;
    float32 spread = 0.0f;
    int32 bodies = 1;
    // RECOIL_DIST (vertical, horizontal; degrees, our reading), RECOIL_TIME (seconds the camera
    // takes to come back), RECOIL_VERT_ACCUMULATOR / MAX_RECOIL_VERT_DIST (the uzi's climb while
    // the trigger is held: each shot within RECOIL_TIME of the last adds that much more, up to the cap).
    float32 recoilUp = 3.0f, recoilSide = 0.0f, recoilTime = 0.2f;
    float32 recoilAccumulator = 0.0f, maxRecoilUp = 0.0f; // 0: no climb, no cap
    // LOCKON_TARGET_RANGES: "if dist < first value then lock on aims at the target's head
    // automatically; if dist < second value then the player is allowed to lock on to the target's
    // head manually".
    float32 autoHeadRange = 5.0f, headRange = 8.0f;
    // NUM_HEAD_SHOTS: "the number of shots to the head it takes to kill a ped" (0: a head shot is
    // a torso shot, ours); TORSO / ARM / LEG_DAMAGE_MULTIPLIER times DAMAGE per zone hit.
    int32 headShots = 3;
    float32 torsoMultiplier = 1.0f, armMultiplier = 1.0f, legMultiplier = 0.5f;
    // The clips <stem>_Fire_T / _ZoomFire_T / _Reload_T and the bank's SFX_WEAPON_FIRE_<sound>_LEFT,
    // SFX_WEAPON_FIRE_EMPTY_<emptySound> and SFX_WEAPON_RELOAD_<reload>: names from the exe and the
    // bank, matched to the records by us (empty stem: nobody fires it).
    std::string clipStem = "NailGun", sound = "NAIL_GUN", emptySound = "NAIL_GUN", reloadSound = "PLASTIC";
    // How it is held. The exe's per-gun clip tables (0x291c84 on): the nail gun, 6shooter,
    // glock, eagle and uzi aim 1Handed_; the sniper and tranq rifles Sniper_; the sawn-off,
    // shotgun and commando 2Handed_.
    enum class Grip { OneHanded, TwoHanded, Rifle };
    Grip grip = Grip::OneHanded;
    // The chance a shot at `distance` metres hits (before HIT_ACCURACY); 0 beyond MAX_FIRE_RANGE.
    [[nodiscard]] float32 hitChance(float32 distance) const;
    // The rounds a gun holds at most (clip included), and the spare rounds besides a full clip.
    [[nodiscard]] int32 maxRounds() const noexcept { return maxClips * clipAmmo; }
    [[nodiscard]] int32 spareCap() const noexcept { return (maxClips - 1) * clipAmmo; }
    // The aim stance: <grip>_Aim_T, zoomed <grip>_ZoomAim_T; moving while zoomed
    // <grip>_ZoomAim_Move_T (the rifles have none: they stand still).
    [[nodiscard]] std::string aimClip(bool zoom) const;
    [[nodiscard]] std::string zoomMoveClip() const;
    [[nodiscard]] bool twoHanded() const noexcept { return grip != Grip::OneHanded; }
};

// The player record's aiming: AIM_LOCKON_ANGLES ("the horizontal and vertical angles relative to
// the camera's view in which an enemy can be locked onto"; the cone's full width, our reading),
// AIM_LOCKON_DIST, CROSSHAIR_SIZES (the crosshair circle radius for sneak, walk and run) and
// CROUCH_RECOIL ("multiplied by the weapon recoil when player is crouching").
struct PlayerAim {
    float32 lockonWidth = 90.0f, lockonHeight = 90.0f; // degrees
    float32 lockonDist = 30.0f;
    std::array<float32, 3> crosshair{0.015f, 0.025f, 0.04f};
    float32 crouchRecoil = 0.5f;
    // Zoomed aiming: ZOOM_LEVELS ("the magnification factor for each zoom level"), ZOOM_SPEED ("how
    // long it takes in seconds for the sniper rifle to zoom in"), ZOOM_MOVE_SCALES ("the amount the
    // camera movement speed is scaled in each zoom level").
    std::array<float32, 2> zoomLevels{2.0f, 12.0f};
    float32 zoomSpeed = 1.2f;
    std::array<float32, 2> zoomMoveScales{0.7f, 0.4f};
};

// entityTypeData.ini, a hunter model's record (Hod_BodB1, ...).
struct HunterTraits {
    // MODEL, and the MODEL of its HEAD record ('' when the record names none: most records are
    // named after their model, Hod_BodB1 with head Hod_HedB1; Estate_Ext's "Cerberus" is
    // Cer_BodB1 with cerberusHead, Cer_HedB1).
    std::string model, head;
    float32 hitPoints = 100.0f;
    float32 swingPause = 1.5f;   // seconds between swings
    float32 runTime = 45.0f;     // RUN_TIME (SetHunterRunTime overrides it)
    float32 hitAccuracy = 100.0f;
    float32 koTime = 15.0f;      // KO_TIME: seconds it lies on the floor when knocked down (our reading)
    bool hardHead = false;       // HARDHEAD (type +0x94 0x800000): head shots hurt as torso shots (0x4a035b)
    float32 stunTime = 60.0f;    // STUN_TIME (unit unknown; the stun lasts its Stun_Melee clip)
    float32 blockDamage = 0.25f; // BLOCK_DAMAGE: share of a blow that gets through its guard
    // BLOCK_ACCURACY: no record sets it; the exe's hunter type defaults it to 50 (and HIT_ACCURACY
    // to 20). Not used: hunters block by their melee traits (Game/ai/melee_traits.h).
    float32 blockAccuracy = 50.0f;
    // HIDE_HEALTH (type +0x174): no record sets it; the type draws 30..50 (0x43ffab: 0x493c70(30,
    // 50)), and each of its hunters starts with the whole part of it (0x41c470 -> +0x80;
    // Hunter::setHideHealth). Ours: drawn once per record at load, in load order.
    float32 hideHealth = 30.0f;
    std::string voice;                     // VOICE (HOODED_BIG, ...)
    std::vector<std::string> skinTextures; // SKIN_TEX variants
};

// The SFX bank prefix of a VOICE: the exe's voice names (HOODED_SMALL, HOODED_MEDIUM,
// HOODED_BIG, ...) are in the same order as its voice ids ("Hooded 1/2/3 Voice 1/2"), and the
// bank's hunter speech is SFX_H<1..3>V<1..2>_<CONTEXT>_<n>. `variant` picks V1 / V2. Empty when
// the bank has no speech for that voice.
std::string voiceSamplePrefix(const std::string& voice, int32 variant);

// A physicsTypeData.ini record (the type's parser 0x443040; the class from 0x42ec10).
struct PhysicsType {
    enum Class : int32 { PointMass = 0, RigidCuboid = 1, RigidSphere = 2, Character = 3, Door = 4, Hanging = 5 };
    int32 cls = PointMass;
    Math::Vector3 centreOfMass{0.0f, 0.0f, 0.0f}; // +0x1c, from the model's pivot (script axes)
    float32 mass = 1.0f;                          // +0x28
    bool properFriction = false;                  // +0x2c: PROPER_FRICTION (else SIMPLE_FRICTION)
    float32 friction = 0.0f;                      // +0x30
    float32 airResistance = 0.0f;                 // +0x34
    float32 spinResistance = 0.0f;                // +0x38
    float32 bounce = 0.0f;                        // +0x3c
    float32 maxSpeed = 0.0f;                      // +0x40 (0: none)
    bool turnedOn = false;                        // +0x44: awake from the start
    bool noGravity = false;                       // +0x48
    bool immovable = false;                       // +0x4c
};
// An entityTypeData record's physics: its PHYSICS record and MADEOFIRON (type +0x94 & 0x10000:
// the crane's magnet takes it, and it crushes characters).
struct PropPhysics {
    PhysicsType type;
    bool iron = false;
    int32 material = 0; // MATERIAL (type +0x48, by 0x5a7850's name list; 0 MATERIAL_NONE)
};
// MATERIAL_<name> -> its index in the exe's list (0x7aafad, 0x28 bytes a name; -1 unknown).
[[nodiscard]] int32 materialIndex(const std::string& name);

struct AiTuning {
    AiTuning();
    VisionData vision;
    HearingData hearing;
    AlertData alert;
    std::unordered_map<std::string, SoundType> sounds; // upper-case name -> type
    MeleeWeapon fists;
    // Every WC_MELEE record of WeaponTypeData.ini (lower-case name), fists included.
    std::unordered_map<std::string, MeleeWeapon> melee;
    // The melee record `name` ("shard"); fists when there is none.
    [[nodiscard]] const MeleeWeapon& meleeWeapon(const std::string& name) const;
    // The kick records (WC_MELEE_KICK), lower-case name; kick("") is kick_fists.
    std::unordered_map<std::string, KickWeapon> kicks;
    [[nodiscard]] const KickWeapon& kick(const std::string& name) const;
    // The firearm records (WC_AMMO), lower-case name; firearm() is nullptr for one no hunter fires.
    std::unordered_map<std::string, Firearm> firearms;
    // WC_EXPLODER weapons (explodingBarrel, GasTank ...): their shot's sphere (SIZE) and the
    // damage at its edge (DAMAGE / PLAYER_DAMAGE: "the nearer to the centre, the greater").
    struct Exploder {
        float32 radius = 5.0f, damage = 100.0f, playerDamage = 80.0f;
        std::string flashFx; // FLASH_FX: the effect it goes off with (Drum_Explode)
    };
    std::unordered_map<std::string, Exploder> exploders;
    [[nodiscard]] const Firearm* firearm(const std::string& name) const;
    // The thrown weapons (WC_THROWING), lower-case name; nullptr for one that is not thrown.
    std::unordered_map<std::string, ThrowWeapon> throwing;
    [[nodiscard]] const ThrowWeapon* throwWeapon(const std::string& name) const;
    // The player record: THROW_CHARGE_TIME ("the length of time it takes to charge up a throw to
    // the maximum as the fire button is held down") and the throw's velocity at no charge and at
    // full charge, MIN_ / MAX_THROW_VELOCITY (x, y, z in his frame).
    float32 throwChargeTime = 1.5f;
    Math::Vector3 minThrowVelocity{1.0f, 2.0f, 5.0f};
    Math::Vector3 maxThrowVelocity{0.8f, 10.0f, 25.0f};
    // The player record: EXECUTE_STAGE_TIMES ("how long the player has to hold down the X button
    // to trigger a stage 2 execution or a stage 3 execution": executions 3 and 4), KO_TIME,
    // WIGGLE_TIME_DECAY (how fast wiggling frees him from a grapple).
    std::array<float32, 2> executeStageTimes{2.0f, 5.0f};
    float32 playerKoTime = 2.0f; // seconds he lies on the floor when knocked down (our reading)
    float32 wiggleTimeDecay = 0.15f;
    PlayerAim aim;
    std::unordered_map<std::string, HunterTraits> hunters; // lower-case record name -> traits
    std::unordered_map<std::string, PhysicsType> physicsTypes; // lower-case physicsTypeData record
    std::unordered_map<std::string, PropPhysics> propPhysics;  // lower-case entity record -> physics
    // SKIN_TEX variants of every record that has them (hunter bodies and heads), lower-case name.
    std::unordered_map<std::string, std::vector<std::string>> skins;
    float32 playerHitPoints = 100.0f;
    StaminaTuning stamina; // the player record's STAMINA_* (Game/stamina.h)
    bool fromPak = false;

    [[nodiscard]] SoundType sound(const std::string& name) const;
    [[nodiscard]] HunterTraits hunter(const std::string& record) const;

    // Fill from the pak; files or records it lacks keep the defaults. `level` names
    // levels/<level>/entityTypeData.ini. Returns a one-line summary for the log.
    std::string read(const Assets::PakFile& pak, const std::string& level);
};

} // namespace Game
