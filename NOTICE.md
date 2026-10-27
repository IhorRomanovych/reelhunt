# Notices

## Not affiliated

Reelhunt is an independent, non-commercial fan project. It is not affiliated with, endorsed
by, or sponsored by Rockstar Games, Take-Two Interactive, or any of their subsidiaries.
The original game's title, "Rockstar Games", "Rockstar North" and related names and logos are
trademarks of their respective owners. This project does not use the game's title or logos,
and names its makers only to say which game it is compatible with.

## No game content

This repository contains **no** game data, executable code or media: no models, textures,
animations, levels, scripts, audio, video, text, or any part of the game's executable. The
engine does nothing without a copy of the game that you own; it reads that copy's files at run
time from your disk. Do not add game files to this repository or to issues and pull requests
(see CONTRIBUTING.md).

## How the engine was written

The engine's behaviour is matched to the original by reverse engineering: the game's file
formats were studied, and the game's executable was analysed with a disassembler/decompiler (Ghidra) to
learn the rules the game follows (AI, combat, timings, camera, audio). Code comments cite the
addresses of the routines and constants that were studied (for example `0x44ea20`) so that
each behaviour can be re-checked. The C++ in this repository is written by hand for this
project, in its own structure; it is not decompiler output pasted in, and contributions must
follow the same rule (CONTRIBUTING.md).

Lookup tables that exist only inside the game's executable are **not** in this repository:
the director's and hunters' speech tables, the ped clip names and behaviours, the default key
bindings, and the names of the game's own files (save slots, its user folder, its tuning
archive, its text font). At start-up the engine finds the executable in the game folder by its
content, reads them from the player's own copy (`Game/exe_tables.cpp`), and writes the tables
to the player's user folder.

What the repository does contain:

| What | Content | Source |
|------|---------|--------|
| `config/script_constants.txt` | The level-script language's constant names → integer values (the names appear in the game's own script sources) | [Sor3nt](https://github.com/Sor3nt)'s file-format toolkit, corrected against the game's compiled scripts |
| Numbers in the code | Ranges, rates, timings and small tables that define a behaviour | The game's executable, cited by address |
| Addresses in comments | Where a behaviour was studied | The game's executable |

These are facts needed for interoperability with the game's data and rules: identifiers and
numbers, not program code or creative content.

## Third-party code

| Component | License | Use |
|-----------|---------|-----|
| [GLM](https://github.com/g-truc/glm) | MIT (or Happy Bunny) | Header-only vector and matrix math: the system package (1.0.1+), else fetched at build time (pinned release) |
| [librw](https://github.com/aap/librw) | MIT | Fetched at build time (pinned commit, one patch in `cmake/patches/`); RenderWare stream parsing (DFF, TXD) |
| [SDL3](https://github.com/libsdl-org/SDL) | zlib | Window, input, display modes. Linked statically on Linux when fetched |
| [Vulkan loader / headers](https://github.com/KhronosGroup) | Apache-2.0 | GPU API; `glslangValidator` compiles the shaders at build time |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | LGPL-2.1+ | Audio. Linked dynamically (the system's `libopenal.so.1` / `OpenAL32.dll`) |
| [FFmpeg](https://ffmpeg.org) `libavcodec/bink.c`, `binkaudio.c`, `binkdsp.c`, `libavformat/bink.c` | LGPL-2.1+ | Ported to C++ in `Codec/`, which stays under LGPL-2.1-or-later (`Codec/COPYING.LGPL-2.1`). FFmpeg itself is neither linked nor required |

Reference material that was read but not copied: Sor3nt's file-format toolkit (the `.mls` script container
and `mapAI.grf` layouts) and the Cook-Torrance/GGX shading formulation in the textbook form used by
many open renderers (`Renderer/backend/shading.glsl`).
