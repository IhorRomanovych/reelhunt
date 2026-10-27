// Game/game_session.cpp - one playthrough of a level or the character rig. See game_session.h.
#include "Game/game_session.h"

#include "Game/exe_tables.h"
#include "Core/platform.h"
#include "Game/material_types.h"
#include "Game/music_ambience.h"
#include "Game/sample_levels.h"
#include "Game/weapon_audio.h"
#include "Game/bindings_sdl.h"

#include "Game/checkpoint.h"
#include "Game/ped_anims.h"

#include "Assets/mls.h"
#include "Assets/rib.h"
#include "Assets/sfx_bank.h"
#include "Assets/txd.h"
#include "Audio/audio.h"
#include "Core/asset_manager.h"
#include "Game/level_content.h"
#include "Game/locomotion.h"
#include "Game/level_runtime.h"
#include "Game/scenes.h"
#include "Math/math.h"
#include "Physics/rigidbody.h"
#include "Renderer/camera.h"
#include "Renderer/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

#include <glm/glm.hpp>

namespace Game {

namespace {

// Character-only rig: the model stands here, facing a fixed camera at eye height.
const Math::Vector3 kRigCharPos{0.0f, 0.0f, 0.0f}; // feet; the model is scaled 8 units tall
const Math::Vector3 kRigCameraPos{0.0f, 4.0f, 10.0f};
constexpr float32 kFieldOfView = 50.0f; // vertical, degrees

// Blend between locomotion clips (a placeholder until the original's blend times are measured).
constexpr float32 kLocomotionXfade = 0.2f;


constexpr float32 kDeathRestartSeconds = 4.0f;
constexpr int16 kHunterHeadBone = 1040; // a hunter's head models ride on it (LevelRuntime)
// How far in front of him a hunter on the floor can be stomped or struck (ours: its body lies
// along the floor from its feet).
constexpr float32 kGroundReach = 2.0f;

float32 wrapAngle(float32 a)
{
    while (a > Math::pi)
        a -= 2.0f * Math::pi;
    while (a < -Math::pi)
        a += 2.0f * Math::pi;
    return a;
}

bool isGunOverlay(const std::string& clip)
{
    return clip.find("Aim_T") != std::string::npos || clip.find("_Fire_T") != std::string::npos ||
           clip.find("_Reload_T") != std::string::npos;
}

} // namespace

GameSession::GameSession(const AppConfig& cfg, Renderer::Renderer& renderer, Audio::AudioEngine& audio, AssetManager& assets)
    : cfg_(cfg), renderer_(renderer), audio_(audio), assets_(assets)
{
}

GameSession::~GameSession()
{
    releaseMouse();
    music_.stop(audio_); // its looping stems would outlive the level
    // Threads first, then every GPU resource keyed by this session's meshes and textures, while
    // the scene that owns those meshes still exists.
    cutscene_.stop(renderer_, audio_);
    if (levelLoaded_)
        assets_.unloadLevel(scene_, renderer_.device());
    renderer_.device()->releaseScene(scene_);
    for (uint32 h : character_.textureHandles())
        renderer_.device()->removeTexture(h);
    if (runtime_)
        for (uint32 h : runtime_->textureHandles())
            renderer_.device()->removeTexture(h);
    for (uint32 h : gunFx_.textureHandles())
        renderer_.device()->removeTexture(h);
}

void GameSession::start(const SessionStart& start)
{
    levelMode_ = !start.characterOnly;
    levelDir_ = cfg_.gameRoot + "levels/" + start.level + "/";
    levelName_ = start.level;
    sceneIndex_ = -1;
    if (auto scenes = loadScenes(cfg_.gameRoot + "initscripts/LEVELS/levels.txt", nullptr))
        for (const SceneInfo& sc : *scenes)
            if (Assets::iequals(sc.folder, start.level))
                sceneIndex_ = sc.number - 1;
    lockOnSpeech_ = LockOnSpeech{};
    milestonesStarted_ = false;
    lockOnSpeech_.step = sceneIndex_ == 0 ? 1 : sceneIndex_ == 1 ? 2 : sceneIndex_ == 2 ? 3 : 0;
    lockOnSpeech_.max = sceneIndex_ == 0 ? 20 : sceneIndex_ == 1 ? 10 : sceneIndex_ == 2 ? 5 : 0;
    std::cerr << "session: " << (levelMode_ ? "level " + start.level : std::string("character-only rig"))
              << (start.savePath.empty() ? "" : " from save " + start.savePath) << "\n";

    camera_ = static_cast<Renderer::Camera*>(scene_.addEntity(std::make_unique<Renderer::Camera>()));
    // Levels are rendered in metres and span a few hundred of them.
    camera_->setProjection(static_cast<float32>(cfg_.width) / static_cast<float32>(cfg_.height), viewFov(kFieldOfView), 0.1f,
                           levelMode_ ? 1000.0f : 100.0f);
    camera_->setView(kRigCameraPos, 0.0f, 0.0f);
    camera_->update();

    // A checkpoint to start from: the one given, or a save's (--save / LOAD GAME).
    std::shared_ptr<const SaveFile> checkpoint = start.checkpoint;
    if (!checkpoint && !start.savePath.empty())
    {
        auto f = std::make_shared<SaveFile>();
        if (loadSaveFile(start.savePath, *f) && !f->entities.empty())
            checkpoint = f;
        else
            std::cerr << "session: no checkpoint in " << start.savePath << "\n";
    }
    if (levelMode_)
    {
        assets_.loadLevel(levelDir_, scene_, renderer_.device(), checkpoint ? &checkpoint->entities : nullptr);
        levelLoaded_ = true;
    }

    // Skinned character + key-mapped animation state machine.
    ModelDesc md;
    md.dffPath = cfg_.characterDffPath();
    md.txdPath = cfg_.characterTxd;
    md.position = kRigCharPos;
    if (levelMode_)
        md.scale = 1.0f;      // natural size (metres), like the level
    else
        md.targetSize = 8.0f; // fill the rig camera's view
    if (!character_.load(md, scene_))
        std::cerr << "session: WARNING - character failed to load\n";
    const std::string ifpPath = cfg_.animations.empty() ? levelDir_ + "allanims.ifp" : cfg_.animations;
    if (auto ifp = Assets::loadIfp(ifpPath))
        animations_ = std::make_shared<const Assets::IfpFile>(std::move(*ifp));
    else
        std::cerr << "session: animations failed to load: " << ifp.error().message << "\n";
    character_.loadAnimations(animations_);
    if (!levelMode_)
    {
        // The rig: every clip on a key (config/keymap.txt).
        if (!keymap_.load(cfg_.keymapPath))
            std::cerr << "session: WARNING - keymap failed to load; animation keys disabled\n";
        animState_.attach(character_);
        animState_.setBase("idle");
    }
    else
    {
        // The player: locomotion clips follow the controller, at the speeds their root motion covers.
        PlayerTuning tuning = playerCtrl_.tuning();
        auto speedOf = [&](const char* clip, float32& out) {
            if (const float32 v = character_.rootSpeed(clip); v > 0.0f)
                out = v;
        };
        speedOf(kMoveClip, tuning.walkSpeed);
        speedOf(kSprintClip, tuning.runSpeed);
        speedOf(kSneakMoveClip, tuning.crouchSpeed);
        speedOf(kSneakRunClip, tuning.crouchRunSpeed);
        tuning.jumpSpeed = kJumpSpeed; // only the run-off jump uses it (the game has no jump key)
        playerCtrl_.setTuning(tuning);
        std::cerr << "session: speeds move " << tuning.walkSpeed << " run " << tuning.runSpeed << " sneak "
                  << tuning.crouchSpeed << " sneak-run " << tuning.crouchRunSpeed << " m/s\n";
        character_.play(kStandClip);

        playerField_.load(levelDir_, *assets_.level(), physics_, scene_);
        {
            // The camera's pivot is over his feet (0x58b820); the controller keeps his body's centre.
            PlayerTuning t = playerCtrl_.tuning();
            t.feetBelow = playerField_.bodyHeight();
            playerCtrl_.setTuning(t);
        }
        const Math::Vector3 spawn = playerField_.body() ? playerField_.body()->position() : Math::Vector3{0.0f, 12.0f, 0.0f};
        playerCtrl_.reset(spawn, playerField_.spawnYaw(), 0.0f);
        if (cfg_.playerAt)
        {
            // Debug start: drop onto the floor at (x, z), searching down from a little above the
            // level's start height (so a roof above the spot is not taken for the floor).
            const float32 fromY = std::isnan((*cfg_.playerAt)[3]) ? spawn.y + 6.0f : (*cfg_.playerAt)[3] + 1.0f;
            Math::Vector3 at{(*cfg_.playerAt)[0], fromY, (*cfg_.playerAt)[1]};
            Math::Vector3 velocity{0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 400 && !playerField_.resolveCharacter(at, velocity); ++i)
                at.y -= 0.25f;
            playerCtrl_.reset(at, Math::radians((*cfg_.playerAt)[2]), 0.0f);
        }
        // The controller tracks the capsule centre; the model is centred on its bind pose.

        // Gameplay: the level's scripts set up its hunters (OnCreate), triggers and doors.
        runtime_ = std::make_unique<LevelRuntime>();
        LevelRuntime::Setup rs;
        rs.levelName = start.level;
        rs.levelDir = levelDir_;
        rs.constantsPath = cfg_.scriptConstantsPath;
        rs.hunterSpeechPath = cfg_.hunterSpeechPath;
        if (PedAnims::shared().empty() && !PedAnims::shared().load(cfg_.pedAnimsPath))
            std::cerr << "session: cannot read " << cfg_.pedAnimsPath << "\n";
        rs.pakPath = cfg_.gameRoot + gameExe().tuningPak;
        rs.gameRoot = cfg_.gameRoot;
        rs.globalTextSuffix = cfg_.settings.globalTextSuffix();
        rs.textSuffix = cfg_.settings.levelTextSuffix();
        if (checkpoint)
            for (const Assets::InstPlacement& p : assets_.level()->placements())
                if (p.entityClass == "Player_Inst" && !p.params.empty() && p.params[0] > 0)
                    rs.playerHealth = static_cast<float32>(p.params[0]); // the checkpoint's health
        rs.hardDifficulty = cfg_.settings.difficulty == 1;
        if (!hud_.load(cfg_.gameRoot))
            std::cerr << "session: HUD art missing (pictures/frontend_pc.txd)\n";
        else if (!radar_.load(hud_.textures()))
            std::cerr << "session: radar art missing (pictures/frontend_pc.txd)\n";
        if (assets_.level())
            decals_.attach(scene_, renderer_.device(), *assets_.level(), hud_); // (its blood and bullet holes)
        if (!weaponStrip_.load(hud_.textures(), cfg_.gameRoot))
            std::cerr << "session: weapon strip art missing (pictures/frontend_pc.txd)\n";
        if (auto fonts = Assets::loadFonts(cfg_.gameRoot + "initscripts/FONTS/font.dat"))
            fonts_ = std::move(*fonts);
        for (const Assets::Font& f : fonts_)
            if (f.id == 1)
                textFont_ = &f;
        if (!director_.load(cfg_.directorSpeechPath))
            std::cerr << "session: no director speech table (" << cfg_.directorSpeechPath << ")\n";
        rs.level = assets_.level();
        rs.field = &playerField_;
        rs.scene = &scene_;
        rs.animations = animations_;
        runtime_->load(rs);
        stamina_.setTuning(runtime_->tuning().stamina);
        stamina_.reset();
        playerAudio_.reset();
        acoustics_.reset();
        if (!cfg_.noMusic && !music_.load(cfg_.gameRoot, start.level))
            std::cerr << "session: no music for " << start.level << "\n";
        playerField_.setDoors(&runtime_->doors());
        if (checkpoint)
            runtime_->restoreInventory(checkpoint->inventory()); // buffer 3 (0x5d2650)
        else if (start.carried)
            runtime_->restoreInventory(*start.carried); // the scene before's (0x5d2590 -> 0x5d2650)
        for (const std::string& item : cfg_.give)
            if (!runtime_->give(item))
                std::cerr << "session: --give " << item << ": no such item\n";
        const LevelSetup& setup = runtime_->levelSetup();
        assets_.level()->setShadowCasters(setup.shadowCasters);
        Renderer::SceneLighting& lighting = scene_.lighting();
        lighting.skyColor = setup.skyColor;
        lighting.fog = setup.fog;
        lighting.fogStart = setup.fogStart;
        lighting.fogEnd = setup.fogEnd;
        // REELHUNT_FOG=start,end overrides the level's fog (renderer debugging).
        if (const char* v = std::getenv("REELHUNT_FOG"))
            if (float32 s = 0.0f, e = 0.0f; std::sscanf(v, "%f,%f", &s, &e) == 2 && e > s)
            {
                lighting.fog = true;
                lighting.fogStart = s;
                lighting.fogEnd = e;
            }
        runtime_->setDebugLog(cfg_.aiLog);
        if (!runtime_->models() && !weaponModels_)
            weaponModels_ = DffLibrary::load(levelDir_ + "pak/modelspc.dff");
        gunFx_.load(scene_, cfg_.gameRoot, runtime_->models() ? runtime_->models() : weaponModels_.get(),
                    &assets_.level()->textures());
        if (checkpoint)
        {
            // The checkpoint's level before any script runs, as the exe loads one (0x4d8560):
            // its entities (0x474330 -> 0x437650(0)) and score and goals (buffer 6, 0x608540),
            // then the saved script memory (buffers 4 / 5 -> 0x71a460 / 0x71a860) written into
            // the programs as they load (0x480b10(level, 1) -> 0x481340(1)); only then does the
            // level's OnCreate run (handler 0, 0x481de0), with the saved VARs in place.
            runtime_->restoreSpawned(checkpoint->entities);
            runtime_->restoreDoors(checkpoint->entities);
            restoreScoreBlock(checkpoint->buffer6);
            runtime_->restoreScriptData(checkpoint->buffer5);
            checkpoint_ = checkpoint;
            std::cerr << "session: started from a checkpoint (" << checkpoint->entities.size() << " records)\n";
        }
        runtime_->start();
        buildCoronas();
        for (const std::string& run : cfg_.runScripts)
        {
            const std::size_t colon = run.find(':');
            if (!runtime_->scripts().start(run.substr(0, colon), run.substr(colon + 1)))
                std::cerr << "session: --run-script " << run << ": no such handler\n";
        }
        levelAmbient_ = scene_.lighting().ambient;
        setLighting(cfg_.lighting);
        if (cfg_.playCutscene && !checkpoint)
            if (cutscene_.open(levelDir_ + start.level + ".bik", renderer_) && cfg_.settings.subtitles)
                loadFmvSubtitles(levelDir_ + start.level + ".bik");
    }

    if (cfg_.debugCamera)
    {
        const auto& c = *cfg_.debugCamera;
        flyCam_.setPose({c[0], c[1], c[2]}, Math::radians(c[3]), Math::radians(c[4]));
        useFlyCam_ = true;
    }

    // Warm up the GPU: the first full render + present compiles pipelines and uploads the level.
    // Doing it here keeps the cutscene's first frame (when its audio starts) fast.
    renderer_.beginFrame();
    renderer_.renderScene(scene_);
    renderer_.present();
}

bool GameSession::handleEvent(const SDL_Event& evt)
{
    const bool keyDown = evt.type == SDL_EVENT_KEY_DOWN && !evt.key.repeat;
    const Bindings b = binds();
    if (keyDown && (evt.key.key == SDLK_SPACE || evt.key.key == SDLK_ESCAPE) && cutscene_.isActive())
        cutscene_.stop(renderer_, audio_); // skip the intro (ESC too: ours)
    else if (slotPressed(b, Bindings::Use, evt) && levelMode_ && runtime_ && !runtime_->inCutscene())
        usePressed_ = true; // USE (Space / Enter by default)
    else if (keyDown && evt.key.key == SDLK_SPACE && runtime_ && runtime_->inCutscene())
    {
        runtime_->skipCutscene(); // skip a scripted cut-scene
        directorQueue_.clear();
    }
    else if (keyDown && evt.key.key == SDLK_V)
    {
        markerVisible_ = !markerVisible_;
        playerField_.setMarkerVisible(markerVisible_);
    }
    else if (keyDown && evt.key.key == SDLK_F5 && runtime_)
    {
        // Debug: the level now as a checkpoint save, written to --save-out.
        const std::shared_ptr<SaveFile> f = snapshot();
        f->setTitle("F5)SCENE " + std::to_string(sceneIndex_ + 1) + "-(IN PROGRESS)");
        f->setHeaderWord(0x90, static_cast<std::uint32_t>(sceneIndex_));
        f->setHeaderWord(0x94, static_cast<std::uint32_t>(sceneIndex_));
        std::cerr << "session: F5 " << (writeSaveFile(cfg_.saveOutPath, *f) ? "wrote " : "could not write ") << cfg_.saveOutPath << "\n";
    }
    else if (keyDown && evt.key.key == SDLK_F12 && levelMode_ && runtime_)
    {
        runtime_->completeLevel(); // debug: win the scene (the results come up after the fade)
    }
    else if (keyDown && evt.key.key == SDLK_F11 && levelMode_ && runtime_)
    {
        runtime_->hurtPlayer(1.0e6f, playerFeet()); // debug: die (Scene Failed 4 s on)
    }
    else if (keyDown && evt.key.key == SDLK_F8 && levelMode_)
    {
        // F8 cycles original -> mixed -> dynamic -> midday -> original.
        using M = Renderer::LightingMode;
        switch (lightingMode_)
        {
        case M::Baked: setLighting(M::Mixed); break;
        case M::Mixed: setLighting(M::Dynamic); break;
        case M::Dynamic: setLighting(M::Midday); break;
        case M::Midday: setLighting(M::Baked); break;
        }
    }
    else if (levelMode_ && !useFlyCam_ && slotPressed(b, Bindings::Action1, evt) &&
             (mouseCaptured_ || evt.type != SDL_EVENT_MOUSE_BUTTON_DOWN))
    {
        attackPressed_ = true;
        sinceAttack_ = 0.0f;
        if (killing() || grapple_.phase != Grapple::Phase::None || down_.phase != Down::Phase::None ||
            carry_.phase != Carry::Phase::None)
            return true; // (carrying: the press drops the body, updateCarry)
        const bool gun = runtime_ && runtime_->playerGun();
        if (gun)
            gun_.triggerHeld = true;
        if (runtime_ && runtime_->playerThrowWeapon())
        {
            throw_.down = throw_.pressed = true; // charge a throw (updateThrow)
            return true;
        }
        if (execTarget_)
        {
            charge_.setStageTimes(runtime_->tuning().executeStageTimes[0], runtime_->tuning().executeStageTimes[1]);
            charge_.start(); // behind an unaware hunter with an execution weapon: hold to execute
        }
        else if (gun)
            gun_.pulled = true; // a gun in his hand fires
        else if (runtime_ && runtime_->downTarget(playerFeet(), playerCtrl_.yaw(), kGroundReach))
            groundPressed_ = true; // a hunter on the floor in front: strike down at it
        else
            combo_.press(); // punch: tap = quick (combos), hold = hard
    }
    else if (levelMode_ && slotReleased(b, Bindings::Action1, evt))
    {
        gun_.triggerHeld = false;
        throw_.down = false;
        if (charge_.charging())
        {
            const ItemType* item = runtime_ ? runtime_->equipped() : nullptr;
            const int32 level = item ? charge_.level(*item) : 0;
            charge_.cancel();
            if (level > 0 && execTarget_)
                startKill(level);
        }
        else
            combo_.release();
    }
    else if (levelMode_ && !useFlyCam_ && slotPressed(b, Bindings::Action2, evt))
        gun_.aimHeld = true; // ACTION2: aim (a gun in his hand: lock on)
    else if (levelMode_ && slotReleased(b, Bindings::Action2, evt))
        gun_.aimHeld = false;
    else if (levelMode_ && slotPressed(b, Bindings::Reload, evt))
        gun_.reloadPressed = true; // RELOAD the gun
    else if (levelMode_ && runtime_ && !killing() &&
             (slotPressed(b, Bindings::Inv1, evt) || slotPressed(b, Bindings::Inv2, evt) ||
              slotPressed(b, Bindings::Inv3, evt) || slotPressed(b, Bindings::Inv4, evt)))
        runtime_->selectSlot(slotPressed(b, Bindings::Inv1, evt)   ? 1
                             : slotPressed(b, Bindings::Inv2, evt) ? 2
                             : slotPressed(b, Bindings::Inv3, evt) ? 3
                                                                   : 4); // INVENTORY ITEM 1..4
    else if (levelMode_ && keyDown && evt.key.key == SDLK_K)
        kickPressed_ = true; // kick (a stunned hunter in front: stomp; ours: Q / E peek, as the exe's defaults)
    else if (levelMode_ && keyDown && evt.key.key == SDLK_G)
        grabPressed_ = true; // grab the hunter in front
    else if (levelMode_ && keyDown && grapple_.phase != Grapple::Phase::None &&
             (evt.key.key == SDLK_W || evt.key.key == SDLK_A || evt.key.key == SDLK_S || evt.key.key == SDLK_D ||
              evt.key.key == SDLK_UP || evt.key.key == SDLK_DOWN || evt.key.key == SDLK_LEFT || evt.key.key == SDLK_RIGHT))
        ++wiggles_; // wiggling free of a grapple
    else if (levelMode_ && runtime_ && gun_.aimHeld && runtime_->playerGun() &&
             ((keyDown && evt.key.key == SDLK_Z) || (evt.type == SDL_EVENT_MOUSE_WHEEL && evt.wheel.y != 0.0f)))
        gun_.zoomSteps += evt.type == SDL_EVENT_MOUSE_WHEEL ? (evt.wheel.y > 0.0f ? 1 : -1) : 1; // zoom in / out
    else if (levelMode_ && runtime_ && !killing() && (evt.type != SDL_EVENT_MOUSE_WHEEL || mouseCaptured_) &&
             (slotPressed(b, Bindings::InvUp, evt) || slotPressed(b, Bindings::InvDown, evt)))
        runtime_->cycleWeapon(slotPressed(b, Bindings::InvDown, evt)); // INVENTORY UP / DOWN (by holster slot: 3, 2, 1, 4)
    else if (keyDown && evt.key.key == SDLK_F9)
    {
        useFlyCam_ = !useFlyCam_;
        if (useFlyCam_ && camera_)
            flyCam_.setPose(camera_->position(), camera_->yaw(), camera_->pitch()); // from the view he had
        std::cerr << "session: camera " << (useFlyCam_ ? "free-fly (debug)" : "default") << "\n";
    }
    else if (useFlyCam_)
        flyCam_.handleEvent(evt); // WASD + hold-LMB look
    else if (keyDown && !levelMode_)
    {
        // Rig animation keys (config/keymap.txt). ESC/V/F5/F9 are reserved.
        const std::string act = keymap_.actionForKey(static_cast<int>(evt.key.scancode));
        if (act.empty())
            return false;
        animState_.trigger(act);
    }
    else if (evt.type == SDL_EVENT_MOUSE_MOTION && (mouseCaptured_ || (evt.motion.state & SDL_BUTTON_LMASK)))
    {
        mouseDX_ += evt.motion.xrel; // mouse-look (the level captures the mouse; the rig drags with LMB)
        mouseDY_ += evt.motion.yrel;
    }
    else
        return false;
    return true;
}

void GameSession::pause()
{
    if (mouseCaptured_)
    {
        mouseCaptured_ = false;
        SDL_SetWindowRelativeMouseMode(renderer_.device()->window(), false);
    }
    mouseDX_ = mouseDY_ = 0.0f;
}

std::shared_ptr<SaveFile> GameSession::snapshot() const
{
    // 0x4d2400: the entity block (0x43a2c0), the inventory (0x5d24c0), the script data (0x4812d0)
    // and the score (0x608480); the global script's data as the game ships it (buffer 4). Buffer
    // 7 (0x710bf4) stays empty.
    auto f = std::make_shared<SaveFile>();
    f->header.assign(SaveLayout::buffer1, 0);
    f->buffer3.assign(SaveLayout::buffer3, 0);
    f->buffer4.assign(SaveLayout::buffer4, 0);
    f->buffer5.assign(SaveLayout::buffer5, 0);
    f->buffer6.assign(SaveLayout::buffer6, 0);
    f->buffer7.assign(SaveLayout::buffer7, 0);
    if (!runtime_)
        return f;
    f->entities = runtime_->checkpointRecords(playerFeet(), playerCtrl_.yaw(), runtime_->playerHealth());
    f->setInventory(runtime_->inventorySlots());
    f->buffer5 = runtime_->scriptData();
    f->buffer6 = scoreBlock();
    f->buffer4 = Checkpoint::globalScriptData(cfg_.gameRoot + "levels/GLOBAL/game.scc");
    f->directorCounts = milestones_.counts(); // 0x5b5e70
    return f;
}

std::vector<std::uint8_t> GameSession::scoreBlock() const
{
    // 0x608480: +0 the hunters (0x7c7e80), +4 those that cannot be executed (0x7c7e84), +8 kills,
    // +0xc executions, +0x10 the time in ms (0x7108f0), +0x1c style points, +0x28 the goals,
    // 16 bytes each (0x7c8660).
    std::vector<std::uint8_t> b(SaveLayout::buffer6, 0);
    auto put = [&](std::size_t at, std::int32_t v) { std::memcpy(b.data() + at, &v, 4); };
    LevelScore s = score_;
    if (runtime_)
        s.setHunters(runtime_->killableHunters().first, runtime_->killableHunters().second);
    put(0x00, s.hunters);
    put(0x04, s.hunters - s.executable);
    put(0x08, s.kills);
    put(0x0c, s.executions);
    put(0x10, s.timeMs);
    put(0x1c, s.stylePoints);
    if (runtime_)
    {
        std::size_t at = 0x28;
        for (const std::string& key : runtime_->gameText().goals())
        {
            if (at + 16 > 0x28 + 0x70)
                break;
            std::memcpy(b.data() + at, key.data(), std::min<std::size_t>(key.size(), 15));
            at += 16;
        }
    }
    return b;
}

void GameSession::restoreScoreBlock(const std::vector<std::uint8_t>& b)
{
    // 0x608540.
    if (b.size() < SaveLayout::buffer6)
        return;
    auto get = [&](std::size_t at) {
        std::int32_t v = 0;
        std::memcpy(&v, b.data() + at, 4);
        return v;
    };
    score_.kills = get(0x08);
    score_.executions = get(0x0c);
    score_.timeMs = get(0x10);
    score_.stylePoints = get(0x1c);
    if (runtime_)
        for (std::size_t at = 0x28; at + 16 <= 0x28 + 0x70; at += 16)
            if (b[at] != 0) // a cleared goal keeps its key with the first byte zeroed
                runtime_->gameText().setGoal(std::string(reinterpret_cast<const char*>(b.data() + at), strnlen(reinterpret_cast<const char*>(b.data() + at), 16)));
}

std::array<SaveInventorySlot, 4> GameSession::inventorySlots() const
{
    return runtime_ ? runtime_->inventorySlots() : std::array<SaveInventorySlot, 4>{};
}

LevelResults GameSession::results() const
{
    LevelScore s = score_;
    if (runtime_)
        s.setHunters(runtime_->killableHunters().first, runtime_->killableHunters().second);
    return levelResults(s, sceneIndex_, cfg_.settings.difficulty == 1);
}

std::vector<std::string> GameSession::levelGoals() const
{
    std::vector<std::string> out;
    if (runtime_)
        for (const std::string& key : runtime_->gameText().goals())
            out.push_back(runtime_->gameText().text(key));
    return out;
}

void GameSession::update(float32 dt)
{
    // SHADOW DETAIL (0x5c3510): the dynamic shadows from 2 up (ours: the sun's, the moon's and
    // the lamps'; the exe's own are its projected character shadows, 256 or 512 texels).
    scene_.lighting().shadows = cfg_.settings.shadowDetail >= 2;
    // Player input, decoupled from SDL for the controller. The PC actions are FORWARD / BACK /
    // STRAFE LEFT / STRAFE RIGHT, RUN and SNEAK; the game has no jump.
    PlayerInput in{};
    // A scripted cut-scene (CutSceneStart) takes the player's control away as the intro video does.
    const bool scripted = runtime_ && (runtime_->inCutscene() || outroFor_ >= 0.0f);
    const bool playing = levelMode_ && !useFlyCam_ && !cutscene_.isActive() && !scripted;
    if (cfg_.movementEnabled && playing)
    {
        // By the bindings (Game/bindings.h; defaults: W S A D and the arrows).
        const Bindings b = binds();
        in.moveZ = (slotHeld(b, Bindings::Forward) ? 1.0f : 0.0f) - (slotHeld(b, Bindings::Back) ? 1.0f : 0.0f);
        in.moveX = (slotHeld(b, Bindings::Right) ? 1.0f : 0.0f) - (slotHeld(b, Bindings::Left) ? 1.0f : 0.0f);
        // The pad's left stick, as an analog move (0x491db0 case 0 -> ped +0x498; ours: its tilt
        // sets the pace).
        if (const PadStick stick = padStick(0); stick.x != 0.0f || stick.y != 0.0f)
        {
            in.moveX = stick.x;
            in.moveZ = -stick.y;
        }
        // SNEAK is held (the PC text: "Hold the Sneak button to move quietly"): left Ctrl / keypad
        // Ins by default; SPRINT (our RUN): Shift / right Ctrl.
        in.crouch = slotHeld(b, Bindings::Sneak);
        in.run = slotHeld(b, Bindings::Sprint);
        if (!stamina_.canSprint())
            in.run = false; // out of breath (Game/stamina.h)
    }
    // The exe's default keys (0x710994, the REMAP page 0x60b1..): LOOK BACK F / keypad 4 turns
    // the view behind him; FIRST PERSON X / End looks from his eyes and holds him still (ours:
    // held, as the peeks are); INVENTORY SWAP Tab / the middle button, held, swaps an item in.
    const Bindings held = binds();
    lookBack_ = playing && slotHeld(held, Bindings::LookBack);
    firstPerson_ = playing && !wall_.on && carry_.phase == Carry::Phase::None && slotHeld(held, Bindings::FirstPerson);
    if (runtime_)
        runtime_->setSwapHeld(playing && slotHeld(held, Bindings::InvSwap));
    if (firstPerson_)
    {
        in.moveX = in.moveZ = 0.0f;
        in.run = false;
    }
    // Mouse-look captures the mouse while the player is in control.
    if ((playing && cfg_.mouseLook) != mouseCaptured_)
    {
        mouseCaptured_ = playing && cfg_.mouseLook;
        SDL_SetWindowRelativeMouseMode(renderer_.device()->window(), mouseCaptured_);
        ignoreMouseFrames_ = 2;
    }
    if (ignoreMouseFrames_ > 0)
    {
        --ignoreMouseFrames_;
        mouseDX_ = mouseDY_ = 0.0f;
    }
    // MOUSE SENSITIVITY X / Y and INVERT Y (Controls page); the default (10) is the old feel.
    const float32 mouseNorm = GameSettings::mouseScale(10);
    // The pad's right stick turns the view (0x491db0 case 1 -> ped +0x4a0; ours: as 900 pixels of
    // mouse a second at full tilt).
    if (playing)
        if (const PadStick look = padStick(1); look.x != 0.0f || look.y != 0.0f)
        {
            mouseDX_ += look.x * 900.0f * dt;
            mouseDY_ += look.y * 600.0f * dt;
        }
    in.mouseDX = mouseDX_ * GameSettings::mouseScale(cfg_.settings.mouseX) / mouseNorm;
    in.mouseDY = mouseDY_ * GameSettings::mouseScale(cfg_.settings.mouseY) / mouseNorm * (cfg_.settings.invertY ? -1.0f : 1.0f);
    if (runtime_ && (!runtime_->playerControllable() || runtime_->movementLocked()))
    {
        // SetPlayerControllable(FALSE) / LockPlayerMovement: the scripts hold him where he is (the
        // view still turns); DisableUserInput: the view too.
        in.moveX = in.moveZ = 0.0f;
        in.run = in.crouch = false;
        if (runtime_->userInputDisabled())
            in.mouseDX = in.mouseDY = 0.0f;
    }
    if (runtime_)
        if (const auto go = runtime_->playerGoto())
        {
            // SetPlayerGotoNode: he walks to the point by himself (as if pushed that way).
            const Math::Vector3 feet = playerFeet();
            const Math::Vector3 to{go->x - feet.x, 0.0f, go->z - feet.z};
            const float32 len = to.length();
            // There within 1 m, or once he stops closing on it for 2 s (blocked; ours: the exe's
            // arrival test is not traced).
            if (len < gotoBest_ - 0.05f || (gotoAt_ - *go).length() > 0.01f)
            {
                gotoBest_ = len;
                gotoStuck_ = 0.0f;
                gotoAt_ = *go;
            }
            if (len < 1.0f || (gotoStuck_ += dt) > 2.0f)
            {
                runtime_->playerArrived();
                gotoBest_ = 1e9f;
            }
            else
            {
                Math::Vector3 fwd, rgt;
                PlayerController::groundBasis(playerCtrl_.yaw(), fwd, rgt);
                in.moveZ = to.dot(fwd) / len;
                in.moveX = to.dot(rgt) / len;
            }
        }
    if (runtime_ && runtime_->takeClearInput())
        usePressed_ = attackPressed_ = false;
    // PlayerPlayFullBodyAnim(clip): he plays it in place (Idle_Player_Hold_Up_Head ...).
    if (runtime_ && levelMode_)
        if (const std::string clip = runtime_->takePlayerAnim(); !clip.empty() && character_.clipDuration(clip) > 0.0f)
            playerAction(clip, 1.0f, true);
    mouseDX_ = mouseDY_ = 0.0f;

    if (!levelMode_)
    {
        character_.setPlacement(kRigCharPos, Math::pi); // face the rig camera
    }
    else
    {
        updateExecution(dt);
        recoil_.update(dt);
        gun_.fired = false;
        // REELHUNT_AIM=<from>:<to> (testing): aim held between those seconds; REELHUNT_SHOOT=<s>[,<s>...]:
        // the fire button pressed at those seconds.
        if (static const char* aim = std::getenv("REELHUNT_AIM"), *shoot = std::getenv("REELHUNT_SHOOT"); aim || shoot)
        {
            static float32 clock = 0.0f;
            const float32 before = clock;
            clock += dt;
            if (aim)
            {
                const std::string a(aim);
                const std::size_t colon = a.find(':');
                const float32 from = std::stof(a.substr(0, colon)), to = colon == std::string::npos ? 1e9f : std::stof(a.substr(colon + 1));
                const bool held = clock >= from && clock < to;
                if (held != gun_.aimHeld)
                    std::cerr << "session: test: aim " << (held ? "held" : "let go") << "\n";
                gun_.aimHeld = held;
            }
            for (std::string s = shoot ? shoot : ""; !s.empty();)
            {
                const std::size_t comma = s.find(',');
                const float32 at = std::stof(s.substr(0, comma));
                if (before < at && clock >= at)
                {
                    attackPressed_ = true;
                    std::cerr << "session: test: fire\n";
                }
                s = comma == std::string::npos ? std::string() : s.substr(comma + 1);
            }
        }
        // REELHUNT_VPAD=<from>:<to> (testing): the virtual pad's left stick held forward between
        // those seconds.
        if (static const char* vpad = std::getenv("REELHUNT_VPAD"); vpad)
        {
            static float32 clock = 0.0f;
            clock += dt;
            const std::string v(vpad);
            const std::size_t c = v.find(':');
            const bool on = c != std::string::npos && clock >= std::stof(v.substr(0, c)) && clock < std::stof(v.substr(c + 1));
            setTestPadStick(0.0f, on ? -1.0f : 0.0f);
        }
        // REELHUNT_WALK=<from>:<to>[:run] (testing): forward held between those seconds.
        if (static const char* walk = std::getenv("REELHUNT_WALK"); walk)
        {
            static float32 clock = 0.0f;
            clock += dt;
            const std::string w(walk);
            const std::size_t c1 = w.find(':'), c2 = w.find(':', c1 + 1);
            const float32 from = std::stof(w.substr(0, c1)), to = std::stof(w.substr(c1 + 1, c2 - c1 - 1));
            if (clock >= from && clock < to)
            {
                in.moveZ = 1.0f;
                in.run = c2 != std::string::npos;
            }
        }
        // REELHUNT_MOVE=<s>:<x>,<z>[,<yaw>][;<s>:...] (testing): at <s> seconds he is put there
        // (scene frame, on the floor below his height + 1 m), facing <yaw> degrees.
        if (static const char* move = std::getenv("REELHUNT_MOVE"); move)
        {
            static float32 clock = 0.0f;
            static const char* next = move;
            clock += dt;
            if (next && *next && clock >= std::stof(next))
            {
                float32 x = 0.0f, z = 0.0f, yawDeg = 0.0f;
                const char* at = std::strchr(next, ':');
                if (at && std::sscanf(at + 1, "%f,%f,%f", &x, &z, &yawDeg) >= 2)
                {
                    Math::Vector3 to{x, playerCtrl_.position().y + 1.0f, z};
                    playerCtrl_.reset(to, Math::radians(yawDeg), playerCtrl_.pitch());
                    std::cerr << "session: test: moved to (" << x << ", " << z << ")\n";
                }
                next = std::strchr(next, ';');
                if (next)
                    ++next;
            }
        }
        if (killing())
        {
            in = PlayerInput{}; // the kill plays out
            lowerGun();
        }
        else if (updateCarry(dt, in))
            lowerGun(); // a body on his shoulder: nothing else
        else if (updateGasCarry(dt, in))
            lowerGun(); // the gas can in his hand: nothing else
        else if (updateClimb(dt, in))
            lowerGun(); // on a ladder: it moves him
        else if (updateWall(dt, in))
            lowerGun(); // his back to a wall: it moves him
        else if (updateCloseCombat(dt))
        {
            in = PlayerInput{}; // a kick or a grapple holds him
            lowerGun();
        }
        else
        {
            updateCombat(dt, in);
            updatePeek(in);
            updateJump(dt, in);
            if (cfg_.aiLog)
                if (const int32 ctx = useContext(); ctx != lastContext_)
                {
                    lastContext_ = ctx;
                    std::cerr << "session: use context " << ctx << std::endl;
                }
        }
        // The free-fly debug camera leaves the player where he is (still placed and animated).
        if (!useFlyCam_)
        {
            // On a ladder the clips carry him: no gravity or collision (the view still turns).
            const Math::Vector3 held = playerCtrl_.position();
            playerCtrl_.update(dt, in, onGround_ || climbing());
            if (climbing())
                playerCtrl_.setPosition(held);
            else if (Physics::RigidBody* body = playerField_.body())
            {
                body->setPosition(playerCtrl_.position()); // drive the capsule kinematically
                onGround_ = playerField_.resolveBody();    // wall-block + ground-snap
                playerCtrl_.setPosition(body->position()); // feed the clamped position back
            }
        }
        if (dt > 0.0f)
        {
            yawRate_ = std::remainder(playerCtrl_.yaw() - lastYaw_, 2.0f * Math::pi) / dt;
            pitchRate_ = (playerCtrl_.pitch() - lastPitch_) / dt;
            // Mouse steps come unevenly: a fast turn holds the turn on for a moment (ours).
            const float32 limit = Math::radians(20.0f); // 0x72872c
            turnHold_ = std::fabs(yawRate_) > limit && std::fabs(pitchRate_) < limit ? 0.25f : std::max(0.0f, turnHold_ - dt);
        }
        lastYaw_ = playerCtrl_.yaw();
        lastPitch_ = playerCtrl_.pitch();
        if (climbing())
            character_.setPlacement(climb_.origin, bodyYaw());
        else
            character_.setPlacement(playerFeet() - wall_.normal * (wall_.on ? wall_.snug : 0.0f), bodyYaw());
        if (actionLeft_ <= 0.0f && !combo_.busy() && deadFor_ < 0.0f && down_.phase == Down::Phase::None && !wall_.on &&
            peek_ == 0 && !climbing() && jump_.phase != Jump::Phase::Air)
        {
            animateLocomotion(in);
            updateFootsteps(dt);
        }
        else
            stepPhase_ = -1.0f;
        if (runtime_ && !cutscene_.isActive())
        {
            // What the hunters perceive of the player this frame.
            PlayerSnapshot snap;
            snap.feet = playerFeet();
            snap.yaw = bodyYaw();
            const auto light = playerField_.lightBelow(snap.feet + Math::Vector3{0.0f, 0.5f, 0.0f});
            snap.light = light ? (light->x + light->y + light->z) / 3.0f : 0.1f;
            // Footsteps by speed: WALK_% / RUN_% of his full (sprint) speed (aiTypeData hearing_data).
            const Math::Vector3 v = playerCtrl_.velocity();
            const float32 fullSpeed = std::max(1.0f, playerCtrl_.tuning().runSpeed);
            snap.sneaking = playerCtrl_.isCrouching();
            snap.carryingBody = carry_.phase != Carry::Phase::None;
            snap.wallSquashed = wall_.on;
            const float32 ground = std::sqrt(v.x * v.x + v.z * v.z);
            snap.gait = ground < 0.2f ? 0 : in.run ? 3 : ground < 0.5f * playerCtrl_.tuning().walkSpeed ? 1 : 2;
            snap.sound = onGround_ ? footstepSound(std::sqrt(v.x * v.x + v.z * v.z) / fullSpeed, snap.sneaking,
                                                   runtime_->tuning().hearing)
                                   : nullptr;
            snap.swiped = swiped_;
            snap.blocking = guard_ && down_.phase == Down::Phase::None;
            snap.down = down_.phase != Down::Phase::None;
            snap.blockQuick = playerMelee().blockDamage[0];
            snap.blockHard = playerMelee().blockDamage[1];
            // His swing, for the hunters' melee traits (Game/ai/melee_traits.h).
            snap.swinging = combo_.busy();
            snap.swingHard = combo_.hard();
            snap.swingTime = combo_.swingTime();
            snap.swingLands = combo_.hard() ? playerMelee().hard.strikeAt : playerMelee().quick.strikeAt;
            snap.hardLands = playerMelee().hard.strikeAt;
            snap.reach = playerMelee().quick.size;
            snap.grabbing = grapple_.phase == Grapple::Phase::Grab;
            snap.noise = killing() ? executionNoise(*kill_.item, kill_.plan.level) : (gun_.fired ? "GUN_SHOT" : nullptr);
            if (gun_.fired)
                lastShotAt_ = time_;
            snap.aimYaw = playerCtrl_.yaw();
            snap.gun = runtime_->playerGun() != nullptr;
            snap.lockOn = gun_.lock ? gun_.lock->name() : std::string();
            snap.shotAgo = time_ - lastShotAt_;
            snap.executing = killing();
            {
                // The pad as the scripts read it (ours: TRIANGLE is the USE slot, SQUARE the attack
                // button, ACTION1 - their keys, or the pad's buttons bound to them).
                const Bindings b = binds();
                snap.pad = (slotHeld(b, Bindings::Use) ? 1u : 0u) | (slotHeld(b, Bindings::Action1) ? 8u : 0u);
            }
            snap.actionBusy = actionLeft_ > 0.0f;
            if (camera_)
            {
                // The camera's world axes: the rows of its view rotation.
                const Math::Matrix4x4 v = camera_->view();
                runtime_->setViewer(camera_->position(), Math::Vector3{v.m00, v.m01, v.m02}, Math::Vector3{v.m10, v.m11, v.m12});
            }
            runtime_->update(dt, snap);
            updateCoronas();
            {
                // Scrap2's crane: the left / right keys turn it (his movement is locked meanwhile).
                const bool turning = runtime_->padTurning();
                // The Action button (the left mouse button) works the magnet; the mouse turns it too.
                runtime_->updateCrane(dt, turning && slotHeld(binds(), Bindings::Left), turning && slotHeld(binds(), Bindings::Right),
                                      turning && slotHeld(binds(), Bindings::Action1),
                                      turning ? mouseDX_ : 0.0f);
            }
            for (Hunter* h : runtime_->takeHeadBursts())
                explodeHead(*h, false);
            // Stamina: sprinting uses it, the rest of the time it comes back by his pace.
            const float32 speed = std::sqrt(v.x * v.x + v.z * v.z);
            const Stamina::Pace pace = speed < 0.2f ? Stamina::Pace::Still
                                       : (snap.sneaking ? Stamina::Pace::Moving : Stamina::Pace::Running);
            stamina_.update(dt, in.run && speed > 0.2f && !snap.sneaking && !killing(), pace);
            // The radar: his noise ring, the scripts' sounds.
            playerNoise_ = std::max(runtime_->soundRange(snap.sound), runtime_->soundRange(snap.noise));
            for (const auto& [at, range] : runtime_->takeRadarNoise())
                radar_.addNoise(at, range);
            radar_.update(dt, snap.feet, playerNoise_);
            if (weaponStripDrawn_)
                weaponStrip_.update(dt);
            if (auto push = runtime_->takePush())
                playerCtrl_.setPosition(playerCtrl_.position() + *push); // (the body resolve keeps him out of walls)
            if (auto to = runtime_->takeTeleport())
                playerCtrl_.reset(*to + Math::Vector3{0.0f, playerField_.bodyHeight(), 0.0f}, playerCtrl_.yaw(), playerCtrl_.pitch());
            // (A debug start position keeps its own heading.)
            if (auto heading = runtime_->takeHeading(); heading && !cfg_.playerAt)
                playerCtrl_.reset(playerCtrl_.position(), *heading, playerCtrl_.pitch());
        }
    }
    if (!levelMode_)
        animState_.update(dt);
    if (levelMode_)
    {
        const auto light = playerField_.lightBelow(playerFeet() + Math::Vector3{0.0f, 0.5f, 0.0f});
        character_.setAmbientLight(playerAmbient_.update(light, dt));
    }
    character_.update(dt);

    const LevelRuntime::ScriptCamera* scriptCam = runtime_ && runtime_->scriptCamera().active ? &runtime_->scriptCamera() : nullptr;
    const float32 fov = viewFov(kFieldOfView / (scriptCam && !useFlyCam_ ? scriptCam->zoom : (useFlyCam_ ? 1.0f : gun_.magnification)));
    if (levelMode_ && camera_->fov() != fov)
    {
        camera_->setProjection(static_cast<float32>(cfg_.width) / static_cast<float32>(cfg_.height), fov, 0.1f, 1000.0f);
        camera_->update();
    }
    if (useFlyCam_)
        flyCam_.update(dt, camera_);
    else if (killing() && !kill_.shots.empty())
    {
        // The execution's camera: the picked set of shots around him.
        const CameraPose pose = cameraPoseAt(kill_.shots, kill_.t, kill_.feet, kill_.yaw);
        if (pose.shot != kill_.shot)
        {
            if (kill_.shot >= 0)
                cutNoise_ = 0.6f; // a burst of static at each cut
            kill_.shot = pose.shot;
        }
        const Math::Vector3 d = pose.target - pose.eye;
        const float32 flat = std::sqrt(d.x * d.x + d.z * d.z);
        float32 yaw = std::atan2(-d.x, -d.z), pitch = std::atan2(d.y, std::max(flat, 1e-4f)), roll = 0.0f;
        shakeByHand(dt, yaw, pitch, roll);
        camera_->setView(pose.eye, yaw, pitch, roll);
    }
    else if (auto crane = runtime_ ? runtime_->craneView() : std::nullopt)
    {
        // The crane camera: at its place, on the magnet.
        const Math::Vector3 d = crane->second - crane->first;
        const float32 flat = std::sqrt(d.x * d.x + d.z * d.z);
        camera_->setView(crane->first, std::atan2(-d.x, -d.z), std::atan2(d.y, std::max(flat, 1e-4f)));
    }
    else if (scriptCam)
    {
        // The cut-scene camera: placed by the scripts, aimed at its target.
        const Math::Vector3 d = scriptCam->target - scriptCam->position;
        const float32 flat = std::sqrt(d.x * d.x + d.z * d.z);
        float32 yaw = std::atan2(-d.x, -d.z), pitch = std::atan2(d.y, std::max(flat, 1e-4f)), roll = 0.0f;
        shakeByHand(dt, yaw, pitch, roll);
        camera_->setView(scriptCam->position, yaw, pitch, roll);
    }
    else if (levelMode_)
    {
        // The follow camera (0x58c280): from the pivot out along the view by its full distance times
        // a share that grows 24 % a frame back to 1 (0x7a1064 / 0x7a1038) and is cut at once to
        // where a 0.5 m sphere swept from the pivot first meets the world (0x4e6a70 / 0x58c9a0,
        // 0x7a1020; ours: the line, kept 0.5 m off). Zoomed, it looks from his eye.
        Math::Vector3 pivot = playerCtrl_.cameraPivot();
        Math::Vector3 desired = playerCtrl_.cameraPosition();
        float32 yaw = playerCtrl_.cameraYaw();
        // A peek carries the pivot out with his lean (short of any wall); looking back puts the
        // camera on his other side.
        if (const Math::Vector3 lean = leanOffset(); lean.x != 0.0f || lean.z != 0.0f)
        {
            const Math::Vector3 out = playerField_.clipCamera(pivot, pivot + lean);
            desired += out - pivot;
            pivot = out;
        }
        if (lookBack_)
        {
            desired = pivot * 2.0f - desired;
            desired.y = playerCtrl_.cameraPosition().y;
            yaw += Math::pi;
        }
        Math::Vector3 eye = zoomed() || firstPerson_ ? zoomEye() : desired;
        if (!zoomed() && !firstPerson_)
        {
            const float32 full = (desired - pivot).length();
            const Math::Vector3 clear = playerField_.clipCamera(pivot, desired, 0.5f);
            const float32 clearShare = full > 1e-3f ? (clear - pivot).length() / full : 1.0f;
            if (camShare_ <= 1e-4f)
                camShare_ = 0.15f; // (0x7a1068)
            camShare_ = std::min({1.0f, camShare_ * std::pow(1.24f, dt * 30.0f), clearShare});
            eye = pivot + (desired - pivot) * camShare_;
        }
        // A shot kicks the view (RECOIL_DIST, back over RECOIL_TIME).
        camera_->setView(eye, yaw + recoil_.yaw(), playerCtrl_.cameraPitch() + recoil_.pitch());
    }

    time_ += dt;
    // A save point walked into: the level as it is now, for SAVE GAME and Retry (0x4d2400).
    if (runtime_ && runtime_->takeSavePoint() >= 0)
    {
        checkpoint_ = snapshot();
        newCheckpoint_ = true;
        std::cerr << "session: save point - checkpoint taken (" << checkpoint_->entities.size() << " records)\n";
    }
    // The scene's time runs once the intro video is over, until the scene is won (0x5db4c0).
    if (levelMode_ && !cutscene_.isActive() && outroFor_ < 0.0f)
        score_.timeMs += static_cast<int32>(dt * 1000.0f + 0.5f);
    updateScreenEffects(dt);
    // 3D audio: the listener rides the camera; the level's sounds play at their positions.
    if (levelMode_ && camera_)
    {
        const float32 yaw = useFlyCam_ ? flyCam_.yaw() : playerCtrl_.cameraYaw();
        audio_.updateListener(camera_->position(), {-std::sin(yaw), 0.0f, -std::cos(yaw)}, {0.0f, 1.0f, 0.0f});
        // The audio manager's look round the camera (Game/acoustic_probe.h), once a frame.
        acoustics_.update(camera_->position(), {-std::sin(yaw), 0.0f, -std::cos(yaw)}, playerFeet(),
                          [this](const Math::Vector3& a, const Math::Vector3& b) { return playerField_.soundHit(a, b); });
    }
    if (runtime_)
    {
        updateGunEffects(dt);
        updatePlayerAudio();
        for (const LevelRuntime::SoundEvent& s : runtime_->takeSounds())
        {
            if (s.weaponEvent != 0)
                playWeaponSound(s.weaponCt, s.weaponEvent, s.weaponValue, s.position);
            else if (s.loopSlot >= 0 && s.loopSlot < static_cast<int32>(scriptLoops_.size()))
            {
                uint32& ch = scriptLoops_[static_cast<std::size_t>(s.loopSlot)];
                if (ch)
                    audio_.stop(ch);
                playSample(s.sample, s.position, s.priority, s.positional, s.range, s.volume, &ch);
            }
            else if (s.exeFalloff)
            {
                // Its level and range: the event's own, or (range 0) the sample's (Game/sample_levels.h).
                int32 level = static_cast<int32>(s.volume * 127.0f + 0.5f);
                float32 range = s.range;
                if (range <= 0.0f)
                {
                    const Assets::SfxBank* bank = assets_.sfxBank();
                    const std::size_t index = s.sample.size() > 1 && s.sample[0] == '#' ? static_cast<std::size_t>(std::strtoull(s.sample.c_str() + 1, nullptr, 10))
                                              : bank ? bank->indexOf(s.sample) : static_cast<std::size_t>(-1);
                    const SampleLevel l = sampleLevel(static_cast<int32>(index));
                    level = static_cast<int32>(static_cast<float32>(l.level) * s.volume + 0.5f);
                    range = l.range;
                }
                const float32 volume = entityVolume(level, range, s.position);
                if (volume > 0.0f)
                    playSample(s.sample, s.position, s.priority, true, 1000.0f, volume, nullptr, s.rate);
            }
            else
            {
                const float32 length = playSample(s.sample, s.position, s.priority, s.positional, s.range, s.volume);
                if (s.slot >= 0)
                    runtime_->slotPlaying(s.slot, length); // IsScriptAudioSlotCompleted
            }
        }
        // The level's music starts once the intro video is over, mixed by the danger he is in.
        if (!musicOn_ && !cutscene_.isActive())
        {
            music_.start(audio_);
            musicOn_ = true;
        }
        if (musicOn_)
        {
            const LevelRuntime::MusicDanger danger = runtime_->musicDanger();
            music_.update(audio_, dt, danger.hunted, danger.spotted, danger.nearest, runtime_->musicAmbience(dt));
        }
        if (auto weather = runtime_->takeWeather())
        {
            Renderer::SceneLighting& lighting = scene_.lighting();
            lighting.skyColor = weather->skyColor;
            lighting.fog = weather->fog;
            lighting.fogStart = weather->fogStart;
            lighting.fogEnd = weather->fogEnd;
        }
        runtime_->setSubtitles(cfg_.settings.subtitles != 0);
        if (LevelRuntime::FrontEndStream fe = runtime_->takeFrontEndStream(); !fe.load.empty() || fe.play >= 0)
        {
            if (!fe.load.empty())
            {
                if (frontEndChannel_)
                    audio_.stop(frontEndChannel_);
                frontEndChannel_ = 0;
                frontEndPath_ = fe.load;
            }
            if (fe.play >= 0 && !frontEndPath_.empty())
            {
                std::ifstream in(Engine::Platform::resolvePath(cfg_.gameRoot + frontEndPath_), std::ios::binary);
                const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                Assets::RibAudio a = Assets::decodeRib(bytes, 2, 0x400, 44100);
                if (!a.samples.empty())
                {
                    if (frontEndChannel_)
                        audio_.stop(frontEndChannel_);
                    Audio::SoundRequest req;
                    req.pcm = a.samples.data();
                    req.lengthBytes = a.samples.size() * sizeof(int16_t);
                    req.format = Audio::SampleFormat::Stereo16;
                    req.sampleRate = 44100;
                    req.priority = 1.0f;
                    req.positional = false;
                    req.volume = static_cast<float32>(fe.play) / 127.0f;
                    req.bus = Audio::Bus::Music;
                    frontEndChannel_ = audio_.play(req); // OpenAL copies the samples
                    std::cerr << "session: front-end stream " << frontEndPath_ << " at " << fe.play << "\n";
                }
                else
                    std::cerr << "session: front-end stream " << frontEndPath_ << " missing\n";
            }
        }
        for (const int32 slot : runtime_->takeEndedLoops())
            if (slot >= 0 && slot < static_cast<int32>(scriptLoops_.size()) && scriptLoops_[static_cast<std::size_t>(slot)])
            {
                audio_.stop(scriptLoops_[static_cast<std::size_t>(slot)]);
                scriptLoops_[static_cast<std::size_t>(slot)] = 0;
            }
        updateMilestones(dt);
        updateDirector(dt);
        updateItems();
        updateDecals(dt);
    }

    updateFmvSubtitles(dt);
    // REELHUNT_RUN=<entity>:<handler>@<s>[;...] (testing): each handler starts <s> seconds in, in
    // the order given (after the level's OnCreate has run, unlike --run-script).
    if (static const char* test = std::getenv("REELHUNT_RUN"); test && runtime_)
    {
        struct Run {
            std::string entity, handler;
            float32 at = 0.0f;
        };
        static const std::vector<Run> runs = [] {
            std::vector<Run> r;
            for (std::string rest(test); !rest.empty();)
            {
                const std::size_t semi = rest.find(';');
                const std::string spec = rest.substr(0, semi);
                rest = semi == std::string::npos ? std::string() : rest.substr(semi + 1);
                const std::size_t at = spec.find('@'), colon = spec.find(':');
                if (colon != std::string::npos)
                    r.push_back({spec.substr(0, colon), spec.substr(colon + 1, at - colon - 1),
                                 at == std::string::npos ? 0.0f : std::stof(spec.substr(at + 1))});
            }
            return r;
        }();
        static float32 clock = 0.0f;
        static std::size_t next = 0;
        clock += dt;
        for (; next < runs.size() && clock >= runs[next].at; ++next)
        {
            const bool ok = runtime_->scripts().start(runs[next].entity, runs[next].handler);
            std::cerr << "session: test: run " << runs[next].entity << ":" << runs[next].handler << (ok ? "" : ": no such handler") << std::endl;
        }
    }
    cutscene_.update(dt, renderer_, audio_);
}

void GameSession::loadFmvSubtitles(const std::string& videoPath)
{
    // 0x4c0c60 / 0x60f890: the video's name up to its '.', then "_n.SUB": a count, then that many
    // lines "<GXT key> <first frame> <last frame>" (frames x 1000 / 30 ms).
    fmvLines_.clear();
    fmvClock_ = 0.0f;
    fmvShown_ = -1;
    const std::size_t dot = videoPath.rfind('.');
    std::ifstream f(Engine::Platform::resolvePath(videoPath.substr(0, dot) + "_n.SUB"));
    int32 count = 0;
    if (!f || !(f >> count))
        return;
    for (int32 i = 0; i < count; ++i)
    {
        FmvLine l;
        int32 a = 0, b = 0;
        if (!(f >> l.key >> a >> b))
            break;
        l.start = static_cast<float32>(a * 1000 / 30) / 1000.0f;
        l.end = static_cast<float32>(b * 1000 / 30) / 1000.0f;
        fmvLines_.push_back(l);
    }
    std::cerr << "session: video subtitles " << fmvLines_.size() << "\n";
}

void GameSession::updateFmvSubtitles(float32 dt)
{
    // The line in force (0x60fa00 steps on past its end; 0x60fa40 shows it from its start), in
    // the director subtitles' place (ours).
    if (fmvLines_.empty() || !runtime_)
        return;
    if (!cutscene_.isActive())
    {
        if (fmvShown_ >= 0)
            runtime_->gameText().killSubtitle();
        fmvLines_.clear();
        fmvShown_ = -1;
        return;
    }
    fmvClock_ += dt;
    int32 now = -1;
    for (int32 i = 0; i < static_cast<int32>(fmvLines_.size()); ++i)
        if (fmvClock_ >= fmvLines_[static_cast<std::size_t>(i)].start && fmvClock_ <= fmvLines_[static_cast<std::size_t>(i)].end)
            now = i;
    if (now == fmvShown_)
        return;
    fmvShown_ = now;
    if (now < 0)
        runtime_->gameText().killSubtitle();
    else
    {
        const FmvLine& l = fmvLines_[static_cast<std::size_t>(now)];
        runtime_->gameText().subtitle(l.key, l.end - fmvClock_ + 0.1f);
    }
}

void GameSession::buildCoronas()
{
    // 0x59bc20: one per Light_Inst (EC_ENTITYLIGHT), at its model's HOTSPOT frame (0x4ccb00),
    // else at its own place.
    coronas_.clear();
    coronaSprites_.clear();
    const LevelContent* level = assets_.level();
    if (!level || !runtime_)
        return;
    const DffLibrary* models = runtime_->models();
    for (const Assets::InstPlacement& p : level->placements())
    {
        if (!Assets::iequals(p.entityClass, "Light_Inst"))
            continue;
        const Math::Vector3 origin = level->frame().placementOrigin(p.position);
        Math::Vector3 at = origin;
        if (const Assets::DffClump* clump = models ? models->find(p.record) : nullptr)
            for (const auto& [name, point] : clump->framePoints)
                if (Assets::iequals(name, "HOTSPOT"))
                {
                    at = level->frame().placedPoint(LevelFrame::placementRotation(p.rotation), origin, point);
                    break;
                }
        if (!coronas_.add(Coronas::fromParams(p.params, at, static_cast<uint16>(p.index))))
            break;
    }
    if (coronas_.size() > 0)
        std::cerr << "session: coronas " << coronas_.size() << "\n";
}

void GameSession::updateTorches()
{
    // 0x593e20: a TORCH_LIGHT gun gets a beam 14 m long (0x7a23c0) opening to 5 m round its end
    // (0x7a23c8): a 32-sided cone (0x7a23b8) from the torch, each edge cut short where a line from
    // it meets the world (0x593850). The cone is not drawn: 0x593f70 marks the stencil where the
    // world lies inside it (its front faces, then its back ones, coloured nothing), and 0x5940e0
    // draws the world round it again where the stencil is set (SRCALPHA / INVSRCALPHA), the
    // characters but the player too, lit by a RenderWare point light of the cone's length
    // (0x594830: rpLIGHTPOINT, radius 14) coloured (0.4, 0.6, 0.77) + (0.25, 0.2, 0.14) (0x7a23d0
    // set). Ours: a RenderWare spot light of that reach, colour and hard edge, lighting the world
    // and the atomics, held back by the static scene's shadow (where the cone's edges stop). Its
    // light sits at TORCH_LIGHT_POS on the gun and shines down the barrel turned by
    // TORCH_LIGHT_ORIEN. 0x594400: a hunter's torch facing the camera flares - a corona of the
    // same colour, 2 across near (0.01 at least), seen to 70 m, its brightness 0 up to a facing of
    // 0.2 (0x7a570c), full from 0.9 (0x7a5710), with the lens flare (0x40); the player's own does not.
    constexpr std::size_t kMaxTorches = 10;
    constexpr float32 kReach = 14.0f, kSpread = 5.0f;
    const Math::Vector4 kColour{0.4f + 0.25f, 0.6f + 0.2f, 0.77f + 0.14f, 1.0f};
    torchFlares_.clear();
    struct Held {
        Math::Matrix4x4 hand;
        const ItemType* gun = nullptr;
        bool player = false;
    };
    std::vector<Held> held;
    if (runtime_ && runtime_->playerAlive())
        if (const ItemType* e = runtime_->equipped(); e && e->torch)
        {
            Held h;
            h.gun = e;
            h.player = true;
            if (character_.boneTransform(1075, h.hand))
                held.push_back(h);
        }
    if (runtime_)
        for (const auto& hunter : runtime_->hunters())
        {
            if (held.size() >= kMaxTorches)
                break;
            const ItemType* w = hunter->alive() ? hunter->weapon() : nullptr;
            Held h;
            h.gun = w;
            if (w && w->torch && hunter->model().boneTransform(1075, h.hand))
                held.push_back(h);
        }
    while (torchLights_.size() < std::min(held.size(), kMaxTorches))
    {
        auto light = std::make_unique<Renderer::Light>();
        light->setLightType(Renderer::LightType::Spot);
        light->setCone(std::atan(kSpread / kReach), std::atan(kSpread / kReach));
        light->setColor(kColour);
        light->setRadius(kReach);
        light->setCastingShadow(true);
        light->setRwFlags(3);
        light->setIntensity(0.0f);
        torchLights_.push_back(static_cast<Renderer::Light*>(scene_.addEntity(std::move(light))));
    }
    if (cfg_.aiLog && held.size() != torchesLogged_)
    {
        torchesLogged_ = held.size();
        std::cerr << "session: torches lit " << held.size() << std::endl;
    }
    const Math::Vector3 eye = camera_ ? camera_->position() : Math::Vector3{0.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < torchLights_.size(); ++i)
    {
        Renderer::Light* light = torchLights_[i];
        if (i >= held.size())
        {
            light->setIntensity(0.0f);
            continue;
        }
        const Held& h = held[i];
        const Math::Vector3 o = h.gun->torchOrien;
        const Math::Vector3 barrel = (Math::Matrix4x4::rotationZ(Math::radians(o.z)) * Math::Matrix4x4::rotationY(Math::radians(o.y)) *
                                      Math::Matrix4x4::rotationX(Math::radians(o.x)))
                                         .multiplyPoint(Math::Vector3{1.0f, 0.0f, 0.0f});
        const Math::Vector3 at = h.hand.multiplyPoint(h.gun->torchPos);
        const Math::Vector3 dir = (h.hand.multiplyPoint(h.gun->torchPos + barrel) - at).normalized();
        light->setPosition(at);
        light->setDirection(dir);
        light->setIntensity(1.0f);
        if (h.player)
            continue;
        // Its beam (0x594400's second pass, flag 0x100): sprites along it in its colour, within 40 m.
        Coronas::Light beam;
        beam.at = at;
        beam.axis = dir;
        beam.r = 102;
        beam.g = 153;
        beam.b = 196;
        beam.nearW = beam.nearH = beam.farW = beam.farH = 1.0f;
        beam.range = 40.0f;
        beam.flags = 1u | 0x100u;
        torchFlares_.push_back(beam);
        const Math::Vector3 toTorch = (at - eye).normalized();
        const float32 facing = -dir.dot(toTorch);
        if (facing <= 0.2f)
            continue;
        Coronas::Light f;
        f.at = at;
        f.r = 102; // (0x594b08: 0x7a23e0.. x 255)
        f.g = 153;
        f.b = 196;
        f.nearW = f.nearH = 2.0f;
        f.farW = f.farH = 0.01f;
        f.range = 70.0f;
        f.intensity = facing <= 0.9f ? (facing - 0.2f) * 255.0f / 0.7f : 255.0f;
        f.flags = 1u | 0x40u;
        torchFlares_.push_back(f);
    }
}

void GameSession::updateCoronas()
{
    updateTorches();
    if (!camera_ || (coronas_.size() == 0 && torchFlares_.empty()))
    {
        coronaSprites_.clear();
        return;
    }
    Coronas::View view;
    view.eye = camera_->position();
    const glm::mat4 clip = camera_->clip().toGlm();
    view.project = [&clip](const Math::Vector3& p) -> std::optional<Math::Vector3> {
        const glm::vec4 c = clip * glm::vec4(p.x, p.y, p.z, 1.0f);
        if (c.w <= 0.1f)
            return std::nullopt;
        return Math::Vector3{c.x / c.w * 0.5f + 0.5f, c.y / c.w * 0.5f + 0.5f, c.w};
    };
    view.clear = [this](const Math::Vector3& a, const Math::Vector3& b) { return playerField_.worldClear(a, b); };
    view.timeMs = static_cast<uint32>(time_ * 1000.0f);
    view.frame = ++coronaFrame_;
    view.detail = cfg_.settings.gameDetail >= 3 ? 2 : cfg_.settings.gameDetail == 2 ? 1 : 0; // (0x59e190)
    view.fog = runtime_ && Assets::iequals(runtime_->levelSetup().weather, "FOGGY") ? 1.0f : 0.0f;
    view.scripted = runtime_ && runtime_->scriptCamera().active;
    coronaSprites_ = coronas_.frame(view, coronaRng_, &torchFlares_);
}

void GameSession::drawLockOn(Renderer::UiBatch& ui) const
{
    // The lock-on mark over the hunter he can execute: white, then yellow, then red as he holds.
    if (execTarget_ && camera_ && !killing())
        if (const Renderer::Texture* mark = hud_.texture("lockon"))
        {
            const Math::Vector3 head = execTarget_->feet() + Math::Vector3{0.0f, 2.1f, 0.0f};
            const glm::vec4 c = camera_->clip().toGlm() * glm::vec4(head.x, head.y, head.z, 1.0f);
            if (c.w > 0.1f)
            {
                const float32 x = c.x / c.w * 0.5f + 0.5f, y = c.y / c.w * 0.5f + 0.5f;
                const ItemType* item = runtime_->equipped();
                const int32 level = charge_.charging() && item ? charge_.level(*item) : 2;
                Renderer::UiQuad q;
                constexpr float32 kHalf = 0.025f;
                q.x0 = x - kHalf * 0.75f;
                q.x1 = x + kHalf * 0.75f;
                q.y0 = y - kHalf;
                q.y1 = y + kHalf;
                q.texture = mark;
                q.r = 255;
                q.g = level >= 4 ? 40 : 255;
                q.b = level >= 3 ? 40 : 255;
                ui.add(q);
            }
        }
}

WeaponStripView GameSession::weaponStripView() const
{
    // The slot of the weapon in his hand; fists are slot 3's.
    WeaponStripView v;
    const ItemType* held = runtime_->equipped();
    if (held)
    {
        v.ct = held->ct;
        if (const int32 slot = inventorySlot(held->holsterSlot); slot >= 1 && slot <= 4)
            v.slot = slot;
    }
    if (const Firearm* gun = runtime_->playerGun())
    {
        const std::string record = runtime_->playerGunRecord();
        v.gun = true;
        v.clip = runtime_->ammo().loaded(record);
        v.spare = runtime_->ammo().spare(record);
        v.clipAmmo = gun->clipAmmo;
        v.maxClips = gun->maxClips;
    }
    std::tie(v.swapSlot, v.swapCt) = runtime_->swapOffer();
    return v;
}

void GameSession::drawOverlay(Renderer::UiBatch& ui) const
{
    weaponStripDrawn_ = false;
    // The lamps' coronas, added to the frame under everything else (not over a video).
    if (levelMode_ && runtime_ && !(cutscene_.isActive() && fmvShown_ >= 0))
        if (const Renderer::Texture* corona = hud_.texture("corona"))
            for (const Coronas::Sprite& s : coronaSprites_)
            {
                Renderer::UiQuad q;
                q.x0 = s.x0;
                q.y0 = s.y0;
                q.x1 = s.x1;
                q.y1 = s.y1;
                q.r = s.r;
                q.g = s.g;
                q.b = s.b;
                q.texture = corona;
                q.blend = Renderer::UiBlend::Additive;
                ui.add(q);
            }
    if (levelMode_ && runtime_ && useFlyCam_)
    {
        drawLockOn(ui); // the debug camera shows the lock-on mark only
        return;
    }
    if (levelMode_ && runtime_ && cutscene_.isActive() && fmvShown_ >= 0)
    {
        runtime_->gameText().draw(ui, UiText(textFont_, hud_.texture(gameExe().fontAtlas))); // the video's subtitles
        return;
    }
    if (!levelMode_ || !runtime_ || cutscene_.isActive() || outroFor_ >= 0.0f)
        return; // (nothing over the outro fade)
    if (runtime_->inCutscene())
    {
        runtime_->gameText().draw(ui, UiText(textFont_, hud_.texture(gameExe().fontAtlas)));
        return; // no status icon while the scripts hold the camera
    }
    // (REELHUNT_CAMCORDER=1 shows the camcorder overlay all the time: debugging its layout.)
    static const bool alwaysCamcorder = std::getenv("REELHUNT_CAMCORDER") != nullptr;
    if (killing() || alwaysCamcorder)
    {
        // An execution is filmed: the parts the execution camera and the scripts have on.
        drawCamcorder(ui, alwaysCamcorder ? std::array<bool, 5>{true, true, true, true, true} : runtime_->look().camcorder);
        runtime_->gameText().draw(ui, UiText(textFont_, hud_.texture(gameExe().fontAtlas)));
        return;
    }
    // DrawHud shows the HUD at all; SetHudFlag / ToggleHudFlag pick its parts (HUD_MAP 1, HUD_MAN
    // 2, HUD_HEALTH 4, HUD_STAMINA 8, HUD_INVENTORY 16), and the flash flags blink them (the tutorial).
    const LevelRuntime::ScriptLook& look = runtime_->look();
    if (!look.drawHud)
    {
        runtime_->gameText().draw(ui, UiText(textFont_, hud_.texture(gameExe().fontAtlas)));
        return;
    }
    drawLockOn(ui);
    drawGunSight(ui);
    const bool blinkOff = std::fmod(time_, 0.512f) >= 0.256f;
    // No radar on Bonus3 (level index 21, the exe 0x5dffa0).
    if ((look.hudFlags & 1u) && !((look.hudFlash & 1u) && blinkOff) && !Assets::iequals(levelName_, "Bonus3"))
    {
        RadarView view;
        view.centre = playerFeet();
        view.viewYaw = playerCtrl_.yaw();
        view.blips = runtime_->radarBlips();
        radar_.draw(ui, view);
    }
    HudState s;
    s.showIcon = (look.hudFlags & 2u) != 0u;
    s.showHealth = (look.hudFlags & 4u) != 0u;
    s.showStamina = (look.hudFlags & 8u) != 0u;
    s.flashHealth = (look.hudFlash & 4u) != 0u;
    s.flashStamina = (look.hudFlash & 8u) != 0u;
    s.health = runtime_->playerHealth() / std::max(1.0f, runtime_->tuning().playerHitPoints);
    s.stamina = stamina_.level();
    s.flash = (look.hudFlash & 2u) != 0u;
    s.crouched = playerCtrl_.isCrouching();
    s.context = useContext();
    // REELHUNT_CONTEXT=<n> (testing): show that context's icon.
    if (static const char* test = std::getenv("REELHUNT_CONTEXT"); test)
        s.context = std::atoi(test);
    s.hidden = runtime_->playerHidden();
    s.spotted = runtime_->playerSpotted();
    static const bool god = std::getenv("REELHUNT_GOD") != nullptr; // (our test switch stands for the cheat)
    s.godMode = god;
    s.bars = runtime_->hudBars();
    hud_.draw(ui, s, time_);
    // The extra bars' names, 0.7 high, right-aligned just left of each bar's foot: a black shadow
    // (x - 0.003 * aspect, y 0.348312 - height), then grey 0x82 (y 0.345313 - height).
    if (s.showHealth)
    {
        const UiText font(textFont_, hud_.texture(gameExe().fontAtlas));
        for (std::size_t i = 0; i < s.bars.size(); ++i)
        {
            const float32 h = font.lineHeight(0.7f), w = font.width(s.bars[i].name, 0.7f);
            const float32 x = Hud::barX(i) - 0.01f - w;
            font.draw(ui, s.bars[i].name, x - 0.003f, 0.348312f - h, 0.7f, UiColor{0, 0, 0, 255});
            font.draw(ui, s.bars[i].name, x, 0.345313f - h, 0.7f, UiColor{0x82, 0x82, 0x82, 255});
        }
    }
    if ((look.hudFlags & 16u) && !((look.hudFlash & 16u) && blinkOff))
    {
        weaponStripDrawn_ = true;
        weaponStrip_.draw(ui, UiText(textFont_, hud_.texture(gameExe().fontAtlas)), weaponStripView());
    }
    runtime_->gameText().draw(ui, UiText(textFont_, hud_.texture(gameExe().fontAtlas)));
    // The scripts' timer or counter, top centre (ours: the exe's place and font not traced).
    if (const std::string counter = runtime_->hudCounter(); !counter.empty())
    {
        const UiText font(textFont_, hud_.texture(gameExe().fontAtlas));
        const float32 x = 0.5f - font.width(counter, 0.7f) * 0.5f;
        font.draw(ui, counter, x + 0.002f, 0.04f + 0.002f, 0.7f, UiColor{0, 0, 0, 200});
        font.draw(ui, counter, x, 0.04f, 0.7f, UiColor{230, 230, 230, 255});
    }
}

void GameSession::drawCamcorder(Renderer::UiBatch& ui, const std::array<bool, 5>& parts) const
{
    // The rolling bar (part 3): a 0.05-high band of (200, 200, 255) moving down 0.009 a 33.3 ms
    // frame from 0, back to -0.2 past 1.2 (the object's +0x354 .. +0x37c, set at 0x58af..); the
    // exe draws it as a stretched copy of the picture, ours as a translucent band (alpha ours).
    if (parts[3])
    {
        const float32 y = std::fmod(time_ * 0.27f, 1.4f) - 0.2f;
        ui.rect(0.0f, y, 1.0f, y + 0.05f, 200, 200, 255, 40);
        ui.rect(0.0f, y + 0.05f, 1.0f, y + 0.055f, 0, 0, 0, 60);
    }
    // Tape fuzz (part 2): 256 short streaks in each of two bands, along the top (0..30/512) and
    // the bottom (490..520/512), 5..35/512 long and 1/256 high, grey 128..254, new every frame.
    if (parts[2])
    {
        uint32 seed = static_cast<uint32>(time_ * 60.0f) * 2654435761u + 1u;
        auto next = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        for (int band = 0; band < 2; ++band)
            for (int i = 0; i < 256; ++i)
            {
                const float32 y = static_cast<float32>(next() % 30 + 490 * band) / 512.0f;
                const float32 x = static_cast<float32>(next() & 0x1ff) / 512.0f;
                const float32 len = static_cast<float32>(next() % 30 + 5) / 512.0f;
                const auto grey = static_cast<uint8>(std::max(128u, next() % 255u));
                ui.rect(x, y, x + len, y + 2.0f / 256.0f, grey, grey, grey, grey);
            }
    }
    if (!parts[0] && !parts[1])
        return;
    // The exe 0x58dbc0, switched on with the kill's camera (0x590440): "TCR : HH MM SS FF"
    // (25 frames a second; the exe counts real time from 0:28:55) at x 0.4, y 0.9 on a black box
    // of alpha 127, the colons between the groups shown on odd seconds; "REC" at x 0.1 in red
    // (255, 30, 40) with a red dot (paspherea, 0.05 high at x 0.06, y 0.07) lit on odd seconds.
    // The text sizes are ours (the exe draws the timecode at 0.9 x 1.5 and REC at 1.3 of its scale).
    constexpr float32 kTcrScale = 0.55f, kRecScale = 0.65f;
    constexpr UiColor kWhite{220, 220, 220, 255}, kRed{255, 30, 40, 255};
    const UiText text(textFont_, hud_.texture(gameExe().fontAtlas));
    const auto ms = static_cast<int64_t>((static_cast<double>(time_) + 1735.36) * 1000.0);
    const int h = static_cast<int>((ms / 3600000) % 24), m = static_cast<int>((ms / 60000) % 60);
    const int s = static_cast<int>((ms / 1000) % 60), f = static_cast<int>((ms % 1000) * 25 / 1000);
    const bool odd = (s & 1) != 0;
    char tcr[48];
    std::snprintf(tcr, sizeof(tcr), "TCR : %02d %02d %02d %02d", h, m, s, f);
    constexpr float32 kX = 0.4f, kY = 0.9f;
    const float32 w = text.width(tcr, kTcrScale), lh = text.lineHeight(kTcrScale);
    if (parts[0])
    {
        ui.rect(kX - 0.01f, kY - 0.01f, kX + w + 0.01f, kY + lh + 0.005f, 0, 0, 0, 127);
        text.draw(ui, tcr, kX, kY, kTcrScale, kWhite);
    }
    if (parts[0] && odd)
    {
        // The colons sit in the gaps after the hours, minutes and seconds.
        const std::string base = tcr;
        const float32 colon = text.width(":", kTcrScale), space = text.width(" ", kTcrScale);
        for (std::size_t gap = base.find(' ', 6); gap != std::string::npos; gap = base.find(' ', gap + 1))
        {
            const float32 gx = kX + text.width(base.substr(0, gap), kTcrScale) + (space - colon) * 0.5f;
            text.draw(ui, ":", gx, kY, kTcrScale, kWhite);
        }
    }
    if (!parts[1])
        return;
    if (odd)
        if (const Renderer::Texture* dot = hud_.texture("paspherea"))
        {
            Renderer::UiQuad q;
            q.x0 = 0.06f;
            q.y0 = 0.07f;
            q.x1 = 0.06f + 0.05f * 0.75f; // 0.05 high on a 4:3 frontend
            q.y1 = 0.12f;
            q.texture = dot;
            q.r = kRed.r;
            q.g = kRed.g;
            q.b = kRed.b;
            ui.add(q);
        }
    text.draw(ui, "REC", 0.1f, 0.075f, kRecScale, kRed);
}

void GameSession::updateDirector(float32 dt)
{
    Assets::SfxBank* bank = assets_.sfxBank();
    if (!bank || !runtime_)
        return;
    // A new line cuts the one being spoken (the earpiece carries one voice).
    for (int32 id : runtime_->takeDirectorSpeech())
    {
        auto parts = director_.samples(
            id, [bank](const std::string& n) { return bank->indexOf(n); },
            [bank](std::size_t i) { return i < bank->nameCountInOrder() ? bank->nameAt(i) : std::string(); }, directorRng_);
        std::cerr << "session: director speech " << id << (parts.empty() ? " (no line)" : " -> " + parts.front()) << "\n";
        if (!parts.empty())
        {
            directorQueue_ = std::move(parts);
            directorLeft_ = 0.0f;
            directorLine_ = id;
            directorPart_ = 0;
        }
    }
    runtime_->setDirectorSpeaking(directorLeft_ - dt > 0.0f || !directorQueue_.empty()); // IsDirectorSpeechPlaying
    if ((directorLeft_ -= dt) > 0.0f || directorQueue_.empty())
        return;
    const std::string next = directorQueue_.front();
    directorQueue_.erase(directorQueue_.begin());
    directorLeft_ = playSample(next, {0.0f, 0.0f, 0.0f}, 1.0f, false);
    // Its subtitle while it plays: part n of line id shows the level text "<key>A<n>".
    const std::string key = director_.subtitleKey(directorLine_, directorPart_++);
    if (!key.empty() && cfg_.settings.subtitles) // SUBTITLES (0x5c4760)
        runtime_->gameText().subtitle(key, std::max(directorLeft_, 1.5f));
}

float32 GameSession::playSample(const std::string& name, const Math::Vector3& at, float32 priority, bool positional,
                                float32 range, float32 volume, uint32* loopChannel, float32 rateScale, float32 pan)
{
    Assets::SfxBank* bank = assets_.sfxBank();
    if (!bank)
        return 0.0f;
    std::string pick = name;
    if (name.size() > 1 && name[0] == '#')
    {
        // A sample by its bank index (the scripts' SFX_SCRIPT_SLOT_* ids).
        const std::size_t index = static_cast<std::size_t>(std::strtoull(name.c_str() + 1, nullptr, 10));
        if (index >= bank->nameCountInOrder())
            return 0.0f;
        pick = bank->nameAt(index);
    }
    else if (!bank->has(name))
    {
        // A category: SFX_H3V1_TAUNT_CHASE -> one of SFX_H3V1_TAUNT_CHASE_1 .. _n.
        auto it = sampleVariants_.find(name);
        if (it == sampleVariants_.end())
        {
            int n = 0;
            while (bank->has(name + "_" + std::to_string(n + 1)))
                ++n;
            it = sampleVariants_.emplace(name, n).first;
            if (n == 0)
                std::cerr << "session: no SFX '" << name << "'\n";
        }
        if (it->second == 0)
            return 0.0f;
        pick = name + "_" + std::to_string(1 + std::rand() % it->second);
    }
    Assets::SfxSample sample;
    if (!bank->getSample(pick, sample))
        return 0.0f;
    if (cfg_.aiLog)
        std::cerr << "[sound] " << pick << "\n";
    Audio::SoundRequest req;
    req.pcm = sample.pcm.data();
    req.lengthBytes = sample.pcm.size();
    req.format = Audio::SampleFormat::Mono16;
    req.sampleRate = rateScale == 1.0f ? sample.sampleRate
                                       : std::min(48000u, static_cast<uint32>(static_cast<float32>(sample.sampleRate) * rateScale + 0.5f));
    req.priority = priority;
    req.position = at;
    req.positional = positional;
    req.pan = pan;
    req.maxDistance = range;
    req.volume = volume;
    req.loop = loopChannel != nullptr;
    const uint32 channel = audio_.play(req);
    if (loopChannel)
        *loopChannel = channel;
    return sample.sampleRate > 0 ? static_cast<float32>(sample.pcm.size() / 2) / static_cast<float32>(sample.sampleRate) : 0.0f;
}

void GameSession::updateFootsteps(float32 dt)
{
    // 0x4b3a10: a foot lands a quarter and three quarters of the way through his gait clip
    // (0x7341f0 / 0x7341f4; the tramp's clips step on frame lists). Its event by the clip: his
    // Sneak_Walk_Fwd is silent (the player only, 0x431b00), Sneak_Run_Fwd 0xf, Run_Fwd and
    // Sprint_Fwd 0x11, any other 0x10. A new gait clip starts its phase afresh (switching clips
    // is not a step).
    constexpr float32 kLeftFoot = 0.25f, kRightFoot = 0.75f;
    sinceStep_ += dt;
    const Math::Vector3 v = playerCtrl_.velocity();
    const float32 speed = std::sqrt(v.x * v.x + v.z * v.z);
    const std::string& clip = character_.currentClip();
    const float32 length = character_.clipDuration(clip);
    if (!onGround_ || speed < 0.3f || length <= 0.0f || runtime_ == nullptr || runtime_->inCutscene())
    {
        stepPhase_ = -1.0f;
        return;
    }
    const float32 phase = std::fmod(std::max(0.0f, character_.clipTime()) / length, 1.0f);
    const float32 last = clip == stepClip_ ? stepPhase_ : -1.0f;
    stepPhase_ = phase;
    stepClip_ = clip;
    if (last < 0.0f)
        return;
    auto crossed = [&](float32 foot) {
        return last <= phase ? (last < foot && foot <= phase) : (foot > last || foot <= phase); // (wrapped)
    };
    if (!crossed(kLeftFoot) && !crossed(kRightFoot))
        return;
    sinceStep_ = 0.0f;
    if (clip == "Sneak_Walk_Fwd")
        return;
    playStep(clip == "Sneak_Run_Fwd" ? 0xf : clip == "Run_Fwd" || clip == "Sprint_Fwd" ? 0x11 : 0x10, playerFeet());
}

float32 GameSession::entityVolume(int32 level, float32 range, const Math::Vector3& at, bool through) const
{
    if (!camera_)
        return -1.0f;
    const Math::Vector3 ear = camera_->position();
    const float32 distance = (ear - at).length();
    if (distance >= range)
        return -1.0f;
    return static_cast<float32>(MusicAmbience::falloff(level, range, distance)) / 127.0f * (through ? playerField_.soundThrough(at, ear) : 1.0f);
}

void GameSession::playWeaponSound(int32 ct, uint16 event, float32 value, const Math::Vector3& at)
{
    // 0x44efe0: the samples the event plays (Game/weapon_audio.h), each at its level (a tail's
    // times its side's openness, Game/acoustic_probe.h) falling off over its range from the camera
    // and through the world; a panned one 2D at 0.707 of that (0x6b2ae4).
    if (!camera_)
        return;
    const int32 v = static_cast<int32>(std::lround(value));
    std::vector<WeaponAudio::Voice> voices;
    if (event == 0xb)
        voices = WeaponAudio::used(ct, v, (camera_->position() - at).length() < 10.0f);
    else if (event == 0xc)
    {
        if (const int32 s = WeaponAudio::empty(ct); s >= 0)
            voices.push_back({s, 1.0f, -1, 0});
    }
    else if (event == 0xe)
    {
        if (const int32 s = WeaponAudio::shell(ct); s >= 0)
            voices.push_back({s, 1.0f, -1, 0});
    }
    for (const WeaponAudio::Voice& voice : voices)
    {
        const SampleLevel l = sampleLevel(voice.sample);
        float32 level = static_cast<float32>(l.level);
        if (voice.tail != 0)
            level *= acoustics_.tail(voice.tail);
        if (event == 0xe)
            level *= value; // (the shell's bounce and surface)
        float32 volume = entityVolume(static_cast<int32>(std::lround(level)), l.range, at);
        if (volume <= 0.0f)
            continue;
        if (voice.pan >= 0)
            volume *= 0.707f;
        // Its rate jittered by up to a sixteenth (0x453080), the empty click's not.
        const float32 jitter = event == 0xc ? 0.0f : static_cast<float32>(std::rand() % 33 - 16) / 256.0f;
        playSample("#" + std::to_string(voice.sample), at, 0.8f, voice.pan < 0, 1000.0f, volume, nullptr,
                   voice.rate * (1.0f + jitter), static_cast<float32>(voice.pan));
    }
}

void GameSession::updatePlayerAudio()
{
    // His heartbeat and breath (Game/player_audio.h): heard from him, not through the world, 2D.
    if (!runtime_ || !camera_ || runtime_->inCutscene())
        return;
    PlayerAudio::State s;
    s.timeMs = time_ * 1000.0f;
    s.health = runtime_->playerHealth() / std::max(1.0f, runtime_->tuning().playerHitPoints);
    s.stamina = stamina_.level();
    s.lockedOn = execTarget_ != nullptr;
    s.dead = !runtime_->playerAlive();
    for (const PlayerAudio::Sound& sound : playerAudio_.update(s))
    {
        const Math::Vector3 at = playerFeet() + Math::Vector3{0.0f, sound.height, 0.0f};
        const float32 volume = entityVolume(std::min(127, sound.level), sound.range, at, false);
        if (volume > 0.0f)
            playSample("#" + std::to_string(sound.sample), at, 0.4f, false, 1000.0f, volume, nullptr, sound.rate);
    }
}

void GameSession::playEntitySound(int32 sample, const Math::Vector3& at, float32 range)
{
    const SampleLevel l = sampleLevel(sample);
    const float32 volume = entityVolume(l.level, range > 0.0f ? range : l.range, at);
    if (volume <= 0.0f)
        return;
    const float32 jitter = static_cast<float32>(std::rand() % 33 - 16) / 256.0f;
    playSample("#" + std::to_string(sample), at, 0.6f, true, 1000.0f, volume, nullptr, 1.0f + jitter);
}

void GameSession::playStep(uint16 event, const Math::Vector3& feet)
{
    // 0x44ea20: a sample of the floor's material type (MaterialTypes::footsteps, one of its range
    // at random; 0x446d40 keeps the last type off the world), at a level over a range by the event
    // (0xf 12 over 10 m, 0x10 25 over 20 m, 0x11 60 over 20 m, 0x13 67 over 30 m). Unheard beyond
    // the range from the camera; within it the level falls off (0x452e70) and is scaled by what
    // gets through the world between (0x4df3b0). Its rate is jittered by up to a sixteenth
    // (0x453080).
    if (!camera_ || runtime_ == nullptr)
        return;
    if (const auto hit = playerField_.shotHit(feet + Math::Vector3{0.0f, 0.5f, 0.0f}, feet - Math::Vector3{0.0f, 1.0f, 0.0f}))
        stepMaterial_ = hit->material;
    const MaterialTypes::Steps steps = MaterialTypes::footsteps(stepMaterial_);
    if (steps.first < 0)
        return;
    int32 level = 25;
    float32 range = 20.0f;
    switch (event)
    {
    case 0xf: level = 12; range = 10.0f; break;
    case 0x11: level = 60; break;
    case 0x13: level = 67; range = 30.0f; break;
    default: break;
    }
    const float32 volume = entityVolume(level, range, feet + Math::Vector3{0.0f, 0.25f, 0.0f}); // (off the floor it stands on)
    if (volume <= 0.0f)
        return;
    const int32 sample = steps.first + std::rand() % (steps.last - steps.first + 1);
    const float32 jitter = static_cast<float32>(std::rand() % 33 - 16) / 256.0f; // (up to 1/16 either way)
    playSample("#" + std::to_string(sample), feet, 0.3f, true, 1000.0f, volume, nullptr, 1.0f + jitter);
    // A body on his shoulder (his move mode 0x11, 0x4705bd) sloshes with each step: event 0x23
    // (0x4b3fc4), SFX_ANIMATION_SPLOSH_1 and _2 by turns (0x44d810), from his feet + 0.9 m
    // (0x6b2ac4), at its level over its range, not through the world.
    if (carry_.phase == Carry::Phase::Carrying)
    {
        splosh_ = !splosh_;
        const int32 s = splosh_ ? 0x10b : 0x10c;
        const SampleLevel l = sampleLevel(s);
        const Math::Vector3 at = feet + Math::Vector3{0.0f, 0.9f, 0.0f};
        const float32 v = entityVolume(l.level, l.range, at, false);
        if (v > 0.0f)
            playSample("#" + std::to_string(s), at, 0.3f, true, 1000.0f, v, nullptr,
                       1.0f + static_cast<float32>(std::rand() % 33 - 16) / 256.0f);
    }
}

void GameSession::animateLocomotion(const PlayerInput& in)
{
    const LocomotionInput loco{.state = playerCtrl_.state(), .moveX = in.moveX, .moveZ = in.moveZ, .run = in.run};
    std::string clip = locomotionClip(loco);
    LocomotionMix mix = locomotionMix(loco);
    // Turning on the spot (the player update 0x4b0143..: standing still, his heading turning
    // faster than 20 degrees a second, 0x72872c, and his pitch not): BT_TURN1 with Stand_Turn_Idle
    // (the player's pack has it as Stand_Turn: ours), Sneak_Turn crouched, SafeZone_Turn hidden
    // in a safe zone; carrying, BT_DEADCARRY_TURN's DeadBody_Turn (below).
    const bool turning = turningOnSpot(in);
    if (turning && carry_.phase == Carry::Phase::None)
    {
        const char* c = !playerCtrl_.isCrouching() ? "Stand_Turn" : runtime_ && runtime_->playerHidden() ? "SafeZone_Turn" : "Sneak_Turn";
        if (character_.clipDuration(c) > 0.0f)
        {
            clip = c;
            mix = {};
        }
    }
    // Aiming with a throwable, standing still: the combat stance (Throw_Stance_Trans, then
    // Throw_Stance; Throw_Stance_Turn turning, Throw_Stance_Charge charging).
    const bool still = in.moveX == 0.0f && in.moveZ == 0.0f;
    const bool stance = still && gun_.aimHeld && onGround_ && !playerCtrl_.isCrouching() && runtime_ &&
                        runtime_->playerThrowWeapon() && carry_.phase == Carry::Phase::None;
    if (stance && !throwStance_)
    {
        throwStance_ = true;
        if (character_.clipDuration("Throw_Stance_Trans") > 0.0f)
        {
            playerAction("Throw_Stance_Trans", 1.0f, true);
            return;
        }
    }
    throwStance_ = stance;
    if (stance)
    {
        if (throw_.charging)
        {
            if (character_.currentClip() != "Throw_Stance_Charge" && character_.clipDuration("Throw_Stance_Charge") > 0.0f)
                character_.crossfade("Throw_Stance_Charge", 0.1f, false); // held at its end while charging
            return;
        }
        const char* c = turning ? "Throw_Stance_Turn" : "Throw_Stance";
        if (character_.clipDuration(c) > 0.0f)
        {
            clip = c;
            mix = {};
        }
    }
    // The gas can in his hand: Gas_Stand, Gas_Turn, Gas_Walk_Fwd / _Bkw / _Left / _Right.
    if (gas_.phase == GasCarry::Phase::Carrying)
    {
        const char* c = still                                         ? (turning ? "Gas_Turn" : "Gas_Stand")
                        : std::fabs(in.moveZ) >= std::fabs(in.moveX) ? (in.moveZ > 0.0f ? "Gas_Walk_Fwd" : "Gas_Walk_Bkw")
                                                                     : (in.moveX > 0.0f ? "Gas_Walk_Right" : "Gas_Walk_Left");
        if (character_.clipDuration(c) > 0.0f)
        {
            clip = c;
            mix = {};
        }
    }
    // A body on his shoulder: DeadBody_Stand, DeadBody_Move_Fwd / _Bkw / _Left / _Right.
    if (carry_.phase == Carry::Phase::Carrying)
    {
        const char* c = in.moveX == 0.0f && in.moveZ == 0.0f        ? (turning ? "DeadBody_Turn" : "DeadBody_Stand")
                        : std::fabs(in.moveZ) >= std::fabs(in.moveX) ? (in.moveZ > 0.0f ? "DeadBody_Move_Fwd" : "DeadBody_Move_Bkw")
                                                                     : (in.moveX > 0.0f ? "DeadBody_Move_Right" : "DeadBody_Move_Left");
        if (character_.clipDuration(c) > 0.0f)
        {
            clip = c;
            mix = {};
        }
    }
    // A gun raised, he walks facing ahead (Gun_Move_Fwd / _Bkw / _Left / _Right).
    if (gun_.raised && !playerCtrl_.isCrouching() && (in.moveX != 0.0f || in.moveZ != 0.0f))
    {
        const char* move = std::fabs(in.moveZ) >= std::fabs(in.moveX) ? (in.moveZ > 0.0f ? "Gun_Move_Fwd" : "Gun_Move_Bkw")
                                                                       : (in.moveX > 0.0f ? "Gun_Move_Right" : "Gun_Move_Left");
        if (character_.clipDuration(move) > 0.0f)
        {
            clip = move;
            mix = {};
        }
    }
    if (clip != character_.currentClip())
        character_.crossfade(clip, PedAnims::shared().blendInto(character_.currentClip(), clip, kLocomotionXfade));
    // Diagonal input blends the side clip in, so the feet follow the way he moves.
    character_.setMix(mix.clip, mix.weight);
    // Match the stride to the ground actually covered (acceleration, wall-blocking, backpedalling).
    const float32 clipSpeed = character_.rootSpeed(clip);
    if (clipSpeed > 0.0f)
    {
        const Math::Vector3 v = playerCtrl_.velocity();
        const float32 ground = std::sqrt(v.x * v.x + v.z * v.z);
        character_.setSpeed(std::clamp(ground / clipSpeed, 0.4f, 1.6f));
    }
}

bool GameSession::turningOnSpot(const PlayerInput& in) const noexcept
{
    return in.moveX == 0.0f && in.moveZ == 0.0f && onGround_ && turnHold_ > 0.0f;
}

Math::Vector3 GameSession::playerFeet() const
{
    return playerCtrl_.position() - Math::Vector3{0.0f, playerField_.bodyHeight(), 0.0f};
}

void GameSession::playerAction(const std::string& clip, float32 lockFraction, bool inPlace)
{
    const float32 d = character_.clipDuration(clip);
    if (d <= 0.0f)
        return;
    // Blended in over its behaviour's time (table 0x68f210).
    character_.crossfade(clip, PedAnims::shared().blendInto(character_.currentClip(), clip, 0.1f), false, 1.0f, inPlace);
    actionLeft_ = d * lockFraction;
}

void GameSession::updateCombat(float32 dt, PlayerInput& in)
{
    if (!runtime_)
        return;
    actionLeft_ = std::max(0.0f, actionLeft_ - dt);

    // Death: the clip plays out; 4 s on the scene has failed (0x46f880).
    if (!runtime_->playerAlive())
    {
        if (deadFor_ < 0.0f)
        {
            deadFor_ = 0.0f;
            lowerGun();
            // On the floor he dies there (Die_Lying_*).
            playerAction(jump_.death ? "Fall_Land_Die_Upright"
                                     : down_.phase == Down::Phase::None ? "Die_Bkw" : (down_.fwd ? "Die_Lying_Fwd" : "Die_Lying_Bkw"),
                         1.0f);
            down_ = Down{};
            if (musicOn_)
                music_.playerDied(audio_); // the _D stream
            std::cerr << "session: the player died\n";
        }
        deadFor_ += dt;
        if (deadFor_ > kDeathRestartSeconds)
            failed_ = true;
        in = PlayerInput{};
        return;
    }
    if (runtime_->levelFailed())
        failed_ = true; // SetLevelFailed

    // Hit by a hunter: flinch (a short lock-out of the controls). A blow's reaction plays in
    // place while he slides its root travel straight away from the attacker.
    if (auto from = runtime_->takeHit())
    {
        std::string react = runtime_->hitClip();
        if (react.empty() || character_.clipDuration(react) <= 0.0f)
        {
            // The attacker's weapon has no clip of his: the fist one of that side.
            const bool behind = react.find("_Bkw_") != std::string::npos;
            react = behind ? "Damage_Melee_Bkw_Fist" : "Damage_Melee_Quick1_Fist";
        }
        const bool melee = react.rfind("Damage_Melee_", 0) == 0;
        playerAction(react, 0.6f, melee);
        hitSlideLeft_ = 0.0f;
        if (melee)
        {
            const Math::Vector3 feet = playerFeet();
            Math::Vector3 away{feet.x - from->x, 0.0f, feet.z - from->z};
            const float32 len = away.length();
            const float32 travel = character_.rootTravel(react);
            if (len > 1e-3f && travel > 0.0f)
            {
                hitSlide_ = away * (travel / len);
                hitSlideTime_ = hitSlideLeft_ = character_.clipDuration(react);
            }
        }
        combo_.cancel();
    }
    if (hitSlideLeft_ > 0.0f)
    {
        const float32 step = std::min(dt, hitSlideLeft_);
        hitSlideLeft_ -= step;
        playerCtrl_.setPosition(playerCtrl_.position() + hitSlide_ * (step / hitSlideTime_)); // (the body resolve keeps him out of walls)
    }

    // Fists: quick combos and the held hard blow (WeaponTypeData.ini fists, Game/melee_combo.h).
    swiped_ = false;
    if (runtime_->playerThrowWeapon() || throw_.charging || throw_.busy > 0.0f)
    {
        combo_.cancel();
        updateThrow(dt, in);
        return;
    }
    if (runtime_->playerGun())
    {
        // A gun in his hand: it aims and fires instead.
        combo_.cancel();
        updateGun(dt, in);
        if (actionLeft_ > 0.0f)
        {
            in.moveX = in.moveZ = 0.0f;
            in.run = false;
        }
        return;
    }
    lowerGun();
    // The weapon in his hand fights by its own record (the shard as a blade; the bag and wire as fists).
    const ItemType* held = runtime_->equipped();
    const MeleeWeapon& weapon = runtime_->tuning().meleeWeapon(held && !held->weaponType.empty() ? held->weaponType : "fists");
    const MeleeCombo::Step step = combo_.update(dt, weapon, actionLeft_ <= 0.0f && onGround_,
                                                [this](const std::string& c) { return character_.clipDuration(c); });
    if (!step.startClip.empty())
    {
        character_.crossfade(step.startClip, 0.1f, false);
        swiped_ = true;
        // The weapon's swing (0x4c9ac0 posts 0xb; fists when he holds none).
        runtime_->weaponSound(held ? held->ct : 0x10, 0xb, 0.0f, playerFeet() + Math::Vector3{0.0f, 1.2f, 0.0f});
    }
    if (step.landed)
        runtime_->playerStrikes(playerFeet(), playerCtrl_.yaw(), *step.landed,
                                step.landed == &weapon.hard ? HitKind::Hard : HitKind::Quick, weapon.clipStem);

    if (actionLeft_ > 0.0f || combo_.busy())
    {
        // Busy: no walking, but the mouse still turns the view.
        in.moveX = in.moveZ = 0.0f;
        in.run = false;
    }
}

void GameSession::updateItems()
{
    if (const ItemType* got = runtime_->takePickedUp())
    {
        weaponStrip_.showName(got->ct); // its name over the weapon strip
        const bool gun = (got->weapon && runtime_->tuning().firearm(got->weaponType)) || got->collectable.rfind("CT_AMMO_", 0) == 0;
        playSample(gun ? "SFX_FRONTEND_PICKUP_GUN" : got->weapon ? "SFX_FRONTEND_PICKUP_MELEE" : "SFX_FRONTEND_PICKUP_HEALTH",
                   playerFeet(), 1.0f, false);
    }

    // The equipped weapon rides on his right hand (Bip01 R Hand, bone 1075 of cash_pc.dff); the
    // other weapons he carries hang where their HOLSTER_SLOT says, at the record's
    // HOLSTER_TRANSLATION / _ROTATION: BELT_LEFT the Left Weapon Slot (4444), BELT_RIGHT the Right
    // Weapon Slot (5555, both on the pelvis), BACK the Back Weapon Slot (3333). SPECIAL items (keys,
    // tapes) are not shown.
    constexpr int16 kRightHand = 1075;
    auto slotBone = [](const std::string& slot) -> int16 {
        if (Assets::iequals(slot, "BELT_LEFT"))
            return 4444;
        if (Assets::iequals(slot, "BELT_RIGHT"))
            return 5555;
        if (Assets::iequals(slot, "BACK"))
            return 3333;
        return -1;
    };
    const DffLibrary* library = runtime_->models();
    if (!library)
    {
        if (!weaponModels_)
            weaponModels_ = DffLibrary::load(levelDir_ + "pak/modelspc.dff");
        library = weaponModels_.get();
    }
    if (!library)
        return;
    const ItemType* equipped = runtime_->equipped();
    // In a kill whose weapon plays its own clip, the kill model is the held weapon (the exe swaps
    // or animates the held weapon's clump, 0x4ca800 / 0x4c90b3), so the static one is put away.
    std::string want = equipped && !(killing() && killModel_) ? equipped->model : std::string();
    if (gas_.inHand)
        want = gas_.pour ? std::string() : "Gas_Can"; // the gas can in his hand instead (its clump in the level's models)
    if (want != heldModel_)
    {
        if (!heldModel_.empty())
            character_.detach(heldModel_, kRightHand);
        heldModel_.clear();
        if (!want.empty() && character_.attach(*library, want, kRightHand, scene_, &assets_.level()->textures()))
            heldModel_ = want;
    }
    // Holstered: one of each other weapon kind he carries.
    std::vector<const ItemType*> belt;
    for (const ItemType* t : runtime_->inventory())
        if (t->weapon && t != equipped && slotBone(t->holsterSlot) >= 0 && std::find(belt.begin(), belt.end(), t) == belt.end())
            belt.push_back(t);
    std::vector<std::string> models;
    for (const ItemType* t : belt)
        models.push_back(t->model + "@" + t->holsterSlot);
    if (models == holstered_)
        return;
    for (const std::string& m : holstered_)
    {
        const std::size_t at = m.rfind('@');
        character_.detach(m.substr(0, at), slotBone(m.substr(at + 1)));
    }
    holstered_.clear();
    for (const ItemType* t : belt)
    {
        // The exe 0x4c8310: the weapon sits on the slot bone at HOLSTER_TRANSLATION (in the
        // bone's space) turned by HOLSTER_ROTATION (x y z w). A weapon with an HAnim skeleton (all
        // of them: a root and a master bone) takes it as its root node's current keyframe, with x,
        // y, z negated (0x4c8471); keyframes in memory are the standard rotation (RenderWare
        // conjugates them on load, which is why our clips conjugate the IFP's), so it is turned by
        // the inverse rotation. Only a clump without one is turned by the rotation itself (the
        // matrix at 0x4c8535).
        const Math::Vector4& q = t->holsterRotation;
        const Assets::DffClump* clump = library->find(t->model);
        Math::Quaternion turn = Math::Quaternion{q.w, q.x, q.y, q.z}.normalized();
        if (clump && !clump->bones.empty())
            turn = turn.conjugate();
        const Math::Matrix4x4 offset = Math::Matrix4x4::translation(t->holsterTranslation) * turn.toMatrix().make4x4();
        if (character_.attach(*library, t->model, slotBone(t->holsterSlot), scene_, &assets_.level()->textures(), &offset))
            holstered_.push_back(t->model + "@" + t->holsterSlot);
    }
}

const MeleeWeapon& GameSession::playerMelee() const
{
    const ItemType* held = runtime_ ? runtime_->equipped() : nullptr;
    return runtime_->tuning().meleeWeapon(held && !held->weaponType.empty() ? held->weaponType : "fists");
}

Math::Vector3 GameSession::zoomEye() const
{
    // His eye, a little in front of his face (ours: clear of his own head).
    const float32 yaw = playerCtrl_.yaw();
    return playerFeet() + Math::Vector3{0.0f, playerCtrl_.isCrouching() ? 1.0f : 1.6f, 0.0f} +
           Math::Vector3{-std::sin(yaw), 0.0f, -std::cos(yaw)} * 0.25f;
}

void GameSession::lowerGun()
{
    if ((gun_.raised || gun_.busy > 0.0f) && isGunOverlay(character_.overlayClip()))
        character_.stopOverlay();
    gun_.raised = false;
    gun_.raisedFor = 0.0f;
    gun_.busy = 0.0f;
    gun_.reloading = false;
    gun_.roundByRound = false;
    gun_.lock = nullptr;
    gun_.pulled = false;
    gun_.zoom = 0;
    gun_.magnification = 1.0f;
    character_.setVisible(true);
}

int32 GameSession::useContext() const
{
    // 0x45eb00 in its order: on the wall 0, carrying 1 (the gas can at a useable that takes it
    // 5), a buddy to order 2 (to follow) / 3 (to wait), the gas can to take 4 (0x4646b0(1)),
    // something to use 5, a wall to put his back to 6, a body to take 7 (0x4646b0(0)), a hunter
    // on the floor to kick 8; none 9. Nothing while dead, held or busy (ours: an action playing).
    if (!runtime_ || !runtime_->playerAlive() || killing() || grapple_.phase != Grapple::Phase::None || climbing() ||
        down_.phase != Down::Phase::None)
        return 9;
    if (wall_.on)
        return 0;
    if (carry_.phase != Carry::Phase::None)
        return carry_.phase == Carry::Phase::Carrying ? 1 : 9;
    const Math::Vector3 feet = playerFeet();
    const float32 yaw = playerCtrl_.yaw();
    if (gas_.phase != GasCarry::Phase::None)
        return gas_.phase != GasCarry::Phase::Carrying ? 9 : runtime_->useableInReach(feet, yaw, true) ? 5 : 1;
    if (actionLeft_ > 0.0f || !onGround_)
        return 9;
    if (Hunter* buddy = runtime_->buddyToOrder(feet, yaw))
        return buddy->buddyFollowing() ? 3 : 2;
    if (runtime_->gasCanInReach(feet, 1.3f))
        return 4;
    if (runtime_->useableInReach(feet, yaw))
        return 5;
    const Math::Vector3 from = feet + Math::Vector3{0.0f, playerCtrl_.isCrouching() ? 0.6f : 1.0f, 0.0f};
    const Math::Vector3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)}, right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    for (const Math::Vector3& dir : {ahead, right, right * -1.0f, ahead * -1.0f})
        if (wallNormal(from, dir, kWallReach))
            return 6;
    if (runtime_->corpseInReach(feet, 1.3f))
        return 7;
    if (runtime_->downTarget(feet, yaw, kGroundReach))
        return 8;
    return 9;
}

