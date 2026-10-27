// main.cpp - engine entry point: the frontend menu and the game sessions it starts.
#include "engine_types.h"
#include "Audio/audio.h"
#include "Core/asset_manager.h"
#include "Core/platform.h"
#include "Game/app_config.h"
#include "Game/game_session.h"
#include "Game/launcher.h"
#include "Game/bindings_sdl.h"
#include "Game/exe_tables.h"
#include "Game/main_menu.h"
#include "Game/perf_overlay.h"
#include "Game/progress.h"
#include "Game/save_slots.h"
#include "Renderer/renderer.h"

#include <cstdlib>
#include <SDL3/SDL.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr double kMaxFrameDt = 0.1; // clamp long frames (debugger, load hitches)

    std::optional<Game::MenuInput> menuInputFor(const SDL_Event& evt)
    {
        if (evt.type != SDL_EVENT_KEY_DOWN)
            return std::nullopt;
        switch (evt.key.key)
        {
        case SDLK_UP:
        case SDLK_W:
            return Game::MenuInput::Up;
        case SDLK_DOWN:
        case SDLK_S:
            return Game::MenuInput::Down;
        case SDLK_LEFT:
        case SDLK_A:
            return Game::MenuInput::Left;
        case SDLK_RIGHT:
        case SDLK_D:
            return Game::MenuInput::Right;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            return evt.key.repeat ? std::nullopt : std::optional(Game::MenuInput::Confirm);
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            return evt.key.repeat ? std::nullopt : std::optional(Game::MenuInput::Back);
        default:
            return std::nullopt;
        }
    }
} // namespace

