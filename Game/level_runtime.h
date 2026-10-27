// Game/level_runtime.h - a running level's gameplay: its scripts, hunters and triggers.
//
// Loads the level's scripts (mapAI.grf, scripts/<level>.mls), spawns the Hunter_Inst hunters,
// gives every named entity a script handle (GetEntity), runs OnCreate and then, every frame,
// the script tasks, the triggers (OnEnterTrigger / OnLeaveTrigger), the hunters and their
// fights with the player. The session feeds it the player's state and applies what it asks of
// the player (damage, teleports, heading).
#pragma once
#include "engine_types.h"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Assets/ifp/ifp.h"
#include "Game/ai/ai_director.h"
#include "Game/ai/ai_tuning.h"
#include "Game/ai/nav_graph.h"
#include "Game/ambient_light.h"
#include "Game/hunter.h"
#include "Game/hunter_speech.h"
#include "Game/camera_splines.h"
#include "Game/doors.h"
#include "Game/entity_audio.h"
#include "Game/crane.h"
#include "Game/physics_props.h"
#include "Game/fx.h"
#include "Game/hand_cam.h"
#include "Game/level_music.h"
#include "Game/hud.h"
#include "Game/game_text.h"
#include "Game/items.h"
#include "Game/radar.h"
#include "Game/level_setup.h"
#include "Game/save_system.h"
#include "Game/script/script_engine.h"

namespace Renderer { class Scene; }

namespace Game {

class LevelContent;
class PlayerField;

class LevelRuntime {
public:
    struct Setup {
        std::string levelName;  // folder name, e.g. jury_turf
        std::string levelDir;   // with a trailing slash
        std::string constantsPath;
        std::string pakPath;    // levels/GLOBAL/DATA/*.pak (AI / melee tuning)
        std::string hunterSpeechPath; // the table read from the exe (Game/hunter_speech.h)
        std::string gameRoot;   // with a trailing slash (pictures/frontend_pc.txd: the pickup halo)
        std::string globalTextSuffix; // the global text's language (GameSettings::globalTextSuffix)
        const LevelContent* level = nullptr;
        PlayerField* field = nullptr; // (a damaged prop takes its collision away)
        Renderer::Scene* scene = nullptr;
        std::shared_ptr<const Assets::IfpFile> animations;
        float32 playerHealth = 0.0f; // 0: the player record's HIT_POINTS
        std::string textSuffix;      // the level text's language (GameSettings::levelTextSuffix)
        bool hardDifficulty = false; // HARDCORE (GameSettings::difficulty 1)
    };

    void load(const Setup& s);
    // The camera this frame (its position and world right / up axes): the pickups' halos face it.
    void setViewer(const Math::Vector3& eye, const Math::Vector3& right, const Math::Vector3& up) noexcept
    {
        viewEye_ = eye;
        viewRight_ = right;
        viewUp_ = up;
    }
    // Run the level's OnCreate handlers (the level first).
    void start();
    void update(float32 dt, const PlayerSnapshot& player);
    // Print every hunter's state every two seconds (--ai-log).
    void setDebugLog(bool on) noexcept { debugLog_ = on; }

    // The player's punch lands now: hunters the shot sphere reaches in front of `feet` along
    // `yaw` take its damage.
    // (`stem`: his weapon's clip stem; a hunter he fells falls by it. Hunters on the floor are
    // out of reach of his standing blows and kicks: see downTarget.)
    void playerStrikes(const Math::Vector3& feet, float32 yaw, const MeleeAttack& attack, HitKind kind = HitKind::Quick,
                       const std::string& stem = "Fist");
    // His kick lands (KICK_WEAPON's shot): a stunned hunter in front is stomped, any other kicked.
    // Returns true if it reached one.
    bool playerKicks(const Math::Vector3& feet, float32 yaw, const KickWeapon& kick);
    // The hunter in front of him within `reach` that can be grabbed (on its feet, not swinging).
    [[nodiscard]] Hunter* grabTarget(const Math::Vector3& feet, float32 yaw, float32 reach);
    // A knocked-down hunter in front of him within `reach` (for stomps and ground attacks).
    [[nodiscard]] Hunter* downTarget(const Math::Vector3& feet, float32 yaw, float32 reach);
    // A hard blow he did not block since the last call: it knocks him down (from where, and the
    // hunter's weapon clip stem for his fall clips).
    struct Knock {
        Math::Vector3 from{0.0f, 0.0f, 0.0f};
        std::string stem;
    };
    [[nodiscard]] std::optional<Knock> takeKnockdown();
    // A hunter's shot that found him since the last call (from where).
    [[nodiscard]] std::optional<Math::Vector3> takeShot();
    // A hunter that grabbed him since the last call (it stood next to him while he blocked).
    [[nodiscard]] Hunter* takeGrab();
    // A blow he took on his guard since the last call: 0 none, 1 quick, 2 hard.
    [[nodiscard]] int32 takeBlockedHit();
    // Damage to him outside the hunters' own swings (grapples).
    void hurtPlayer(float32 damage, const Math::Vector3& from);
    // A deadly landing (0x4a0c10 case 4: 0x431480(0)): his health goes, unless invulnerable.
    void killPlayer() noexcept
    {
        if (!playerInvulnerable_)
            playerHealth_ = 0.0f;
    }
    // A HunterWorld for driving hunters from outside (grapples).
    [[nodiscard]] HunterWorld hunterWorld() { return world(player_); }

    // --- his guns (Game/gunplay.h) ---
    // The firearm in his hand (nullptr: fists or a melee weapon) and its WeaponTypeData record
    // (lower case, the key of its clip in ammo()).
    [[nodiscard]] const Firearm* playerGun() const;
    [[nodiscard]] std::string playerGunRecord() const;
    [[nodiscard]] const GunAmmo& ammo() const noexcept { return ammo_; }
    // The hunter he locks on to aiming from `eye` along (yaw, pitch): the nearest one alive in
    // AIM_LOCKON_DIST and the AIM_LOCKON_ANGLES cone, in plain sight (not hidden by the world).
    [[nodiscard]] Hunter* lockTarget(const Math::Vector3& eye, float32 yaw, float32 pitch);
    // Whether a lock on `h` holds: alive, within AIM_LOCKON_DIST of `eye` and in plain sight.
    [[nodiscard]] bool keepsLock(const Hunter& h, const Math::Vector3& eye);
    // He pulls the trigger: a round out of the clip, MAX_SHOTS lines from `origin` along `dir`
    // (spread over SHOT_SPREAD) that stop at the world and go through up to BODIES_HIT hunters
    // within the shot's SIZE, each taking the damage of the zone hit. False on an empty clip (the
    // gun clicks). `from`: his feet (where the hunters know he is).
    bool playerFires(const Math::Vector3& origin, const Math::Vector3& dir, const Math::Vector3& from);
    // Fills the clip from his spare rounds; false when it is full or he has none.
    bool playerReloads();
    // A RELOAD_LOOP gun's turn: one round in, its reload sound; false when none went in.
    bool playerReloadsOne();
    // The shots and reloads since the last call, his and the hunters' (for their effects).
    struct GunEvent {
        const Hunter* hunter = nullptr; // nullptr: the player
        const ItemType* item = nullptr; // the gun
        bool reload = false;
        std::vector<ShotLine> lines;
    };
    [[nodiscard]] std::vector<GunEvent> takeGunEvents() { return std::exchange(gunEvents_, {}); }