bool GameSession::startGasCarry(const Math::Vector3& feet, float32 yaw)
{
    const uint32 can = runtime_ ? runtime_->gasCanInReach(feet, 1.3f) : 0;
    if (can == 0)
        return false;
    (void)yaw;
    playerAction("Gas_Pick_Up", 1.0f, true);
    gas_ = GasCarry{GasCarry::Phase::Pickup, can, std::max(0.1f, character_.clipDuration("Gas_Pick_Up")), false, nullptr};
    std::cerr << "session: picking up the gas can\n";
    return true;
}

bool GameSession::updateGasCarry(float32 dt, PlayerInput& in)
{
    // REELHUNT_USE=<s>[,<s>...] (testing): Use is pressed at those seconds.
    if (static const char* test = std::getenv("REELHUNT_USE"); test)
    {
        static float32 clock = 0.0f;
        static std::size_t next = 0;
        static std::vector<float32> times = [&] {
            std::vector<float32> t;
            std::string s(test);
            for (std::size_t at = 0; at < s.size();)
            {
                const std::size_t comma = s.find(',', at);
                t.push_back(std::stof(s.substr(at, comma - at)));
                at = comma == std::string::npos ? s.size() : comma + 1;
            }
            return t;
        }();
        clock += dt;
        if (next < times.size() && clock >= times[next])
        {
            ++next;
            usePressed_ = true;
            const uint32 can = runtime_->gasCanInReach(playerFeet(), 1000.0f);
            const Math::Vector3 at = runtime_->positionOf(can), me = playerFeet();
            std::cerr << "session: test: use; he is at (" << me.x << ", " << me.y << ", " << me.z << "), the gas can " << can << " at ("
                      << at.x << ", " << at.y << ", " << at.z << ")" << std::endl;
        }
    }
    if (gas_.phase == GasCarry::Phase::None)
        return false;
    const Math::Vector3 feet = playerFeet();
    const float32 yaw = playerCtrl_.yaw();
    const Math::Vector3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    auto endPour = [&] {
        if (gas_.pour)
        {
            gas_.pour->setVisible(false);
            spentModels_.push_back(std::move(gas_.pour));
        }
    };
    auto putDown = [&] {
        if (gas_.inHand)
            runtime_->putGasCan(gas_.can, feet + ahead * 0.5f, yaw);
        endPour();
        gas_ = GasCarry{};
    };
    if (!runtime_->playerAlive())
    {
        putDown(); // he lets go where he falls (ours)
        return false;
    }
    actionLeft_ = std::max(0.0f, actionLeft_ - dt); // (updateCombat, which counts it down, is held)
    if (gas_.phase == GasCarry::Phase::Pickup || gas_.phase == GasCarry::Phase::Drop)
    {
        in = PlayerInput{}; // the clip holds him
        const float32 whole = std::max(0.1f, character_.clipDuration(gas_.phase == GasCarry::Phase::Pickup ? "Gas_Pick_Up" : "Gas_Put_Down_Slow"));
        // Taken from the floor halfway through Gas_Pick_Up (ours).
        if (gas_.phase == GasCarry::Phase::Pickup && !gas_.inHand && gas_.left <= whole * 0.5f)
        {
            runtime_->takeGasCan(gas_.can);
            gas_.inHand = true;
        }
        if ((gas_.left -= dt) > 0.0f)
            return true;
        if (gas_.phase == GasCarry::Phase::Pickup)
        {
            if (!gas_.inHand)
            {
                runtime_->takeGasCan(gas_.can);
                gas_.inHand = true;
            }
            gas_.phase = GasCarry::Phase::Carrying;
            return true;
        }
        putDown();
        return true;
    }
    if (gas_.phase == GasCarry::Phase::Pour)
    {
        // Pouring at the useable: his clip holds him; the can in his hand rides his right hand
        // (1075) as the held item does.
        in = PlayerInput{};
        if (gas_.pour)
        {
            Math::Matrix4x4 hand;
            if (character_.boneTransform(1075, hand))
                gas_.pour->setPlacementMatrix(hand);
            gas_.pour->update(dt);
        }
        if ((gas_.left -= dt) > 0.0f)
            return true;
        endPour();
        gas_.phase = GasCarry::Phase::Carrying;
        return true;
    }
    // Carrying: Use at a useable that takes the Gas_Can pours it (Gas_Can_Pour, the generator
    // class's clip; 0x4d40b0); otherwise Use sets it down (Gas_Put_Down_Slow), the fire button
    // drops it (_Quick).
    const bool use = std::exchange(usePressed_, false), attack = std::exchange(attackPressed_, false);
    if (use && gas_.inHand)
        if (const auto pour = runtime_->playerUses(feet, yaw, true))
        {
            if (pour->yaw != yaw)
                playerCtrl_.reset(playerCtrl_.position(), pour->yaw, playerCtrl_.pitch());
            playerAction(pour->clip, 1.0f, true);
            gas_.phase = GasCarry::Phase::Pour;
            gas_.left = std::max(0.1f, character_.clipDuration(pour->clip));
            if (!pour->itemClip.empty())
            {
                const DffLibrary* library = runtime_->models();
                ModelDesc md;
                md.dffPath = levelDir_ + "pak/modelspc.dff";
                md.library = library;
                md.clumpName = "Gas_Can";
                md.textures = &assets_.level()->textures();
                md.centerOnOrigin = false;
                md.frameRest = true;
                auto model = std::make_unique<Character>();
                if (library && model->load(md, scene_))
                {
                    model->loadAnimations(animations_);
                    if (model->hasSkeleton() && model->play(pour->itemClip, false))
                        gas_.pour = std::move(model);
                    else
                    {
                        std::cerr << "session: the gas can cannot play " << pour->itemClip << "\n";
                        model->setVisible(false);
                        spentModels_.push_back(std::move(model));
                    }
                }
            }
            std::cerr << "session: pouring the gas can (" << pour->clip << (gas_.pour ? ", the can " + pour->itemClip : std::string()) << ")\n";
            in = PlayerInput{};
            return true;
        }
    if (use || attack)
    {
        const std::string clip = use ? "Gas_Put_Down_Slow" : "Gas_Put_Down_Quick";
        playerAction(clip, 1.0f, true);
        gas_.phase = GasCarry::Phase::Drop;
        gas_.left = std::max(0.1f, character_.clipDuration(clip));
        in = PlayerInput{};
        return true;
    }
    in.run = false; // he walks with it (ours)
    in.crouch = false;
    return true;
}

