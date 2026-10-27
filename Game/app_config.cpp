// Game/app_config.cpp - runtime configuration: data paths and launch mode.
#include "Game/app_config.h"

#include "Core/platform.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>

#ifndef REELHUNT_DEFAULT_GAME_ROOT
#define REELHUNT_DEFAULT_GAME_ROOT "../game/"
#endif
#ifndef REELHUNT_SOURCE_DIR
#define REELHUNT_SOURCE_DIR "."
#endif

namespace Game {

namespace {

std::string withTrailingSlash(std::string p)
{
    if (!p.empty() && p.back() != '/' && p.back() != '\\')
        p += '/';
    return p;
}

// The engine's own settings folder: %APPDATA%/Reelhunt (else ~/.config/Reelhunt).
std::string defaultUserDir()
{
#ifdef _WIN32
    if (const char* app = std::getenv("APPDATA"))
        return withTrailingSlash((std::filesystem::path(app) / "Reelhunt").string());
#else
    if (const char* home = std::getenv("HOME"))
        return withTrailingSlash((std::filesystem::path(home) / ".config" / "Reelhunt").string());
#endif
    return "./";
}

} // namespace

std::string gameSaveDir(const std::string& userFilesDir, const std::string& saveGamesDir)
{
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home)
        return {};
    return (std::filesystem::path(home) / "Documents" / userFilesDir / saveGamesDir).string();
}

std::string AppConfig::characterDffPath() const
{
    return characterDff.empty() ? gameRoot + "levels/GLOBAL/CHARPAK/cash_pc.dff" : characterDff;
}

std::string AppConfig::animationsPath() const
{
    return animations.empty() ? levelDir() + "allanims.ifp" : animations;
}

static std::string defaultGameRoot()
{
    // 1. $REELHUNT_GAME_ROOT; 2. the folder the executable sits in, when it is a game install
    // (the engine dropped next to the exe); 3. the root baked in at build time (development).
    if (const char* env = std::getenv("REELHUNT_GAME_ROOT"); env && *env)
        return withTrailingSlash(env);
    if (const auto exe = Engine::Platform::executableDir(); Engine::Platform::isGameRoot(exe))
        return withTrailingSlash(exe.string());
    return withTrailingSlash(REELHUNT_DEFAULT_GAME_ROOT);
}

static std::string defaultConfigDir()
{
    // The engine's own data tables: config/ next to the executable (an installed build), else the
    // source tree's (a development build).
    std::error_code ec;
    if (const auto exe = Engine::Platform::executableDir(); !exe.empty() && std::filesystem::is_directory(exe / "config", ec))
        return withTrailingSlash((exe / "config").string());
    return std::string(REELHUNT_SOURCE_DIR) + "/config/";
}

AppConfig defaultConfig()
{
    AppConfig cfg;
    cfg.gameRoot = defaultGameRoot();
    const std::string config = defaultConfigDir();
    cfg.keymapPath = config + "keymap.txt";
    cfg.scriptConstantsPath = config + "script_constants.txt";
    cfg.userDir = defaultUserDir();
    std::error_code ec;
    const auto tmp = std::filesystem::temp_directory_path(ec);
    cfg.saveOutPath = ((ec ? std::filesystem::path(".") : tmp) / "REELHUNT.SAV").string();
    return cfg;
}

std::string commandLineHelp()
{
    return
        "usage: Reelhunt [options]\n"
        "Starts at the main menu unless one of the first three options is given.\n"
        "  --skip-menu           start straight into the level (default jury_turf, scene 1)\n"
        "  --level <name>        level directory under <game-root>/levels; implies --skip-menu\n"
        "  --character-only      start straight into the character-only animation rig\n"
        "  --no-cutscene         skip the level's Bink intro\n"
        "  --no-movement         keep the player still in level mode (mouse-look only)\n"
        "  --no-mouse-look       do not capture the mouse (scripted runs, screenshots)\n"
        "  --ai-log              print every hunter's state every two seconds\n"
        "  --perf                show frame time, FPS and GPU time per pass (F7 toggles; logged every 2 s)\n"
        "  --give <record>       debug: start with this item (entityTypeData record, e.g. Bag_(CT)); repeatable\n"
        "  --run-script <entity>:<handler> debug: start a level script handler (player:GenerateSavePoint); repeatable\n"
        "  --player-at x,z[,yaw[,y]] debug: start the player at this scene position and heading (degrees);\n"
        "                        y picks the floor (searched down from y + 1, else from above the spawn)\n"
        "  --lighting <mode>     level lighting: mixed (baked light + shadow-casting lamps, default), original\n"
        "                        (the level's baked light only), dynamic (no baked light: moon + lamps), midday\n"
        "                        (debug sun); F8 cycles\n"
        "  --unlock-all          every scene open in SELECT SCENE\n"
        "  --game-root <dir>     game data root (default: $REELHUNT_GAME_ROOT, else the executable's\n"
        "                        folder when it holds the game, else the build setting)\n"
        "  --character <dff>     character model (default levels/GLOBAL/CHARPAK/cash_pc.dff)\n"
        "  --animations <ifp>    animation file (default levels/<level>/allanims.ifp)\n"
        "  --keymap <file>       key-mapping file (default config/keymap.txt)\n"
        "  --save <file>         original save to restore (with --skip-menu / --level)\n"
        "  --save-dir <dir>      the original game's save folder (LOAD GAME)\n"
        "  --save-out <file>     where F5 writes the save (default: temp dir)\n"
        "  --size <w>x<h>        window size (default 800x600; with --windowed or a direct level start)\n"
        "  --no-launcher         skip the launcher window: the last display and screen mode chosen in it\n"
        "  --windowed            skip the launcher: a window of --size on the last chosen display\n"
        "  --validation          enable the Vulkan validation layer if installed\n"
        "  --camera x,y,z,yaw,pitch  start in the free-fly camera at this pose (degrees)\n"
        "  --help                this text\n";
}