    // --- executions ---
    // The hunter the player at `feet` facing `yaw` could execute now: alive and on its feet,
    // within reach (1.6 m, ours), its back to him (he stands within 70 degrees of straight
    // behind it, facing it) and not watching him (sight below medium, not chasing or fighting).
    [[nodiscard]] Hunter* executionTarget(const Math::Vector3& feet, float32 yaw);
    // The nearest living, visible hunter on its feet, anywhere (testing: REELHUNT_AUTO_EXECUTE=n!).
    [[nodiscard]] Hunter* nearestHunter(const Math::Vector3& feet);
    // The nearest body lying within `reach` of `feet` (on his floor) that he can pick up.
    [[nodiscard]] Hunter* corpseInReach(const Math::Vector3& feet, float32 reach);
    // A buddy of his he can order (0x4643f0): within 10 m (0x7105f8), his facing within acos 0.55
    // of the way to it (0x7106b4), and seen from 1.7 m above his feet to 1.7 m above its own
    // (0x4de3f0).
    [[nodiscard]] Hunter* buddyToOrder(const Math::Vector3& feet, float32 yaw);
    // He orders it: one following waits, one waiting follows (the mode AISetBuddyFollow /
    // AISetBuddyWait set).
    void orderBuddy(Hunter& buddy);
    // The gas can (a GAS_CAN record, type +0x94 & 0x400000: Asylum's Gas_Can), carried like a
    // body (the Use key's context 4 / 1, 0x4646b0 / 0x4657b0): the one within `reach` of his
    // feet (its handle, 0 none); taken up (gone from the floor, its collision too); put down at
    // `at` facing `yaw` (ours: without collision there).
    [[nodiscard]] uint32 gasCanInReach(const Math::Vector3& feet, float32 reach) const;
    void takeGasCan(uint32 handle);
    void putGasCan(uint32 handle, const Math::Vector3& at, float32 yaw);
    [[nodiscard]] bool carryingGasCan() const noexcept { return gasCarried_ != 0; }
    // An entity's position (scene frame; testing), the origin for none.
    [[nodiscard]] Math::Vector3 positionOf(uint32 handle) const
    {
        return handle >= 1 && handle <= entities_.size() ? entities_[handle - 1].position : Math::Vector3{0.0f, 0.0f, 0.0f};
    }

    // --- ladders (EC_CLIMABLE: CLIMABLE_TYPE BASE / MID / TOP pieces stacked; 0x441870) ---
    struct Ladder {
        Math::Vector3 base{0.0f, 0.0f, 0.0f}; // the BASE piece's origin
        float32 top = 0.0f;                   // the TOP piece's height: the floor he climbs onto
        Math::Vector3 face{0.0f, 0.0f, -1.0f}; // the way a climber faces (toward what it stands on)
        float32 foot = 0.0f;                  // the floor under the BASE piece (it may hang above it)
        Math::Vector3 wall{0.0f, 0.0f, 0.0f}; // where its face meets the climber's way, on the floor (ours)
        float32 reach = 0.5f;                 // HOTSPOT_RADIUS (ACTIVATION_DIST if given)
        float32 baseFocus = 45.0f;            // FOCUS_RADIUS (degrees) of the BASE / TOP pieces
        float32 topFocus = 180.0f;
    };
    [[nodiscard]] const std::vector<Ladder>& ladders() const noexcept { return ladders_; }

    // --- use (EC_SWITCH / EC_USEABLE: buttons, levers, valves, padlocks, searchables) ---
    // What using the useable in front of him does: his clip (USEABLE_CLASS: Gen_Power_Lever_On,
    // ...; empty for a searchable), how long it plays and where to face.
    struct UseStart {
        std::string clip;
        float32 yaw = 0.0f;
        std::string itemClip; // the clip the gas can in his hand plays (COLLECTABLE_ANIM)
    };
    // The useable in reach in front of `feet` along `yaw`, if one can be used now: it starts
    // (exe 0x4d40b0) and its effect follows USE_DELAY later (0x4d4390). `gasCan`: only
    // one whose USE_ITEM is the Gas_Can (he is carrying it).
    std::optional<UseStart> playerUses(const Math::Vector3& feet, float32 yaw, bool gasCan = false);
    // A useable he may use now is in reach (the HUD's use prompt).
    [[nodiscard]] bool useableInReach(const Math::Vector3& feet, float32 yaw, bool gasCan = false) const;

    // --- throwing (WC_THROWING items: the bottle, half brick, can, severed head) ---
    // The equipped item's throw record, nullptr when it is not thrown.
    [[nodiscard]] const ThrowWeapon* playerThrowWeapon() const;
    // It leaves his hand at `from` with `velocity` (scene frame, m/s) and his inventory.
    void playerThrows(const Math::Vector3& from, const Math::Vector3& velocity);
    // A sound for the hunters at `at` this frame (exe 0x520640): an AISounds.ini name.
    void postSound(const std::string& name, const Math::Vector3& at);
    // Starts the kill: the hunter stands where the player does, facing as he does (the paired
    // clips share one origin), and plays `clip`. finishExecution kills it with `dieClip`.
    void beginExecution(Hunter& h, const Math::Vector3& feet, float32 yaw, const std::string& clip);
    void finishExecution(Hunter& h, const std::string& dieClip);
    // The hunter lets go of its weapon: it falls in front of it as a pickup.
    void dropHunterWeapon(Hunter& h);
    // EXECUTE_HEAD_SEVER_TIME: its head comes off and lies there as a Severed_Head_(CT) pickup.
    void severHead(Hunter& h);

    // The music's danger (Game/level_music.h): a hunter after him (hunting or investigating),
    // one that has spotted him (chasing or fighting), and how far the nearest of them is.
    struct MusicDanger {
        bool hunted = false, spotted = false;
        float32 nearest = 1e9f;
    };
    [[nodiscard]] MusicDanger musicDanger() const;
    // The music's ambience channels this frame (Game/music_ambience.h): the fires, the machines
    // the script slots started, the rain less the shelter above him and the camera (rays 100 m
    // up, every 8 frames; 0x5afa10).
    [[nodiscard]] MusicAmbience::Frame musicAmbience(float32 dt);
    // The shelter above him and the camera (0x7b3278, 0..1, kept by musicAmbience) and the wind:
    // the weather's (table 0x7b3220) times one less the shelter (0x5b003a).
    [[nodiscard]] float32 shelter() const noexcept { return shelter_; }
    [[nodiscard]] float32 wind() const;
    // SwitchLitterOn (0x2a1 -> 0x5c8e60): the litter shows (Game/litter.h).
    [[nodiscard]] bool litterOn() const noexcept { return litterOn_; }

    // --- the radar (Game/radar.h) ---
    // What it shows: the hunters after him, the dead, and the scripts' blips.
    [[nodiscard]] std::vector<RadarBlip> radarBlips();
    // Sounds the scripts raised since the last call (AITriggerSound): where, and how far (m).
    [[nodiscard]] std::vector<std::pair<Math::Vector3, float32>> takeRadarNoise() { return std::exchange(radarNoise_, {}); }
    // Hunters whose heads the scripts blew off this frame (MutilateHunter), for the session.
    [[nodiscard]] std::vector<Hunter*> takeHeadBursts() { return std::exchange(headBursts_, {}); }
    // The hearing distance of a sound (AISounds.ini volume -> VOLUME_DISTANCES), 0 if unknown.
    [[nodiscard]] float32 soundRange(const char* sound) const;