bool GameSession::startCarry(const Math::Vector3& feet, float32 yaw)
{
    Hunter* body = runtime_ ? runtime_->corpseInReach(feet, 1.3f) : nullptr; // (his reach: ours)
    if (!body)
        return false;
    // 0x4646b0: DeadBody_Pickup_Fwd / _Bkw by how it lies (corpse +0x15c), the 180 clips when he
    // faces the way it does (lying on its back; the other way lying on its front). He takes its
    // facing, turned half round for a face-up body taken from the front or a face-down one from
    // behind (0x6197e0, 180 degrees), and stands off its root by 0x710894 (0.263, 0, -0.283) for
    // the 180 clips, else 0x710888 (-0.15, 0, 0.3), negated for a face-down body (0x4332f0).
    const bool down = body->faceDown();
    const Math::Vector3 mine{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    const Math::Vector3 its{-std::sin(body->yaw()), 0.0f, -std::cos(body->yaw())};
    const bool turn = down ? mine.dot(its) < 0.0f : mine.dot(its) > 0.0f;
    const std::string clip = std::string("DeadBody_Pickup_") + (turn ? "180_" : "") + (down ? "Fwd" : "Bkw");
    const bool flip = (!down && !turn) || (down && turn);
    const float32 rootYaw = body->yaw() + (flip ? Math::pi : 0.0f);
    const float32 sign = down ? -1.0f : 1.0f;
    // The body's half plays in his frame turned half round, this far off his feet: his right
    // by the offset's x, ahead by minus its z (the exe's frame turned to ours).
    const Math::Vector3 right{std::cos(rootYaw), 0.0f, -std::sin(rootYaw)}, ahead{-std::sin(rootYaw), 0.0f, -std::cos(rootYaw)};
    const Math::Vector3 offset = (turn ? right * 0.263f + ahead * 0.283f : right * -0.15f + ahead * -0.3f) * sign;
    playerCtrl_.reset(body->feet() - offset + Math::Vector3{0.0f, playerField_.bodyHeight(), 0.0f}, rootYaw, playerCtrl_.pitch());
    playerAction(clip, 1.0f, true);
    const float32 length = std::max(0.1f, character_.clipDuration(clip));
    // The body's half is shorter (GenHunterAnims DeadBody_Pickup_180_Bkw 1.37 s to his 2.17 s): it
    // lies still until the two end together (ours: the exe starts it when he takes hold, 0x470377).
    carry_ = Carry{Carry::Phase::Pickup, body, length, std::max(0.0f, length - body->model().clipDuration(clip)), clip, offset};
    body->carry(body->model().currentClip(), body->feet(), body->yaw(), false);
    std::cerr << "session: picking up " << body->name() << " (" << clip << ")\n";
    return true;
}

bool GameSession::updateCarry(float32 dt, PlayerInput& in)
{
    if (carry_.phase == Carry::Phase::None || !carry_.body)
        return false;
    Hunter& body = *carry_.body;
    if (!runtime_->playerAlive())
    {
        body.putDown(); // he lets go where he falls (ours); updateCombat plays his death
        carry_ = Carry{};
        return false;
    }
    actionLeft_ = std::max(0.0f, actionLeft_ - dt); // (updateCombat, which counts it down, is held)
    const Math::Vector3 feet = playerFeet();
    const float32 yaw = playerCtrl_.yaw();
    if (carry_.phase == Carry::Phase::Pickup || carry_.phase == Carry::Phase::Drop)
    {
        // Picking up and putting down, the body plays in his frame turned half round (0x4654e0:
        // the drop's 0x6197e0 by 180 degrees), the pickup's off his feet by its offset.
        in = PlayerInput{}; // the clip holds him
        if ((carry_.bodyIn -= dt) <= 0.0f)
            body.carry(carry_.clip, feet + carry_.offset, yaw + Math::pi, false);
        else
            body.carry(body.model().currentClip(), body.feet(), body.yaw(), false);
        if ((carry_.left -= dt) > 0.0f)
            return true;
        if (carry_.phase == Carry::Phase::Pickup)
        {
            carry_.phase = Carry::Phase::Carrying;
            return true;
        }
        body.putDown(); // it lies where it was put
        std::cerr << "session: put down " << body.name() << "\n";
        carry_ = Carry{};
        return true;
    }
    // Carrying: Use puts it down (DeadBody_Drop), the fire button drops it (DeadBody_Quick_Drop);
    // the scripts' PlayerDropBody puts it down.
    const bool use = std::exchange(usePressed_, false) || runtime_->takeDropBody(), attack = std::exchange(attackPressed_, false);
    if (use || attack)
    {
        const std::string clip = use ? "DeadBody_Drop" : "DeadBody_Quick_Drop";
        playerAction(clip, 1.0f, true);
        carry_.phase = Carry::Phase::Drop;
        carry_.clip = clip;
        carry_.left = std::max(0.1f, character_.clipDuration(clip));
        carry_.bodyIn = 0.0f;
        carry_.offset = {0.0f, 0.0f, 0.0f};
        body.carry(clip, feet, yaw + Math::pi, false);
        in = PlayerInput{};
        return true;
    }
    // He walks with it at DeadBody_Move_Fwd's pace. It rides on his shoulder: its matrix is his
    // bone 1095's (0x4656f3), playing his clip's partner (DeadBody_Stand / _Move_*).
    in.run = false;
    in.crouch = false;
    const float32 pace = character_.rootSpeed("DeadBody_Move_Fwd");
    in.maxSpeed = pace > 0.0f ? pace : 2.0f;
    const std::string& mine = character_.currentClip();
    const std::string theirs = mine.rfind("DeadBody_", 0) == 0 ? mine : std::string("DeadBody_Stand");
    Math::Matrix4x4 shoulder;
    if (character_.boneTransform(kCarryBone, shoulder))
        body.carryIn(theirs, shoulder, feet);
    else
        body.carry(theirs, feet, yaw, true);
    return true;
}

float32 GameSession::bodyYaw() const noexcept
{
    if (climbing() && climb_.ladder)
        return std::atan2(-climb_.ladder->face.x, -climb_.ladder->face.z);
    return wall_.on ? std::atan2(-wall_.normal.x, -wall_.normal.z) : playerCtrl_.yaw();
}

bool GameSession::updateClimb(float32 dt, PlayerInput& in)
{
    if (!runtime_)
        return false;
    Climb& c = climb_;
    if (c.phase == Climb::Phase::None)
    {
        // 0x4c4c30: at the BASE he walks into it (a moving behaviour, pressing ahead) until the
        // timer reaches the player's +0x21c, within its FOCUS_RADIUS; at the TOP he walks toward
        // it, his push within 0.7 (0x737208) of the way to it. Then MUST_ALIGN puts him on it.
        const Math::Vector3 feet = playerFeet();
        const float32 cy = playerCtrl_.cameraYaw();
        const Math::Vector3 ahead{-std::sin(cy), 0.0f, -std::cos(cy)}, right{std::cos(cy), 0.0f, -std::sin(cy)};
        Math::Vector3 push = ahead * in.moveZ + right * in.moveX;
        const float32 pushLen = push.length();
        const bool free = onGround_ && actionLeft_ <= 0.0f && !killing() && pushLen > 0.1f && runtime_->playerAlive();
        auto flat = [](const Math::Vector3& p, const Math::Vector3& q) { return std::hypot(p.x - q.x, p.z - q.z); };
        bool near = false;
        for (const LevelRuntime::Ladder& l : runtime_->ladders())
        {
            if (!free)
                break;
            push = push * (1.0f / pushLen);
            const Math::Vector3 line = Math::Vector3{l.wall.x, 0.0f, l.wall.z} - l.face * kLadderStand;
            if (std::fabs(feet.y - l.foot) < 0.6f && flat(feet, line) <= l.reach + 0.3f && in.moveZ > 0.0f &&
                ahead.dot(l.face) >= std::cos(Math::radians(l.baseFocus * 0.5f)))
            {
                near = true;
                if ((c.into += dt) < kLadderInto)
                    break;
                c = Climb{Climb::Phase::OnBase, &l, line, {line.x, l.foot, line.z}, 0.0f, 0.0f};
                playerAction("Ladder_OnBase", 1.0f, false);
                c.left = std::max(0.1f, character_.clipDuration("Ladder_OnBase"));
                std::cerr << "session: ladder up (Ladder_OnBase)\n";
                break;
            }
            // Ladder_OnTop starts him on the top floor 1.045 m in from the ladder (its root's travel),
            // facing away from the drop (his heading + 180 degrees, 0x73720c).
            const Math::Vector3 start = line + l.face * 1.045f;
            Math::Vector3 to{line.x - feet.x, 0.0f, line.z - feet.z};
            const float32 toLen = to.length();
            if (std::fabs(feet.y - l.top) < 0.6f && flat(feet, start) <= l.reach + 0.6f && toLen > 1e-3f &&
                push.dot(to * (1.0f / toLen)) > 0.7f)
            {
                c = Climb{Climb::Phase::OnTop, &l, line, {start.x, l.top, start.z}, 0.0f, 0.0f};
                playerAction("Ladder_OnTop", 1.0f, false);
                c.left = std::max(0.1f, character_.clipDuration("Ladder_OnTop"));
                std::cerr << "session: ladder down (Ladder_OnTop)\n";
                break;
            }
        }
        if (c.phase == Climb::Phase::None)
        {
            if (!near)
                c.into = 0.0f;
            return false;
        }
        // The view turns to the ladder with him (ours).
        playerCtrl_.reset(playerCtrl_.position(), bodyYaw(), playerCtrl_.pitch());
    }
    const LevelRuntime::Ladder& l = *c.ladder;
    if (!runtime_->playerAlive())
    {
        c = Climb{}; // (ours: he drops where he is)
        return false;
    }
    actionLeft_ = std::max(0.0f, actionLeft_ - dt); // (updateCombat, which counts it down, is held)
    // Struck on it, he drops off (0x49f150: behaviours 0x16..0x1a -> BT_FALL_LAUNCH with
    // Fall_Launch_Ladder, landing Fall_Land_Ladder), a little out from its face (ours).
    if (runtime_->takeHit())
    {
        const Math::Vector3 feet = (c.phase == Climb::Phase::Move ? c.origin - Math::Vector3{0.0f, kLadderHips, 0.0f} : playerFeet()) -
                                   l.face * 0.3f;
        playerCtrl_.reset(feet + Math::Vector3{0.0f, playerField_.bodyHeight(), 0.0f}, bodyYaw(), playerCtrl_.pitch());
        if (Physics::RigidBody* body = playerField_.body())
            body->setPosition(playerCtrl_.position());
        c = Climb{};
        jump_ = Jump{Jump::Phase::Air, 0.0f, feet.y, true, false, true};
        character_.crossfade("Fall_Launch_Ladder", 0.1f, false);
        std::cerr << "session: knocked off the ladder\n";
        return false;
    }
    const float32 climbIn = in.moveZ;
    in = PlayerInput{}; // the ladder holds him
    // The controller follows his feet (the camera and the hunters look there).
    auto follow = [&](const Math::Vector3& feet) {
        playerCtrl_.setPosition(feet + Math::Vector3{0.0f, playerField_.bodyHeight(), 0.0f});
    };
    switch (c.phase)
    {
    case Climb::Phase::OnBase:
    case Climb::Phase::OnTop:
        follow(c.phase == Climb::Phase::OnBase ? c.origin : Math::Vector3{c.line.x, l.top - kLadderTopHips - 1.0f, c.line.z});
        if ((c.left -= dt) > 0.0f)
            break;
        // On: Ladder_Cycle's origin is his root (0x422530 / 0x4226f0: BT_CLIMABLE_MOVE).
        c.origin = {c.line.x, c.phase == Climb::Phase::OnBase ? l.foot + kLadderHips : l.top - kLadderTopHips, c.line.z};
        c.phase = Climb::Phase::Move;
        character_.crossfade("Ladder_Cycle", 0.1f, true);
        character_.setSpeed(0.0f);
        break;
    case Climb::Phase::Move:
    {
        // Up / down at Ladder_Cycle's pace (1.006 m a cycle), played backwards going down; still,
        // it holds. Past the top piece Ladder_OffTop, past the base Ladder_OffBase (0x4c4c30).
        const float32 pace = std::max(0.1f, character_.rootRise("Ladder_Cycle") / std::max(0.1f, character_.clipDuration("Ladder_Cycle")));
        const float32 dir = climbIn > 0.0f ? 1.0f : climbIn < 0.0f ? -1.0f : 0.0f;
        c.origin.y += dir * pace * dt;
        character_.setSpeed(dir);
        follow(c.origin - Math::Vector3{0.0f, 1.0f, 0.0f});
        if (dir > 0.0f && c.origin.y >= l.top - kLadderTopHips)
        {
            c.origin.y = l.top - kLadderTopHips;
            c.phase = Climb::Phase::OffTop;
            playerAction("Ladder_OffTop", 1.0f, false);
            c.left = std::max(0.1f, character_.clipDuration("Ladder_OffTop"));
        }
        else if (dir < 0.0f && c.origin.y <= l.foot + kLadderHips)
        {
            c.origin = {c.line.x, l.foot, c.line.z}; // Ladder_OffBase's origin is his feet
            c.phase = Climb::Phase::OffBase;
            playerAction("Ladder_OffBase", 1.0f, false);
            c.left = std::max(0.1f, character_.clipDuration("Ladder_OffBase"));
        }
        break;
    }
    case Climb::Phase::OffTop:
    case Climb::Phase::OffBase:
    {
        character_.setSpeed(1.0f);
        if ((c.left -= dt) > 0.0f)
            break;
        // Off: on the top floor where Ladder_OffTop's root ends (1.124 m in), or at the base.
        const Math::Vector3 feet = c.phase == Climb::Phase::OffTop ? Math::Vector3{c.line.x, l.top, c.line.z} + l.face * 1.124f
                                                                   : Math::Vector3{c.line.x, l.foot, c.line.z};
        playerCtrl_.reset(feet + Math::Vector3{0.0f, playerField_.bodyHeight(), 0.0f}, bodyYaw(), playerCtrl_.pitch());
        if (Physics::RigidBody* body = playerField_.body())
            body->setPosition(playerCtrl_.position());
        std::cerr << "session: off the ladder (" << (c.phase == Climb::Phase::OffTop ? "top" : "base") << ")\n";
        c = Climb{};
        return true;
    }
    default:
        break;
    }
    return true;
}

void GameSession::updateJump(float32 dt, PlayerInput& in)
{
    // 0x4e8f0f: the ground lost under a moving character launches it (+0x4e4); 0x4b0abd plays
    // BT_JUMP_LAUNCH (Jump_Launch, then Jump_Glide), BT_JUMP_LAND on the ground (Jump_Land).
    // Ours: running at an edge with no floor within 1 m below 0.7 m ahead, he jumps (the GXT:
    // "run as fast as you can towards the edge and the player will jump automatically").
    // REELHUNT_FALL=<m>@<s> (testing): at <s> seconds he is lifted <m> metres, to fall.
    if (static const char* test = std::getenv("REELHUNT_FALL"); test)
    {
        static float32 clock = 0.0f;
        static bool lifted = false;
        const std::string spec(test);
        const std::size_t at = spec.find('@');
        if (!lifted && (clock += dt) >= (at == std::string::npos ? 0.0f : std::stof(spec.substr(at + 1))))
        {
            lifted = true;
            playerCtrl_.setPosition(playerCtrl_.position() + Math::Vector3{0.0f, std::stof(spec.substr(0, at)), 0.0f});
            if (Physics::RigidBody* body = playerField_.body())
                body->setPosition(playerCtrl_.position());
            onGround_ = false;
            std::cerr << "session: test: lifted " << spec.substr(0, at) << " m\n";
        }
    }
    switch (jump_.phase)
    {
    case Jump::Phase::None:
    {
        const Math::Vector3 v = playerCtrl_.velocity();
        // Off an edge without jumping: falling with no floor within 1 m under him (0x4a2470;
        // 0x728058), he flails (BT_FALL_FLAIL: Fall_Flail_Upright, landing Fall_Land_Upright).
        if (!onGround_ && v.y < 0.0f && runtime_->playerAlive() &&
            !playerField_.raycast(playerFeet() + Math::Vector3{0.0f, 0.1f, 0.0f}, playerFeet() - Math::Vector3{0.0f, kFallFlail, 0.0f}))
        {
            jump_ = Jump{Jump::Phase::Air, 0.0f, playerFeet().y, true};
            character_.crossfade("Fall_Flail_Upright", 0.15f, true);
            std::cerr << "session: falling\n";
            return;
        }
        const float32 speed = std::sqrt(v.x * v.x + v.z * v.z);
        if (!onGround_ || !in.run || in.crouch || in.moveZ <= 0.0f || speed < playerCtrl_.tuning().walkSpeed || actionLeft_ > 0.0f)
            return;
        const Math::Vector3 ahead = playerFeet() + Math::Vector3{v.x, 0.0f, v.z} * (0.7f / speed);
        if (playerField_.raycast(ahead + Math::Vector3{0.0f, 0.5f, 0.0f}, ahead - Math::Vector3{0.0f, 1.0f, 0.0f}))
            return;
        in.jump = true;
        jump_ = Jump{Jump::Phase::Air, 0.0f, playerFeet().y};
        character_.crossfade("Jump_Launch", 0.1f, false);
        std::cerr << "session: jump\n";
        return;
    }
    case Jump::Phase::Air:
    {
        jump_.t += dt;
        const float32 drop = jump_.fromY - playerFeet().y;
        // A jump more than 1 m down flails; 8 m down it is a death fall (BT_FALL_FLAIL_DEATH,
        // Fall_Flail_Die_Upright), and he screams (entity audio 0x49: a random
        // SFX_GENERIC_PLAYER_PAIN_LONG_1..14, 0x44e6d0).
        if (!jump_.flail && drop > kFallFlail)
        {
            jump_.flail = true;
            character_.crossfade("Fall_Flail_Upright", 0.15f, true);
        }
        if (!jump_.death && drop >= kFallDeath && runtime_->playerAlive())
        {
            jump_.death = true;
            character_.crossfade("Fall_Flail_Die_Upright", 0.15f, true);
            playSample("SFX_GENERIC_PLAYER_PAIN_LONG", playerFeet() + Math::Vector3{0.0f, 1.6f, 0.0f}, 0.9f, true);
            std::cerr << "session: a deadly fall\n";
        }
        if (jump_.t > 0.1f && onGround_)
        {
            std::cerr << "session: landed after " << jump_.t << " s, " << drop << " m down, at " << playerFeet().x << ", "
                      << playerFeet().y << ", " << playerFeet().z << "\n";
            // The landing is heard (0x520640 sound type 6, JUMP_LAND) and thuds (entity audio 0x13).
            runtime_->postSound("JUMP_LAND", playerFeet());
            playStep(0x13, playerFeet());
            if (jump_.death)
            {
                runtime_->killPlayer(); // (its death clip is Fall_Land_Die_Upright)
                jump_.phase = Jump::Phase::Land;
                return;
            }
            const bool flail = jump_.flail, ladder = jump_.ladder;
            jump_ = Jump{Jump::Phase::Land, 0.0f};
            if (ladder)
                playerAction("Fall_Land_Ladder", 1.0f, true);
            else if (flail)
            {
                playerAction("Fall_Land_Upright", 1.0f, true);
                jump_.flail = true; // then Getup_Upright (BT_FALL_GETUP)
            }
            else
                playerAction("Jump_Land", 0.5f, true);
            return;
        }
        if (!jump_.flail && jump_.t >= character_.clipDuration("Jump_Launch") && character_.currentClip() != "Jump_Glide")
            character_.crossfade("Jump_Glide", 0.1f, true);
        if (jump_.ladder && !jump_.death && jump_.t >= character_.clipDuration("Fall_Launch_Ladder") &&
            character_.currentClip() == "Fall_Launch_Ladder")
            character_.crossfade("Fall_Flail_Upright", 0.15f, true);
        return;
    }
    case Jump::Phase::Land:
        if (jump_.death || actionLeft_ > 0.0f)
            return;
        if (jump_.flail)
        {
            jump_ = Jump{Jump::Phase::Getup, 0.0f};
            playerAction("Getup_Upright", 1.0f, true);
            return;
        }
        jump_ = Jump{};
        return;
    case Jump::Phase::Getup:
        if (actionLeft_ <= 0.0f)
            jump_ = Jump{};
        return;
    }
}

std::optional<Math::Vector3> GameSession::wallNormal(const Math::Vector3& from, const Math::Vector3& dir, float32 reach) const
{
    const Math::Vector3 side{-dir.z, 0.0f, dir.x};
    const Math::Vector3 a = from + side * 0.15f, b = from - side * 0.15f;
    const auto ha = playerField_.raycast(a, a + dir * reach);
    const auto hb = playerField_.raycast(b, b + dir * reach);
    if (!ha || !hb)
        return std::nullopt;
    Math::Vector3 along = (a + dir * (reach * *ha)) - (b + dir * (reach * *hb));
    along.y = 0.0f;
    const float32 len = along.length();
    if (len < 1e-4f)
        return std::nullopt;
    Math::Vector3 n{along.z / len, 0.0f, -along.x / len};
    if (n.dot(dir) > 0.0f)
        n = n * -1.0f;
    // Square enough to put his back to (ours: within 45 degrees of the probe).
    if (n.dot(dir) > -0.7f)
        return std::nullopt;
    return n;
}

bool GameSession::startWall(float32 yaw)
{
    // 0x4a6700 tries ahead, his right, his left, then behind (vtable +0x17c probes); ours: rays
    // from his middle (his knees crouched) out to kWallReach.
    const bool crouch = playerCtrl_.isCrouching();
    const Math::Vector3 from = playerFeet() + Math::Vector3{0.0f, crouch ? 0.6f : 1.0f, 0.0f};
    const Math::Vector3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)}, right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    for (const Math::Vector3& dir : {ahead, right, right * -1.0f, ahead * -1.0f})
    {
        const auto n = wallNormal(from, dir, kWallReach);
        if (!n)
            continue;
        wall_ = Wall{};
        wall_.on = true;
        wall_.normal = *n;
        // 0x4a3d40: Wall_Stand_Trans_Left / _Right (crouched Wall_Crouch_Trans_*), by the way he
        // turns to put his back to it (ours), then his stance (Wall_Stand_Fwd / Wall_Crouch_Fwd).
        const float32 turn = std::remainder(bodyYaw() - yaw, 2.0f * Math::pi);
        const std::string clip = std::string(crouch ? "Wall_Crouch_Trans_" : "Wall_Stand_Trans_") + (turn > 0.0f ? "Left" : "Right");
        playerAction(clip, 1.0f, true);
        // The view turns to the wall, so Left / Right slide him and Down takes him off (ours).
        playerCtrl_.reset(playerCtrl_.position(), bodyYaw() + Math::pi, playerCtrl_.pitch());
        std::cerr << "session: wall strafe (" << clip << ")\n";
        return true;
    }
    return false;
}

