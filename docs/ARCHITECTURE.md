# Architecture and design decisions

This document is for people who want to work on the engine: what it is made of, how a frame
and a level flow through it, where to find things, and why it is built the way it is.

## Goals

1. **Play like the original.** Behaviour is reproduced from the game's own data and from the
   rules in the game's executable, not invented. When the engine does something its own way, the code
   says so.
2. **Use the original data as shipped.** No conversion step and no repacked assets. The engine
   reads the user's install directly.
3. **Run on current systems.** Vulkan, SDL3 and OpenAL; Windows and Linux; any resolution.
4. **Stay maintainable.** Small modules, pure logic separated from I/O and GPU code, tests for
   the logic.

## Technology

| Area | Choice | Why |
|------|--------|-----|
| Language | C++23 (`std::expected`, `std::span`, designated initializers) | Value types and explicit errors without exceptions in hot paths. GCC 13+ and Clang 19+ cover what we use |
| Build | CMake 3.25+, Ninja, `FetchContent` | One build on both platforms. Dependencies the user can't get easily are fetched at a pinned version |
| Window, input, displays | SDL3 | Cross-platform windows, raw mouse, display modes. The original used DirectInput/DirectDraw |
| Rendering | Vulkan 1.3, GLSL → SPIR-V via `glslangValidator` | Explicit, portable, and fast enough for dynamic lights and shadows at modern resolutions. The original was RenderWare on Direct3D 8 |
| Audio | OpenAL (OpenAL Soft) | 3D positional sources, available everywhere. The original used Miles Sound System |
| Math | GLM (system package, else a pinned release fetched by CMake) | Standard, header-only, matches GLSL conventions |
| RenderWare parsing | librw, *NULL* platform | A tested reader for DFF/TXD streams. We use only its parser and do our own GPU upload. One patch makes raster creation a no-op on the NULL platform |
| Video | Own Bink 1 decoder (ported from FFmpeg) | The game's cut-scenes are `.bik`. Linking FFmpeg would add a large dependency for one codec |
| Scripts | Own compiler and VM | The levels' `.mls` files ship their Pascal-like source, which the engine compiles at load (see below) |

## Source layout

```
main.cpp              Start-up: config, launcher, renderer/audio, main menu <-> game session loop
Core/                 Platform layer (executable dir, case-insensitive paths), entity ids, asset manager
Math/                 Thin types over GLM (Vector3, Matrix4, Quaternion, Transform)
Physics/              Spheres/boxes, rigid body, triangle field (world collision), ray casts
Assets/               Parsers for the game's file formats (one pair of files per format)
Codec/                Bink video and audio decoders (LGPL, from FFmpeg)
Audio/                OpenAL device, prioritized voices, streaming source for cut-scenes
Renderer/             Scene, camera, materials, lighting modes; backend/ = Vulkan
  backend/            Device and swapchain, passes, pipelines, resource cache, GLSL shaders
Game/                 Gameplay: everything above the engine layers
  script/             Level-script compiler and cooperative task VM
  ai/                 AI director (packs, leaders, goals), senses, stealth, nav graph, search, tuning
  level_runtime.*     A running level: entities, triggers, hunters, items, doors, scripts' natives
  game_session.*      One level from load to end: player, camera, cut-scenes, HUD, saves
  *_menu.*, launcher  Front end: launcher dialog, main/scene/settings/save/pause/bonus menus
config/               Tables the engine reads at start-up (see "Data tables")
tests/                ctest executables, one per area
tools/                Developer tools: dump a TXD/DFF/IFP/COL/BSP/INST/GRF/MLS, check all scripts
cmake/                MinGW toolchain file, librw patch
```

Two rules hold the layout together:

- **`engine_core`** (a static library) holds everything except `main.cpp`. The tests link it.
  Most of `Game/` is pure logic (no SDL, no Vulkan) and can be unit-tested.
- **One owner per concern.** For example `Game/player_controller` is movement and camera
  math only, `Game/locomotion` maps movement to gait clips, and `Game/game_session` wires them
  together.

## Start-up and the main loop

1. `Game::defaultConfig()` decides the paths:
   - **The game root:** `--game-root`, else `$REELHUNT_GAME_ROOT`, else the executable's folder
     if it holds `levels/`, `audio/` and `pictures/`, else the build's development default.
   - **The `config/` tables:** next to the executable, else the source tree's.
   - `main` stops with a message box if the game root has no game in it.
2. **The launcher** (`Game/launcher`) is the original's start dialog: display adapter, screen
   mode, windowed or fullscreen. The choice is saved for the next start.
3. **The renderer and audio** start.
4. **The main menu runs.** PLAY or a loaded save creates a `GameSession` for a level. The
   session runs until the level ends, the player quits, or the next level is due; then the
   menu or the next session follows.