    // --- the player, as the level sees him ---
    [[nodiscard]] float32 playerHealth() const noexcept { return playerHealth_; }
    // SetPlayerControllable(FALSE): the scripts hold him (no input).
    [[nodiscard]] bool playerControllable() const noexcept { return playerControllable_ && !userInputDisabled_; }
    // DisableUserInput: no input at all (the view does not turn either); LockPlayerMovement: he
    // does not move (he still acts).
    [[nodiscard]] bool userInputDisabled() const noexcept { return userInputDisabled_; }
    [[nodiscard]] bool movementLocked() const noexcept { return movementLocked_; }
    // SetPlayerGotoNode: where the scripts walk him (nullopt none); the session reports arrival.
    [[nodiscard]] std::optional<Math::Vector3> playerGoto() const noexcept { return playerGoto_; }
    void playerArrived();
    // Requests for the session: a full-body clip (PlayerPlayFullBodyAnim), put the carried body
    // down (PlayerDropBody), forget pressed buttons (ClearInputStates).
    [[nodiscard]] std::string takePlayerAnim() { return std::exchange(playerAnim_, {}); }
    [[nodiscard]] bool takeDropBody() { return std::exchange(dropBody_, false); }
    [[nodiscard]] bool takeClearInput() { return std::exchange(clearInput_, false); }
    // ForceWeatherType since the last call: the level's setup (fog, sky) with that weather.
    [[nodiscard]] std::optional<LevelSetup> takeWeather() { return std::exchange(forcedWeather_, std::nullopt); }
    [[nodiscard]] bool playerAlive() const noexcept { return playerHealth_ > 0.0f; }
    // SetMaxNumberOfRats (0x57e3f6: at most 16; 16 until a script says otherwise).
    [[nodiscard]] int32 ratLimit() const noexcept { return ratLimit_; }
    // Hidden in the shadows (a safe zone), and whether any hunter is after him (HUD status icon).
    [[nodiscard]] bool playerHidden() const noexcept { return player_.hidden; }
    [[nodiscard]] bool playerSpotted() const noexcept;
    // Damage taken since the last call (for the hit reaction), and from where.
    [[nodiscard]] std::optional<Math::Vector3> takeHit();
    // The reaction clip the last hit asks for ("" = the usual flinch; Damage_Obstruct_Hit when a
    // hunter struck him with its gun).
    [[nodiscard]] const std::string& hitClip() const noexcept { return hitClip_; }
    // A shove since the last call (a hunter's blow with its gun): how far he is pushed.
    [[nodiscard]] std::optional<Math::Vector3> takePush() { return std::exchange(push_, std::nullopt); }
    // A teleport (MoveEntity(GetPlayer, ...)) or heading (SetPlayerHeading) the scripts asked for.
    [[nodiscard]] std::optional<Math::Vector3> takeTeleport();
    [[nodiscard]] std::optional<float32> takeHeading();

    // Sounds the level asked for since the last call: an SFX bank name or a category whose
    // numbered variants (<name>_1, _2, ...) the player picks from.
    // How far a sound carries (ours; the data gives no ranges): voices, blows and steps fade out
    // by 30 m, gunshots by 80 m.
    static constexpr float32 kSoundRange = 30.0f, kGunshotRange = 80.0f;
    static constexpr float32 kSpeechRange = 40.0f; // every voice sample's (Game/sample_levels.h)
    struct SoundEvent {
        std::string sample;
        Math::Vector3 position{0.0f, 0.0f, 0.0f};
        float32 priority = 0.5f;
        bool positional = true;
        float32 range = kSoundRange; // metres at which it fades out
        int32 loopSlot = -1;         // >= 0: loops as that script audio slot until it is ended
        float32 volume = 1.0f;
        int32 slot = -1;             // >= 0: a script audio slot's one-shot (the session reports its length)
        // The audio manager's entity sounds (doors): `volume` x 127 is its level, heard as 0x452e70
        // falls it off over `range` from the camera and as much as gets through the world (0x4df3b0).
        bool exeFalloff = false;
        float32 rate = 1.0f; // its sample rate scaled (the footsteps' jitter)
        // A weapon's entity audio event (Game/weapon_audio.h): the session picks its samples.
        int32 weaponCt = -1;
        uint16 weaponEvent = 0;
        float32 weaponValue = 0.0f;
    };
    // A weapon of collectable type `ct` posts entity audio `event` (0xb used, 0xc empty, 0xe a
    // shell landing) with `value` at `at`. `shooter` tells an automatic's bursts apart.
    void weaponSound(int32 ct, uint16 event, float32 value, const Math::Vector3& at, const void* shooter = nullptr);
    // The session's report: a script audio slot's one-shot plays for `seconds`
    // (IsScriptAudioSlotCompleted), and whether a director line is being spoken
    // (IsDirectorSpeechPlaying).
    void slotPlaying(int32 slot, float32 seconds);
    void setDirectorSpeaking(bool on) noexcept { directorSpeaking_ = on; }
    // Script audio slots the scripts ended (EndScriptAudioSlotLooped).
    [[nodiscard]] std::vector<int32> takeEndedLoops() { return std::exchange(endedLoops_, {}); }
    // The SUBTITLES setting (0x7d6af4): director lines and the speech placeholders show text.
    void setSubtitles(bool on) noexcept { subtitles_ = on; }
    // The front-end stream (LoadFrontEndAudioStream / PlayFrontEndAudioStreamOneShot, 0x5a6ac0 /
    // 0x5a6be0): a stereo .RIB of audio/PC/SCRIPTED on voices 14 and 15 (44100 Hz, 0x400 chunks),
    // opened paused, then played once at volume 0..127 (times the music volume).
    struct FrontEndStream {
        std::string load;   // the file to open (empty: none this frame)
        int32 play = -1;    // the volume to play it at (-1: not this frame)
    };
    [[nodiscard]] FrontEndStream takeFrontEndStream() { return std::exchange(frontEnd_, {}); }
    // EntityPlayAnim on a prop (the body bag, Piggsy's grill, doors, the asylum victim): its clip
    // from the level's EntityAnims, on its model standing at `at` (the session animates it in
    // place of its static meshes).
    struct PropAnim {
        const Assets::InstPlacement* placement = nullptr;
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
        std::string clip;
        bool loop = false;
    };
    [[nodiscard]] std::vector<PropAnim> takePropAnims() { return std::exchange(propAnims_, {}); }
    // HideEntity / ShowEntity on a prop.
    [[nodiscard]] bool propHidden(int32 placement) const { return hiddenProps_.contains(placement); }
    [[nodiscard]] std::vector<SoundEvent> takeSounds();
    // Director lines the scripts asked for (PlayDirectorSpeech AISPEECH ids), oldest first.
    [[nodiscard]] std::vector<int32> takeDirectorSpeech();
    // A director line the game itself asks for (the exe's lock-on and milestone lines).
    void playDirectorSpeech(int32 id) { directorSpeech_.push_back(id); }
    [[nodiscard]] bool directorSpeechPending() const noexcept { return !directorSpeech_.empty(); }
    // Hunters that died since the last call (their OnDeath; the director's kill count, 0x5b63a0).
    [[nodiscard]] int32 takeHunterDeaths() noexcept { return std::exchange(hunterDeaths_, 0); }
    // SetNumberOfKillableHuntersInLevel(total, executable): the scene's score target.
    [[nodiscard]] std::pair<int32, int32> killableHunters() const noexcept { return killable_; }
    void emitSound(const std::string& sample, const Math::Vector3& at, float32 priority, bool positional = true,
                   float32 range = kSoundRange);

    [[nodiscard]] std::vector<std::unique_ptr<Hunter>>& hunters() noexcept { return hunters_; }
    // The model record a hunter was placed with (Hod_BodM1, Pig_BodF1, ...), or "".
    [[nodiscard]] std::string hunterRecord(const Hunter& h) const
    {
        const auto it = hunterRecords_.find(&h);
        return it != hunterRecords_.end() ? it->second : std::string();
    }
    [[nodiscard]] const AiDirector& director() const noexcept { return director_; }
    [[nodiscard]] const AiTuning& tuning() const noexcept { return tuning_; }
    // Fog, far clip, sky colour and the shadow-casting props (levelSetup.ini, entityTypeData.ini).
    [[nodiscard]] const LevelSetup& levelSetup() const noexcept { return levelSetup_; }
    // The on-screen text the scripts show (hint box, subtitles, goals).
    [[nodiscard]] GameText& gameText() noexcept { return text_; }
    [[nodiscard]] const GameText& gameText() const noexcept { return text_; }
    [[nodiscard]] const NavGraph& nav() const noexcept { return nav_; }