bool GameSession::updateWall(float32 dt, PlayerInput& in)
{
    if (!wall_.on)
        return false;
    if (!runtime_->playerAlive() || runtime_->inCutscene())
    {
        wall_ = Wall{};
        return false;
    }
    actionLeft_ = std::max(0.0f, actionLeft_ - dt); // (updateCombat, which counts it down, is held)
    const bool crouch = in.crouch;
    const Math::Vector3 n = wall_.normal;
    const float32 yaw = bodyYaw();
    const Math::Vector3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    // What he presses, on the ground as the camera sees it.
    const float32 cy = playerCtrl_.cameraYaw();
    const Math::Vector3 want = Math::Vector3{-std::sin(cy), 0.0f, -std::cos(cy)} * in.moveZ +
                               Math::Vector3{std::cos(cy), 0.0f, -std::sin(cy)} * in.moveX;
    const bool use = std::exchange(usePressed_, false), attack = std::exchange(attackPressed_, false);
    in = PlayerInput{};
    in.crouch = crouch;
    if (wall_.leaving)
    {
        if (actionLeft_ <= 0.0f)
        {
            // He comes off it facing away from it (0x4a3ee0: his heading + 180 degrees).
            playerCtrl_.reset(playerCtrl_.position(), bodyYaw(), playerCtrl_.pitch());
            wall_ = Wall{};
        }
        return true;
    }
    // Off it: Use, or pressing away from it (the GXT's "Down"): Wall_Stand_Trans_Quick (0x4a3ee0).
    const Math::Vector3 from = playerFeet() + Math::Vector3{0.0f, crouch ? 0.6f : 1.0f, 0.0f};
    const auto behind = playerField_.raycast(from, from - n * kWallReach);
    if (use || want.dot(n) > 0.7f || !behind)
    {
        playerAction(crouch ? "Wall_Crouch_Trans_Quick" : "Wall_Stand_Trans_Quick", 1.0f, true);
        wall_.leaving = true;
        wall_.peek = 0;
        return true;
    }
    wall_.snug = std::clamp(kWallReach * *behind - kWallBack, 0.0f, 0.4f);
    if (actionLeft_ > 0.0f)
        return true; // getting on, or a blow at the wall
    // Aiming a gun leans him out to fire (the aim handler 0x4668fa -> 0x4a44b0, behaviour 0x37):
    // crouched, over the wall when nothing is in the way 1.8 m up (Wall_Crouch_Fire_1Handed /
    // _2Handed, "2" from Wall_Crouch_Fwd2); standing, round its end - the side he last slid or
    // leans to when the wall ends there, else the other (Wall_Fire_1Handed / _2Handed_Left /
    // _Right: 0x41e680 / 0x41e740). Let go, he comes back (crouched through Wall_Crouch_Trans2
    // to Wall_Crouch_Fwd2, 0x4a3ee0).
    // Without a gun the lean is the blow or the throw itself (0x41e680 / 0x41e740 with no gun
    // class): a beater (MELEE_CLASS BEATER1 / BEATER2, or the player's chainsaw CT 0x6c) swings
    // round the end (Wall_Attack_Melee_Left / _Right), anything else he can throw is thrown
    // round it (Wall_Fire_Throw_Left / _Right) or, crouched, over it (Wall_Crouch_Fire_Throw,
    // 0x41e800). Ours: the blow lands and the item leaves his hand halfway through the clip, at
    // the quick blow's shot and the throw's MAX_THROW_VELOCITY; once per press of aim.
    const Firearm* gun = runtime_->playerGun();
    const ItemType* held = runtime_->equipped();
    const bool beater = !gun && held && (held->ct == 0x6c || (held->weapon && playerMelee().clipStem.rfind("Beater", 0) == 0));
    const bool toss = !gun && !beater && runtime_->playerThrowWeapon() != nullptr;
    if (!gun_.aimHeld)
        wall_.acted = false;
    const Math::Vector3 wallFrom = playerFeet() + Math::Vector3{0.0f, crouch ? 0.6f : 1.0f, 0.0f};
    auto wallEnds = [&](int32 side) { return !wallNormal(wallFrom + right * (static_cast<float32>(side) * 0.4f), n * -1.0f, kWallReach); };
    if (wall_.act != 0)
    {
        const float32 whole = std::max(0.1f, wall_.actLeft + wall_.actFor);
        wall_.actFor += dt;
        if (!wall_.acted2 && wall_.actFor >= whole * 0.5f)
        {
            wall_.acted2 = true;
            const Math::Vector3 out = wall_.fire == 2 ? n * -1.0f : n;
            const Math::Vector3 at = playerFeet() + right * (wall_.fire == 2 ? 0.0f : static_cast<float32>(wall_.fire) * 0.5f);
            if (wall_.act == 1)
            {
                // Round the end: at what is beside the wall there.
                const Math::Vector3 side = right * static_cast<float32>(wall_.fire);
                runtime_->playerStrikes(at, std::atan2(-side.x, -side.z), playerMelee().quick, HitKind::Quick, playerMelee().clipStem);
            }
            else
            {
                const Math::Vector3 v = runtime_->tuning().maxThrowVelocity;
                const Math::Vector3 across{-out.z, 0.0f, out.x};
                runtime_->playerThrows(at + Math::Vector3{0.0f, 1.5f, 0.0f} + out * 0.3f,
                                       across * v.x + Math::Vector3{0.0f, v.y, 0.0f} + out * v.z);
            }
        }
        if ((wall_.actLeft -= dt) > 0.0f)
            return true;
        wall_.act = 0;
        wall_.fire = 0;
        wall_.peek = 0;
    }
    if ((beater || toss) && gun_.aimHeld && !wall_.acted && wall_.fire == 0)
    {
        int32 side = 0;
        if (crouch && toss)
        {
            const Math::Vector3 top = playerFeet() + Math::Vector3{0.0f, 1.8f, 0.0f};
            if (!playerField_.raycast(top, top - n * (kWallReach + 0.3f)))
                side = 2;
        }
        if (side == 0)
        {
            const int32 first = (wall_.peek < 0 || (wall_.peek == 0 && wall_.facing < 0)) ? -1 : 1;
            side = wallEnds(first) ? first : wallEnds(-first) ? -first : 0;
        }
        if (side != 0)
        {
            const std::string clip = side == 2 ? std::string("Wall_Crouch_Fire_Throw")
                                     : std::string(beater ? "Wall_Attack_Melee_" : "Wall_Fire_Throw_") + (side < 0 ? "Left" : "Right");
            character_.crossfade(clip, 0.1f, false);
            wall_.fire = side;
            wall_.act = beater ? 1 : 2;
            wall_.acted = true;
            wall_.acted2 = false;
            wall_.actFor = 0.0f;
            wall_.actLeft = std::max(0.1f, character_.clipDuration(clip));
            std::cerr << "session: wall " << (beater ? "blow" : "throw") << " (" << clip << ")\n";
            return true;
        }
    }
    if (gun && gun_.aimHeld && wall_.fire == 0)
    {
        int32 side = 0;
        if (crouch)
        {
            const Math::Vector3 top = playerFeet() + Math::Vector3{0.0f, 1.8f, 0.0f};
            if (!playerField_.raycast(top, top - n * (kWallReach + 0.3f)))
                side = 2;
        }
        else
        {
            // With no target only where the wall ends; with one, whichever side a line from his
            // gun out there reaches it (0x4a44b0's line tests).
            const int32 first = (wall_.peek < 0 || (wall_.peek == 0 && wall_.facing < 0)) ? -1 : 1;
            const Math::Vector3 gunAt = playerFeet() + Math::Vector3{0.0f, 1.5f, 0.0f};
            if (const Hunter* lock = runtime_->lockTarget(gunAt, std::atan2(-n.x, -n.z), 0.0f))
            {
                for (const int32 s : {first, -first})
                    if (playerField_.lineOfSight(gunAt + right * (static_cast<float32>(s) * 0.5f), aimPoint(lock->feet(), false)))
                    {
                        side = s;
                        break;
                    }
            }
            else
                side = wallEnds(first) ? first : wallEnds(-first) ? -first : 0;
        }
        if (side != 0)
        {
            wall_.fire = side;
            wall_.shotWait = 0.0f;
            const std::string hands = gun->twoHanded() ? "2Handed" : "1Handed";
            const std::string clip = side == 2 ? "Wall_Crouch_Fire_" + hands + (wall_.low ? "2" : "")
                                               : "Wall_Fire_" + hands + (side < 0 ? "_Left" : "_Right");
            character_.crossfade(clip, 0.1f, false);
            std::cerr << "session: wall fire (" << clip << ")\n";
        }
    }
    else if (wall_.fire != 0 && (!gun || !gun_.aimHeld))
    {
        const bool over = wall_.fire == 2;
        wall_.fire = 0;
        wall_.peek = 0;
        if (over)
        {
            wall_.low = true;
            playerAction("Wall_Crouch_Trans2", 1.0f, true);
            return true;
        }
    }
    if (wall_.fire != 0)
    {
        // Out from the wall: the fire button shoots from his gun at the hunter his aim locks on
        // to the way he faces (over the wall: beyond it), else straight out (ours: the wall's
        // camera looks at him).
        wall_.peek = wall_.fire == 2 ? 0 : wall_.fire;
        if ((wall_.shotWait -= dt) <= 0.0f && attack && gun)
        {
            const Math::Vector3 gunAt = playerFeet() + Math::Vector3{0.0f, wall_.fire == 2 ? 1.2f : 1.5f, 0.0f} +
                                        right * (wall_.fire == 2 ? 0.0f : static_cast<float32>(wall_.fire) * 0.5f);
            const Math::Vector3 out = wall_.fire == 2 ? n * -1.0f : n; // (over the wall: beyond it)
            const Hunter* lock = runtime_->lockTarget(gunAt, std::atan2(-out.x, -out.z), 0.0f);
            const Math::Vector3 dir = lock ? (aimPoint(lock->feet(), false) - gunAt).normalized() : out;
            if (runtime_->playerFires(gunAt, dir, playerFeet()))
            {
                gun_.fired = true;
                recoil_.kick(*gun, crouch ? runtime_->tuning().aim.crouchRecoil : 1.0f, std::uniform_int_distribution<int>(0, 1)(gunRng_) == 0);
                wall_.shotWait = std::max(0.1f, character_.clipDuration(gun->clipStem + "_Fire_T"));
            }
            else
                wall_.shotWait = 0.3f; // the click of an empty gun
        }
        character_.setSpeed(1.0f);
        return true;
    }
    // The fire button hits the wall (Wall_Attack_Melee_Left / _Right): a lure (0x4f7120: LURE_HIGH).
    if (attack && !gun)
    {
        playerAction(wall_.facing < 0 ? "Wall_Attack_Melee_Left" : "Wall_Attack_Melee_Right", 1.0f, true);
        runtime_->postSound("LURE_HIGH", playerFeet());
        return true;
    }
    // He slides toward the side he presses unless the wall ends there (+0x338 0x10000 / 0x20000);
    // pressed on past its end for 0.5 s (+0x48c) he leans out (0x4a4270 -> 0x4a1320). Q / E lean
    // him out too.
    const float32 s = want.dot(right);
    const int32 dir = std::fabs(s) > 0.3f ? (s > 0.0f ? 1 : -1) : 0;
    int32 peek = slotHeld(binds(), Bindings::PeekLeft) ? -1 : slotHeld(binds(), Bindings::PeekRight) ? 1 : 0;
    const std::string walk = std::string(crouch ? "Wall_Crouch_Walk_" : "Wall_Walk_") + (dir < 0 ? "Left" : "Right");
    bool sliding = false;
    if (dir != 0 || peek != 0)
        wall_.low = false; // (his behaviour changes: the plain crouch again)
    if (dir != 0)
    {
        wall_.facing = dir;
        if (wallNormal(from + right * (static_cast<float32>(dir) * 0.4f), n * -1.0f, kWallReach))
        {
            wall_.edgeHeld = 0.0f;
            const float32 pace = character_.rootSpeed(walk);
            // (and a little into the wall, which his body keeps him off: he stays on it)
            playerCtrl_.setPosition(playerCtrl_.position() + right * (static_cast<float32>(dir) * (pace > 0.0f ? pace : 1.0f) * dt) -
                                    n * (0.5f * dt));
            sliding = true;
        }
        else if ((wall_.edgeHeld += dt) >= 0.5f)
            peek = dir;
    }
    else
        wall_.edgeHeld = 0.0f;
    wall_.peek = peek;
    // His clip: leaning out (Wall_Peek_* / Wall_Crouch_Peek_*: 0x41e580 / 0x41e600; with a gun
    // of class under 5 the _1Handed_ ones, a bigger one the _2Handed_ ones), sliding, or standing
    // the way he last slid (0x4a4160: Wall_Stand_Fwd / _Left / _Right, crouched Wall_Crouch_*).
    const char* facing = wall_.facing < 0 ? "Left" : wall_.facing > 0 ? "Right" : "Fwd";
    const std::string peekHands = gun ? (gun->twoHanded() ? "2Handed_" : "1Handed_") : "";
    const std::string clip = peek != 0 ? std::string(crouch ? "Wall_Crouch_Peek_" : "Wall_Peek_") + peekHands + (peek < 0 ? "Left" : "Right")
                             : sliding ? walk
                             : crouch && wall_.low && character_.clipDuration("Wall_Crouch_Fwd2") > 0.0f ? std::string("Wall_Crouch_Fwd2")
                                                                                                        : std::string(crouch ? "Wall_Crouch_" : "Wall_Stand_") + facing;
    if (clip != character_.currentClip() && character_.clipDuration(clip) > 0.0f)
        character_.crossfade(clip, 0.15f, peek == 0);
    character_.setSpeed(1.0f);
    return true;
}

