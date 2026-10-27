// Game/game_session.h - one playthrough of a level (or the character-only rig): its scene,
// character, collision, save, cutscene and per-frame logic. Destroying it releases everything
// it put on the GPU, so the frontend can start and end sessions repeatedly.
#pragma once
#include "engine_types.h"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <optional>
#include <random>
#include <unordered_map>
#include <string>
#include <vector>

#include "Game/coronas.h"
#include "Game/bindings.h"
#include "Game/anim_state.h"
#include "Game/app_config.h"
#include "Game/character.h"
#include "Game/cutscene_player.h"
#include "Assets/font.h"
#include "Game/director_milestones.h"
#include "Game/level_score.h"
#include "Game/director_speech.h"
#include "Game/execution.h"
#include "Game/gun_fx.h"
#include "Game/gunplay.h"
#include "Game/ambient_light.h"
#include "Game/hud.h"
#include "Game/level_music.h"
#include "Game/decals.h"
#include "Game/rats.h"
#include "Game/crows.h"
#include "Game/litter.h"
#include "Game/radar.h"
#include "Game/weapon_strip.h"
#include "Game/stamina.h"
#include "Game/acoustic_probe.h"
#include "Game/player_audio.h"
#include "Game/keymap.h"
#include "Game/level_runtime.h"
#include "Game/melee_combo.h"
#include "Game/player_controller.h"
#include "Game/player_field.h"
#include "Game/save_system.h"
#include "Physics/physics.h"
#include "Renderer/fly_camera.h"
#include "Renderer/scene.h"

class AssetManager;
namespace Audio { class AudioEngine; }
namespace Renderer { class Camera; class Renderer; }

namespace Game {

struct SessionStart {
    std::string level;          // level folder under levels/ (ignored for the rig)
    std::string savePath;       // optional save to restore its checkpoint from (empty = the level's start)
    bool characterOnly = false; // the animation rig instead of a level
    // A save point's checkpoint to start from (Game/checkpoint.h): the level built from its
    // entity block, no intro, and its inventory, score and script data (0x474330).
    std::shared_ptr<const SaveFile> checkpoint;
    // Without a checkpoint: the inventory the scene before left (0x5d2590), if it carries one.
    std::optional<std::array<SaveInventorySlot, 4>> carried = std::nullopt;
};

class GameSession {
public:
    GameSession(const AppConfig& cfg, Renderer::Renderer& renderer, Audio::AudioEngine& audio, AssetManager& assets);
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // Load everything for the session. Blocks while the level loads.
    void start(const SessionStart& start);

    // Feed one SDL event. Returns true when it was consumed.
    bool handleEvent(const SDL_Event& evt);
    void update(float32 dt);

    // Release the mouse (the level captures it for mouse-look). Called when the session ends.
    void releaseMouse();