    // --- cut-scenes: the camera the scripts drive ---
    // CutSceneStart .. CutSceneEnd: the player is not in control.
    [[nodiscard]] bool inCutscene() const noexcept { return cutscene_; }
    // Runs the scripts ahead (splines, sleeps, text) until the cut-scene ends (SPACE skips).
    void skipCutscene();
    struct ScriptCamera {
        bool active = false;                       // the scripts own the camera this frame
        Math::Vector3 position{0.0f, 0.0f, 0.0f};  // scene frame
        Math::Vector3 target{0.0f, 0.0f, 0.0f};    // the point it looks at
        float32 zoom = 1.0f;                       // SetZoomLerp: divides the field of view
    };
    [[nodiscard]] const ScriptCamera& scriptCamera() const noexcept { return camera_; }

    // Scrap2's crane. SetPadControlEntityRotation's entity turns with the pad (Game/crane.h; ours:
    // the left / right keys while his movement is locked); what is attached to it
    // (AttachToEntity) and hung from it (LinkHangingEntToEnt) turns and moves with it.
    // `magnet`: the Action button held (it toggles the magnet, 0x432a10 / pad button 6); `mouseDX`
    // the mouse's x this frame (it turns the crane: x x -18 degrees a second, 0x69b4ac).
    void updateCrane(float32 dt, bool left, bool right, bool magnet = false, float32 mouseDX = 0.0f);
    [[nodiscard]] bool magnetOn() const noexcept { return magnetOn_; }
    [[nodiscard]] const PhysicsProps& physicsProps() const noexcept { return physics_; }
    [[nodiscard]] bool padTurning() const noexcept { return padTurned_ != 0; }
    // The crane camera (SetCameraModeCrane / PosCrane / LookAtEntityCrane; 0x58b4d0, 0x58b4f0,
    // in the player's camera 0x58f5e0): at its position, looking at the entity + its offset;
    // nullopt when off. Its eye and target, scene frame.
    [[nodiscard]] std::optional<std::pair<Math::Vector3, Math::Vector3>> craneView();

    // The cut-scene look and the HUD as the scripts set them.
    struct ScriptLook {
        bool handCam = false;      // HandCamSetActive: the camera shakes like a hand-held one
        HandCam::Set handCamLook;  // its yaw and pitch, and its roll (Game/hand_cam.h)
        HandCam::Set handCamRoll;
        bool allEffects = false;   // HandCamSetAllEffects
        bool otherEffects = false; // HandCamSetOtherEffects
        float32 noise = 0.0f;      // WhiteNoiseSetVal burst (0..1), decaying
        bool drawHud = true;       // DrawHud
        uint32 hudFlags = 0xffu;   // SetHudFlag / ToggleHudFlag: HUD_MAP 1, HUD_MAN 2, HUD_HEALTH 4,
                                   // HUD_STAMINA 8, HUD_INVENTORY 16 (HUD_ALL_ON 255)
        uint32 hudFlash = 0u;      // HUDToggleFlashFlags, same bits
        // The camcorder overlay's parts (exe 0x7a1364..68, drawn by 0x58dbc0 while the
        // execution camera runs): 0 the TCR timecode, 1 REC and its dot, 2 tape fuzz, 3 the rolling
        // bar, 4 "other" (read by the execution camera, 0x5904f4). HandCamSetVideoEffectTimeCode /
        // _RecordDot / _Fuzz / _ScrollBar (natives 0x25f..0x262) set 0..3, HandCamSetAllEffects
        // (0x277) all five, HandCamSetOtherEffects (0x276) 4.
        std::array<bool, 5> camcorder{};
    };
    // The execution camera turns every part on as it starts (0x590440) and off as it ends (0x58fa5d).
    void setCamcorder(bool on) noexcept { look_.camcorder.fill(on); }
    // ... and holds it by hand: on with the defaults as it starts (0x4b2dd0), off as it ends (0x4b2eb0).
    void setExecutionHandCam(bool on) noexcept
    {
        look_.handCam = on;
        if (on)
            look_.handCamLook = look_.handCamRoll = HandCam::Set{};
    }
    [[nodiscard]] const ScriptLook& look() const noexcept { return look_; }