void GameSession::updatePeek(PlayerInput& in)
{
    // 0x4a1320 (actions 0x12 / 0x13): standing still only (+0x498 speed 0); behaviour 0xf with
    // Peek_L_ / Peek_R_ Stand, or _Sneak crouched. He leans while the button is held (ours).
    const bool free = runtime_ && runtime_->playerAlive() && !runtime_->inCutscene() && runtime_->playerControllable() &&
                      onGround_ && !gun_.raised && actionLeft_ <= 0.0f && !killing() && !firstPerson_;
    const int32 want = !free ? 0 : slotHeld(binds(), Bindings::PeekLeft) ? -1 : slotHeld(binds(), Bindings::PeekRight) ? 1 : 0;
    const bool moving = in.moveX != 0.0f || in.moveZ != 0.0f;
    if (peek_ == 0)
    {
        const Math::Vector3 v = playerCtrl_.velocity();
        if (want == 0 || moving || v.x * v.x + v.z * v.z > 0.01f)
            return;
        peek_ = want;
        character_.crossfade(std::string(want < 0 ? "Peek_L_" : "Peek_R_") + (playerCtrl_.isCrouching() ? "Sneak" : "Stand"), 0.1f, false);
    }
    else if (want != peek_ || moving)
    {
        peek_ = 0; // back to his stance (animateLocomotion cross-fades to it)
        return;
    }
    in.run = false;
}

