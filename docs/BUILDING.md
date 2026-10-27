# Building, installing and testing

The build is plain CMake (3.25+) with Ninja. It needs a C++23 compiler, Vulkan headers and
loader plus `glslangValidator`, SDL3, OpenAL, and GLM. Three libraries are handled by CMake:

- **librw** is always fetched at a pinned commit and patched (`cmake/patches/`).
- **SDL3** is fetched and linked statically when the system has no SDL3 package.
- **GLM** (header-only) comes from the system package when it is 1.0.1 or newer, else release
  1.0.3 is fetched (`REELHUNT_GLM_TAG`). Ubuntu 24.04's `libglm-dev` is 0.9.9, so it is fetched there.

Fetching needs `git` and network access on the first configure.

| Platform | Compiler | Status |
|----------|----------|--------|
| Linux x86-64 | GCC 13+ or Clang 19+ | Supported, tested on Ubuntu 24.04 (GCC 13.3, Clang 20) |
| Windows x64 | MinGW-w64 GCC 13+ (tested with 16.1) | Supported (the primary development platform) |
| Clang 18 and older | — | Not supported: with libstdc++ they lack `std::expected` |
| Windows, MSVC | — | Not supported yet: the Windows branch of `CMakeLists.txt` links MinGW import libraries |
| macOS | — | Not supported (would need MoltenVK; contributions welcome) |

---

## Linux

### 1. Dependencies

Ubuntu 24.04 / Debian 13:

```bash
sudo apt install build-essential cmake ninja-build git pkg-config \
    libvulkan-dev glslang-tools libopenal-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
    libasound2-dev libpulse-dev
```

Fedora 40+:

```bash
sudo dnf install gcc-c++ cmake ninja-build git pkgconf-pkg-config \
    vulkan-loader-devel vulkan-headers glslang openal-soft-devel SDL3-devel glm-devel
```

Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake ninja git vulkan-icd-loader vulkan-headers \
    glslang openal sdl3 glm
```

When the distribution packages SDL3 (Fedora, Arch, Debian 13+), CMake uses it. Otherwise SDL3
3.2.24 is fetched and built statically; the X11/Wayland/ALSA/Pulse `-dev` packages above are
needed only for that.

At runtime the machine needs a Vulkan driver: Mesa (`mesa-vulkan-drivers`) for AMD and Intel,
or NVIDIA's own driver. Check with `vulkaninfo --summary`.

### 2. Configure and build

```bash
git clone https://github.com/<you>/<repo>.git
cd <repo>
cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build _build
```

The build directory then holds `Reelhunt`, the SPIR-V shaders (`*.spv`), `config/`, the
tools and the tests.

### 3. Install into the game folder

Copy the game's folder from a Windows install, Steam (Proton), or a disc, then install the
engine into it:

```bash
cmake --install _build --prefix ~/Games/<game folder> --strip
~/Games/<game folder>/Reelhunt
```

The install puts `Reelhunt`, the `*.spv` shaders and `config/` directly in the prefix.
Started from there, the engine sees `levels/`, `audio/` and `pictures/` beside itself and uses
that folder as the game root.

### 4. A redistributable tarball

```bash
cmake --install _build --prefix "$PWD/dist/Reelhunt-linux-x86_64" --strip
tar -C dist -czf Reelhunt-linux-x86_64.tar.gz Reelhunt-linux-x86_64
```

The binary links `libvulkan.so.1`, `libopenal.so.1` and the C/C++ runtime dynamically. A
fetched SDL3 is linked in and loads X11 or Wayland at run time. Build on the oldest
distribution you want to support (glibc is forward-compatible only).

---

## Windows (MinGW-w64)

The Windows build uses no package manager. The SDKs sit next to the source tree, and their
locations are CMake cache variables:

```
workspace/                         (any parent folder)
├── Reelhunt/                      this repository
├── SDL3_3418/x86_64-w64-mingw32/  SDL3 MinGW development package (SDL3_ROOT default)
├── OpenAl_SDK/                    OpenAL 1.1 SDK: include/, libs/Win64/ (OAL_ROOT default)
└── game/                          the original game (REELHUNT_GAME_ROOT default, for development and tests)
C:/VulkanSDK/1.4.363.0/            LunarG Vulkan SDK (VK_ROOT)
```

Downloads:

- MinGW-w64: the [MinGW-Builds](https://github.com/niXman/mingw-builds-binaries/releases) `x86_64-posix-seh` package, or MSYS2's `mingw-w64-ucrt-x86_64-gcc`.
- SDL3: [`SDL3-devel-3.x.x-mingw.tar.gz`](https://github.com/libsdl-org/SDL/releases).
- Vulkan: [LunarG Vulkan SDK](https://vulkan.lunarg.com/).
- OpenAL: the OpenAL 1.1 SDK, or OpenAL Soft's binary package, which has the same `include/AL` and `libs/Win64` layout.
- CMake and Ninja: their official installers, or `winget install Kitware.CMake Ninja-build.Ninja`.

Configure and build (from Git Bash, MSYS2 or PowerShell):

```bash
cmake -S . -B _build_mingw -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DVK_ROOT=C:/VulkanSDK/1.4.363.0 \
  -DSDL3_ROOT=D:/path/to/SDL3/x86_64-w64-mingw32 \
  -DOAL_ROOT=D:/path/to/OpenAl_SDK