    // --- weapons and collectables ---
    // INVENTORY SWAP is held: walking over an item for a filled holster slot swaps it in.
    void setSwapHeld(bool held) noexcept { swapHeld_ = held; }
    [[nodiscard]] const ItemCatalog& items() const noexcept { return items_; }
    // The level's parsed modelspc.dff (hunters, heads, weapon models); nullptr before the first use.
    [[nodiscard]] const DffLibrary* models() const noexcept { return models_.get(); }
    // What the player carries, in pickup order; the weapon in his hand (nullptr = fists).
    [[nodiscard]] const std::vector<const ItemType*>& inventory() const noexcept { return inventory_; }
    [[nodiscard]] const ItemType* equipped() const noexcept { return equipped_; }
    // The swap offer this frame (0x5df980): the holster slot and collectable type of an item he
    // stands on for a slot he has filled; {0, 0} none.
    [[nodiscard]] std::pair<int32, int32> swapOffer() const noexcept { return swapOffer_; }
    // An item was picked up since the last call (for the session: sound, the weapon in his hand).
    [[nodiscard]] const ItemType* takePickedUp();
    // Removes one of `item` from the inventory (a disposable weapon used up in an execution).
    void useUp(const ItemType* item);
    // The next weapon he carries after the one in his hand: by holster slot 3, 2, 1, 4, fists for
    // an empty slot 3 (Game/weapon_strip.h weaponCycle).
    void cycleWeapon(bool back = false);
    // INV1..INV4: the item in that holster slot (1 BACK, 2 BELT_RIGHT, 3 BELT_LEFT, 4 BELT_REAR) in
    // hand, if there is one (ours: the exe's handler is not traced).
    void selectSlot(int32 slot);
    [[nodiscard]] bool weaponSelectionDisabled() const noexcept { return weaponSelectionDisabled_; }
    // The extra HUD bars (Game/hud.h HudBar): the buddy's, then the boss's.
    [[nodiscard]] std::vector<HudBar> hudBars();
    // An FX project system (or INI template) by name at `at`, facing `forward` (muzzle flashes,
    // bullet hits, blood); 0 when the level has no such effect.
    int32 playFx(const std::string& name, const Math::Vector3& at, const Math::Vector3& forward = {1.0f, 0.0f, 0.0f});
    // Decals the frame asked for (Game/decals.h): a bullet's hole where it struck, a drum's
    // scorch, a thrown head's splat, the blood of a bat kill.
    struct DecalRequest {
        int32 type = 0;
        Math::Vector3 at{0.0f, 0.0f, 0.0f}, normal{0.0f, 1.0f, 0.0f};
        float32 size = -1.0f; // across, metres (< 0: the type's own)
    };
    // The exe's uniform draw between two bounds (0x614d50 & 0x7ff over 2047).
    [[nodiscard]] float32 drawBetween(float32 lo, float32 hi);
    [[nodiscard]] std::vector<DecalRequest> takeDecals() { return std::exchange(decals_, {}); }
    // A shot struck the world (0x4f3500): its material's hit effect (FXBWOOD, FXBSTON ...) along
    // the surface normal, else the gun's HARD_BSP_HIT_FX (ours); then SMOKE_BSP_HIT_FX.
    void shotStruck(const Firearm& gun, const Math::Vector3& at, const Math::Vector3& dir, const PlayerField::ShotHit& hit,
                    bool smoke);
    // The execution's level (2 hasty .. 4 gruesome) as GetExecutionType gives it (0 .. 2).
    void setExecutionLevel(int32 level) noexcept { executionType_ = std::clamp(level - 2, 0, 2); }
    // The inventory as a save keeps it (exe 0x5d24c0): slots 1..4 by HOLSTER_SLOT, the
    // latest item of each, a gun with its rounds (clip + spare).
    [[nodiscard]] std::array<SaveInventorySlot, SaveFile::kInventorySlots> inventorySlots() const;
    // Restore from a save (0x5d2650): the items of slots 1..4, each gun SetAmmo'd with its rounds;
    // then fists in his hand (0x46e580(0), our reading).
    void restoreInventory(const std::array<SaveInventorySlot, SaveFile::kInventorySlots>& slots);
    // Debug: puts the item `record` in his inventory (a weapon goes to his hand); false if unknown.
    bool give(const std::string& record);
    // The TimedVectorPairs.ini records (execution cameras).
    [[nodiscard]] const std::vector<Assets::IniRecord>& cameraShots() const noexcept { return tvp_; }
    [[nodiscard]] const CameraSplines& splines() const noexcept { return splines_; }
    // The level's doors (the collision field consults them; the scripts lock and swing them).
    [[nodiscard]] const Doors& doors() const noexcept { return doors_; }
    // SetLevelCompleted was called: the level is won.
    [[nodiscard]] bool levelCompleted() const noexcept { return levelCompleted_; }
    // SetLevelFailed: the scripts failed the scene.
    [[nodiscard]] bool levelFailed() const noexcept { return levelFailed_; }
    // The scripts' timer (SetTimer .. ShowTimer: "M:SS") or counter (SetCounter .. ShowCounter)
    // shown on the HUD; empty when neither is.
    [[nodiscard]] std::string hudCounter() const;
    // --- save points and checkpoints (Game/checkpoint.h) ---
    // A save point the player walked into this frame (its placement index), each once; it is gone
    // from the level after (its entity "NUL" in the checkpoint, as the exe's).
    [[nodiscard]] int32 takeSavePoint() noexcept { return std::exchange(savePointHit_, -1); }
    // The level's entity block as the exe writes it (0x43a2c0), the player's state put in.
    [[nodiscard]] std::vector<SaveEntityRecord> checkpointRecords(const Math::Vector3& playerFeet, float32 playerYaw,
                                                                  float32 playerHealth) const;
    // The level program's data with its VARs in it, as a save's buffer 5 (0x4812d0), and back.
    [[nodiscard]] std::vector<std::uint8_t> scriptData() const;
    void restoreScriptData(const std::vector<std::uint8_t>& data);
    // The doors' locks as a checkpoint has them (its Door_Inst records' third word; ours).
    void restoreDoors(const std::vector<SaveEntityRecord>& records);
    // The entities the scripts had spawned, from a checkpoint's records after the .inst ones
    // (0x437650(0) builds them with the rest); before the scripts start, so a script that
    // spawns only when its entity is missing (Attic's Gen_Save_Point01) finds it.
    void restoreSpawned(const std::vector<SaveEntityRecord>& records);
    // Debug (F12): win the scene as SetLevelCompleted does.
    void completeLevel() noexcept { levelCompleted_ = true; }
    [[nodiscard]] Script::Engine& scripts() noexcept { return scripts_; }
    // The texture handles the runtime's models own (none today: hunters share the level's).
    [[nodiscard]] std::vector<uint32> textureHandles() const;

private:
    enum class Kind { Level, Player, Hunter, Trigger, Placement };
    struct Entity {
        std::string name;
        Kind kind = Kind::Placement;
        Math::Vector3 position{0.0f, 0.0f, 0.0f}; // scene frame
        Hunter* hunter = nullptr;
        int32 trigger = -1;
        int32 placement = -1; // its .inst placement (index), -1 for the scripts' own
    };
    struct Trigger {
        std::string name;
        bool sphere = true;
        bool box = false; // CreateBoxTrigger: the box between corners a and b
        Math::Vector3 a{0.0f, 0.0f, 0.0f}, b{0.0f, 0.0f, 0.0f}; // scene frame
        float32 radius = 1.0f;
        bool inside = false, entered = false, left = false;
        // The classes it also notices (TriggerAddEntityClass: a COL_ mask, CTrigger +0x180), and
        // the entities of them in it: those already inside (+0x150) and those that came in this
        // frame (+0x14c), each with its entity class (EC_HUNTER 0x1f, EC_COLLECTABLE 0x1001,
        // EC_WEAPON 0x3001).
        uint32 classes = 0;
        std::vector<std::pair<std::string, int32>> occupants, arrivals;
        // AttachToEntity(trigger, entity): it rides along, at its offset from the entity.
        uint32 attachedTo = 0;
        Math::Vector3 attachOffset{0.0f, 0.0f, 0.0f};
        int32 placement = -1;    // its .inst placement (index)
        bool savePoint = false;  // a Gen_Save_Point: entering it takes the checkpoint (0x4d2400)
        bool used = false;
        bool gone = false;       // DestroyEntity
        // A save point's look (CTrigger 0x4d1ac0): its model spinning at 2 rad/s about the vertical
        // and its halo; walked into, it goes 0.1 s later (hitAt: when).
        std::vector<Renderer::Mesh*> model;
        Renderer::Mesh* halo = nullptr;
        Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        float32 hitAt = -1.0f;
    };
    // A save point trigger's model (entityTypeData MODEL, built from the level's DFF) and halo.
    void dressSavePoint(Trigger& t);
    // A model of the level's DFF as scene meshes about the origin, oriented as placement `p`.
    std::vector<Renderer::Mesh*> addModelMeshes(const std::string& model, const Assets::InstPlacement& p);
    void updateSavePoints(float32 dt);
    void updateHalos();

    uint32 addEntity(const std::string& name, Kind kind, const Math::Vector3& pos);
    [[nodiscard]] Entity* entity(uint32 handle);
    [[nodiscard]] uint32 handleOf(const std::string& name) const;
    void spawnHunters();
    std::unordered_map<std::string, std::string> animBlocks_; // record (lower case) -> its ANIMATION_BLOCK
    // One hunter from its placement (a Hunter_Inst, or a script's spawn); its entity handle, 0
    // if its model is missing.
    uint32 spawnHunter(const Assets::InstPlacement& p);
    // An entity a script spawns (SpawnEntityWithDirection): kept after the .inst placements, in
    // the order spawned, as the exe's entity registry keeps them (and so the entity block).
    // A hunter record (EC_HUNTER) is a hunter; an item (EC_WEAPON / EC_COLLECTABLE) a pickup;
    // Gen_Save_Point a save point's trigger; anything else a named place. Its handle.
    uint32 addSpawned(Assets::InstPlacement p);
    // The placement of index `index` (.inst or spawned), or nullptr.
    [[nodiscard]] const Assets::InstPlacement* placementAt(int32 index) const;
    // Hunter skin variant `id` (0 = the model's own textures, 1.. = its SKIN_TEX entries).
    void applySkin(Hunter& h, const std::string& record, int32 id);
    void bindNatives();
    void updateTriggers(const PlayerSnapshot& player);
    void separate(const PlayerSnapshot& player);
    void updateCamera(float32 dt);
    void buildPickups();
    // A pickup made at run time (a dropped weapon, a severed head): its own model, lying at `ground`.
    // `rounds`: the ammo in a dropped gun (-1: a level's own, DROPPED_AMMO as well).
    void spawnPickup(const ItemType* type, const Math::Vector3& ground, float32 yaw, const std::string& model = {},
                     int32 rounds = -1);
    // He takes the gun `type` with `rounds` in it: a new kind keeps them, one he carries adds them
    // to its spare rounds. False when that one is full (the gun stays where it lies).
    bool takeGun(const ItemType* type, int32 rounds);
    // An ammo box: its rounds go to the first matching gun he carries; false with none or full.
    bool loadBox(const AmmoBox& box);
    void updatePickups(float32 dt);
    void endCutscene();
    [[nodiscard]] Math::Vector3 toScene(const Math::Vector3& script) const;
    [[nodiscard]] Math::Vector3 toScript(const Math::Vector3& scene) const;
    [[nodiscard]] HunterWorld world(const PlayerSnapshot& player);