int main(int argc, char** argv)
{
    Game::AppConfig cfg = Game::defaultConfig();
    {
        bool help = false;
        const std::vector<std::string> args(argv + 1, argv + argc);
        if (const auto err = Game::parseCommandLine(args, cfg, help))
        {
            std::cerr << "main: " << *err << "\n" << Game::commandLineHelp();
            return 2;
        }
        if (help)
        {
            std::cout << Game::commandLineHelp();
            return 0;
        }
    }
    std::cerr << "main: game root " << cfg.gameRoot << "\n";
    if (!Engine::Platform::isGameRoot(cfg.gameRoot))
    {
        // Without the original data there is nothing to run: say where it was looked for.
        const std::string msg = "The game data was not found in\n" + cfg.gameRoot +
                                "\n\nPut Reelhunt in the game's folder (next to its executable, with levels, "
                                "audio and pictures), or start it with --game-root <folder> or REELHUNT_GAME_ROOT.";
        std::cerr << "main: " << msg << "\n";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Reelhunt", msg.c_str(), nullptr);
        return 1;
    }
    // The tables that live in the game's executable are read from the player's own copy.
    {
        auto tables = Game::findGameExe(cfg.gameRoot);
        const std::string dir = cfg.userDir + "exe_tables/";
        if (!tables || !Game::writeExeTables(*tables, dir))
        {
            const std::string msg = tables ? "Cannot write to " + dir : tables.error();
            std::cerr << "main: " << msg << "\n";
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Reelhunt", msg.c_str(), nullptr);
            return 1;
        }
        std::cerr << "main: tables from " << tables->exePath << "\n";
        cfg.directorSpeechPath = dir + "director_speech.txt";
        cfg.hunterSpeechPath = dir + "hunter_speech.txt";
        cfg.pedAnimsPath = dir + "ped_anims.txt";
        if (cfg.saveDir.empty())
            cfg.saveDir = Game::gameSaveDir(tables->userFilesDir, tables->saveGamesDir);
        Game::setGameExe(std::move(*tables));
    }

    // The launcher (Game/launcher.h): the display adapter and the fullscreen screen mode.
    Renderer::Backend::VulkanCreateParams video{.enableValidation = cfg.validation};
    {
        const std::vector<Game::DisplayAdapter> adapters = Game::enumerateAdapters();
        Game::VideoChoice choice = Game::loadVideoChoice(cfg.videoChoicePath()).value_or(Game::VideoChoice{});
        if (cfg.launcher == Game::AppConfig::Launcher::Show)
        {
            const auto picked = Game::runLauncher(Game::gameExe().exePath, adapters, choice);
            if (!picked)
                return 0; // Quit
            choice = *picked;
            if (!Game::saveVideoChoice(cfg.videoChoicePath(), choice))
                std::cerr << "main: could not save " << cfg.videoChoicePath() << "\n";
        }
        if (!adapters.empty())
        {
            const auto device = static_cast<std::size_t>(std::clamp<int32>(choice.device, 0, static_cast<int32>(adapters.size()) - 1));
            const Game::DisplayAdapter& adapter = adapters[device];
            video.display = adapter.id;
            if (cfg.launcher != Game::AppConfig::Launcher::Windowed)
            {
                // 0x4c0910: the chosen mode, fullscreen, at the refresh rate 0x4c0830 picks.
                const std::vector<Game::ScreenMode> rows = Game::launcherModes(adapter.modes);
                if (!rows.empty())
                {
                    const Game::ScreenMode& mode = rows[static_cast<std::size_t>(Game::launcherModeRow(rows, choice))];
                    video.fullscreen = true;
                    video.refresh = Game::launcherRefresh(adapter.modes, mode);
                    cfg.width = mode.width;
                    cfg.height = mode.height;
                }
            }
            std::cerr << "main: display " << adapter.name << " (" << adapter.monitor << "), "
                      << (video.fullscreen ? Game::modeLabel({cfg.width, cfg.height, 32u, 0.0f}) : std::string("windowed")) << "\n";
        }
    }

    Renderer::Renderer renderer;
    if (!renderer.initialize(cfg.width, cfg.height, video))
    {
        std::cerr << "main: Vulkan initialisation failed\n";
        return 1;
    }
    Audio::AudioEngine audio;
    (void)audio.init(); // The game runs silently without an audio device
    AssetManager assets;
    assets.loadSharedAssets(cfg.gameRoot);

    // Settings.dat (0x605e50) in the save folder, else the exe's defaults.
    Game::SettingsFile settingsFile = Game::SettingsFile::defaults();
    if (!settingsFile.load(cfg.settingsPath()))
        settingsFile = Game::SettingsFile::defaults();
    cfg.settings = settingsFile.settings;
    cfg.bindings = &settingsFile.bindingsA; // the keys (Game/bindings.h), REMAP CONTROLS edits them

    Game::MainMenu menu;
    if (!menu.load(cfg.gameRoot, cfg.saveDir))
        std::cerr << "main: WARNING - frontend assets incomplete\n";
    menu.setAspect(static_cast<float32>(cfg.height) / static_cast<float32>(cfg.width));
    menu.setSettings(&cfg.settings);
    menu.setBindings(&settingsFile.bindingsA);
    menu.setPadBindings(&settingsFile.bindingsB);
    // The pad (Game/bindings_sdl.h): read through Settings.dat's second table.
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    Game::setPadTable(&settingsFile.bindingsB);
    if (std::getenv("REELHUNT_VPAD"))
        Game::attachTestPad(); // (testing: a virtual pad; the session tilts its stick)
    if (std::getenv("REELHUNT_REMAP"))
        menu.openRemap(); // (testing: the REMAP CONTROLS page)
    // The scenes done (Game/progress.h), kept by saves; the save slots (Game/save_slots.h).
    Game::GameProgress progress;
    progress.setUnlockAll(cfg.unlockAll);
    menu.setProgress(&progress);
    Game::SaveSlots slots;
    slots.setDirs(cfg.saveDir, "");
    slots.setBindings(&settingsFile.bindingsA);
    menu.setSaveSlots(&slots);

    std::unique_ptr<Game::GameSession> session;
    Game::SessionStart current;
    // The last save point's checkpoint, kept in memory as the exe keeps its buffers: PLAY goes
    // back to it while the scene's state (0x7d6ae8) is that scene.
    std::shared_ptr<const Game::SaveFile> heldCheckpoint;
    auto startSession = [&](Game::SessionStart start) {
        int32 sceneIndex = -1;
        for (const auto& s : menu.scenes())
            if (s.folder == start.level)
                sceneIndex = s.number - 1;
        // Not from a checkpoint: the inventory the scene before was won with, where it carries
        // (0x5d2590).
        if (!start.checkpoint && start.savePath.empty() && !start.characterOnly && sceneIndex >= 0)
            if (const auto* carried = progress.carriedInto(sceneIndex))
                start.carried = *carried;
        current = start;
        // One frame of the loading page, then block while the level loads.
        menu.drawLoading(renderer.ui(), start.level);
        renderer.beginFrame();
        renderer.renderScene(renderer.scene());
        renderer.present();
        session.reset();
        session = std::make_unique<Game::GameSession>(cfg, renderer, audio, assets);
        session->start(start);
        // The scene now played (0x7d6ae4); its state (0x7d6ae8) the scene when it starts from a
        // checkpoint, else -1 until a save point.
        if (session->checkpoint())
            heldCheckpoint = session->checkpoint();
        if (sceneIndex >= 0)
            progress.setLast(sceneIndex, session->checkpoint() ? sceneIndex : Game::GameProgress::kSceneDone);
    };
    auto sceneFolder = [&](int number) -> std::string {
        const auto& scenes = menu.scenes();
        if (number >= 1 && static_cast<std::size_t>(number) <= scenes.size())
            return scenes[static_cast<std::size_t>(number - 1)].folder;
        return cfg.level;
    };

    if (cfg.characterOnly)
        startSession({.level = cfg.level, .savePath = {}, .characterOnly = true, .checkpoint = nullptr});
    else if (cfg.skipMenu)
        startSession({.level = cfg.level, .savePath = cfg.savePath, .characterOnly = false, .checkpoint = nullptr});

    Game::PerfOverlay perf;
    perf.setShown(cfg.perfOverlay);

    uint64 lastTimeNs = SDL_GetTicksNS();
    bool running = true;
    bool paused = false;        // the in-game menu or the results are open over the session
    bool continueScene = false; // the results were confirmed: on to the next scene
    bool failed = false;        // Scene Failed is up over the session (it keeps running)
    while (running)
    {
        SDL_Event evt;
        while (SDL_PollEvent(&evt))
        {
            Game::padEvent(evt);
            if (evt.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            else if (evt.type == SDL_EVENT_KEY_DOWN && !evt.key.repeat && evt.key.key == SDLK_F7)
            {
                perf.toggle(); // frame-time counter
            }
            else if (session && !paused && !failed)
            {
                // ESC opens the in-game menu (exe page 8) with the level held.
                if (evt.type == SDL_EVENT_KEY_DOWN && !evt.key.repeat && evt.key.key == SDLK_ESCAPE && !session->playingIntro())
                {
                    paused = true;
                    session->pause();
                    audio.setPaused(true);
                    menu.openPause(session->levelGoals(), session->checkpoint());
                }
                else
                {
                    session->handleEvent(evt);
                }
            }
            else if (menu.capturingKey())
            {
                // REMAP CONTROLS waits for a key: the next key, button or wheel step (a pad button
                // goes to the pad's table).
                if (const int32 code = Game::codeOfEvent(evt); code != 0)
                    menu.captureKey(code);
                else if (const uint32 bit = Game::padBitOfEvent(evt); bit != 0)
                    menu.capturePad(bit);
            }
            else if (const auto in = menuInputFor(evt))
            {
                const Game::MenuAction act = menu.input(*in);
                switch (act.kind)
                {
                case Game::MenuAction::Kind::StartScene:
                {
                    // PLAY (0x5d6720(scene, 1)) resumes the scene's checkpoint; a scene picked from
                    // the list (0x5d6720(scene, 0)) starts from the top.
                    const bool resume = act.resume && heldCheckpoint && progress.sceneState() == act.scene - 1;
                    startSession({.level = sceneFolder(act.scene), .savePath = {}, .characterOnly = false,
                                  .checkpoint = resume ? heldCheckpoint : nullptr});
                    break;
                }
                case Game::MenuAction::Kind::LoadSave:
                    startSession({.level = sceneFolder(act.scene), .savePath = act.savePath, .characterOnly = false, .checkpoint = nullptr});
                    break;
                case Game::MenuAction::Kind::Quit:
                    running = false;
                    break;
                case Game::MenuAction::Kind::Resume:
                    paused = false;
                    audio.setPaused(false);
                    break;
                case Game::MenuAction::Kind::Continue:
                    paused = false;
                    continueScene = true;
                    break;
                case Game::MenuAction::Kind::Retry:
                {
                    // 0x5d79c0: again from the save point's checkpoint, else from the start.
                    failed = false;
                    const std::shared_ptr<const Game::SaveFile> from = session ? session->checkpoint() : nullptr;
                    startSession({.level = current.level, .savePath = {}, .characterOnly = false, .checkpoint = from});
                    break;
                }
                case Game::MenuAction::Kind::QuitLevel:
                    paused = false;
                    failed = false;
                    session.reset();
                    audio.stopAll();
                    audio.setPaused(false);
                    menu.open();
                    break;
                case Game::MenuAction::Kind::None:
                    break;
                }
            }
        }

        const uint64 nowNs = SDL_GetTicksNS();
        const double frameSeconds = static_cast<double>(nowNs - lastTimeNs) * 1e-9;
        const float32 dt = static_cast<float32>(std::min(frameSeconds, kMaxFrameDt));
        lastTimeNs = nowNs;

        // The audio settings apply at once (the Audio page's sliders are heard as they move).
        audio.setBusVolume(Audio::Bus::Effects, cfg.settings.sfxGain());
        audio.setBusVolume(Audio::Bus::Music, cfg.settings.musicGain());
        renderer.scene().effects().gamma = cfg.settings.gammaExponent();

        if (session && !paused && !continueScene && session->levelCompleted())
        {
            // The level is won: its results (Game/results_menu.cpp) over the faded-out scene.
            paused = true;
            session->pause();
            audio.stopAll();
            session->playLevelEndMusic(); // the _L stream (0x5a5a50)
            const Game::LevelResults results = session->results();
            if (results.scene >= 0)
            {
                progress.complete(results.scene, results.stars);          // 0x5d2320
                progress.storeInventory(results.scene, session->inventorySlots()); // 0x5d24a0
                progress.setLast(results.scene, Game::GameProgress::kSceneDone);
            }
            menu.openResults(results);
        }
        if (continueScene)
        {
            continueScene = false;
            // On from the results: the next story scene, or back to the menu after the last.
            const auto& scenes = menu.scenes();
            const auto it = std::find_if(scenes.begin(), scenes.end(), [&](const auto& s) { return s.folder == current.level; });
            if (it != scenes.end() && it + 1 != scenes.end() && it->number < Game::kStoryScenes)
            {
                std::cerr << "main: scene " << it->number << " complete, starting scene " << (it + 1)->number << "\n";
                startSession({.level = (it + 1)->folder, .savePath = {}, .characterOnly = false, .checkpoint = nullptr});
            }
            else
            {
                session.reset();
                menu.open();
            }
        }
        if (session && !paused && !failed && session->sceneFailed())
        {
            // The player died: Scene Failed over the scene; Retry plays it again (from the last
            // checkpoint, if one was reached).
            failed = true;
            menu.openFailed();
        }
        if (session && !paused)
        {
            session->update(dt);
            if (session->takeNewCheckpoint())
            {
                heldCheckpoint = session->checkpoint();
                progress.setLast(progress.lastScene(), progress.lastScene()); // 0x7d6ae8: a checkpoint in this scene
            }
            session->drawOverlay(renderer.ui());
            if (failed)
            {
                menu.update(dt);
                menu.drawOverlay(renderer.ui());
            }
            perf.draw(renderer.ui(), menu.lcdFont());
            renderer.beginFrame();
            renderer.renderScene(session->scene());
        }
        else
        {
            menu.update(dt);
            menu.draw(renderer.ui());
            perf.draw(renderer.ui(), menu.lcdFont());
            renderer.beginFrame();
            renderer.renderScene(renderer.scene()); // empty 3D scene under the menu
        }
        // CPU time: update, draw-list build and command recording (present waits for the GPU).
        const float32 cpuMs = static_cast<float32>(static_cast<double>(SDL_GetTicksNS() - nowNs) * 1e-6);
        renderer.present();
        perf.frame(static_cast<float32>(frameSeconds), cpuMs, renderer.frameStats());
    }

    // 0x606130: the settings are written on the way out.
    settingsFile.settings = cfg.settings;
    if (!settingsFile.save(cfg.settingsPath()))
        std::cerr << "main: could not save " << cfg.settingsPath() << "\n";

    session.reset(); // frees its GPU resources while the device is alive
    renderer.shutdown();
    audio.shutdown();
    return 0;
}