    [[nodiscard]] Renderer::Scene& scene() noexcept { return scene_; }
    // The player died 4 s ago (0x46f880): the scene has failed (the Scene Failed screen comes up
    // over it while it goes on).
    [[nodiscard]] bool sceneFailed() const noexcept { return failed_; }
    // The level script called SetLevelCompleted: the next scene should load.
    // (After the outro: the picture fades to black first.)
    [[nodiscard]] bool levelCompleted() const noexcept { return outroFor_ >= kOutroSeconds; }
    // The scene's results are up: the level's _L stream (Game/level_music.h).
    void playLevelEndMusic() { music_.playLevelEnd(audio_); }
    // This frame's 2D overlay (the HUD), before the frame is rendered.
    void drawOverlay(Renderer::UiBatch& ui) const;

private:
    // Pick and pace the player's locomotion clip for this frame's movement.
    void animateLocomotion(const PlayerInput& in);
    // The player's punches, hit reactions and death.
    void updateCombat(float32 dt, PlayerInput& in);
    void playerAction(const std::string& clip, float32 lockFraction, bool inPlace = false);
    // Play an SFX bank sample (or a random variant of a category) at a scene position.
    // Returns the sample's length in seconds (0 if nothing was played).
    // `name`: an SFX bank sample, a category (<name>_1.._n picked at random) or "#<index>". A
    // looped one gives its channel through `loopChannel`. A 2D one (not positional) may be panned
    // (`pan` degrees: 90 right, 270 left).
    float32 playSample(const std::string& name, const Math::Vector3& at, float32 priority, bool positional,
                       float32 range = LevelRuntime::kSoundRange, float32 volume = 1.0f, uint32* loopChannel = nullptr,
                       float32 rateScale = 1.0f, float32 pan = -1.0f);
    void updateFootsteps(float32 dt);
    // A step's sound (0x44ea20): event 0xf sneaking, 0x10 walking, 0x11 running, 0x13 landing.
    void playStep(uint16 event, const Math::Vector3& feet);
    // The audio manager's volume (0..1) for a sound of `level` (0..127) at `at` heard over `range`
    // from the camera: unheard (negative) at or beyond it, else 0x452e70's falloff times what gets
    // through the world between (0x4df3b0; `through` false: not, as for his heart and breath).
    [[nodiscard]] float32 entityVolume(int32 level, float32 range, const Math::Vector3& at, bool through = true) const;
    // A weapon's entity audio event (Game/weapon_audio.h) for collectable type `ct` at `at`.
    void playWeaponSound(int32 ct, uint16 event, float32 value, const Math::Vector3& at);
    // His heartbeat and breath (Game/player_audio.h), once a frame.
    void updatePlayerAudio();
    // A bank sample played the audio manager's way: its level (Game/sample_levels.h) falling off
    // over its range (or `range` when given) from the camera, its rate jittered (0x453080).
    void playEntitySound(int32 sample, const Math::Vector3& at, float32 range = 0.0f);
    void updateDirector(float32 dt);
    void updateScreenEffects(float32 dt);
    void updateLockOnSpeech(float32 dt);
    void startMilestones();
    void updateMilestones(float32 dt);
    // The camcorder overlay's parts that are on (LevelRuntime::ScriptLook::camcorder).
    void drawCamcorder(Renderer::UiBatch& ui, const std::array<bool, 5>& parts) const;
    // Pickup sounds, and the equipped weapon's model in his right hand.
    void updateItems();
    // Executions: lock-on, the charge, the kill.
    void updateExecution(float32 dt);
    void drawLockOn(Renderer::UiBatch& ui) const;
    // What he holds, for the HUD's weapon strip.
    [[nodiscard]] WeaponStripView weaponStripView() const;
    // A gun in his hand: aiming (the aim button held: lock-on), the trigger, reloads, the recoil
    // (Game/gunplay.h). Called from updateCombat instead of the melee combo.
    void updateGun(float32 dt, PlayerInput& in);
    // A thrown item in his hand (Game/ai/ai_tuning.h ThrowWeapon): the fire button held charges
    // the throw (Throw_Charge_T, Throw_Charge_Run_T on the move), let go it leaves (Throw_Fire_T).
    void updateThrow(float32 dt, PlayerInput& in);
    // Puts the gun down (another weapon, a kill, a fall ...).
    void lowerGun();
    // The gun's sight: the lock mark on its target, else the crosshair; the rounds left.
    void drawGunSight(Renderer::UiBatch& ui) const;
    // Close combat: his guard, kicks / stomps, grabbing a hunter and being grabbed. True while a
    // kick or a grapple holds him (no walking or punching then).
    bool updateCloseCombat(float32 dt);
    [[nodiscard]] const MeleeWeapon& playerMelee() const;
    void startKill(int32 level);
    [[nodiscard]] bool killing() const noexcept { return kill_.hunter != nullptr; }
    [[nodiscard]] Math::Vector3 playerFeet() const;
    // Ambient light for a character standing at `feet`: the baked light of the floor there.
    void setLighting(Renderer::LightingMode mode);

    const AppConfig& cfg_;
public:
    // The in-game menu is open: no updates come, so let the mouse go (update() takes it back).
    void pause();
    // The level's intro video is playing (ESC skips it rather than opening the menu).
    [[nodiscard]] bool playingIntro() const noexcept { return cutscene_.isActive(); }
    // The last save point's checkpoint (null before one): what SAVE GAME writes and Retry restarts
    // from (0x4d2400). takeNewCheckpoint() is true once after each.
    [[nodiscard]] std::shared_ptr<const SaveFile> checkpoint() const noexcept { return checkpoint_; }
    [[nodiscard]] bool takeNewCheckpoint() noexcept { return std::exchange(newCheckpoint_, false); }
    // The scene's results so far (Game/level_score.h), for the results screen.
    [[nodiscard]] LevelResults results() const;
    // The player's inventory slots 1..4 now (0x5d24c0).
    [[nodiscard]] std::array<SaveInventorySlot, 4> inventorySlots() const;
    // The level goals' text, for the in-game menu.
    [[nodiscard]] std::vector<std::string> levelGoals() const;

private:
    // The vertical field of view after the RATIO setting.
    [[nodiscard]] float32 viewFov(float32 fov) const;
    Renderer::Renderer& renderer_;
    Audio::AudioEngine& audio_;
    AssetManager& assets_;

