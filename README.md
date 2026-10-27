# Reelhunt

An open-source game engine in C++23 with Vulkan, SDL3 and OpenAL that runs the data of
Rockstar North's 2003 stealth-horror game (the 2004 PC release). It reads that game's levels,
models, animations, scripts, audio, Bink video and saves, and aims to play like the original,
rule for rule, on modern Windows and Linux.

**You need your own PC copy of the original game.** This repository contains no game files,
and the engine does nothing without them. See [NOTICE.md](NOTICE.md).

## Status

- The whole story campaign loads: the main menu, scene select, launcher, settings, pause menu,
  loading screens, saves (the original `*.SAV` format) and level-to-level flow.
- The levels' own scripts run: the engine compiles the script source that ships inside each
  level's `.mls` file. 933 of the 934 shipped scripts compile.
- Hunters, executions, stealth, weapons, guns, the director's voice and the HUD follow the
  rules and numbers traced in the game's executable, with the exe addresses cited in the code.
- A smoke run of all 24 levels (45 s each, AI on) finishes without a crash.
- It is not a finished 1:1 port. Open items are tracked as GitHub issues. The larger ones: the in-game VIDEO
  settings page, fall damage, some boss-level AI details, and a fixed-step simulation
  (it currently advances by the frame time).

Platforms: Windows 10/11 x64 (MinGW-w64 build) and Linux x86-64 (GCC 13+ or Clang 19+).
A Vulkan 1.3 GPU driver is required.

## Quick start (drop-in)

The engine is meant to sit **next to the game's executable**, in the game's own folder:

```
<game folder>/           <- your installed game
├── <the game's .exe>    (not run; a few lookup tables are read from it)
├── levels/  audio/  pictures/  ...
├── Reelhunt(.exe)       <- the engine
├── *.spv                <- its shaders
└── config/              <- its tables
```

Started from there, it finds the game data by itself: no settings, no command line.

**Windows:** build it (see [docs/BUILDING.md](docs/BUILDING.md)), then copy it into the game folder:

```bash
cmake --install _build_mingw --prefix "C:/Games/<game folder>"
```

Start `Reelhunt.exe`. OpenAL must be installed: either OpenAL Soft's `OpenAL32.dll` placed
next to the exe, or Creative's `oalinst.exe` redistributable.

**Linux:** build it, then install it into the game folder (copied from a Windows install,
Steam/Proton, or a disc):

```bash
cmake --install _build --prefix ~/Games/<game folder> --strip
~/Games/<game folder>/Reelhunt
```

Case does not matter: `levels/GLOBAL` and `LEVELS/global` both work, because the data's own
references mix cases and are resolved case-insensitively.

**Elsewhere:** the engine can also live in its own folder and be pointed at the game:
`--game-root /path/to/game` or `REELHUNT_GAME_ROOT=/path/to/game`.

Saves are read from and written to the original game's own save folder,
`<Documents>/<its user-files folder>/SaveGames` (the folder names are read from its executable),
so its saves load. The engine's own settings go to `%APPDATA%/Reelhunt` or
`~/.config/Reelhunt`.

## Controls

Default keys (the bindings start from the original's default table and can be changed in
SETTINGS → CONTROLS):

| Action | Keys |
|--------|------|
| Move / strafe | W A S D or arrows |
| Run / sneak (hold) | Shift / C or Ctrl |
| Attack, execute (hold to charge), fire | Left mouse |
| Aim and lock on (guns) | Right mouse |
| Use, pick up body, wall hug | Space |
| Kick / grab | K / G |
| Reload / zoom | R / mouse wheel or Z (while aiming) |
| Switch weapon | Mouse wheel |
| Peek left / right, look back, first person | Q / E, F, X |
| Pause menu | Esc |

Debug keys: F5 writes a save to `--save-out` (never over an original), F7 shows the frame-time
counter, F8 cycles the lighting mode, F9 toggles a free-fly camera, V shows the spawn marker.

## Command line

`Reelhunt --help` lists everything. The most useful options:

| Option | Effect |
|--------|--------|
| `--level <name>` | Start straight into a level (`jury_turf`, `asylum`, ...) |
| `--no-cutscene` | Skip the level's intro video |
| `--windowed`, `--size 1280x720` | Skip the launcher; a window of this size |
| `--game-root <dir>` | Where the game data is |
| `--lighting original\|mixed\|dynamic\|midday` | Baked light only (as the original), baked + shadowed lamps (default), fully dynamic, debug sun |
| `--perf` | Frame time and GPU time per pass |
| `--ai-log` | Print every hunter's state every two seconds |
| `--validation` | Enable the Vulkan validation layer |

## Building

See **[docs/BUILDING.md](docs/BUILDING.md)**. Summary for Linux (Ubuntu 24.04):

```bash
sudo apt install build-essential cmake ninja-build git pkg-config \
    libvulkan-dev glslang-tools libopenal-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
    libasound2-dev libpulse-dev
cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build _build
ctest --test-dir _build --output-on-failure
```

## Documentation

| Document | Content |
|----------|---------|
| [docs/BUILDING.md](docs/BUILDING.md) | Build, install, package and test on Windows and Linux |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the engine is put together, the technology used, and why |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to work on it: rules for code, research and pull requests |
| [NOTICE.md](NOTICE.md) | Legal: what is and isn't in this repository, third-party code |

## License

The engine's own code is under the [MIT license](LICENSE). The Bink decoders in `Codec/` are
ported from FFmpeg and stay under the LGPL-2.1-or-later. The game and its data belong to their
owners; nothing here grants any right to them.