cmake --build _build_mingw
```

Install into the game folder, next to the game's executable:

```bash
cmake --install _build_mingw --prefix "C:/Games/<game folder>"
```

This copies `Reelhunt.exe`, the shaders, `config/`, `SDL3.dll`, `vulkan-1.dll` and the MinGW
runtime DLLs. `OpenAL32.dll` is not copied. Install OpenAL (Creative's `oalinst.exe`) or put
OpenAL Soft's `soft_oal.dll`, renamed to `OpenAL32.dll`, next to the exe.

---

## Build options

| CMake variable | Default | Meaning |
|----------------|---------|---------|
| `CMAKE_BUILD_TYPE` | `RelWithDebInfo` | `Release` for players, `Debug` for debugging |
| `REELHUNT_BUILD_TESTS` | `ON` | Build the ctest suite |
| `REELHUNT_BUILD_TOOLS` | `ON` | Build the dump tools (`txd_dump`, `mls_dump`, `script_check`, ...) |
| `REELHUNT_BUILD_EXAMPLES` | `ON` on Windows | The Vulkan triangle sample (Windows SDK layout only) |
| `REELHUNT_FETCH_SDL3` | `ON` | Fetch SDL3 when no package is found (`OFF`: fail instead) |
| `REELHUNT_GLM_TAG` | `1.0.3` | GLM release fetched when no system GLM 1.0.1+ is found |
| `REELHUNT_TEST_FIXTURES` | `$TEMP/reelhunt_fixtures` | Reference decodes for `bink_test` |
| `REELHUNT_GAME_ROOT` | `../game/` | Fallback game root for development builds and tests |
| `LIBRW_SRC_DIR` | `../librw` | Use a prebuilt librw checkout (`_build/src/librw.a`) instead of fetching |
| `VK_ROOT`, `SDL3_ROOT`, `OAL_ROOT` | see above | Windows SDK locations |

How the engine finds things at run time:

| What | Order |
|------|-------|
| Game data | `--game-root` → `$REELHUNT_GAME_ROOT` → the executable's folder, if it holds `levels/`, `audio/` and `pictures/` → `REELHUNT_GAME_ROOT` from the build |
| Shaders | Next to the executable |
| `config/` tables | `config/` next to the executable → the source tree's `config/` |
| Tables from the exe | The game's executable in the game root (PC 1.0 build, found by content), read at start-up; text tables written to `<engine settings>/exe_tables/` |
| Original saves | `--save-dir` → the original game's save folder, `<Documents>/<its user-files folder>/SaveGames` (names read from its executable) |
| Engine settings | `%APPDATA%/Reelhunt` (Windows), `~/.config/Reelhunt` (Linux) |

---

## Tests

```bash
ctest --test-dir _build --output-on-failure
```

| Test | Needs | Covers |
|------|-------|--------|
| `core_test` | — | Core types, math, physics, script VM, keymap, save restore, settings, platform paths |
| `librw_test` | — | RenderWare stream parsing |
| `player_test` | — | Player controller and follow camera |
| `script_test` | (game data for the level part) | Script compiler and task VM, then a level's real scripts |
| `ai_test` | (game data for the level part) | Senses, combat rules, tuning, nav graph, a level's OnCreate |
| `assets_test` | game data | Every asset parser against the shipped files |
| `frontend_test` | game data | Game text, fonts, scene list, menu navigation |
| `anim_test` | game data | Key-mapped clips exist and drive Cash's skeleton |
| `bink_test` | game data, fixtures | Bink video, bit-exact against an FFmpeg decode |
| `bink_audio_test` | game data | Bink audio decode |
| `save_test` | an original save | Save slot 1: parse, restore and byte-exact write-back |

A test that lacks its input exits with code 77, which ctest reports as *skipped*. So the suite
passes on a machine (or CI runner) without the game, running only the data-free tests. Inputs
are found through the following:

- `REELHUNT_GAME_ROOT` — the game folder.
- `REELHUNT_SAVE_DIR` — a folder with an original save in slot 1.
- `REELHUNT_TEST_FIXTURES` — reference decodes. Create them with FFmpeg:
  `ffmpeg -i levels/asylum/asylum.bik -frames:v 8 -f rawvideo -pix_fmt yuv420p <fixtures>/bink_verify/ref.yuv`.

`script_check` (a tool) compiles every level's scripts and reports failures. Run it after any
change to the script compiler.

## Troubleshooting

- **`Could NOT find Vulkan (missing: glslangValidator)`**: install `glslang-tools` (Debian/Ubuntu)
  or `glslang` (Fedora, Arch).
- **No window / `vkCreateInstance` fails**: no Vulkan driver. `vulkaninfo --summary` should
  list a GPU. In WSL2 only the software `llvmpipe` driver is usually available: it works, slowly.
- **"The game data was not found in ..."**: the engine is not in the game folder.
  Install it there, or pass `--game-root`. The folder must have `levels/`, `audio/` and
  `pictures/`.
- **"... is not the PC 1.0 build of the game ..."**: the engine reads a few lookup tables
  from the game's executable at fixed addresses, and this exe is a different build. Please open
  an issue saying where your copy comes from (disc, Steam, other), and include the exe's size.
- **No sound**: OpenAL is missing (Windows) or no audio server is running (Linux:
  PipeWire/PulseAudio/ALSA are tried in that order by OpenAL Soft).