Math::Vector3 GameSession::leanOffset() const
{
    // The lean's own reach sideways (his root off his feet in the peek clip), carried four times
    // as far, up to the 1.5 m the exe's peek camera stands off (0x728124; ours).
    const int32 side = wall_.on ? wall_.peek : peek_;
    if (side == 0)
        return {0.0f, 0.0f, 0.0f};
    const float32 yaw = bodyYaw();
    const Math::Vector3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    float32 lateral = 0.0f;
    for (const Character::BonePoint& b : character_.bonePoints())
        if (b.id == 1000)
        {
            lateral = (b.position - playerFeet()).dot(right);
            break;
        }
    return right * (static_cast<float32>(side) * std::clamp(std::fabs(lateral) * 4.0f, 0.0f, 1.5f));
}

void GameSession::updateThrow(float32 dt, PlayerInput& in)
{
    const AiTuning& t = runtime_->tuning();
    const bool moving = in.moveX != 0.0f || in.moveZ != 0.0f;
    // REELHUNT_THROW=<s> (testing): at <s> seconds a tap of fire throws what he holds.
    if (static const char* test = std::getenv("REELHUNT_THROW"); test)
    {
        static float32 clock = 0.0f;
        static bool thrown = false;
        if (!thrown && (clock += dt) >= std::stof(test))
        {
            thrown = true;
            throw_.pressed = true;
            std::cerr << "session: test: throw\n";
        }
    }
    if (throw_.busy > 0.0f && (throw_.busy -= dt) <= 0.0f && character_.overlayClip() == "Throw_Fire_T")
        character_.stopOverlay();
    if ((throw_.down || throw_.pressed) && !throw_.charging && throw_.busy <= 0.0f && runtime_->playerThrowWeapon())
    {
        throw_.charging = true;
        throw_.held = 0.0f;
    }
    throw_.pressed = false;
    // Aiming with it on the move: Throw_Aim_T over his walk (ours: the exe's use isn't traced).
    const bool aimMove = gun_.aimHeld && moving && !throw_.charging && throw_.busy <= 0.0f;
    if (aimMove && character_.overlayClip() != "Throw_Aim_T")
        character_.playOverlay("Throw_Aim_T");
    else if (!aimMove && character_.overlayClip() == "Throw_Aim_T")
        character_.stopOverlay();
    if (!throw_.charging)
        return;
    throw_.held += dt;
    const std::string clip = moving ? "Throw_Charge_Run_T" : "Throw_Charge_T";
    if (throwStance_)
    {
        if (!character_.overlayClip().empty() && character_.overlayClip().rfind("Throw_", 0) == 0)
            character_.stopOverlay(); // the stance's own Throw_Stance_Charge (full body)
    }
    else if (character_.overlayClip() != clip)
        character_.playOverlay(clip);
    if (throw_.down)
        return;
    // Let go: MIN_THROW_VELOCITY at no charge to MAX_THROW_VELOCITY after THROW_CHARGE_TIME, in his
    // frame (x across - our reading, y up, z ahead).
    const float32 k = t.throwChargeTime > 0.0f ? std::min(throw_.held / t.throwChargeTime, 1.0f) : 1.0f;
    const Math::Vector3 local = t.minThrowVelocity + (t.maxThrowVelocity - t.minThrowVelocity) * k;
    const float32 yaw = playerCtrl_.yaw();
    const Math::Vector3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)}, across{std::cos(yaw), 0.0f, -std::sin(yaw)};
    const Math::Vector3 velocity = across * local.x + Math::Vector3{0.0f, local.y, 0.0f} + ahead * local.z;
    // From his right hand at shoulder height (ours).
    const Math::Vector3 hand = playerFeet() + Math::Vector3{0.0f, 1.5f, 0.0f} + ahead * 0.3f + across * 0.2f;
    runtime_->playerThrows(hand, velocity);
    throw_.charging = false;
    if (throwStance_ && character_.clipDuration("Throw_Stance_Fire") > 0.0f)
    {
        playerAction("Throw_Stance_Fire", 1.0f, true); // from the stance (behaviour 0x1d)
        throw_.busy = std::max(0.2f, character_.clipDuration("Throw_Stance_Fire"));
        return;
    }
    throw_.busy = character_.playOverlay("Throw_Fire_T", false) ? std::max(0.2f, character_.clipDuration("Throw_Fire_T")) : 0.4f;
}

void GameSession::updateGun(float32 dt, PlayerInput& in)
{
    const Firearm* gun = runtime_->playerGun();
    const std::string record = runtime_->playerGunRecord();
    if (record != gun_.weapon)
    {
        lowerGun(); // another gun in his hand
        gun_.weapon = record;
    }
    bool pulled = std::exchange(gun_.pulled, false);
    const bool reloadPressed = std::exchange(gun_.reloadPressed, false);
    gun_.busy = std::max(0.0f, gun_.busy - dt);
    gun_.raisedFor = std::max(0.0f, gun_.raisedFor - dt);
    if (gun_.roundByRound)
    {
        // RELOAD_LOOP: each time the clip passes the loop's end a round goes in; while the clip
        // can take another and a spare round is left it goes back to the loop's start, else it
        // plays out (ours: the exe's handler is not traced; nothing breaks the loop off).
        const std::string clip = gun->clipStem + "_Reload_T";
        const float32 t = character_.overlayClip() == clip ? character_.overlayTime() : gun->reloadLoopEnd;
        if (t >= gun->reloadLoopEnd)
        {
            const bool in = runtime_->playerReloadsOne();
            if (in && runtime_->ammo().canReload(*gun, record))
                character_.setOverlayTime(gun->reloadLoopStart);
            else
            {
                gun_.roundByRound = false;
                gun_.busy = std::max(0.0f, character_.clipDuration(clip) - t);
                std::cerr << "session: reloaded round by round: " << runtime_->ammo().loaded(record) << " in the clip, "
                          << runtime_->ammo().spare(record) << " spare\n";
            }
        }
        else
            gun_.busy = std::max(gun_.busy, 0.1f);
    }
    if (gun_.busy <= 0.0f)
        gun_.reloading = false;
    const bool free = actionLeft_ <= 0.0f && onGround_;
    const Math::Vector3 feet = playerFeet();
    // His eye (the lock's sight line) and the camera's pivot (the free aim's ray passes it).
    const Math::Vector3 eye = feet + Math::Vector3{0.0f, playerCtrl_.isCrouching() ? 1.0f : 1.6f, 0.0f};
    const Math::Vector3 pivot = playerCtrl_.cameraPivot();
    const GunAmmo& ammo = runtime_->ammo();

    // --- zoom: while aiming, the wheel or Z steps through ZOOM_LEVELS (the rifles both, the other
    // guns the first: ours); the view eases there over ZOOM_SPEED; zoomed, the mouse moves the aim
    // ZOOM_MOVE_SCALES as fast and there is no lock ---
    const PlayerAim& aimData = runtime_->tuning().aim;
    const int32 maxZoom = gun->grip == Firearm::Grip::Rifle ? 2 : 1;
    gun_.zoom = gun_.aimHeld && free ? std::clamp(gun_.zoom + std::exchange(gun_.zoomSteps, 0), 0, maxZoom) : 0;
    gun_.zoomSteps = 0;
    {
        const float32 want = gun_.zoom > 0 ? aimData.zoomLevels[static_cast<std::size_t>(gun_.zoom - 1)] : 1.0f;
        const float32 rate = (aimData.zoomLevels[1] - 1.0f) / std::max(0.05f, aimData.zoomSpeed);
        const float32 step = rate * dt;
        gun_.magnification = std::abs(want - gun_.magnification) <= step ? want
                           : gun_.magnification + (want > gun_.magnification ? step : -step);
    }
    character_.setVisible(!zoomed() && !firstPerson_); // (the view is his own)
    if (gun_.zoom > 0)
    {
        const float32 scale = aimData.zoomMoveScales[static_cast<std::size_t>(gun_.zoom - 1)];
        in.mouseDX *= scale;
        in.mouseDY *= scale;
        gun_.lock = nullptr;
    }

    // --- the lock: aiming, on the nearest hunter in AIM_LOCKON_DIST and the AIM_LOCKON_ANGLES cone ---
    if (gun_.aimHeld && free && gun_.zoom == 0)
    {
        if (gun_.lock && !runtime_->keepsLock(*gun_.lock, eye))
            gun_.lock = nullptr;
        if (!gun_.lock)
        {
            gun_.lock = runtime_->lockTarget(eye, playerCtrl_.yaw(), playerCtrl_.pitch());
            gun_.wantHead = false;
            gun_.headPush = 0.0f;
            if (gun_.lock)
                std::cerr << "session: locked on to " << gun_.lock->name() << "\n";
        }
    }
    else
        gun_.lock = nullptr;
    if (gun_.lock)
    {
        // Locked: he swings round to it, and the mouse moves the lock between its chest and its
        // head (LOCKON_TARGET_RANGES; 40 px of travel, ours).
        gun_.headPush = std::clamp(gun_.headPush - in.mouseDY, -60.0f, 60.0f);
        if (gun_.headPush > 40.0f)
            gun_.wantHead = true;
        else if (gun_.headPush < -40.0f)
            gun_.wantHead = false;
        in.mouseDX = in.mouseDY = 0.0f;
        const Math::Vector3 at = gun_.lock->feet();
        const float32 dist = std::sqrt((at.x - feet.x) * (at.x - feet.x) + (at.z - feet.z) * (at.z - feet.z));
        gun_.head = lockOnHead(*gun, dist, gun_.wantHead);
        // He faces it; the camera behind him tilts to keep it in view (ours: within 20 degrees down, 15 up).
        const Math::Vector3 target = aimPoint(at, gun_.head);
        const Math::Vector3 to = target - eye;
        const Math::Vector3 fromCam = target - playerCtrl_.cameraPosition();
        const float32 camFlat = std::sqrt(fromCam.x * fromCam.x + fromCam.z * fromCam.z);
        const float32 wantPitch = std::clamp(std::atan2(fromCam.y, std::max(camFlat, 1e-3f)), -0.35f, 0.26f);
        const float32 k = 1.0f - std::exp(-12.0f * dt); // (ours: how fast he turns to it)
        const float32 yaw = playerCtrl_.yaw() + wrapAngle(std::atan2(-to.x, -to.z) - playerCtrl_.yaw()) * k;
        playerCtrl_.setLook(yaw, playerCtrl_.pitch() + (wantPitch - playerCtrl_.pitch()) * k);
    }

    // --- reloading: R, or by itself once the clip is empty (ours) ---
    auto startReload = [&] {
        if (!runtime_->playerReloads())
            return;
        const std::string clip = gun->clipStem + "_Reload_T";
        character_.stopOverlay();
        const bool played = character_.playOverlay(clip, false);
        gun_.busy = played ? std::max(0.3f, character_.clipDuration(clip)) : 1.5f;
        gun_.reloading = true;
        // A RELOAD_LOOP gun loads round by round (the clip loops); without its clip, all at once.
        gun_.roundByRound = gun->reloadsRoundByRound();
        if (gun_.roundByRound && !played)
        {
            while (runtime_->playerReloadsOne())
                ;
            gun_.roundByRound = false;
        }
        std::cerr << "session: reload " << record << " (" << ammo.loaded(record) << " in the clip, "
                  << ammo.spare(record) << " spare)\n";
    };
    if (free && gun_.busy <= 0.0f && ammo.canReload(*gun, record) && (reloadPressed || ammo.loaded(record) == 0))
        startReload();

    // --- a hunter in the gun's way (OBSTRUCT_WEAPON): the press strikes with the gun instead,
    // <grip>_Obstruct_Hit, the blow at its COMBAT_DELAYS (obstruct_1handedGun: 0.4 s, 4 damage) ---
    const ItemType* held = runtime_->equipped();
    const MeleeWeapon& strike = runtime_->tuning().meleeWeapon(
        held && !held->obstructWeapon.empty() ? held->obstructWeapon
                                              : std::string(gun->twoHanded() ? "obstruct_2handedGun" : "obstruct_1handedGun"));
    if (gun_.strikeT >= 0.0f)
    {
        gun_.strikeT += dt;
        if (!gun_.strikeLanded && gun_.strikeT >= strike.quick.strikeAt)
        {
            gun_.strikeLanded = true;
            runtime_->playerStrikes(feet, playerCtrl_.yaw(), strike.quick, HitKind::Obstruct, "Fist");
        }
        if (gun_.strikeT >= gun_.strikeLength)
            gun_.strikeT = -1.0f;
    }
    if (pulled && free && gun_.busy <= 0.0f && gun_.strikeT < 0.0f)
    {
        const float32 reach = strike.quick.size;
        const Math::Vector3 ahead{-std::sin(playerCtrl_.yaw()), 0.0f, -std::cos(playerCtrl_.yaw())};
        for (const auto& h : runtime_->hunters())
        {
            const Math::Vector3 to{h->feet().x - feet.x, 0.0f, h->feet().z - feet.z};
            const float32 d = to.length();
            if (!h->alive() || h->down() || h->beingExecuted() || d > reach + h->radius() || std::fabs(h->feet().y - feet.y) > 1.2f ||
                (d > 0.3f && ahead.dot(to * (1.0f / d)) < 0.5f))
                continue;
            std::string clip = gun->twoHanded() ? "2Handed_Obstruct_Hit" : "1Handed_Obstruct_Hit";
            for (const char* c : {"1Handed_Obstruct_Hit", "Melee_Obstruct_Hit"})
                if (character_.clipDuration(clip) <= 0.0f)
                    clip = c;
            character_.stopOverlay();
            playerAction(clip, 1.0f);
            gun_.strikeT = 0.0f;
            gun_.strikeLanded = false;
            gun_.strikeLength = std::max(0.3f, character_.clipDuration(clip));
            gun_.raised = false;
            runtime_->emitSound("SFX_WEAPON_SWING", feet + Math::Vector3{0.0f, 1.2f, 0.0f}, 0.5f);
            pulled = false; // (the press is spent on the blow)
            break;
        }
    }

    // --- the trigger: once per press; the climbing guns (RECOIL_VERT_ACCUMULATOR: uzi, commando)
    // keep firing while it is held (ours) ---
    const bool automatic = gun->recoilAccumulator > 0.0f;
    if ((pulled || (gun_.triggerHeld && automatic)) && free && gun_.busy <= 0.0f)
    {
        Math::Vector3 origin, dir;
        if (gun_.lock)
        {
            // At the lock's chest or head, from his eye (over the gun).
            origin = eye;
            dir = (aimPoint(gun_.lock->feet(), gun_.head) - origin).normalized();
        }
        else
        {
            // Where the crosshair is: along the camera's view, from beside him.
            const float32 yaw = playerCtrl_.cameraYaw() + recoil_.yaw();
            const float32 pitch = playerCtrl_.cameraPitch() + recoil_.pitch();
            dir = {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
            const Math::Vector3 cam = playerCtrl_.cameraPosition();
            origin = zoomed() ? zoomEye() : cam + dir * std::max(0.0f, (pivot - cam).dot(dir));
        }
        if (runtime_->playerFires(origin, dir, feet))
        {
            gun_.fired = true;
            const std::string clip = gun->clipStem + (gun_.zoom > 0 ? "_ZoomFire_T" : "_Fire_T");
            character_.stopOverlay(); // (restarts it shot after shot)
            gun_.busy = character_.playOverlay(clip, false) ? std::max(0.1f, character_.clipDuration(clip)) : 0.3f;
            const float32 scale = playerCtrl_.isCrouching() ? runtime_->tuning().aim.crouchRecoil : 1.0f;
            recoil_.kick(*gun, scale, std::uniform_int_distribution<int>(0, 1)(gunRng_) == 0);
            gun_.raisedFor = 0.8f; // a shot from the hip keeps the gun up a moment (ours)
        }
        else
            gun_.busy = 0.3f; // the click of an empty gun
    }

    // --- the stance: up while aiming (and just after a shot); he walks at the Gun_Move_ pace ---
    gun_.raised = free && (gun_.aimHeld || gun_.raisedFor > 0.0f);
    if (overlayLeft_ > 0.0f || gun_.busy > 0.0f)
        ; // a hit reaction, or the fire / reload clip, plays
    else if (gun_.raised)
    {
        // Zoomed and moving: <grip>_ZoomAim_Move_T; zoomed <grip>_ZoomAim_T; else <grip>_Aim_T.
        const bool moving = in.moveX != 0.0f || in.moveZ != 0.0f;
        const std::string walk = gun->zoomMoveClip();
        character_.playOverlay(gun_.zoom > 0 && moving && !walk.empty() ? walk : gun->aimClip(gun_.zoom > 0));
    }
    else if (isGunOverlay(character_.overlayClip()))
        character_.stopOverlay();
    if (gun_.raised)
    {
        in.run = false;
        const float32 pace = character_.rootSpeed("Gun_Move_Fwd");
        in.maxSpeed = pace > 0.0f ? pace : 2.5f;
        if (gun_.zoom > 0 && gun->zoomMoveClip().empty())
            in.moveX = in.moveZ = 0.0f; // a rifle to the eye: he stands still
    }
}

void GameSession::updateGunEffects(float32 dt)
{
    constexpr int16 kRightHand = 1075; // the gun hand (Bip01 R Hand)
    for (const LevelRuntime::GunEvent& e : runtime_->takeGunEvents())
    {
        // A shot's line kills the rats and crows within 0.5 m of it (0x5cf7e0).
        for (const ShotLine& line : e.lines)
        {
            for (const Math::Vector3& at : crows_.shoot(line.from, line.to))
                critterDies(at, 2);
            for (const Math::Vector3& at : rats_.shoot(line.from, line.to))
                critterDies(at, 1);
        }
        if (!e.item)
            continue;
        Math::Matrix4x4 hand;
        const Character& who = e.hunter ? e.hunter->model() : character_;
        if (!who.boneTransform(kRightHand, hand))
            continue;
        const float32 feetY = e.hunter ? e.hunter->feet().y : playerFeet().y;
        if (e.reload)
            gunFx_.reloaded(e.item->gun, hand, feetY);
        else
        {
            // The shot's FLASH_FX (<gun>_muzFlash) at the muzzle along its line; without one the
            // stand-in sprite.
            const Math::Vector3 muzzle = hand.multiplyPoint(e.item->gun.flashPos);
            const Math::Vector3 dir = e.lines.empty() ? Math::Vector3{1.0f, 0.0f, 0.0f} : e.lines.front().to - e.lines.front().from;
            const Firearm* gun = runtime_->tuning().firearm(e.item->weaponType);
            const bool flashed = gun && runtime_->playFx(gun->flashFx, muzzle, dir) != 0;
            gunFx_.fired(e.item->gun, hand, e.lines, feetY, !flashed);
        }
    }
    gunFx_.update(dt, camera_ ? camera_->position() : playerFeet(), playerField_);
    for (const GunEffects::Landing& l : gunFx_.takeLandings())
        playWeaponSound(l.ct, 0xe, l.value, l.at);
}

void GameSession::drawGunSight(Renderer::UiBatch& ui) const
{
    const Firearm* gun = runtime_->playerGun();
    if (!gun || !camera_ || killing())
        return;
    const Renderer::Texture* scope = hud_.texture("scope");
    if (gun_.raised && zoomed() && gun->grip == Firearm::Grip::Rifle && scope)
    {
        // A rifle to the eye: its scope (frontend_pc.txd "scope": the black surround, the window
        // and the cross hairs) fills the view's height, black either side.
        constexpr float32 kHalf = 0.5f * 0.75f; // a square on a 4:3 view
        ui.rect(0.0f, 0.0f, 0.5f - kHalf, 1.0f, 0, 0, 0, 255);
        ui.rect(0.5f + kHalf, 0.0f, 1.0f, 1.0f, 0, 0, 0, 255);
        Renderer::UiQuad q;
        q.x0 = 0.5f - kHalf;
        q.x1 = 0.5f + kHalf;
        q.y0 = 0.0f;
        q.y1 = 1.0f;
        q.texture = scope;
        ui.add(q);
    }
    else if (gun_.raised)
    {
        auto project = [&](const Math::Vector3& p, float32& x, float32& y) {
            const glm::vec4 c = camera_->clip().toGlm() * glm::vec4(p.x, p.y, p.z, 1.0f);
            if (c.w <= 0.1f)
                return false;
            x = c.x / c.w * 0.5f + 0.5f;
            y = c.y / c.w * 0.5f + 0.5f;
            return true;
        };
        // The crosshair (ours: four ticks round a circle of CROSSHAIR_SIZES, by gait: sneak, walk,
        // run) at the middle of the view, or red on the point a lock aims at.
        float32 x = 0.5f, y = 0.5f, radius = 0.0f;
        uint8 r = 255, g = 255, b = 255;
        if (gun_.lock)
        {
            if (!project(aimPoint(gun_.lock->feet(), gun_.head), x, y))
                return;
            radius = 0.02f;
            g = b = 50;
            // The lock-on mark over its head, as the execution lock draws it.
            float32 hx = 0.0f, hy = 0.0f;
            if (const Renderer::Texture* mark = hud_.texture("lockon");
                mark && project(gun_.lock->feet() + Math::Vector3{0.0f, 2.1f, 0.0f}, hx, hy))
            {
                Renderer::UiQuad q;
                constexpr float32 kHalf = 0.025f;
                q.x0 = hx - kHalf * 0.75f;
                q.x1 = hx + kHalf * 0.75f;
                q.y0 = hy - kHalf;
                q.y1 = hy + kHalf;
                q.texture = mark;
                q.g = q.b = 50;
                ui.add(q);
            }
        }
        else
        {
            const Math::Vector3 v = playerCtrl_.velocity();
            const float32 speed = std::sqrt(v.x * v.x + v.z * v.z);
            const auto& sizes = runtime_->tuning().aim.crosshair;
            radius = playerCtrl_.isCrouching() ? sizes[0] : (speed > 3.0f ? sizes[2] : sizes[1]);
        }
        const float32 rx = radius * 0.75f, tick = 0.012f, w = 0.0015f; // (a 4:3 view)
        ui.rect(x - rx - tick * 0.75f, y - w, x - rx, y + w, r, g, b, 230);
        ui.rect(x + rx, y - w, x + rx + tick * 0.75f, y + w, r, g, b, 230);
        ui.rect(x - w * 0.75f, y - radius - tick, x + w * 0.75f, y - radius, r, g, b, 230);
        ui.rect(x - w * 0.75f, y + radius, x + w * 0.75f, y + radius + tick, r, g, b, 230);
    }
    // (The rounds show on the weapon strip, Game/weapon_strip.h.)
}

bool GameSession::updateCloseCombat(float32 dt)
{
    if (!runtime_)
        return false;
    const AiTuning& tuning = runtime_->tuning();
    const MeleeWeapon& mine = playerMelee();
    const std::string stem = mine.clipStem;
    const Math::Vector3 feet = playerFeet();
    const float32 yaw = playerCtrl_.yaw();
    sinceAttack_ += dt;
    const bool kickPressed = std::exchange(kickPressed_, false);
    const bool grabPressed = std::exchange(grabPressed_, false);
    if (std::exchange(usePressed_, false) && actionLeft_ <= 0.0f && !killing() && grapple_.phase == Grapple::Phase::None &&
        down_.phase == Down::Phase::None && runtime_->playerAlive())
    {
        // In the context's order (0x45eb00's second switch): a buddy, the gas can, a useable, a
        // wall, a body.
        if (Hunter* buddy = runtime_->buddyToOrder(feet, yaw))
        {
            // 0x4643f0(1): he signs to it on his upper body (Signal_Come_T 0x141 to one waiting,
            // Signal_Stop_T 0x142 to one following; torso state 0x17) and it changes its mode.
            const char* sign = buddy->buddyFollowing() ? "Signal_Stop_T" : "Signal_Come_T";
            if (character_.playOverlay(sign, false))
                overlayLeft_ = std::max(0.2f, character_.clipDuration(sign));
            runtime_->orderBuddy(*buddy);
        }
        else
        {
            auto useIt = [&] {
                const auto use = runtime_->playerUses(feet, yaw);
                if (!use)
                    return false;
                // He turns to it (MUST_ALIGN) and plays the class's clip, held while it plays.
                if (use->yaw != yaw)
                    playerCtrl_.reset(playerCtrl_.position(), use->yaw, playerCtrl_.pitch());
                if (!use->clip.empty())
                    playerAction(use->clip, 1.0f, true);
                return true;
            };
            if (!startGasCarry(feet, yaw) && !useIt() && !startWall(yaw))
                startCarry(feet, yaw);
        }
    }
    const bool attackPressed = std::exchange(attackPressed_, false);
    const int32 wiggles = std::exchange(wiggles_, 0);
    if (overlayLeft_ > 0.0f && (overlayLeft_ -= dt) <= 0.0f)
        character_.stopOverlay();
    const bool groundPressed = std::exchange(groundPressed_, false);
    if (!runtime_->playerAlive() && deadFor_ < 0.0f)
    {
        // He dies: whatever held him lets go (updateCombat plays his death).
        if (grapple_.hunter)
            grapple_.hunter->release();
        grapple_ = Grapple{};
        kick_ = Kick{};
        return false;
    }
    // Shot: the hit jolts his upper body (Firearm_Damage_*_T where the level has them).
    if (runtime_->takeShot() && overlayLeft_ <= 0.0f)
    {
        static const char* kShot[] = {"Firearm_Damage_Fwd_Left_01_T", "Firearm_Damage_Fwd_Right_01_T",
                                      "Firearm_Damage_Fwd_Left_02_T", "Firearm_Damage_Fwd_Right_02_T"};
        const std::string pick = kShot[std::uniform_int_distribution<int>(0, 3)(killRng_)];
        for (const std::string& c : {pick, std::string("Firearm_Damage_T")})
            if (character_.playOverlay(c, false))
            {
                overlayLeft_ = std::max(0.15f, character_.clipDuration(c));
                break;
            }
    }
    if (deadFor_ >= 0.0f)
    {
        grapple_ = Grapple{};
        kick_ = Kick{};
        return false;
    }
    auto hunterStem = [&](const Hunter& h) { return h.melee() ? h.melee()->clipStem : std::string("Fist"); };
    auto hunterMelee = [&](const Hunter& h) -> const MeleeWeapon& { return h.melee() ? *h.melee() : tuning.fists; };
    auto endGrapple = [&] {
        if (grapple_.hunter)
            grapple_.hunter->release();
        grapple_ = Grapple{};
        actionLeft_ = 0.0f;
    };

    // --- knocked down: he falls, lies KO_TIME and gets up; hunters strike him there ---
    auto fallClip = [&](const std::string& prefix, bool fwd, const std::string& by) {
        const std::string base = prefix + (fwd ? "Fwd" : "Bkw");
        for (const std::string& c : {base + "_" + by, base + "_Fist", base})
            if (character_.clipDuration(c) > 0.0f)
                return c;
        return base;
    };
    if (auto knock = runtime_->takeKnockdown();
        knock && down_.phase == Down::Phase::None && grapple_.phase == Grapple::Phase::None && !killing())
    {
        // A hard blow from in front lays him on his back (Bkw), from behind on his face (Fwd).
        const Math::Vector3 to{knock->from.x - feet.x, 0.0f, knock->from.z - feet.z};
        const Math::Vector3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)};
        down_ = Down{};
        down_.phase = Down::Phase::Launch;
        down_.fwd = ahead.dot(to) < 0.0f;
        down_.stem = knock->stem;
        kick_ = Kick{};
        combo_.cancel();
        guard_ = false;
        character_.stopOverlay();
        const std::string clip = fallClip("Fall_Launch_Melee_", down_.fwd, down_.stem);
        playerAction(clip, 1.0f);
        down_.left = std::max(0.05f, character_.clipDuration(clip));
        std::cerr << "session: knocked down\n";
    }
    if (down_.phase != Down::Phase::None)
    {
        (void)runtime_->takeGrab(); // nothing grabs him on the floor
        if (runtime_->takeHit() && down_.phase == Down::Phase::Lying)
            playerAction(down_.fwd ? "Damage_Lying_Fwd" : "Damage_Lying_Bkw", 1.0f);
        if ((down_.left -= dt) <= 0.0f)
        {
            switch (down_.phase)
            {
            case Down::Phase::Launch:
            {
                down_.phase = Down::Phase::Land;
                const std::string clip = fallClip("Fall_Land_Melee_", down_.fwd, down_.stem);
                playerAction(clip, 1.0f);
                down_.left = std::max(0.05f, character_.clipDuration(clip));
                break;
            }
            case Down::Phase::Land:
            {
                // Lying: the lying cycle where the level has it, else the landing's last pose.
                down_.phase = Down::Phase::Lying;
                const std::string cycle = down_.fwd ? "Firearm_Lying_Cycle_Fwd" : "Firearm_Lying_Cycle_Bkw";
                if (character_.clipDuration(cycle) > 0.0f)
                    character_.crossfade(cycle, 0.2f, true);
                down_.left = tuning.playerKoTime;
                break;
            }
            case Down::Phase::Lying:
            {
                down_.phase = Down::Phase::Getup;
                const std::string clip = down_.fwd ? "Getup_Fwd" : "Getup_Bkw";
                playerAction(clip, 1.0f);
                down_.left = std::max(0.05f, character_.clipDuration(clip));
                break;
            }
            default:
                down_ = Down{};
                actionLeft_ = 0.0f;
                break;
            }
        }
        return down_.phase != Down::Phase::None;
    }

    // --- a grapple in progress ---
    Grapple& g = grapple_;
    if (g.phase != Grapple::Phase::None)
    {
        g.t += dt;
        Hunter& h = *g.hunter;
        HunterWorld w = runtime_->hunterWorld();
        if (!h.alive() && g.phase != Grapple::Phase::Attack)
        {
            endGrapple();
            return false;
        }
        switch (g.phase)
        {
        case Grapple::Phase::Grab:
            // Fist_Grab: at GRAB_TIME it has him, unless it got away or is swinging.
            if (g.t >= mine.grabTime && g.length > 0.0f && g.blow == 0)
            {
                const Math::Vector3 d = h.feet() - feet;
                if (h.alive() && h.state() != HunterState::Attacking && d.x * d.x + d.z * d.z < 1.7f * 1.7f)
                {
                    g.phase = Grapple::Phase::Attack;
                    g.blow = 0;
                    g.queued = true; // the first blow follows the grab
                    g.t = g.length = 0.0f;
                    std::cerr << "session: grapple " << h.name() << "\n";
                }
                else
                {
                    playerAction("Fist_Grab_Fail", 1.0f);
                    g.blow = -1;
                }
            }
            if (g.phase == Grapple::Phase::Grab && g.t >= g.length)
                endGrapple();
            return g.phase != Grapple::Phase::None;
        case Grapple::Phase::Attack:
        {
            const MeleeWeapon& attacker = g.byHunter ? hunterMelee(h) : mine;
            const std::string aStem = g.byHunter ? hunterStem(h) : stem;
            const std::string vStem = g.byHunter ? stem : hunterStem(h);
            if (g.byHunter)
            {
                // Wiggling empties the grapple timer; an empty one frees him with a counter-blow.
                g.escape -= static_cast<float32>(wiggles) * tuning.wiggleTimeDecay + dt * 0.1f;
                if (g.escape <= 0.0f && g.blow > 0)
                {
                    g.phase = Grapple::Phase::Broken;
                    g.t = 0.0f;
                    playerAction(stem + "_Grappled_Break_Attack1", 1.0f);
                    g.length = character_.clipDuration(stem + "_Grappled_Break_Attack1");
                    h.hold(hunterStem(h) + "_Grapple_Break_Attack1", h.feet(), h.yaw());
                    h.hit(mine.quick.damage, feet, w, HitKind::Grapple);
                    std::cerr << "session: broke free of " << h.name() << "\n";
                    return true;
                }
            }
            else if (attackPressed && g.blow > 0 && g.t >= attacker.grappleWindowStart &&
                     g.t <= attacker.grappleWindowStart + attacker.grappleWindowLength)
                g.queued = true;
            // The blow lands GRAPPLE_DAMAGE_TIMES into it.
            if (g.blow > 0 && !g.landed && g.t >= attacker.grappleDamageTimes[static_cast<std::size_t>(g.blow - 1)])
            {
                g.landed = true;
                const std::size_t i = static_cast<std::size_t>(g.blow - 1);
                if (g.byHunter)
                    runtime_->hurtPlayer(attacker.grappleDamagePlayer[i], h.feet());
                else
                    h.hit(attacker.grappleDamage[i], feet, w, HitKind::Grapple);
            }
            if (g.t < g.length)
                return true;
            // Next blow, or the grapple ends: three blows, or he did not press in time (it breaks free).
            if ((g.byHunter || g.queued) && g.blow < 3 && h.alive())
            {
                ++g.blow;
                g.queued = false;
                g.landed = false;
                g.t = 0.0f;
                const std::string n = std::to_string(g.blow);
                if (g.byHunter)
                {
                    h.hold(aStem + "_Grapple_Attack" + n, h.feet(), h.yaw());
                    playerAction(vStem + "_Grappled_Damage" + n, 1.0f);
                    g.length = character_.clipDuration(vStem + "_Grappled_Damage" + n);
                }
                else
                {
                    playerAction(aStem + "_Grapple_Attack" + n, 1.0f);
                    g.length = character_.clipDuration(aStem + "_Grapple_Attack" + n);
                    h.hold(vStem + "_Grappled_Damage" + n, h.feet(), h.yaw());
                    // Its health goes with the blow (dieHeld then takes it down).
                }
                if (g.length <= 0.0f)
                    endGrapple();
                return true;
            }
            if (!g.byHunter && g.blow < 3 && h.alive())
            {
                // He did not press on: it breaks his hold and hits back.
                g.phase = Grapple::Phase::Broken;
                g.t = 0.0f;
                h.hold(vStem + "_Grappled_Break_Attack1", h.feet(), h.yaw());
                playerAction(stem + "_Grapple_Break_Attack1", 1.0f);
                g.length = character_.clipDuration(stem + "_Grapple_Break_Attack1");
                runtime_->hurtPlayer(hunterMelee(h).quick.playerDamage, h.feet());
                return true;
            }
            endGrapple();
            return false;
        }
        case Grapple::Phase::Broken:
            if (g.t >= g.length)
                endGrapple();
            return g.phase != Grapple::Phase::None;
        default:
            endGrapple();
            return false;
        }
    }

    // --- a kick in progress ---
    if (kick_.t >= 0.0f)
    {
        kick_.t += dt;
        if (!kick_.landed && kick_.t >= kick_.strikeAt)
        {
            kick_.landed = true;
            if (Hunter* h = kick_.target)
            {
                // A stomp or a ground attack: the hunter on the floor, if it still lies in reach.
                const Math::Vector3 d = h->feet() - feet;
                if (h->alive() && h->down() && d.x * d.x + d.z * d.z < (kGroundReach + 0.3f) * (kGroundReach + 0.3f))
                {
                    HunterWorld w = runtime_->hunterWorld();
                    h->hit(kick_.damage, feet, w, kick_.kind, stem);
                }
            }
            else
            {
                const ItemType* held = runtime_->equipped();
                runtime_->playerKicks(feet, yaw, tuning.kick(held ? held->kickWeapon : std::string()));
            }
        }
        if (kick_.t >= kick_.length)
            kick_ = Kick{};
        return kick_.t >= 0.0f;
    }

    const bool free = actionLeft_ <= 0.0f && !combo_.busy() && !charge_.charging();
    // --- a hunter grabs him (he stood behind his guard within its reach) ---
    if (Hunter* by = runtime_->takeGrab(); by && free)
    {
        g = Grapple{};
        g.phase = Grapple::Phase::Attack;
        g.byHunter = true;
        g.hunter = by;
        g.blow = 0;
        g.t = 0.0f;
        by->hold("Fist_Grab", by->feet(), by->yaw());
        g.length = by->model().clipDuration("Fist_Grab");
        guard_ = false;
        character_.stopOverlay();
        std::cerr << "session: " << by->name() << " grabs him\n";
        return true;
    }
    // --- he kicks, or stomps a stunned hunter ---
    if (kickPressed && free)
    {
        // A hunter on the floor in front is stomped (Stomp_01), any other kicked (Kick_01).
        Hunter* floored = runtime_->downTarget(feet, yaw, kGroundReach);
        const ItemType* held = runtime_->equipped();
        const KickWeapon& kick = tuning.kick(held ? held->kickWeapon : std::string());
        const std::string clip = floored ? "Stomp_01" : "Kick_01";
        playerAction(clip, 1.0f);
        kick_ = Kick{0.0f, floored ? kick.stompAt : kick.kickAt, character_.clipDuration(clip), false, floored,
                     kick.shot.damage, HitKind::Stomp};
        runtime_->emitSound("SFX_WEAPON_SWING", feet + Math::Vector3{0.0f, 0.6f, 0.0f}, 0.4f);
        return true;
    }
    // --- he strikes down at a hunter on the floor: his weapon's <stem>_Ground_Attack_1 with its
    // hard blow, or with fists a stomp ---
    if (groundPressed && free)
        if (Hunter* floored = runtime_->downTarget(feet, yaw, kGroundReach))
        {
            const std::string ground = stem + "_Ground_Attack_1";
            const ItemType* held = runtime_->equipped();
            const KickWeapon& kick = tuning.kick(held ? held->kickWeapon : std::string());
            const bool armed = stem != "Fist" && character_.clipDuration(ground) > 0.0f;
            const std::string clip = armed ? ground : std::string("Stomp_01");
            playerAction(clip, 1.0f);
            const float32 length = character_.clipDuration(clip);
            // (ours: the ground attack lands halfway through its clip; the data gives no time)
            kick_ = Kick{0.0f, armed ? length * 0.5f : kick.stompAt, length, false, floored,
                         armed ? mine.hard.damage : kick.shot.damage, armed ? HitKind::Hard : HitKind::Stomp};
            runtime_->emitSound("SFX_WEAPON_SWING", feet + Math::Vector3{0.0f, 0.6f, 0.0f}, 0.5f);
            return true;
        }
    // --- he grabs the hunter in front ---
    if (grabPressed && free)
        if (Hunter* h = runtime_->grabTarget(feet, yaw, 1.6f))
        {
            g = Grapple{};
            g.phase = Grapple::Phase::Grab;
            g.hunter = h;
            playerAction("Fist_Grab", 1.0f);
            g.length = character_.clipDuration("Fist_Grab");
            return true;
        }

    // --- his guard: up while a fighting hunter faces him and he neither attacks nor runs ---
    bool facingFight = false;
    for (const auto& h : runtime_->hunters())
    {
        if (!h->alive() || h->action() != AiAction::Combat)
            continue;
        const Math::Vector3 to{h->feet().x - feet.x, 0.0f, h->feet().z - feet.z};
        const float32 dist = to.length();
        const Math::Vector3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)};
        if (dist < 3.0f && (dist < 0.2f || ahead.dot(to * (1.0f / dist)) > 0.5f))
            facingFight = true;
    }
    const Math::Vector3 v = playerCtrl_.velocity();
    // (No guard with a gun in hand: the guns have no block clips.)
    const bool gunUp = gun_.raised || gun_.busy > 0.0f || runtime_->playerGun();
    const bool wantGuard = facingFight && free && !gunUp && sinceAttack_ > 0.4f && std::sqrt(v.x * v.x + v.z * v.z) < 2.5f;
    guardUp_ = wantGuard ? guardUp_ + dt : 0.0f;
    guard_ = wantGuard && guardUp_ >= mine.blockSpeed;
    if (const int32 blocked = runtime_->takeBlockedHit(); blocked && overlayLeft_ <= 0.0f)
    {
        const std::string clip = stem + (blocked == 2 ? "_Block_Hit_Hard_T" : "_Block_Hit_Quick_T");
        if (character_.playOverlay(clip, false))
            overlayLeft_ = std::max(0.15f, character_.clipDuration(clip));
    }
    else if (overlayLeft_ <= 0.0f && !charge_.charging())
    {
        if (guard_)
            character_.playOverlay(stem + "_Block_T");
        else if (character_.overlayClip().find("_Block") != std::string::npos)
            character_.stopOverlay();
    }
    return false;
}