    Setup setup_;
    Script::Engine scripts_;
    Script::ConstantTable constants_;
    AiDirector director_;
    AiTuning tuning_;
    LevelSetup levelSetup_;
    GameText text_;
    bool hardDifficulty_ = false;
    CameraSplines splines_;
    Doors doors_;
    EntityAudio entityAudio_;                                      // the entities' sound events
    Math::Vector3 bumpFeet_{0.0f, 0.0f, 0.0f};                     // the player's feet last frame (bumps)
    float32 bumpAt_ = -1e9f;                                       // his last bump lure
    float32 painAt_ = -1e9f;                                       // his last pain cry
    // The hunters' footsteps (0x4b3a10 for every ped; played by 0x44b5a0 -> 0x44ea20).
    struct StepTrack {
        std::string clip;
        float32 phase = -1.0f;
        uint8 material = 0;
    };
    std::unordered_map<const Hunter*, StepTrack> stepTracks_;
    void hunterSteps(const Hunter& h);
    void playerCries(const Math::Vector3& head);
    int32 savePointHit_ = -1;
    std::deque<Assets::InstPlacement> spawned_; // the scripts' spawns (indices instCount() on)
    // The level program's MLS symbols and data, for a save's script data.
    std::string levelProgram_;
    std::vector<Assets::MlsSymbol> levelSymbols_;
    std::vector<std::uint8_t> levelData_;
    Math::Vector3 lastPlayerFeet_{0.0f, 0.0f, 0.0f};
    std::unordered_map<const Hunter*, Math::Vector3> lastHunterFeet_;
    bool levelCompleted_ = false;
    bool levelFailed_ = false;
    // The scripts' countdown (SetTimer / StartTimer / StopTimer / ShowTimer / HideTimer /
    // HasTimerEnded, OnTimerEnded) and counter (SetCounter / IncrementCounter / ShowCounter).
    float32 timerLeft_ = 0.0f;
    bool timerRunning_ = false, timerShown_ = false, timerEnded_ = false;
    int64 counter_ = 0;
    bool counterShown_ = false;
    bool started_ = false; // start() ran: an entity made from now on starts its own OnCreate

    // Cut-scene camera state (see updateCamera).
    bool cutscene_ = false;
    std::optional<Math::Vector3> camPosition_, camView_; // SetCameraPosition / SetCameraView (scene frame)
    uint32 camLookAt_ = 0;                                // CameraForceLookAtEntity
    const CameraSpline* followSpline_ = nullptr;          // PlaySplineFileDefault, ISFOLLOW 1
    const CameraSpline* lookSpline_ = nullptr;            // ... ISFOLLOW 0
    float32 followT_ = 0.0f, lookT_ = 0.0f;               // seconds into each
    float32 zoomFrom_ = 1.0f, zoomTo_ = 1.0f, zoomTime_ = 0.0f, zoomT_ = 0.0f;
    std::vector<int32> cutscenesPlayed_;                  // MarkCutSceneAsPlayed ids
    std::vector<std::string> cutsceneActors_;             // hunters that act during cut-scenes (lower case)
    bool cutsceneAllActors_ = false;                      // AICutSceneAllEntitiesEnable(TRUE)
    ScriptCamera camera_;
    ScriptLook look_;

    // A prop that takes damage (an EC_BASIC record with HIT_POINTS and LOD_DATA): its health
    // as a percentage of HIT_POINTS picks the LOD_DATA entry shown (exe 0x4336b0), and
    // falling below the entry it shows damages it once (0x4334e0).
    struct DamageProp {
        int32 placement = -1;
        const Assets::InstPlacement* p = nullptr;
        Math::Vector3 origin{0.0f, 0.0f, 0.0f}; // scene frame
        float32 hitPoints = 100.0f;             // HIT_POINTS (type +0x88)
        float32 health = 100.0f;                // (entity +0x14)
        std::vector<std::pair<int32, int32>> lods; // LOD_DATA: (LOD, lowest health %), in file order
        int32 lod = 0;                          // the LOD shown (+0xf0)
        bool damaged = false;                   // (+0xe8)
        bool invulnerable = false;              // ARMOUR_CLASS ARM_INVULNERABLE
        bool noColOnDamage = false;             // type +0x94 & 0x100000
        std::string damageFx;                   // DAMAGE_FX (type +0xd0)
        std::string spawn;                      // DAMAGE_SPAWN (+0xac)
        Math::Vector3 spawnPosition{0.0f, 0.0f, 0.0f}; // SPAWN_POSITION (+0xb4), model space
        [[nodiscard]] int32 percent() const noexcept;
    };
    void buildDamageProps(const std::vector<Assets::IniRecord>& entityTypes);
    // `damage` off a prop's health, then its LOD (0x4336b0); `restoring`: a checkpoint's damage
    // put back (0x4334e0's argument): no FX and nothing spawned.
    void damageProp(DamageProp& d, float32 damage, bool restoring = false);