5. **Each frame:**
   1. Input is read through `Game/bindings` (the original's action table, rebindable).
   2. The session updates scripts, AI, player, physics, animation and audio.
   3. The renderer draws the scene and the 2D layer.

   Simulation advances by the frame's delta time. A fixed-step loop is an open item.

## Levels

`GameSession` loads a level's folder through the `Assets/` parsers:

| File | Parser | Becomes |
|------|--------|---------|
| `scene1.bsp`, `scene2.bsp` | `Assets/world` | World mesh (baked prelight per vertex), decal layer, collision triangles |
| `pak/*.dff`, `pak/*.txd` | `Assets/dff`, `Assets/txd` (librw) | Prop and character models, textures |
| `entity.inst`, `entity2.inst` | `Assets/inst` | Placements: props, hunters, triggers, lights, doors, the player |
| `collisions.col` | `Assets/col` | Collision shapes of props and characters |
| `allanims.ifp` | `Assets/ifp` | Animation clips |
| `mapAI.grf` | `Assets/grf` | AI navigation graph |
| `scripts/*.mls` | `Assets/mls` | The level's scripts (source included) |
| `levels/GLOBAL/DATA/*.pak` | `Assets/pak`, `Assets/ini_records` | Tuning: AI types, weapons, shots, entity types |
| `*.gxt` | `Assets/gxt` | Text (UTF-16) |
| `audio/PC/SFX/sfx.*`, `*.RIB` | `Assets/sfx_bank`, `Assets/rib` | Sound effects, execution sounds, music |
| `*.bik` | `Codec/` | Cut-scenes |

`Game/level_frame.h` holds the single level-to-scene transform (the data is Z-up; the renderer
is Y-up), so world, props and collision always agree.

## Scripts

Every shipped `.mls` keeps its source in the `DBUG/SRCE` section. The engine works from that
source rather than from the compiled bytecode:

1. `Game/script/script_compiler` compiles the source to a small stack IR at level load.
2. `Game/script/script_engine` runs each handler (`OnCreate`, `OnDeath`, ...) as a cooperative
   task; `Sleep` yields.
3. Calls to game functions resolve to C++ natives by name, mostly in `Game/level_runtime.cpp`
   and `Game/ai/ai_natives.cpp`. An unknown native logs once and returns 0.

`tools/script_check` compiles every level's scripts. 933 of the 934 shipped scripts compile.

Working from the source rather than the bytecode keeps the VM small and readable, and makes
script behaviour easy to trace.

## AI

- **`Game/ai/ai_director`:** the level's packs, leaders, sub-packs, goals and enemies, as the
  scripts set them up.
- **`Game/hunter`:** one hunter's body, state machine and actions.
- **`Game/ai/senses`, `stealth`:** sight (cones, light level, the player's shadow) and
  hearing (noise by movement and floor material).
- **`Game/ai/search`, `nav_graph`:** searching, and routes over `mapAI.grf`. Routes use the
  exe's own breadth-first route rule.
- **`Game/ai/ai_tuning`:** the numbers from the tuning archive (`levels/GLOBAL/DATA/*.pak`).
- **`Game/ai/gun_combat.h`, `melee_traits`:** cover, peeking and squad behaviour for gunmen;
  melee positioning.

## Rendering

The Vulkan backend (`Renderer/backend/`) runs these passes each frame:

1. **Shadow maps:** three cascades for the sun or moon, plus spot-light (lamp) shadow maps. The
   static part of a lamp's map is cached.
2. **G-buffer:** albedo, normal, material, baked light and depth for opaque and alpha-tested
   surfaces.
3. **Lighting:** a deferred, full-screen pass (`shading.glsl`) into an HDR target.
4. **Forward:** alpha-blended surfaces (glass, decals, effects), back to front, using the same
   lighting code.
5. **Composite:** screen effects (fades, flashes, camcorder grain), the Bink video layer, the 2D
   UI, then out to the swapchain.

**Lighting modes:**

| Mode | What it shows |
|------|---------------|
| `original` | What the game showed: the level's baked prelight |
| `mixed` (default) | The prelight, plus the level's lamps as shadow-casting lights for moving characters |
| `dynamic` | No prelight: moon and lamps only |
| `midday` | A debug sun |

`REELHUNT_DEBUG_VIEW=albedo|light|normal|baked|product` shows one G-buffer input instead of the
lit scene.

GPU resources (meshes, textures with mips, material sets) are created on first use and cached
per level (`resource_cache`). Bone matrices for every skinned character go in one storage buffer
per frame.

## Audio

- **`Audio/audio`:** a fixed pool of OpenAL sources with priorities, plus one streaming source
  for cut-scene audio.
- **What decides a sound:** gameplay code posts sound events — footsteps by floor material,
  weapon sounds, voices, the director's lines. The rules for a sound's level, range and rate
  are those of the original audio manager, cited in `Game/player_audio` and
  `Game/weapon_audio`. `Game/acoustic_probe` measures how open the camera's surroundings are,
  which shapes the guns' tails.

## Saves

`Game/save_*` reads and writes the original `*.SAV` format. The original game's saves load,
and a parsed save written back unchanged is byte-identical (`save_test`). F5 writes a test save to
`--save-out`, never over an original.

## Tables read from the game's executable

Some lookup tables live in the game's executable itself, not in the data files:

- the director's and hunters' speech tables (AISPEECH id → SFX samples);
- PlayHunterSpeech's id → SPECIFIC_SPEECH slot (a switch, decoded from its jump table);
- the ped clip names and behaviours, with their blend times;
- the default key bindings;
- the names of the game's own files: the save slots, its user-files folder, the tuning archive,
  and the text font's texture. The source never spells them, so nothing in the code names the
  original game.

The engine does not ship them. At start-up `Game/exe_tables.cpp` finds the game's executable
in the game folder by its content (any `.exe` there whose tables read) and reads them from the
player's own copy:

1. It finds each table by its virtual address, through the PE section table.
2. It checks that the file is the PC 1.0 build it knows.
3. It writes the text tables to `<user dir>/exe_tables/`, where the loaders read them.

The `exe_tables` tool writes the same files anywhere, for inspection.

`config/` holds what the engine needs from elsewhere:

- `script_constants.txt`: the level-script language's constant names and values.
- `keymap.txt`: the engine's own animation-rig key → clip map.

## Platform layer

`Core/platform.{h,cpp}` holds the few platform differences:

- **`executableDir()`:** `GetModuleFileNameW`, or `/proc/self/exe`.
- **`resolvePath()`:** on Linux, a path that does not exist as written is matched one component
  at a time, case-insensitively, against the directory's entries (cached per directory, so each
  directory is listed once). The data's own references mix `GLOBAL`, `Global` and `global`. On
  Windows it returns the path unchanged.
- **`isGameRoot()`:** the "is this a game install" test used by the drop-in detection.

Every place that opens game data goes through `resolvePath`. New file-opening code must too.

## How behaviour is researched

The game's executable is analysed in Ghidra (the PC 1.0 executable, image base `0x400000`):

1. **Find the routine** that implements a behaviour: start from its strings, its data-file
   field names, or its callers.
2. **Write down what it does** in plain terms: conditions, numbers, order.
3. **Implement it** in this engine's own structure.
4. **Cite the addresses** in a comment next to the code, for example
   `// 0x44ea20: a ped named "Journalist" steps at 1.3 times the rate`.
5. **Mark our own choices.** Where the engine deliberately differs, or where a value was not
   found, the comment says so ("ours", "not traced").
6. **Test it:** a unit test where the logic is pure, a scripted run (`--level`,
   `--player-at`, the `REELHUNT_*` debug variables) where it is not.

Never paste decompiler output; see [CONTRIBUTING.md](../CONTRIBUTING.md).

## Debug switches

Command-line options are listed by `--help`. Environment variables (each read once, where it
is used; `grep -rn 'getenv("REELHUNT_' .` lists them all):

| Variable | Effect |
|----------|--------|
| `REELHUNT_GAME_ROOT` | Game data folder |
| `REELHUNT_GOD=1` | The player cannot be hurt |
| `REELHUNT_DEBUG_VIEW=albedo\|light\|normal\|baked\|product` | Show one G-buffer input |
| `REELHUNT_FOG=start,end` | Override the level's fog distances |
| `REELHUNT_PROVOKE=*` / `<name>` | Make hunters aware of the player at once |
| `REELHUNT_KILL=<hunter>[:<hp>[!]][@<s>]` | Kill a hunter, or leave it at `<hp>` (`!`: by a blow from the player), after `<s>` seconds |
| `REELHUNT_AUTO_EXECUTE=2\|3\|4[!][@<s>]` | Charge and release an execution automatically |
| `REELHUNT_HUNTER_WEAPON=<record>` | Arm every hunter with this item |
| `REELHUNT_CAMCORDER=1` | Show the execution camcorder overlay all the time |

## Design decisions, and what they cost

- **Data-driven over hard-coded.** Tuning comes from the tuning archive and the level files.
  Behaviour that is in the exe is ported as rules plus cited constants. *Cost:* every rule
  needs research before it can be written.
- **Our own script compiler instead of a bytecode VM.** *Cost:* the compiler must accept every
  quirk of the original sources. One script still fails.
- **Deferred renderer with optional dynamic lighting.** *Cost:* more GPU work than the original
  needed. `--lighting original` gives the original's look.
- **Zero package manager on Windows.** The SDKs are unpacked next to the tree and found through
  cache variables. *Cost:* the Windows setup is manual (docs/BUILDING.md). MSVC is not supported
  yet.
- **librw fetched and patched, not vendored.** It keeps the repository small and the upstream
  diff visible. *Cost:* the first configure needs network access.
- **No game data, no exe code in the repository.** The engine reads a user's install. *Cost:*
  only data-free tests can run in CI.
- **Variable time step.** Simple, and correct at any refresh rate for most systems. *Cost:*
  physics and AI are not bit-reproducible run to run. A fixed-step loop is a planned improvement.