bool GameSession::loadGlobalModels()
{
    if (!globalModels_)
    {
        globalModels_ = DffLibrary::load(cfg_.gameRoot + "levels/GLOBAL/PAK/gmodelspc.dff");
        if (auto txd = Assets::Txd::load(cfg_.gameRoot + "levels/GLOBAL/PAK/gmodelspc.txd"))
            globalTextures_.add(*txd);
    }
    return globalModels_ != nullptr;
}

void GameSession::explodeHead(Hunter& h, bool scored)
{
    // The exe 0x49ccb0: the head entity is replaced by Explode_Head (then _2, _3 in turn;
    // EC_PEDHEAD, EXPLODEABLE, models DedHed1..3 in the global gmodelspc.dff) placed at the
    // victim's bone 1001 and burst at once (0x4f1ac0) into its EXPLODE_BIT_MODELs, DedBit1..4.
    // The head is gone; how the pieces fly is ours: up and away at 2..4 m/s, spinning, under
    // gravity, and they stay where they land.
    Math::Matrix4x4 bone;
    if (!h.model().boneTransform(kHunterHeadBone, bone) || !loadGlobalModels())
        return;
    std::string head = runtime_->hunterRecord(h);
    if (const std::size_t bod = head.find("Bod"); bod != std::string::npos)
        head.replace(bod, 3, "Hed");
    h.model().detach(head);
    h.model().detach("Bag_Head");
    if (scored)
        score_.headBurst(); // one style point (0x49ccb0; ours: not for the scripts' MutilateHunter)
    const Math::Vector3 at = bone.position();
    // It bursts with SFX_MISC_HEAD_SPLAT (0x4f1ac0 posts entity audio 0x16 for the ped: 0x44de60,
    // sample 228).
    playEntitySound(228, at);
    std::uniform_real_distribution<float32> unit(0.0f, 1.0f);
    for (int32 i = 1; i <= 4; ++i)
    {
        ModelDesc md;
        md.dffPath = cfg_.gameRoot + "levels/GLOBAL/PAK/gmodelspc.dff";
        md.library = globalModels_.get();
        md.clumpName = "DedBit" + std::to_string(i);
        md.textures = &globalTextures_;
        md.position = at;
        auto model = std::make_unique<Character>();
        if (!model->load(md, scene_))
            continue;
        HeadBit b;
        const float32 angle = unit(bitRng_) * 6.2831853f;
        const float32 speed = 2.0f + 2.0f * unit(bitRng_);
        b.position = at;
        b.velocity = {std::cos(angle) * speed * 0.6f, speed, std::sin(angle) * speed * 0.6f};
        b.yaw = angle;
        b.spin = (unit(bitRng_) - 0.5f) * 20.0f;
        b.floor = h.feet().y;
        b.model = std::move(model);
        headBits_.push_back(std::move(b));
    }
    std::cerr << "session: " << h.name() << "'s head bursts\n";
}

void GameSession::updateHeadBits(float32 dt)
{
    for (HeadBit& b : headBits_)
    {
        if (b.velocity.x != 0.0f || b.velocity.y != 0.0f || b.velocity.z != 0.0f)
        {
            b.velocity.y -= 9.81f * dt;
            b.position = b.position + b.velocity * dt;
            b.yaw += b.spin * dt;
            if (b.position.y <= b.floor)
            {
                b.position.y = b.floor;
                b.velocity = {0.0f, 0.0f, 0.0f};
                // A bit landing (its first two times) splats: one of SFX_MISC_BRAIN_SPLAT_1..5
                // heard within 15 m (0x59b32d posts 0x15: 0x44e400, 230 + rand % 5, 0x6b2ac0).
                if (b.landings++ < 2)
                    playEntitySound(230 + static_cast<int32>(bitRng_() % 5u), b.position, 15.0f);
            }
        }
        b.model->setPlacement(b.position, b.yaw);
        b.model->update(dt);
    }
}

void GameSession::updateExecution(float32 dt)
{
    updateHeadBits(dt);
    updateRats(dt);
    updateCrows(dt);
    updateLitter(dt);
    updatePropAnims(dt);
    if (killing())
    {
        Kill& k = kill_;
        k.t += dt;
        const ExecutionLevel& e = *k.plan.data;
        // A single-use weapon (bag, wire, shard) is used up: gone from his hand at
        // EXECUTE_PLAYER_WEAPON_DESTROY_TIME. The rest (bats, crowbars, blades) have none and
        // are kept.
        // The victim lets go of its weapon (EXECUTE_HUNTER_WEAPON_DROP_TIME); some kills take its
        // head off (EXECUTE_HEAD_SEVER_TIME: the wire's gruesome one at 6.8 s).
        if (!k.hunterDropped && k.t >= e.hunterWeaponDrop)
        {
            k.hunterDropped = true;
            runtime_->dropHunterWeapon(*k.hunter);
        }
        if (!k.headOff && e.headSever > 0.0f && k.t >= e.headSever)
        {
            k.headOff = true;
            runtime_->severHead(*k.hunter);
        }
        if (!k.headBurst && e.headExplode > 0.0f && k.t >= e.headExplode)
        {
            k.headBurst = true;
            explodeHead(*k.hunter);
        }
        if (!k.weaponGone && e.playerWeaponDestroy >= 0.0f && k.t >= e.playerWeaponDestroy)
        {
            k.weaponGone = true;
            runtime_->useUp(k.item);
            // The bag (CT_BAG) stays on the victim: the held weapon is destroyed and the victim's
            // head is replaced by the Bag_Head entity (EC_PEDHEAD, model Bag_Head in the global
            // gmodelspc.dff) - except on Ram_Bod1 (exe 0x47019e -> 0x49cc70).
            if (k.item->collectable == "CT_BAG" && !Assets::iequals(runtime_->hunterRecord(*k.hunter), "Ram_Bod1"))
            {
                std::string head = runtime_->hunterRecord(*k.hunter);
                if (const std::size_t bod = head.find("Bod"); bod != std::string::npos)
                    head.replace(bod, 3, "Hed");
                if (loadGlobalModels() &&
                    k.hunter->model().attach(*globalModels_, "Bag_Head", kHunterHeadBone, scene_, &globalTextures_))
                {
                    k.hunter->model().detach(head);
                    if (killModel_)
                        killModel_->setVisible(false); // (the weapon is gone; the head carries the bag)
                }
            }
        }
        if (killModel_)
        {
            // The weapon's model (EXECUTE_MODEL: the bag) is the held weapon itself with its clump
            // swapped in (exe 0x46d478 -> 0x4ca800), so it rides where the weapon is held:
            // the held weapon copies the holder's EXECUTE_BONE matrix every frame, the hand
            // weapons are held in (1075) when the record gives none, the holder's own matrix for
            // 1000 (0x4c8170). Its clip moves the bag's bones about that frame, and Cash's right
            // hand holds it over the hunter's head through the choke.
            const int32 bone = e.bone == 0 ? 1075 : e.bone;
            Math::Matrix4x4 carrier;
            if (character_.boneTransform(static_cast<int16>(bone == 1000 ? 10000 : bone), carrier))
                killModel_->setPlacementMatrix(carrier);
            killModel_->update(dt);
        }
        // The victim's half (Damage_<weapon>_Sneak_Attack<n>) is shorter than his: it goes
        // straight on into its Die_ clip (and dies) when that ends, not when his clip does.
        if (!k.victimDead && k.t >= k.hunter->model().clipDuration(k.plan.hunterClip))
        {
            k.victimDead = true;
            runtime_->finishExecution(*k.hunter, k.plan.dieClip);
        }
        if (k.t >= k.length)
        {
            if (killModel_)
            {
                killModel_->setVisible(false);
                spentModels_.push_back(std::move(killModel_));
            }
            if (!k.weaponGone && e.playerWeaponDestroy >= 0.0f)
                runtime_->useUp(k.item);
            if (!k.victimDead)
                runtime_->finishExecution(*k.hunter, k.plan.dieClip);
            // His clip has played out: free him (updateCombat, which counts the action lock down,
            // does not run during the kill, so the lock would otherwise hold him as long again).
            actionLeft_ = 0.0f;
            runtime_->setCamcorder(false); // (0x58fa5d)
            runtime_->setExecutionHandCam(false);
            std::cerr << "session: execution done\n";
            k = Kill{};
        }
        return;
    }
    // Lock on: an execution weapon in his hand, behind an unaware hunter. Once he is charging,
    // the hunter only escapes by noticing him or getting away (turning about does not; ours).
    const ItemType* item = runtime_->equipped();
    const bool able = item && item->canExecute() && deadFor_ < 0.0f && actionLeft_ <= 0.0f && !combo_.busy() && !gun_.raised;
    Hunter* kept = nullptr;
    if (able && charge_.charging() && execTarget_ && execTarget_->state() == HunterState::Normal && execTarget_->sight() < kMedium &&
        execTarget_->action() != AiAction::Chase && execTarget_->action() != AiAction::Combat)
    {
        const Math::Vector3 d = execTarget_->feet() - playerFeet();
        if (d.x * d.x + d.z * d.z < 4.0f)
            kept = execTarget_;
    }
    execTarget_ = kept ? kept : (able ? runtime_->executionTarget(playerFeet(), playerCtrl_.yaw()) : nullptr);
    if (!execTarget_)
        charge_.cancel();
    updateLockOnSpeech(dt);
    charge_.update(dt);
    // While charging, his arms take the hold-to-execute stance over whatever his legs are doing.
    if (charge_.charging())
    {
        const Math::Vector3 v = playerCtrl_.velocity();
        const float32 speed = std::sqrt(v.x * v.x + v.z * v.z);
        const char* gait = speed < 0.2f ? "Stand" : (speed < 2.0f ? "Walk" : "Run");
        character_.playOverlay(std::string("Melee_Hold_Execute_Sneak_") + gait + "_T");
    }
    else if (character_.overlayClip().rfind("Melee_Hold_Execute", 0) == 0)
        character_.stopOverlay(); // (only its own stance: the guard, hit reactions and guns keep theirs)
    // REELHUNT_AUTO_EXECUTE=<2..4>[!][@<s>]: execute at that level as soon as he locks on, or once
    // the level has run <s> seconds; with '!' the nearest hunter, wherever it stands (debugging;
    // the charge itself is the same as holding the button, see ExecutionCharge).
    if (item && !killing())
        if (const char* v = std::getenv("REELHUNT_AUTO_EXECUTE"))
            if (const char* at = std::strchr(v, '@'); !at || time_ >= static_cast<float32>(std::atof(at + 1)))
            {
                if (!execTarget_ && std::strchr(v, '!'))
                    execTarget_ = runtime_->nearestHunter(playerFeet());
                if (execTarget_)
                    startKill(std::atoi(v));
            }
}