    // Pickups lying in the level (the placements of EC_WEAPON / EC_COLLECTABLE records).
    struct Pickup {
        const ItemType* type = nullptr;
        Math::Vector3 position{0.0f, 0.0f, 0.0f}; // scene frame, the model origin
        Math::Vector3 centre{0.0f, 0.0f, 0.0f};   // the middle of its meshes (spins about this)
        std::vector<Renderer::Mesh*> meshes;
        Renderer::Mesh* halo = nullptr;           // its halo (Game/pickup_halo.h; owned by the scene)
        std::shared_ptr<Character> model;         // a pickup made at run time draws itself (not .inst)
        float32 yaw = 0.0f;
        int32 rounds = -1;                        // a gun's ammo (-1: its DROPPED_AMMO)
        int32 placement = -1;                     // its .inst placement (index), -1 when made at run time
        bool taken = false;
        bool waiting = false;                     // swapped out: not taken again until he steps off it (ours)
        bool noticed = false;                     // a severed head a hunter has seen (0x53ef90)
        std::string name;                         // its entity name when made at run time (a head: <hunter>_h)
        // Made above the floor, it falls there first (COLLECTABLE_PHYS, a TURNED_ON point mass:
        // 25 m/s^2 held to 30 m/s, 0x48bb80; no bounce): metres still to fall, and its speed.
        float32 fall = 0.0f, fallSpeed = 0.0f;
    };
    ItemCatalog items_;
    std::shared_ptr<const DffLibrary> models_;
    std::vector<Pickup> pickups_;
    std::vector<DamageProp> damageProps_;
    // A switch or useable (exe CSwitch / CUseable): its record's USEABLE_CLASS and use
    // rules, and its state (entity +0x154: 0 never used, 1 on, 2 off).
    struct Useable {
        int32 placement = -1;
        std::string name;              // its entity (lower case)
        Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        int32 cls = 0;                 // SWITCH 0, BUTTON 1, CONSOLE_BUTTON 2, LEVER 3, POWER_LEVER 4,
                                       // WHEEL 5, PADLOCK 6, ROPE 7, GENERATOR 8, VANDOORS 9, LOCK 10,
                                       // SEARCHABLE 11
        float32 reach = 0.7f;          // HOTSPOT_RADIUS (+ HOTSPOT_HORIZ_DIST: ours)
        float32 focus = 70.0f;         // FOCUS_RADIUS: the cone he must face it within (degrees: ours)
        float32 useDelay = 0.5f;       // USE_DELAY: from the start of the use to its effect
        bool multiple = false;         // MULTIPLE: can be used again
        bool twoState = false;         // 2_STATE: on / off in turn
        bool mustAlign = false;        // MUST_ALIGN: he turns to it
        std::vector<std::string> useItems; // USE_ITEM: one must be in his hand
        std::string anim, offAnim;     // USEABLE_ANIM / USEABLE_OFF_ANIM: its own clip (EntityAnims)
        std::string collectableAnim;   // COLLECTABLE_ANIM: the clip of the item used on it
        int32 state = 0;
        bool enabled = true;           // EnableUseable
        bool used = false;             // (a single-use one is done)
        float32 effectIn = -1.0f;      // the use's effect, this long from now
    };
    std::vector<Useable> useables_;
    void buildUseables(const std::vector<Assets::IniRecord>& entityTypes);
    void updateUseables(float32 dt);
    [[nodiscard]] Useable* useableAt(const Math::Vector3& feet, float32 yaw);
    [[nodiscard]] Useable* useableNamed(const std::string& name);
    std::vector<Ladder> ladders_;
    void buildLadders(const std::vector<Assets::IniRecord>& entityTypes);
    // Thrown items in flight (a throwerShot: GRENADE_PHYS, a point mass of 5 under the physics'
    // 25 m/s^2, held to 30 m/s: 0x48bb80).
    struct Thrown {
        const ItemType* type = nullptr;
        std::shared_ptr<Character> model;
        Math::Vector3 pos{0.0f, 0.0f, 0.0f}, vel{0.0f, 0.0f, 0.0f};
        float32 spin = 0.0f;
        bool done = false;
    };
    std::vector<Thrown> thrown_;
    std::vector<WorldSound> worldSounds_; // posted this frame; heard by the hunters, then cleared
    void updateThrown(float32 dt, HunterWorld& w);
    void landThrown(Thrown& t, const Math::Vector3& at);
    TextureSet haloTexture_; // frontend_pc.txd's "halo" alone
    // Particle effects (Game/fx.h): the FX project and the INI templates, their textures
    // (levels/GLOBAL/SFX/mh_fx_pc.txd and sfx_pc.txd), what plays. The scripts' effects are fx
    // ids; an effect attached to an entity's matrix (AttachEffectToMatrix) rides it, at the
    // offset SetEffectPosition gives.
    FxLibrary fxLib_;
    FxManager fx_;
    TextureSet fxTextures_;
    struct FxAttach {
        uint32 entity = 0;
        Math::Vector3 offset{0.0f, 0.0f, 0.0f};
    };
    std::unordered_map<int32, FxAttach> fxAttached_;
    // The music's fire sources (0x5a5b50, at most 40): where, and the effect that keeps one
    // going (0: for good).
    struct MusicSource {
        Math::Vector3 position{0.0f, 0.0f, 0.0f};
        int32 fx = 0;
    };
    std::vector<MusicSource> musicSources_;
    std::vector<DecalRequest> decals_;
    int32 ratLimit_ = 16;
    void addMusicSource(const Math::Vector3& at, int32 fx);
    MusicAmbience ambience_; // the level handlers' state (reset with the level)
    MusicAmbience& musicAmbienceState(); // set up for this level on first use
    bool shelteredPlayer_ = false, shelteredCamera_ = false;
    float32 shelter_ = 0.0f; // 0x7b3278
    bool litterOn_ = true;   // ! 0x7c522c
    uint32 musicFrame_ = 0;
    void loadFx(const Assets::PakFile& pak);
    std::string savePointModel_ = "CJ_Save_Point(VHSTape)"; // Gen_Save_Point's MODEL
    std::string checkpointText_;  // CHEKPNT, global text ("Save Point Hit")
    // Halos of things that are not pickups (gas cans): the mesh, where, how, and whose.
    struct FixedHalo {
        Renderer::Mesh* mesh = nullptr;
        Math::Vector3 centre{0.0f, 0.0f, 0.0f};
        PickupHalo halo;
        int32 placement = -1;
    };
    std::vector<FixedHalo> fixedHalos_;
    Math::Vector3 viewEye_{0.0f, 0.0f, 0.0f}, viewRight_{1.0f, 0.0f, 0.0f}, viewUp_{0.0f, 1.0f, 0.0f};
    std::unordered_set<int32> destroyed_; // placements DestroyEntity took away (checkpoint: "NUL")
    std::vector<const ItemType*> inventory_;
    // The entity name each carried item had lying in the level (IsItemInInventory,
    // GetIndexFromInventoryItemName: Asylum's 'Hunter1_Tower_h').
    std::unordered_map<const ItemType*, std::string> inventoryNames_;
    // A hunter's two weapons from its placement (WEAPON melee, WEAPON2 firearm): the scripts
    // switch it to the other (GetIndexFromInventoryItemType / SetInventoryItemAsCurrent).
    std::unordered_map<const Hunter*, std::array<const ItemType*, 2>> hunterArms_;
    const ItemType* fistsSaved_ = nullptr; // TemporarySetPlayerToFists: what goes back to his hand
    bool fistsTemporary_ = false;
    bool weaponSelectionDisabled_ = false; // DisableWeaponSelection
    // G5: SetHunterDropAmmo (rounds its gun falls with), SetPedDecayInstantly (when the body
    // goes), the type of the execution under way (GetExecutionType).
    std::unordered_map<const Hunter*, int32> dropAmmo_;
    // Movers (EC_MOVER, "_(M)" props): SetMoverIdlePosition sets where it goes,
    // MoveMoverToIdlePosition sends it there at SetMoverSpeed (metres a second; ours: no
    // acceleration). Its meshes and collision move with it.
    struct Mover {
        std::string name;
        int32 placement = -1;
        Math::Vector3 origin{0.0f, 0.0f, 0.0f};
        Math::Vector3 offset{0.0f, 0.0f, 0.0f}, target{0.0f, 0.0f, 0.0f};
        float32 speed = 1.0f;
        bool moving = false;
        std::vector<Renderer::Mesh*> meshes;
    };
    std::vector<Mover> movers_;
    Mover* moverOf(uint32 handle);
    // A level light the scripts drive (CEntityLight +0x14c..+0x16c; LevelContent::lightOf):
    // switched on / off (0x4cd8a0 / 0x4cd8e0: its RpLight flags; the fades they start take no
    // time, their lengths +0x17c / +0x184 are never set) and flickering (SetLightFlicker: mode 2).
    struct LightState {
        bool on = true;
        int32 flicker = 0;                   // +0x14c: 2 flickering
        bool forever = false;                // +0x150 == -1
        float32 left = 0.0f;                 // +0x154: ms of flicker left (0 none)
        float32 onMs = 0.0f, offMs = 0.0f;   // +0x158 / +0x160: the longest it stays lit / dark
        float32 timer = 0.0f;                // +0x168
        bool offAtEnd = false;               // +0x16c: switched off when the flicker ends
    };
    std::unordered_map<int32, LightState> lights_; // by placement index
    LightState* lightOf(uint32 handle);
    void updateLights(float32 dt);
    void updateMovers(float32 dt);
    std::unordered_map<const Hunter*, float32> decayAt_;
    std::unordered_set<const Hunter*> decayInstantly_;
    int32 executionType_ = 0;
    bool bossShown_ = false; // the boss's bar, once he has fought him (0x7d34d8 / 0x7d34dc)
    // The exe's inventory slot of a carried item (ped +0x150, the scripts' slot numbers): its
    // holster (BACK 1 .. BELT_REAR 4, SPECIAL 5), others from 6 in the order taken (ours); -1
    // when he does not carry it.
    [[nodiscard]] int32 slotOf(const ItemType* t) const;
    [[nodiscard]] const ItemType* itemAtSlot(int64 slot) const;
    void disarmHunter(Hunter& hunter);
    const ItemType* equipped_ = nullptr;
    const ItemType* pickedUp_ = nullptr;
    bool swapHeld_ = false;            // INVENTORY SWAP held (Tab / the middle button)
    std::string swapPrompt_;           // SWAPINV, global text
    std::pair<int32, int32> swapOffer_{0, 0}; // this frame's offer: holster slot, collectable type (0x5df980)
    std::unordered_map<int32, std::string> itemNames_; // collectable type -> its global-text name (0x4c5870)
    const void* prompted_ = nullptr;   // the pickup the swap prompt was last shown for
    int32 lastPickedUp_ = 0; // eCollectableType (GetLastItemPickedUp)
    GunAmmo ammo_;
    std::vector<Assets::IniRecord> tvp_;
    NavGraph nav_;
    std::vector<std::unique_ptr<Hunter>> hunters_;
    HunterSpeech hunterSpeech_; // which line a hunter says (its own speech, PlayHunterSpeech)
    std::unordered_map<const Hunter*, std::string> hunterRecords_; // model record of each hunter
    std::array<MeleeRepeats, kMeleeTraitCount> meleeRepeats_{};    // the melee-traits records' repeat counts
    std::unordered_map<const Hunter*, AmbientLight> hunterAmbient_; // eased floor light (exe 0x49a6f0)
    // Hunters noticing bodies (the squad coordinator's scan, 0x50cb80): its tick count (one hunter
    // in 11 looks each tick).
    uint32 bodyScan_ = 0;
    void scanBodies(HunterWorld& w);
    // Wait for Backup (coordinator mode 7): the squad standing by a body until its backup is there.
    struct BackupWait {
        Math::Vector3 at{0.0f, 0.0f, 0.0f};       // the body
        Math::Vector3 finderAt{0.0f, 0.0f, 0.0f}; // where the backup goes (the finder's place)
        float32 until = 0.0f;
        const Hunter* finder = nullptr;
        std::vector<Hunter*> squad, backup;
    };
    std::vector<BackupWait> backupWaits_;
    void updateBackupWaits(HunterWorld& w);
    // Territories (AISubpackStayInTerritory + AIAddAreaForSubpack): each squad's nodes, and each
    // hunter's last answer (HunterWorld::boundaryNode), worked out again each second.
    struct Territory {
        std::size_t areas = 0; // the subpack's area count it was built from
        std::vector<uint8> in; // per nav node
        bool any = false;
    };
    std::unordered_map<std::string, Territory> territories_;
    struct BoundaryAnswer {
        float32 at = -1e9f;
        int32 playerNode = -1, node = -1;
    };
    std::unordered_map<const Hunter*, BoundaryAnswer> boundary_;
    int32 boundaryNode(const Hunter& h);
    // Each hunter on the radar (exe 0x51a510, 0x40f0a0, 0x4ea1b0): its state (0 lost him,
    // 1 heard him, 2 seen him, 3 not after him: left off), how visible its blip is (0..180, a
    // byte the exe draws as a percentage up to 100), and AISetHunterOnRadar.
    struct RadarTrack {
        int32 state = 3;
        float32 visible = 0.0f;
        int32 lookIn = 0; // ticks to the next line-of-sight check while it has lost him
        bool onRadar = true;
    };
    std::unordered_map<const Hunter*, RadarTrack> radarTracks_;
    float32 radarTick_ = 0.0f;
    std::array<std::pair<uint32, int32>, 10> radarScript_{}; // RadarPositionSetEntity: handle, MAP_COLOR
    std::vector<std::pair<Math::Vector3, float32>> radarNoise_;
    std::vector<Hunter*> headBursts_;
    std::array<int64, 8> scriptAudio_{-1, -1, -1, -1, -1, -1, -1, -1}; // LoadScriptAudioSlot: SFX bank ids
    std::array<float32, 8> slotEnds_{};    // when each slot's one-shot ends (runtime time)
    bool directorSpeaking_ = false;
    bool playerControllable_ = true; // SetPlayerControllable
    bool userInputDisabled_ = false, movementLocked_ = false;
    std::optional<Math::Vector3> playerGoto_;
    bool playerArrived_ = true;
    std::string playerAnim_;
    bool dropBody_ = false, clearInput_ = false;
    uint32 padPressed_ = 0, padHeld_ = 0; // HasPadButtonBeenPressed latches
    std::optional<LevelSetup> forcedWeather_; // ForceWeatherType: the level's setup with that weather
    bool playerInvulnerable_ = false; // SetEntityInvulnerable(GetPlayer, ..)
    std::vector<int32> endedLoops_;
    bool subtitles_ = true;
    float32 speechSeconds_ = 10.0f; // the subtitle box's page time (0x722050, 10 s at 0x5c458a)
    int32 frontEndId_ = -1;         // the loaded front-end stream (0x49..0x56)
    FrontEndStream frontEnd_;
    std::vector<PropAnim> propAnims_;
    // The crane (see updateCrane / craneView).
    struct CraneCamera {
        bool on = false;
        float32 onFor = 0.0f;               // seconds since it came on
        Math::Vector3 position{0.0f, 0.0f, 0.0f}; // scene frame
        uint32 lookAt = 0;                  // entity handle (0: the last target)
        Math::Vector3 offset{0.0f, 0.0f, 0.0f};
        Math::Vector3 target{0.0f, 0.0f, 0.0f};
    };
    CraneCamera craneCam_;
    uint32 padTurned_ = 0;                  // SetPadControlEntityRotation's entity
    CraneTurn craneTurn_;
    std::unordered_map<int32, float32> propYaw_; // by placement: how far it has turned (radians)
    // Physics props (Game/physics_props.h) and the hanging magnet (Game/crane.h).
    PhysicsProps physics_;
    bool magnetOn_ = false, magnetButton_ = false;
    std::unordered_map<int32, uint32> placementEntity_; // placement index -> entity handle
    void buildPhysicsProps();
    void updatePhysicsProps(float32 dt, HunterWorld& w);
    [[nodiscard]] uint32 entityOfPlacement(int32 placement);
    struct PropLink {
        uint32 child = 0, parent = 0;
        Math::Vector3 offset{0.0f, 0.0f, 0.0f}; // LinkHangingEntToEnt: in the parent's frame (script axes)
        bool hanging = false;
        Hanging swing;                          // a hanging one's pendulum
    };
    std::vector<PropLink> propLinks_;
    // A prop's meshes turned by `yaw` about the vertical and their origin moved to `to`.
    void placeProp(Entity& en, float32 yaw, const Math::Vector3& to);
    // ... or by a whole transform (from where its meshes were placed), its origin now at `to`.
    void placeProp(Entity& en, const Math::Matrix4x4& transform, const Math::Vector3& to);
    std::unordered_set<int32> animatedProps_, hiddenProps_; // by placement index
    // A hunter takes up an item in its hand (its spawn weapon, CreateInventoryItem).
    void armHunter(Hunter& hunter, const ItemType* item);
    void updateRadar(float32 dt);
    std::vector<float32> nodeLight_;                               // baked light at each graph node
    std::unordered_map<int32, float32> searched_;                  // node -> when it was last searched
    std::vector<SoundEvent> sounds_;
    // An uzi firing (0x4b6e50): its burst's end posts event 0xb with value 1 once it stops.
    struct Burst {
        const void* shooter = nullptr;
        float32 last = 0.0f;
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
    };
    std::vector<Burst> bursts_;
    std::vector<int32> directorSpeech_;
    int32 hunterDeaths_ = 0;
    std::pair<int32, int32> killable_{0, 0};
    float32 time_ = 0.0f;
    std::vector<Entity> entities_; // handle = index + 1
    std::unordered_set<std::string> exploded_; // shot or blown up already (once each: ours)
    std::unordered_set<std::string> gasCanRecords_; // the GAS_CAN records (lower case)
    uint32 gasCarried_ = 0;                         // the can on his hand, 0 none
    std::unordered_map<std::string, uint32> byName_;
    std::vector<Trigger> triggers_;
    uint32 playerHandle_ = 0;
    PlayerSnapshot player_;
    std::mt19937 rng_{0x4d48u};

    float32 playerHealth_ = 100.0f;
    bool debugLog_ = false;
    float32 logTimer_ = 0.0f;
    std::optional<Math::Vector3> hit_;
    std::string hitClip_;
    std::optional<Math::Vector3> push_;
    Hunter* grab_ = nullptr;
    std::optional<Knock> knock_;
    std::optional<Math::Vector3> shot_;
    std::vector<GunEvent> gunEvents_;
    int32 blockedHit_ = 0;
    std::optional<Math::Vector3> teleport_;
    float32 followRate_ = 1.0f, lookRate_ = 1.0f; // PlaySplineFile: its length scaled to the duration given
    std::optional<float32> heading_;
};

} // namespace Game