    Renderer::Scene scene_;
    Renderer::Camera* camera_ = nullptr;
    Physics::PhysicsEngine physics_;
    Character character_;
    std::shared_ptr<const Assets::IfpFile> animations_; // parsed once, shared by every character
    std::unique_ptr<LevelRuntime> runtime_;            // scripts, hunters, triggers (level mode)
    Keymap keymap_;
    AnimStateMachine animState_{keymap_};
    // The level now as a checkpoint (entity block, inventory, script data, score; the header and
    // state block are the save slot's to fill).
    [[nodiscard]] std::shared_ptr<SaveFile> snapshot() const;
    [[nodiscard]] std::vector<std::uint8_t> scoreBlock() const;
    void restoreScoreBlock(const std::vector<std::uint8_t>& block);
    std::shared_ptr<const SaveFile> checkpoint_;
    bool newCheckpoint_ = false;
    PlayerField playerField_;
    PlayerController playerCtrl_;
    CutscenePlayer cutscene_;
    Renderer::FlyCamera flyCam_;

    std::string levelDir_;
    std::string levelName_; // its folder name (Jury_Turf)
    bool levelMode_ = false;
    bool levelLoaded_ = false;
    bool useFlyCam_ = false;
    bool markerVisible_ = false;
    bool onGround_ = true;
    bool mouseCaptured_ = false;
    int ignoreMouseFrames_ = 0; // capture starts with a jump of the cursor to the window centre
    Renderer::LightingMode lightingMode_ = Renderer::LightingMode::Mixed;
    Math::Vector3 levelAmbient_{0.1f, 0.1f, 0.12f}; // the level's mean prelight (baked modes)
    AmbientLight playerAmbient_;
    MeleeCombo combo_;            // the player's fist attacks
    bool swiped_ = false;         // a punch started this frame (MELEE_SWIPE for the hunters)
    // SetPlayerGotoNode: its point, his nearest yet and how long he has not got nearer.
    Math::Vector3 gotoAt_{0.0f, 0.0f, 0.0f};
    float32 gotoBest_ = 1e9f, gotoStuck_ = 0.0f;
    float32 actionLeft_ = 0.0f;   // seconds the player is locked by a one-shot (flinch, death)
    std::unordered_map<std::string, int> sampleVariants_; // SFX category -> number of _<n> variants
    DirectorSpeech director_;                 // AISPEECH id -> SFX bank lines
    Hud hud_;
    Radar radar_;
    LevelMusic music_;
    bool musicOn_ = false; // started (after the intro video)
    WeaponStrip weaponStrip_;
    // The strip was drawn last frame: its name fades only then (exe moves the timer in the
    // strip's draw, 0x5de320), so a pickup's name waits out a script's camera.
    mutable bool weaponStripDrawn_ = false;
    Stamina stamina_;
    PlayerAudio playerAudio_;   // his heartbeat and breath
    float32 camShare_ = 1.0f;   // the follow camera's share of its distance out (0x7a1038)
    bool splosh_ = false;       // the carried body's last splosh was the first of the two
    AcousticProbe acoustics_;   // the camera's surroundings (the guns' tails)
    float32 playerNoise_ = 0.0f; // how far his sounds carry this frame (m): his radar ring
    // A blow's reaction carries him away from the attacker: the clip's root travel, spread over
    // its length (the clip itself plays in place).
    Math::Vector3 hitSlide_{0.0f, 0.0f, 0.0f};
    float32 hitSlideLeft_ = 0.0f, hitSlideTime_ = 0.0f;
    std::array<uint32, 8> scriptLoops_{}; // looped script audio slots: their channels (0 = none)
    // Animated props (LevelRuntime::PropAnim): each one's model by placement index; its static
    // meshes are hidden.
    std::unordered_map<int32, std::unique_ptr<Character>> propModels_;
    void updatePropAnims(float32 dt);
    // The front-end stream the scripts opened (LevelRuntime::FrontEndStream) and its channel.
    std::string frontEndPath_;
    uint32 frontEndChannel_ = 0;
    float32 time_ = 0.0f;
    float32 lastShotAt_ = -1e9f; // level time of his last gunshot (hunters under fire)
    std::vector<std::string> directorQueue_;  // parts of the line being spoken, next first
    int32 directorLine_ = -1;                 // AISPEECH id of the line being spoken
    std::size_t directorPart_ = 0;            // parts of it started so far
    std::vector<Assets::Font> fonts_;         // initscripts/FONTS/font.dat
    const Assets::Font* textFont_ = nullptr;  // font 1: game text (its atlas in frontend_pc.txd: ExeTables::fontAtlas)
    std::shared_ptr<const DffLibrary> weaponModels_; // modelspc.dff when the runtime has not parsed it
    // levels/GLOBAL/PAK/gmodelspc.dff / .txd, loaded on first use (Bag_Head, ...).
    std::shared_ptr<const DffLibrary> globalModels_;
    TextureSet globalTextures_;
    [[nodiscard]] bool loadGlobalModels();
    // A burst head's pieces (DedBit1..4) flying off and lying where they land.
    struct HeadBit {
        std::unique_ptr<Character> model;
        Math::Vector3 position{0.0f, 0.0f, 0.0f}, velocity{0.0f, 0.0f, 0.0f};
        float32 yaw = 0.0f, spin = 0.0f, floor = 0.0f;
        int32 landings = 0;
    };
    std::vector<HeadBit> headBits_;
    std::mt19937 bitRng_{20031118u};
    void explodeHead(Hunter& h, bool scored = true);
    void updateHeadBits(float32 dt);
    std::string heldModel_;                   // the weapon model in his hand ("" = fists)
    std::vector<std::string> holstered_;      // weapon models hanging on his belt
    Hunter* execTarget_ = nullptr;            // the hunter he is locked on to (behind it, unseen)
    ExecutionCharge charge_;
    struct Kill {
        Hunter* hunter = nullptr;             // nullptr: no kill running
        const ItemType* item = nullptr;
        ExecutionPlan plan;
        std::vector<CameraShot> shots;        // the camera set picked for it
        Math::Vector3 feet{0.0f, 0.0f, 0.0f}; // his origin (the camera shots are in his frame)
        Math::Vector3 hunterFeet{0.0f, 0.0f, 0.0f}; // the victim's origin (he stands 1 m behind it)
        float32 yaw = 0.0f;
        float32 t = 0.0f, length = 0.0f;
        int32 shot = -1;
        bool weaponGone = false;
        bool hunterDropped = false, headOff = false;
        bool victimDead = false;              // its Damage_ clip ended and its Die_ clip plays
        bool headBurst = false;               // EXECUTE_HEAD_EXPLODE_TIME passed
    } kill_;
    float32 cutNoise_ = 0.0f;                 // white noise at the kill's camera cuts
    // His gun (see updateGun).
    struct Gun {
        bool aimHeld = false;                 // the aim button (right mouse) is down
        bool triggerHeld = false;             // the attack button is down ...
        bool pulled = false;                  // ... and was pressed since the last frame
        bool reloadPressed = false;           // R
        bool raised = false;                  // the aim stance is up
        float32 raisedFor = 0.0f;             // a shot without aiming keeps it up this long
        Hunter* lock = nullptr;               // locked on to
        bool head = false;                    // the lock aims at its head
        bool wantHead = false;                // the player pushed the lock up to the head
        float32 headPush = 0.0f;              // mouse travel up (+) / down (-) while locked
        float32 busy = 0.0f;                  // seconds the fire / reload clip still plays
        bool reloading = false;
        bool roundByRound = false;            // a RELOAD_LOOP reload looping a round at a time
        bool fired = false;                   // a shot this frame (GUN_SHOT for the hunters)
        std::string weapon;                   // the gun's record (another one resets all this)
        int32 zoom = 0;                       // zoom level: 0 none, 1 ZOOM_LEVELS[0], 2 [1] (rifles)
        int32 zoomSteps = 0;                  // wheel / Z presses since the last frame (+ in, - out)
        float32 magnification = 1.0f;         // the view's now, easing to the level's
        float32 strikeT = -1.0f;              // a blow with the gun (a hunter in its way): time in, < 0 none
        float32 strikeLength = 0.0f;
        bool strikeLanded = false;
    } gun_;
    Recoil recoil_;
    GunEffects gunFx_;                        // flashes, streaks, shells and nails
    // The shots and reloads of this frame (his and the hunters') -> their effects.
    void updateGunEffects(float32 dt);
    // Zoomed aiming: the view from his eye, magnified (see updateGun).
    [[nodiscard]] bool zoomed() const noexcept { return gun_.magnification > 1.01f; }
    [[nodiscard]] Math::Vector3 zoomEye() const;
    std::mt19937 gunRng_{0x4e41494cu};
    // Close combat (see updateCloseCombat).
    bool guard_ = false;                      // his guard is up
    float32 guardUp_ = 0.0f;                  // seconds it has been rising (BLOCK_SPEED)
    float32 sinceAttack_ = 10.0f;             // seconds since he last pressed attack
    float32 overlayLeft_ = 0.0f;              // a one-shot upper-body reaction still playing
    bool kickPressed_ = false, grabPressed_ = false, attackPressed_ = false;
    bool usePressed_ = false; // Space: use the switch / useable in front (LevelRuntime::playerUses)
    // Body carry (exe BT_DEADCARRY_PICKUP / _STAND / _MOVE / _DROP; 0x4646b0 picks it up):
    // the body plays its half of each paired clip (GenHunterAnims DeadBody_*) at his root.
    struct Carry {
        enum class Phase { None, Pickup, Carrying, Drop } phase = Phase::None;
        Hunter* body = nullptr;
        float32 left = 0.0f;   // of the pickup / drop clip
        float32 bodyIn = 0.0f; // until the body's half of it starts
        std::string clip;      // that clip
        Math::Vector3 offset{0.0f, 0.0f, 0.0f}; // the body's half off his feet (the pickup)
    } carry_;
    static constexpr int16 kCarryBone = 1095; // the body rides in this bone's frame (0x4656fe)
    // The gas can carried (0x4646b0 / 0x4657b0 with the gas clips: Gas_Pick_Up 0x316, Gas_Stand /
    // Gas_Turn / Gas_Walk_*, Gas_Put_Down_Slow / _Quick 0x319 / 0x31a): the can in his right hand.
    struct GasCarry {
        enum class Phase { None, Pickup, Carrying, Drop, Pour } phase = Phase::None;
        uint32 can = 0;       // its entity handle
        float32 left = 0.0f;  // of the pickup / put-down clip
        bool inHand = false;  // taken off the floor (halfway through Gas_Pick_Up: ours)
        std::unique_ptr<Character> pour; // the can playing its COLLECTABLE_ANIM in his hand
    } gas_;
    bool startGasCarry(const Math::Vector3& feet, float32 yaw);
    bool updateGasCarry(float32 dt, PlayerInput& in);
    // Space with no useable in reach: the body there, if any, is picked up.
    bool startCarry(const Math::Vector3& feet, float32 yaw);
    // Carrying drives him (and holds the rest of combat): true while it does.
    bool updateCarry(float32 dt, PlayerInput& in);
    // Wall strafe (exe behaviours 0x31..0x37): 0x4a6700 finds a wall about him, 0x4a3d40
    // puts his back to it, 0x4a4270 slides him along it and leans him out past its end, 0x4a4160
    // picks his stance, 0x4a3ee0 takes him off it.
    struct Wall {
        bool on = false;
        bool leaving = false;                 // Wall_Stand_Trans_Quick playing
        Math::Vector3 normal{0.0f, 0.0f, 1.0f}; // out of the wall: the way he faces
        int32 facing = 0;                     // -1 / 0 / +1: the way he last slid (Wall_Stand_Left / _Fwd / _Right)
        int32 peek = 0;                       // -1 / +1 while he leans out
        float32 edgeHeld = 0.0f;              // seconds pressed on past the wall's end
        float32 snug = 0.0f;                  // his model drawn this much nearer the wall (ours)
        // Leaning out to fire (behaviour 0x37, 0x4a44b0): -1 / +1 round the wall's end, 2 over
        // it crouched; 0 not. `low` after a crouched one (Wall_Crouch_Fwd2, 0x4a4160).
        int32 fire = 0;
        bool low = false;
        float32 shotWait = 0.0f;
        // With no gun the lean is a blow (1) or a throw (2), `actFor` into its clip, `actLeft`
        // to go; `acted` until aim is let go, `acted2` once it has landed / left his hand.
        int32 act = 0;
        float32 actFor = 0.0f, actLeft = 0.0f;
        bool acted = false, acted2 = false;
    } wall_;
    static constexpr float32 kWallReach = 0.9f; // the wall probes' reach from his middle (ours: his 0.5 m body + 0.4)
    static constexpr float32 kWallBack = 0.25f; // his back behind his root (ours)
    // Space with nothing to use or pick up: a wall about him, if any, takes his back.
    bool startWall(float32 yaw);
    bool updateWall(float32 dt, PlayerInput& in);
    // The wall `reach` along `dir` from `from`, as its normal toward `from` (two rays a hand apart).
    [[nodiscard]] std::optional<Math::Vector3> wallNormal(const Math::Vector3& from, const Math::Vector3& dir, float32 reach) const;
    // Ladders (behaviours 0x16 BT_CLIMABLE_BASE_MOUNT Ladder_OnBase, 0x17 _BASE_DISMOUNT
    // Ladder_OffBase, 0x18 _MOVE Ladder_Cycle, 0x19 _TOP_MOUNT Ladder_OnTop, 0x1a _TOP_DISMOUNT
    // Ladder_OffTop; the climbable's handler 0x4c4c30 starts them).
    struct Climb {
        enum class Phase { None, OnBase, OnTop, Move, OffBase, OffTop } phase = Phase::None;
        const LevelRuntime::Ladder* ladder = nullptr;
        Math::Vector3 line{0.0f, 0.0f, 0.0f}; // his root's spot against the ladder (x, z)
        Math::Vector3 origin{0.0f, 0.0f, 0.0f}; // where his model stands for the clip playing
        float32 left = 0.0f;                  // of the mount / dismount clip
        float32 into = 0.0f;                  // walking into the base (+0x178 against the record's +0x21c)
    } climb_;
    static constexpr float32 kLadderStand = 0.4f;  // his root off the ladder's face (ours)
    static constexpr float32 kLadderInto = 0.25f;  // walking into the base this long mounts it (ours)
    static constexpr float32 kLadderHips = 1.523f; // Ladder_OnBase's last root height: Ladder_Cycle's origin over the base
    static constexpr float32 kLadderTopHips = 1.003f; // Ladder_OnTop's last root depth under the top
    bool updateClimb(float32 dt, PlayerInput& in);
    [[nodiscard]] bool climbing() const noexcept { return climb_.phase != Climb::Phase::None; }
    // What the Use key would do now (0x45eb00, the status icon's context; Game/hud.h), 9 none.
    [[nodiscard]] int32 useContext() const;
    // Jumping (behaviours 0x13 BT_JUMP_LAUNCH Jump_Launch, 0x14 _GLIDE Jump_Glide, 0x15 _LAND
    // Jump_Land): running off an edge launches him (0x4e8f0f sets +0x4e4, 0x4b0abd plays it).
    // The combat stance with a throwable (0x4aff30 -> 0x41ddd0: standing still with the aim held,
    // +0x338 0x1000; behaviours 0x1b / 0x1c with Throw_Stance_Trans / Throw_Stance, firing 0x1d
    // with Throw_Stance_Fire).
    bool throwStance_ = false;
    // The intro video's subtitles (SUBTITLES on: <video>_n.SUB, 0x60f890): a GXT key from its
    // start to its end (video frames at 30 a second), timed from the video's start (0x60fa00).
    struct FmvLine {
        std::string key;
        float32 start = 0.0f, end = 0.0f; // seconds
    };
    std::vector<FmvLine> fmvLines_;
    float32 fmvClock_ = 0.0f;
    int32 fmvShown_ = -1;
    void loadFmvSubtitles(const std::string& videoPath);
    void updateFmvSubtitles(float32 dt);
    // The player's key bindings (Game/bindings.h; cfg_.bindings).
    [[nodiscard]] Bindings binds() const noexcept { return Bindings(cfg_.bindings ? cfg_.bindings : &defaultBindings_); }
    mutable std::array<uint8, 0x130> defaultBindings_ = SettingsFile::defaults().bindingsA;
    int32 lastContext_ = 9; // the status icon's last context (--ai-log)
    struct Jump {
        enum class Phase { None, Air, Land, Getup } phase = Phase::None;
        float32 t = 0.0f; // in the air so far
        // A fall (0x4a2470): where it began (+0x354), flailing (BT_FALL_FLAIL), past the 8 m that
        // kill (BT_FALL_FLAIL_DEATH), knocked off a ladder (Fall_Launch_Ladder).
        float32 fromY = 0.0f;
        bool flail = false, death = false, ladder = false;
    } jump_;
    static constexpr float32 kFallDeath = 8.0f; // 0x728054: a drop this far kills
    static constexpr float32 kFallFlail = 1.0f; // 0x72812c: a jump this far down flails
    void updateJump(float32 dt, PlayerInput& in);
    static constexpr float32 kJumpSpeed = 4.5f; // his launch upward (ours: under the 22 m/s2 gravity, about half a metre up)
    // Peek left / right, look back and first person (the exe's default binding slots 18, 19,
    // 31 and 8: Q, E, X and the middle mouse button, 0x710994).
    int32 peek_ = 0;           // -1 / +1: leaning out (behaviour 0xf, Peek_L_ / Peek_R_ Stand / Sneak)
    bool lookBack_ = false;    // X held: the camera looks behind him
    bool firstPerson_ = false; // the middle button held: he looks from his eyes
    void updatePeek(PlayerInput& in);
    // The way his model faces: along the wall's normal on it, else the controller's.
    [[nodiscard]] float32 bodyYaw() const noexcept;
    // How far a lean (peek) carries the camera's pivot sideways.
    [[nodiscard]] Math::Vector3 leanOffset() const;
    // Turning on the spot: the heading's and the pitch's rates this frame (radians a second).
    float32 lastYaw_ = 0.0f, lastPitch_ = 0.0f, yawRate_ = 0.0f, pitchRate_ = 0.0f;
    float32 turnHold_ = 0.0f; // the turn clip keeps playing this long after the last fast turn (ours)
    [[nodiscard]] bool turningOnSpot(const PlayerInput& in) const noexcept;
    struct ThrowState {
        bool down = false;     // the fire button is held
        bool pressed = false;  // ... was pressed (a tap shorter than a frame still throws)
        bool charging = false;
        float32 held = 0.0f;   // seconds charged
        float32 busy = 0.0f;   // the release clip plays
    } throw_;
    bool groundPressed_ = false;              // attack pressed over a hunter on the floor
    float32 stepPhase_ = -1.0f;               // last gait-cycle phase (< 0: not walking)
    std::string stepClip_;                    // the gait clip that phase belongs to
    float32 sinceStep_ = 1.0f;                // seconds since the last footstep
    uint8 stepMaterial_ = 0;                  // the material type last stood on (0x1bc)
    int32 wiggles_ = 0;                       // movement presses this frame (breaking free)
    struct Kick {
        float32 t = -1.0f;                    // < 0: no kick
        float32 strikeAt = 0.0f, length = 0.0f;
        bool landed = false;
        Hunter* target = nullptr;             // a stomp / ground attack: the hunter on the floor
        float32 damage = 0.0f;
        HitKind kind = HitKind::Kick;
    } kick_;
    // Knocked down (a hard blow he did not block): Fall_Launch_Melee_*, Fall_Land_Melee_*, lying
    // KO_TIME, Getup_*.
    struct Down {
        enum class Phase { None, Launch, Land, Lying, Getup } phase = Phase::None;
        bool fwd = false;                     // he fell on his face
        std::string stem;                     // the weapon that felled him
        float32 left = 0.0f;                  // seconds left of the phase
    } down_;
    struct Grapple {
        enum class Phase { None, Grab, Attack, Broken, Grabbed } phase = Phase::None;
        Hunter* hunter = nullptr;
        bool byHunter = false;                // the hunter grabbed him (Phase::Grabbed / Attack)
        int32 blow = 0;                       // 1..3
        float32 t = 0.0f, length = 0.0f;
        bool landed = false, queued = false;
        float32 escape = 1.0f;                // being grabbed: wiggling empties it (WIGGLE_TIME_DECAY)
    } grapple_;
    // The weapon's own model in a kill (EXECUTE_MODEL Bag_Execute_(CT) playing its _W clip), and
    // those of earlier kills (hidden; their meshes stay in the scene until the level unloads).
    std::unique_ptr<Character> killModel_;
    // The lamps' coronas (Game/coronas.h): registered at the level's start, this frame's sprites.
    Coronas coronas_;
    std::vector<Coronas::Sprite> coronaSprites_;
    std::mt19937 coronaRng_{0x5eed};
    uint32 coronaFrame_ = 0;
    void buildCoronas();
    void updateCoronas();
    // Torches (exe 0x593e20 / 0x593bd0, 10 at most at 0x7a2410): a spot light for each
    // gun with TORCH_LIGHT in a hand, and a hunter's flare when its beam faces the camera.
    std::vector<Renderer::Light*> torchLights_;
    std::vector<Coronas::Light> torchFlares_;
    std::size_t torchesLogged_ = 0;
    void updateTorches();
    std::vector<std::unique_ptr<Character>> spentModels_;
    std::mt19937 killRng_{0x4b494c4cu};
    float32 directorLeft_ = 0.0f;             // seconds left of the part playing
    // The director's "kill him" lines when a lock-on appears (exe 0x413f50): AISPEECH 0
    // (EXEC1..20) on lock-on n, then every `step`-th one, at most `max` times per level; none
    // while he talks or within 10 s of the last lock-on ending. step / max by scene (0x413f00):
    // scene 1 every lock-on, 20 times; scene 2 every 2nd, 10; scene 3 every 3rd, 5; later none.
    struct LockOnSpeech {
        bool on = false, handled = false;
        float32 sinceOff = 1e9f;             // seconds since the last lock-on ended
        int32 step = 0, max = 0, count = 0, next = 1, played = 0;
    } lockOnSpeech_;
    LevelScore score_;                        // the scene's results (time, kills, style points)
    int32 sceneIndex_ = -1;                   // 0-based position in levels.txt (-1: not a story scene)
    // The director's milestone commentary (AISPEECH 1..38; Game/director_milestones.h), set up on
    // the level's first frame.
    DirectorMilestones milestones_;
    bool milestonesStarted_ = false;
    std::array<int32, DirectorMilestones::kStats> milestoneCounts_{};
    std::mt19937 directorRng_{0x44495245u};
    // Decals (Game/decals.h): the runtime's bullet holes, and the blood pool under each body
    // (0x4b30a0: from 0.01 m it grows 0.01 a tick to 1.8..2.2 m, type 4 blood_dead, laid on the
    // floor below it, while nobody carries it; the death set it up, 0x49c6b1).
    Decals decals_;
    struct BloodPool {
        uint32 id = 0;          // its decal (UINT32_MAX: no floor took it)
        float32 size = 0.01f, max = 2.0f;
        float32 deadFor = 0.0f;
    };
    std::unordered_map<const void*, BloodPool> bloodPools_; // by body (a hunter, or the player)
    std::mt19937 decalRng_{0x44454341u};
    void updateDecals(float32 dt);
    // The rats (Game/rats.h): their logic, and one CJ_RAT (global gmodelspc.dff) per slot, scaled
    // by it, shown while the slot is in use.
    Rats rats_;
    std::array<std::unique_ptr<Character>, Rats::kMax> ratModels_;
    std::mt19937 ratRng_{0x52415453u};
    // The hand-held shake of the scripted and execution cameras (Game/hand_cam.h).
    HandCam handCam_;
    std::mt19937 handCamRng_{0x48434d53u};
    // With HandCamSetActive on: the shake added to this frame's camera.
    void shakeByHand(float32 dt, float32& yaw, float32& pitch, float32& roll);
    void updateRats(float32 dt);
    // A rat or crow dies (0x5cf8c0 / 0x5d1a50): a squeal, FXRAT1 and blood of `bloodType`.
    void critterDies(const Math::Vector3& at, int32 bloodType);
    // The crows (Game/crows.h): one global CJ_CROW per slot and its four wing quads (inner and
    // outer, each side), "cloud" from frontend_pc.txd.
    Crows crows_;
    std::array<std::unique_ptr<Character>, Crows::kMax> crowModels_;
    std::array<std::array<Renderer::Mesh*, 4>, Crows::kMax> crowWings_{}; // owned by the scene
    void updateCrows(float32 dt);
    // The litter (Game/litter.h): a flat quad per sheet, newspaper01..04 from frontend_pc.txd.
    Litter litter_;
    std::array<Renderer::Mesh*, Litter::kSheets> litterSheets_{}; // owned by the scene
    std::mt19937 litterRng_{0x4c495454u};
    void updateLitter(float32 dt);
    float32 deadFor_ = -1.0f;     // seconds since the player died (< 0: alive)
    static constexpr float32 kOutroSeconds = 1.5f;
    float32 outroFor_ = -1.0f;    // seconds since SetLevelCompleted (< 0: still playing)
    bool failed_ = false;
    float32 mouseDX_ = 0.0f, mouseDY_ = 0.0f;
};

} // namespace Game