void GameSession::startKill(int32 level)
{
    const ItemType* item = runtime_->equipped();
    if (!item || !execTarget_)
        return;
    const ExecutionPlan plan = planExecution(*item, level, levelName_, runtime_->hunterRecord(*execTarget_));
    const float32 length = character_.clipDuration(plan.playerClip);
    if (plan.level == 0 || length <= 0.0f)
    {
        std::cerr << "session: no execution " << level << " for " << item->record << " (clip '" << plan.playerClip << "')\n";
        return;
    }
    Kill& k = kill_;
    k = Kill{};
    runtime_->setCamcorder(true); // the execution camera films it (0x590440)
    runtime_->setExecutionHandCam(true);
    k.hunter = execTarget_;
    k.item = item;
    k.plan = plan;
    // The exe 0x46d325: the victim stays where it stands and he is moved behind it, facing
    // the way it faces: its position minus its facing times 1.0 m (1.35 m behind Pig_BodF1,
    // Piggsy). The paired clips share his new origin's frame with the victim's.
    const float32 behind = Assets::iequals(runtime_->hunterRecord(*k.hunter), "Pig_BodF1") ? 1.35f : 1.0f;
    k.yaw = k.hunter->yaw();
    k.hunterFeet = k.hunter->feet();
    k.feet = k.hunterFeet - Math::Vector3{-std::sin(k.yaw), 0.0f, -std::cos(k.yaw)} * behind;
    playerCtrl_.reset(k.feet + Math::Vector3{0.0f, playerField_.bodyHeight(), 0.0f}, k.yaw, playerCtrl_.pitch());
    k.length = length;
    // One of the camera record's sets of shots.
    for (const Assets::IniRecord& r : runtime_->cameraShots())
        if (Assets::iequals(r.name, plan.data->tvp))
        {
            const auto sets = parseCameraShots(r);
            if (!sets.empty())
                k.shots = sets[std::uniform_int_distribution<std::size_t>(0, sets.size() - 1)(killRng_)];
        }
    runtime_->setExecutionLevel(plan.level);
    runtime_->beginExecution(*k.hunter, k.hunterFeet, k.yaw, plan.hunterClip);
    startMilestones();
    milestones_.execution(plan.level); // its OnStartExecution counts for the director (0x5b62e0)
    score_.execution(plan.level);      // and its style points (0x48120a)
    character_.stopOverlay();
    playerAction(plan.playerClip, 1.0f);
    // The weapon's execution model (the bag pulled over the head) plays its clip from the same
    // origin. A weapon with a clip but no model of its own for the level (the shard's
    // Knife_Sneak_Attack2_W, the bats') plays it on the held weapon itself (0x4c90b3 binds it to
    // the weapon's own clump), posed from the clump's frames like the held one (frameRest).
    const std::string killClump = !plan.data->model.empty() ? plan.data->model
                                  : !plan.data->modelAnim.empty() ? item->model
                                                                  : std::string();
    if (!killClump.empty())
    {
        const DffLibrary* library = runtime_->models() ? runtime_->models() : weaponModels_.get();
        if (!library && (weaponModels_ = DffLibrary::load(levelDir_ + "pak/modelspc.dff")))
            library = weaponModels_.get();
        ModelDesc md;
        md.dffPath = levelDir_ + "pak/modelspc.dff";
        md.library = library;
        md.clumpName = killClump;
        md.textures = &assets_.level()->textures();
        md.position = k.hunterFeet;
        md.centerOnOrigin = false; // its clip is authored about the kill's origin
        md.frameRest = true;
        auto model = std::make_unique<Character>();
        if (library && model->load(md, scene_))
        {
            model->loadAnimations(animations_);
            if (!model->play(plan.data->modelAnim, false))
                std::cerr << "session: execution model " << killClump << " cannot play " << plan.data->modelAnim << "\n";
            model->setPlacement(k.feet, k.yaw);
            if (model->hasSkeleton())
                killModel_ = std::move(model);
            else
            {
                // Without a skeleton the clip has nothing to move.
                std::cerr << "session: execution model " << killClump << " has no skeleton; the held weapon stays\n";
                model->setVisible(false);
                spentModels_.push_back(std::move(model));
            }
        }
    }
    execTarget_ = nullptr;
    // Its sound: one take, EXECUTE/<folder>/<stem><level - 1>A (B in the Cerberus levels).
    if (plan.soundFile.empty())
        std::cerr << "session: no execution sound for " << item->record << "\n";
    else if (auto sound = Assets::loadExecutionSound(cfg_.gameRoot + "audio/PC/EXECUTE/" + plan.soundFile))
    {
        Audio::SoundRequest req;
        req.pcm = sound->samples.data();
        req.lengthBytes = sound->samples.size() * sizeof(int16_t);
        req.format = Audio::SampleFormat::Mono16;
        req.sampleRate = sound->sampleRate;
        req.priority = 1.0f;
        req.positional = false;
        (void)audio_.play(req); // OpenAL copies the samples
    }
    else
        std::cerr << "session: execution sound: " << sound.error().message << "\n";
    std::cerr << "session: execution " << level << " of " << k.hunter->name() << " with " << item->record << " ("
              << plan.playerClip << ", " << length << " s, camera " << plan.data->tvp << ", sound " << plan.soundFile << ")\n";
}

void GameSession::updateLockOnSpeech(float32 dt)
{
    LockOnSpeech& l = lockOnSpeech_;
    const bool on = execTarget_ != nullptr;
    if (!on)
        l.sinceOff += dt;
    if (l.max > 0 && l.played >= l.max)
        return;
    if (on == l.on)
        return;
    l.on = on;
    if (!on)
    {
        l.handled = false;
        l.sinceOff = 0.0f;
        return;
    }
    if (l.handled)
        return;
    const bool talking = directorLeft_ > 0.0f || !directorQueue_.empty();
    if (talking || l.sinceOff < 10.0f)
    {
        l.handled = true;
        return;
    }
    if (l.step == 0 || ++l.count < l.next)
        return;
    l.next += l.step;
    ++l.played;
    runtime_->playDirectorSpeech(0); // AISPEECH 0: SFX_D1V1_SCRIPTED_EXEC_RANDOM
}

void GameSession::updateDecals(float32 dt)
{
    for (const LevelRuntime::DecalRequest& r : runtime_->takeDecals())
        decals_.add(r.type, r.at, r.normal, r.size, -1.0f, decalRng_);
    // The blood under each body (0x4b30a0). Ours: it starts once the body has lain 1 s (the exe
    // waits for it to stop, +0x14), on the floor at its feet.
    auto bleed = [&](const void* body, const Math::Vector3& feet) {
        BloodPool& p = bloodPools_[body];
        p.deadFor += dt;
        if (p.deadFor < 1.0f || p.id == UINT32_MAX)
            return;
        if (p.id == 0)
        {
            // 0x49c6b1: 0.01 m, at most 1.8 + 0.4 x random.
            p.max = 1.8f + 0.4f * static_cast<float32>(std::uniform_int_distribution<int32>(0, 0x7ff)(decalRng_)) / 2047.0f;
            p.id = decals_.add(4, feet + Math::Vector3{0.0f, 0.1f, 0.0f}, {0.0f, 1.0f, 0.0f}, p.max, -1.0f, decalRng_);
            std::cerr << "session: blood pool at (" << feet.x << ", " << feet.z << ") y " << feet.y << (p.id ? "" : ": no floor") << "\n";
            if (p.id == 0)
            {
                p.id = UINT32_MAX;
                return;
            }
        }
        p.size = std::min(p.max, p.size + 0.3f * dt); // 0.01 a tick (0x7281b8)
        decals_.setScale(p.id, p.size / p.max);
    };
    for (const auto& h : runtime_->hunters())
        if (!h->alive() && !h->gone() && h.get() != carry_.body)
            bleed(h.get(), h->feet());
    if (deadFor_ >= 0.0f)
        bleed(this, playerFeet());
}

void GameSession::updateRats(float32 dt)
{
    if (!runtime_ || !assets_.level() || cutscene_.isActive())
        return; // (not under the intro video)
    const PlayerField* field = &playerField_;
    rats_.setLimit(runtime_->ratLimit());
    Rats::World w;
    w.floor = [field](float32 x, float32 top, float32 z, float32 depth) -> std::optional<Rats::Floor> {
        const Math::Vector3 a{x, top, z}, b{x, top - depth, z};
        const auto hit = field->shotHit(a, b);
        if (!hit)
            return std::nullopt;
        const float32 y = top - depth * hit->fraction;
        const auto light = field->lightBelow({x, y + 0.1f, z});
        const Math::Vector3 c = light ? *light : Math::Vector3{0.0f, 0.0f, 0.0f}; // (no prelight: dark)
        return Rats::Floor{y, c.x + c.y + c.z, c};
    };
    w.clear = [field](const Math::Vector3& a, const Math::Vector3& b) { return field->lineOfSight(a, b); };
    // The camera, at his eye height (ours: our camera rides higher than the exe's, and a floor
    // search from 2 m above it would start over the ceiling and land rats on the storey above).
    w.camera = playerCtrl_.cameraPosition();
    w.camera.y = playerFeet().y + 1.6f;
    w.cameraYaw = std::atan2(-std::sin(playerCtrl_.yaw()), -std::cos(playerCtrl_.yaw()));
    w.player = playerFeet();
    w.playerAlive = runtime_->playerAlive();
    for (const auto& h : runtime_->hunters())
        if (!h->alive() && !h->gone())
            w.bodies.push_back(h->feet());
    w.nowMs = static_cast<uint32>(time_ * 1000.0f);
    if (std::getenv("REELHUNT_RATS_NEAR"))
        w.spawnRange = 4.0f; // (testing: they appear 4 m from the camera)
    // Squashed under foot (0x5cf8c0).
    for (const Math::Vector3& at : rats_.update(dt, w, ratRng_))
        critterDies(at, 1);
    // Drawn 0.1 m up, laid as a level prop (Z up), turned to where it heads (0x5cf3d0).
    for (int32 i = 0; i < Rats::kMax; ++i)
    {
        const Rats::Rat& r = rats_.rats()[static_cast<std::size_t>(i)];
        auto& model = ratModels_[static_cast<std::size_t>(i)];
        if (!r.active)
        {
            if (model)
                model->setVisible(false);
            continue;
        }
        if (!model)
        {
            if (!loadGlobalModels())
                return;
            ModelDesc md;
            md.dffPath = cfg_.gameRoot + "levels/GLOBAL/PAK/gmodelspc.dff";
            md.library = globalModels_.get();
            md.clumpName = "CJ_RAT";
            md.textures = &globalTextures_;
            md.flipForVulkan = false;
            md.centerOnOrigin = false;
            model = std::make_unique<Character>();
            if (!model->load(md, scene_))
            {
                model.reset();
                continue;
            }
        }
        const Math::Vector3 at = r.pos + Math::Vector3{0.0f, 0.1f, 0.0f};
        model->setPlacementMatrix(Math::Matrix4x4::translation(at) * Math::Matrix4x4::rotationY(rats_.drawYaw(i, w.nowMs)) *
                                  LevelFrame::placementRotation({0.0f, 0.0f, 0.0f, 1.0f}) *
                                  Math::Matrix4x4::scale(Rats::scale(i)));
        model->setAmbientLight({r.colour.x, r.colour.y, r.colour.z, 1.0f});
        model->setVisible((r.pos - w.camera).length() < 27.0f);
        model->update(dt);
    }
}

void GameSession::critterDies(const Math::Vector3& at, int32 bloodType)
{
    // A squeal (#230..234), FXRAT1 there, blood 0.7..0.9 m (blood for a rat, blood2 for a crow).
    playSample("#" + std::to_string(230 + std::uniform_int_distribution<int32>(0, 4)(ratRng_)), at, 0.5f, true);
    if (runtime_)
        runtime_->playFx("FXRAT1", at);
    decals_.add(bloodType, at + Math::Vector3{0.0f, 0.1f, 0.0f}, {0.0f, 1.0f, 0.0f},
                0.7f + 0.2f * static_cast<float32>(std::uniform_int_distribution<int32>(0, 0x7ff)(ratRng_)) / 2047.0f, -1.0f,
                decalRng_);
}

void GameSession::updateCrows(float32 dt)
{
    if (!runtime_ || !assets_.level() || cutscene_.isActive())
        return;
    const PlayerField* field = &playerField_;
    const Math::Vector3 camera = playerCtrl_.cameraPosition();
    Crows::World w;
    w.clear = [field](const Math::Vector3& a, const Math::Vector3& b) { return field->lineOfSight(a, b); };
    w.camera = camera;
    w.cameraLook = {-std::sin(playerCtrl_.yaw()), 0.0f, -std::cos(playerCtrl_.yaw())};
    w.player = playerFeet();
    w.openSky = runtime_->shelter() < 0.1f; // out under the sky (0x7b3278)
    for (const auto& h : runtime_->hunters())
        if (!h->alive() && !h->gone())
            w.bodies.push_back(h->feet());
    w.nowMs = static_cast<uint32>(time_ * 1000.0f);
    crows_.update(dt, w, ratRng_);

    const Renderer::Texture* cloud = hud_.texture("cloud");
    for (int32 i = 0; i < Crows::kMax; ++i)
    {
        const Crows::Crow& c = crows_.crows()[static_cast<std::size_t>(i)];
        auto& model = crowModels_[static_cast<std::size_t>(i)];
        auto& wings = crowWings_[static_cast<std::size_t>(i)];
        if (!wings[0])
            for (int32 k = 0; k < 4; ++k)
            {
                // A unit quad: x 0..1 out along the span, z -0.5..0.5 across the chord; the inner
                // pieces take v 0.5..0.75 of the texture, the outer 0.75..1.
                const float32 v0 = k < 2 ? 0.5f : 0.75f, v1 = v0 + 0.25f;
                auto mesh = std::make_unique<Renderer::Mesh>();
                const Math::Vector3 n{0.0f, 1.0f, 0.0f};
                mesh->addVertex({0.0f, 0.0f, 0.5f}, n, {0.0f, v0});
                mesh->addVertex({0.0f, 0.0f, -0.5f}, n, {1.0f, v0});
                mesh->addVertex({1.0f, 0.0f, 0.5f}, n, {0.0f, v1});
                mesh->addVertex({1.0f, 0.0f, -0.5f}, n, {1.0f, v1});
                mesh->addTriangle(0, 3, 1);
                mesh->addTriangle(0, 2, 3);
                mesh->computeBoundingBox();
                Renderer::Material m = Renderer::Material::defaultMaterial();
                m.setFlag(Engine::RenderMode::BackCull, false);
                m.setFlag(Engine::RenderMode::BlendOn, true);
                if (cloud)
                    m.setTexture(*cloud);
                m.setDiffuse(Math::Vector4{0.0f, 0.0f, 0.0f, 1.0f});
                mesh->setMaterial(m);
                mesh->setLighting(Renderer::MeshLighting::Unlit);
                mesh->setCastsShadow(false);
                mesh->setVisible(false);
                wings[static_cast<std::size_t>(k)] = mesh.get();
                scene_.addEntity(std::move(mesh));
            }
        if (!c.active)
        {
            if (model)
                model->setVisible(false);
            for (Renderer::Mesh* m : wings)
                m->setVisible(false);
            continue;
        }
        if (!model)
        {
            if (!loadGlobalModels())
                return;
            ModelDesc md;
            md.dffPath = cfg_.gameRoot + "levels/GLOBAL/PAK/gmodelspc.dff";
            md.library = globalModels_.get();
            md.clumpName = "CJ_CROW";
            md.textures = &globalTextures_;
            md.flipForVulkan = false;
            md.centerOnOrigin = false;
            model = std::make_unique<Character>();
            if (!model->load(md, scene_))
            {
                model.reset();
                continue;
            }
        }
        const Math::Vector3 at = c.pos + Math::Vector3{0.0f, crows_.bob(i, w.nowMs), 0.0f};
        model->setPlacementMatrix(Math::Matrix4x4::translation(at) * Math::Matrix4x4::rotationY(c.yaw) *
                                  LevelFrame::placementRotation({0.0f, 0.0f, 0.0f, 1.0f}));
        if (const auto light = field->lightBelow(c.pos))
            model->setAmbientLight({light->x, light->y, light->z, 1.0f});
        const float32 dist = (c.pos - camera).length();
        model->setVisible(dist < 27.0f);
        model->update(dt);
        // The wings (0x5d0fc0): its right (cos yaw, 0, -sin yaw), up, and ahead (sin yaw, 0, cos yaw).
        float32 ex, ey, tx, ty;
        crows_.wing(i, w.nowMs, ex, ey, tx, ty);
        const bool flying = c.state == 1 || c.state == 2;
        const float32 fade = dist < 18.0f ? 1.0f : std::max(0.0f, 1.0f - (dist - 18.0f) / 9.0f);
        const Math::Vector3 right{std::cos(c.yaw), 0.0f, -std::sin(c.yaw)}, up{0.0f, 1.0f, 0.0f},
            ahead{std::sin(c.yaw), 0.0f, std::cos(c.yaw)};
        for (int32 k = 0; k < 4; ++k)
        {
            Renderer::Mesh* mesh = wings[static_cast<std::size_t>(k)];
            const float32 side = (k & 1) ? -1.0f : 1.0f;
            const float32 ax = k < 2 ? 0.0f : ex, ay = k < 2 ? 0.0f : ey, bx = k < 2 ? ex : tx, by = k < 2 ? ey : ty;
            const Math::Vector3 a = at + right * (ax * side) + up * ay;
            const Math::Vector3 span = right * ((bx - ax) * side) + up * (by - ay);
            const Math::Vector3 chord = ahead * 0.5f;
            const Math::Vector3 normal = span.cross(chord);
            Math::Matrix4x4 t = Math::Matrix4x4::identity();
            t.m00 = span.x; t.m10 = span.y; t.m20 = span.z;
            t.m01 = normal.x; t.m11 = normal.y; t.m21 = normal.z;
            t.m02 = chord.x; t.m12 = chord.y; t.m22 = chord.z;
            t.m03 = a.x; t.m13 = a.y; t.m23 = a.z;
            mesh->setTransform(t);
            Renderer::Material m = mesh->material();
            m.setDiffuse(Math::Vector4{0.0f, 0.0f, 0.0f, fade});
            mesh->setMaterial(m);
            mesh->setVisible(flying && fade > 0.0f && span.length() > 1e-4f);
        }
    }
}

void GameSession::updatePropAnims(float32 dt)
{
    if (!runtime_ || !assets_.level())
        return;
    for (const LevelRuntime::PropAnim& pa : runtime_->takePropAnims())
    {
        std::unique_ptr<Character>& model = propModels_[pa.placement->index];
        if (!model)
        {
            // Its own clump (named as its record), posed from its frames, playing the level's
            // EntityAnims.
            const DffLibrary* library = runtime_->models();
            ModelDesc md;
            md.dffPath = levelDir_ + "pak/modelspc.dff";
            md.library = library;
            md.clumpName = pa.placement->record;
            md.textures = &assets_.level()->textures();
            md.flipForVulkan = false;
            md.centerOnOrigin = false;
            md.frameRest = true;
            model = std::make_unique<Character>();
            if (!library || !model->load(md, scene_))
            {
                std::cerr << "session: prop " << pa.placement->record << " has no model to animate\n";
                model.reset();
                propModels_.erase(pa.placement->index);
                continue;
            }
            model->loadAnimations(animations_);
            model->setAnimationBlock("EntityAnims");
            for (Renderer::Mesh* m : assets_.level()->meshesOf(*pa.placement))
                m->setVisible(false);
        }
        model->setPlacementMatrix(Math::Matrix4x4::translation(pa.at) * LevelFrame::placementRotation(pa.placement->rotation));
        if (!model->play(pa.clip, pa.loop))
            std::cerr << "session: prop " << pa.placement->record << " cannot play " << pa.clip << "\n";
        else
            std::cerr << "session: prop " << pa.placement->record << " plays " << pa.clip << (pa.loop ? " (loop)" : "") << " at (" << pa.at.x << ", " << pa.at.y << ", " << pa.at.z << ")" << "\n";
    }
    for (auto& [index, model] : propModels_)
    {
        model->setVisible(!runtime_->propHidden(index));
        model->update(dt);
    }
}

void GameSession::updateLitter(float32 dt)
{
    if (!runtime_ || !assets_.level() || cutscene_.isActive())
        return;
    const PlayerField* field = &playerField_;
    const Math::Vector3 camera = camera_->position();
    Litter::World w;
    w.hit = [field](const Math::Vector3& a, const Math::Vector3& b) -> std::optional<Litter::Hit> {
        const auto h = field->shotHit(a, b);
        if (!h)
            return std::nullopt;
        return Litter::Hit{a + (b - a) * h->fraction, h->normal};
    };
    w.camera = camera;
    w.cameraLook = {-std::sin(camera_->yaw()) * std::cos(camera_->pitch()), std::sin(camera_->pitch()),
                    -std::cos(camera_->yaw()) * std::cos(camera_->pitch())};
    w.scripted = runtime_->scriptCamera().active;
    w.wind = runtime_->wind();
    w.nowMs = static_cast<uint32>(time_ * 1000.0f);
    litter_.setOn(runtime_->litterOn());
    litter_.update(dt, w, litterRng_);

    // The sheets (0x5c8800): newspaper01..04 by eights, half sizes and colours by group.
    static constexpr float32 kHalf[4][2] = {{0.15f, 0.07f}, {0.23f, 0.17f}, {0.15f, 0.15f}, {0.15f, 0.15f}};
    static constexpr uint8 kColour[3][3][8] = {
        {{132, 102, 120, 96, 120, 94, 79, 120}, {120, 120, 120, 96, 96, 105, 121, 98}, {120, 102, 102, 120, 103, 120, 105, 98}},
        {{89, 89, 89, 89, 89, 89, 89, 89}, {89, 89, 89, 89, 89, 89, 89, 89}, {89, 89, 89, 89, 89, 89, 89, 89}},
        {{89, 124, 61, 22, 53, 19, 83, 89}, {90, 99, 47, 38, 71, 48, 82, 90}, {31, 29, 8, 24, 53, 19, 38, 31}}};
    for (int32 i = 0; i < Litter::kSheets; ++i)
    {
        const int32 group = i / 8;
        Renderer::Mesh*& mesh = litterSheets_[static_cast<std::size_t>(i)];
        if (!mesh)
        {
            // A unit quad: x and z -1..1, its corners (1, 1) (1, -1) (-1, 1) (-1, -1) at uv (0, 0)
            // (1, 0) (0, 1) (1, 1).
            auto quad = std::make_unique<Renderer::Mesh>();
            const Math::Vector3 n{0.0f, 1.0f, 0.0f};
            quad->addVertex({1.0f, 0.0f, 1.0f}, n, {0.0f, 0.0f});
            quad->addVertex({1.0f, 0.0f, -1.0f}, n, {1.0f, 0.0f});
            quad->addVertex({-1.0f, 0.0f, 1.0f}, n, {0.0f, 1.0f});
            quad->addVertex({-1.0f, 0.0f, -1.0f}, n, {1.0f, 1.0f});
            quad->addTriangle(0, 2, 1);
            quad->addTriangle(1, 2, 3);
            quad->computeBoundingBox();
            Renderer::Material m = Renderer::Material::defaultMaterial();
            m.setFlag(Engine::RenderMode::BackCull, false);
            m.setFlag(Engine::RenderMode::BlendOn, true);
            if (const Renderer::Texture* tex = hud_.texture("newspaper0" + std::to_string(group + 1)))
                m.setTexture(*tex);
            quad->setMaterial(m);
            quad->setLighting(Renderer::MeshLighting::Unlit);
            quad->setCastsShadow(false);
            quad->setVisible(false);
            mesh = quad.get();
            scene_.addEntity(std::move(quad));
        }
        const int32 alpha = litter_.drawAlpha(i, camera);
        mesh->setVisible(alpha > 0);
        if (alpha <= 0)
            continue;
        const Litter::Sheet& s = litter_.sheets()[static_cast<std::size_t>(i)];
        const Math::Vector3 p = litter_.position(i);
        const float32 sx = (0.7f + 0.1f * static_cast<float32>(i & 3)) * kHalf[group][0];
        const float32 sz = (0.7f + 0.1f * static_cast<float32>((i >> 1) & 3)) * kHalf[group][1];
        const Math::Vector3 a{sx * std::sin(s.angle), 0.0f, sx * std::cos(s.angle)};
        const Math::Vector3 b{sz * std::cos(s.angle), 0.0f, -sz * std::sin(s.angle)};
        Math::Matrix4x4 t = Math::Matrix4x4::identity();
        t.m00 = a.x; t.m10 = a.y; t.m20 = a.z;
        t.m01 = 0.0f; t.m11 = 1.0f; t.m21 = 0.0f;
        t.m02 = b.x; t.m12 = b.y; t.m22 = b.z;
        t.m03 = p.x; t.m13 = p.y; t.m23 = p.z;
        mesh->setTransform(t);
        const auto& colour = kColour[group == 0 ? 0 : group == 1 ? 1 : 2];
        Renderer::Material m = mesh->material();
        m.setDiffuse(Math::Vector4{colour[0][i & 7] / 255.0f, colour[1][i & 7] / 255.0f, colour[2][i & 7] / 255.0f,
                                   static_cast<float32>(alpha) / 255.0f});
        mesh->setMaterial(m);
    }
}

void GameSession::startMilestones()
{
    // Level start (0x5b5cd0): the milestones for this scene, X / KT scaled by its hunters
    // (0x7917e4, ours: the hunters it has on its first frame).
    if (milestonesStarted_)
        return;
    milestonesStarted_ = true;
    // A checkpoint resumed in this run brings the save point's counts back (0x5b5a60).
    milestoneCounts_ = checkpoint_ && checkpoint_->directorCounts ? *checkpoint_->directorCounts : std::array<int32, 4>{};
    milestones_.start(sceneIndex_, static_cast<int32>(runtime_->hunters().size()), directorRng_, milestoneCounts_);
    std::cerr << "session: director milestones (scene " << sceneIndex_ << ", " << runtime_->hunters().size() << " hunters)";
    for (int32 i = 0; i < 14; ++i)
        std::cerr << (i == 0 ? " X" : i == 3 ? " KT" : i == 6 ? " H" : i == 9 ? " S" : "") << ' ' << milestones_.milestone(i);
    std::cerr << "\n";
}

void GameSession::updateMilestones(float32 dt)
{
    if (!levelMode_)
        return;
    startMilestones();
    for (int32 n = runtime_->takeHunterDeaths(); n > 0; --n)
    {
        milestones_.kill();
        score_.kill(); // 0x4ecdea
    }
    DirectorMilestones::Frame f;
    f.dt = dt;
    f.hidden = runtime_->playerHidden();
    f.seen = runtime_->playerSpotted();
    f.cutscene = runtime_->inCutscene() || cutscene_.isActive();
    f.talking = directorLeft_ > 0.0f || !directorQueue_.empty() || runtime_->directorSpeechPending();
    if (sceneIndex_ == 0) // only scene 1 asks (0x414670)
    {
        const NavGraph& nav = runtime_->nav();
        const int32 n = nav.nearest(playerFeet());
        f.inArena = n >= 0 && Assets::iequals(nav.areaName(nav.node(n).area), "aiarena");
    }
    const int32 id = milestones_.update(f);
    std::array<int32, DirectorMilestones::kStats> now{};
    for (int32 s = 0; s < DirectorMilestones::kStats; ++s)
        now[static_cast<std::size_t>(s)] = milestones_.count(static_cast<DirectorMilestones::Stat>(s));
    if (now != milestoneCounts_)
    {
        milestoneCounts_ = now;
        std::cerr << "session: director stats X " << now[0] << " H " << now[1] << " S " << now[2] << " KT " << now[3] << "\n";
    }
    if (id >= 0)
        runtime_->playDirectorSpeech(id);
}

float32 GameSession::viewFov(float32 fov) const
{
    // RATIO WIDESCREEN: the exe's view window goes from 0.7 x 0.546875 to 0.74 x 0.486875
    // (0x475bc0), for a 16:9 picture squeezed into 4:3. The window here already has its own
    // aspect, so the setting keeps the shorter vertical view.
    if (cfg_.settings.ratio != 1)
        return fov;
    const float32 half = std::tan(Math::radians(fov) * 0.5f) * (0.486875f / 0.546875f);
    return Math::degrees(std::atan(half) * 2.0f);
}

void GameSession::shakeByHand(float32 dt, float32& yaw, float32& pitch, float32& roll)
{
    const LevelRuntime::ScriptLook& look = runtime_->look();
    if (!look.handCam)
        return;
    handCam_.update(dt, yaw, pitch, look.handCamLook, look.handCamRoll, handCamRng_);
    yaw += handCam_.yaw();
    pitch += handCam_.pitch();
    roll += handCam_.roll();
}

void GameSession::updateScreenEffects(float32 dt)
{
    Renderer::ScreenEffects& fx = scene_.effects();
    fx = Renderer::ScreenEffects{};
    fx.time = time_;
    fx.gamma = cfg_.settings.gammaExponent(); // BRIGHTNESS
    if (!runtime_)
        return;
    // The camcorder look of the scripted cut-scenes (grain and interlace lines with
    // HandCamSetAllEffects, a vignette with HandCamSetOtherEffects; amounts ours) and the
    // white-noise bursts at their camera cuts.
    const LevelRuntime::ScriptLook& look = runtime_->look();
    const bool cut = runtime_->inCutscene();
    fx.noise = look.noise;
    fx.grain = cut && look.allEffects ? 0.5f : 0.0f;
    fx.scanlines = cut && look.allEffects ? 0.6f : 0.0f;
    fx.vignette = cut && look.otherEffects ? 0.6f : 0.0f;
    // An execution is filmed like a cut-scene: grain, interlace lines, static at the cuts.
    cutNoise_ = std::max(0.0f, cutNoise_ - dt * 2.0f);
    if (killing())
    {
        fx.grain = 0.5f;
        fx.scanlines = 0.6f;
        fx.vignette = 0.4f;
    }
    fx.noise = std::max(fx.noise, cutNoise_);
    // NOISE FX off: no static; CAMERA FX off: no camcorder grain, lines or vignette.
    if (!cfg_.settings.noiseFx)
        fx.noise = 0.0f;
    if (!cfg_.settings.cameraFx)
        fx.grain = fx.scanlines = fx.vignette = 0.0f;
    // SetLevelCompleted: fade to black, then the next scene loads.
    if (runtime_->levelCompleted())
    {
        if (outroFor_ < 0.0f)
        {
            outroFor_ = 0.0f;
            std::cerr << "session: level complete, fading out\n";
        }
        outroFor_ += dt;
        fx.fade = {0.0f, 0.0f, 0.0f, std::min(1.0f, outroFor_ / kOutroSeconds)};
    }
}

void GameSession::setLighting(Renderer::LightingMode mode)
{
    using M = Renderer::LightingMode;
    lightingMode_ = mode;
    Renderer::SceneLighting& lighting = scene_.lighting();
    lighting.mode = mode;
    switch (mode)
    {
    case M::Midday: // debug daylight
        lighting.ambient = Math::Vector3{0.5f, 0.5f, 0.5f};
        lighting.sunDirection = Math::Vector3{-0.2f, -1.0f, -0.4f};
        lighting.sunColor = Math::Vector3{1.0f, 0.95f, 0.8f};
        lighting.sunIntensity = 4.0f;
        break;
    case M::Dynamic: // night without the prelight: a cold moon, a dim ambient, the lamps (ours)
        lighting.ambient = Math::Vector3{0.025f, 0.03f, 0.045f};
        lighting.sunDirection = Math::Vector3{0.35f, -1.0f, 0.25f};
        lighting.sunColor = Math::Vector3{0.45f, 0.55f, 0.8f};
        lighting.sunIntensity = 0.8f;
        break;
    case M::Baked:
    case M::Mixed:
        lighting.ambient = levelAmbient_;
        break;
    }
    std::cerr << "session: lighting " << Renderer::lightingModeName(mode) << "\n";
}

void GameSession::releaseMouse()
{
    if (mouseCaptured_)
        SDL_SetWindowRelativeMouseMode(renderer_.device()->window(), false);
    mouseCaptured_ = false;
}

} // namespace Game