std::optional<std::string> parseCommandLine(const std::vector<std::string>& args, AppConfig& cfg, bool& helpRequested)
{
    helpRequested = false;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        auto value = [&](std::string& out) -> bool {
            if (i + 1 >= args.size())
                return false;
            out = args[++i];
            return true;
        };
        std::string v;
        if (a == "--help" || a == "-h")
            helpRequested = true;
        else if (a == "--skip-menu" || a == "--level-mode")
        {
            cfg.skipMenu = true;
            cfg.launcher = AppConfig::Launcher::Windowed;
        }
        else if (a == "--character-only")
        {
            cfg.characterOnly = true;
            cfg.launcher = AppConfig::Launcher::Windowed;
        }
        else if (a == "--no-launcher")
            cfg.launcher = AppConfig::Launcher::Saved;
        else if (a == "--windowed")
            cfg.launcher = AppConfig::Launcher::Windowed;
        else if (a == "--no-cutscene")
            cfg.playCutscene = false;
        else if (a == "--no-music")
            cfg.noMusic = true;
        else if (a == "--unlock-all")
            cfg.unlockAll = true;
        else if (a == "--no-movement")
            cfg.movementEnabled = false;
        else if (a == "--movement")
            cfg.movementEnabled = true; // the old opt-in; movement is now the default
        else if (a == "--perf")
            cfg.perfOverlay = true;
        else if (a == "--run-script")
        {
            if (i + 1 >= args.size() || args[i + 1].find(':') == std::string::npos)
                return "--run-script needs <entity>:<handler>";
            cfg.runScripts.push_back(args[++i]);
        }
        else if (a == "--give")
        {
            if (i + 1 >= args.size())
                return "--give needs an item record";
            cfg.give.push_back(args[++i]);
        }
        else if (a == "--ai-log")
            cfg.aiLog = true;
        else if (a == "--player-at")
        {
            float32 x = 0.0f, z = 0.0f, yaw = 0.0f, y = std::numeric_limits<float32>::quiet_NaN();
            if (!value(v) || std::sscanf(v.c_str(), "%f,%f,%f,%f", &x, &z, &yaw, &y) < 2)
                return "--player-at needs x,z[,yaw[,y]]";
            cfg.playerAt = std::array<float32, 4>{x, z, yaw, y};
        }
        else if (a == "--no-mouse-look")
            cfg.mouseLook = false;
        else if (a == "--lighting")
        {
            using M = Renderer::LightingMode;
            if (!value(v))
                return "--lighting needs original, mixed, dynamic or midday";
            if (v == "original")
                cfg.lighting = M::Baked;
            else if (v == "mixed")
                cfg.lighting = M::Mixed;
            else if (v == "dynamic")
                cfg.lighting = M::Dynamic;
            else if (v == "midday")
                cfg.lighting = M::Midday;
            else
                return "--lighting needs original, mixed, dynamic or midday";
        }
        else if (a == "--validation")
            cfg.validation = true;
        else if (a == "--level")
        {
            if (!value(cfg.level))
                return "--level needs a name";
            cfg.skipMenu = true;
            cfg.launcher = AppConfig::Launcher::Windowed;
        }
        else if (a == "--game-root")
        {
            if (!value(v))
                return "--game-root needs a directory";
            cfg.gameRoot = withTrailingSlash(v);
        }
        else if (a == "--character")
        {
            if (!value(cfg.characterDff))
                return "--character needs a .dff path";
        }
        else if (a == "--animations")
        {
            if (!value(cfg.animations))
                return "--animations needs an .ifp path";
        }
        else if (a == "--keymap")
        {
            if (!value(cfg.keymapPath))
                return "--keymap needs a path";
        }
        else if (a == "--save")
        {
            if (!value(cfg.savePath))
                return "--save needs a path";
        }
        else if (a == "--save-dir")
        {
            if (!value(cfg.saveDir))
                return "--save-dir needs a directory";
        }
        else if (a == "--save-out")
        {
            if (!value(cfg.saveOutPath))
                return "--save-out needs a path";
        }
        else if (a == "--camera")
        {
            std::array<float32, 5> c{};
            if (!value(v) || std::sscanf(v.c_str(), "%f,%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3], &c[4]) != 5)
                return "--camera needs x,y,z,yawDeg,pitchDeg";
            cfg.debugCamera = c;
        }
        else if (a == "--size")
        {
            unsigned w = 0, h = 0;
            if (!value(v) || std::sscanf(v.c_str(), "%ux%u", &w, &h) != 2 || w < 64 || h < 64)
                return "--size needs <width>x<height>";
            cfg.width = w;
            cfg.height = h;
        }
        else
            return "unknown option '" + a + "'";
    }
    return std::nullopt;
}

} // namespace Game
