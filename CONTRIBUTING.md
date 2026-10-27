# Contributing

Thanks for helping. Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) first; it explains
where things live and how behaviour is researched.

## The four hard rules

Breaking these gets a pull request closed, whatever else it does. They protect the
project's existence.

1. **No game content.** Never commit, attach or paste any of the original game's data or code. That covers
   models, textures, levels, scripts, audio, video, text and saves. It covers screenshots of
   extracted assets in the repository, and any part of the game's executable, including binary blobs
   and hex dumps of its code. Screenshots of the engine running are fine in issues and pull
   requests.
2. **No decompiler output.** Do not paste Ghidra/IDA pseudo-code, or code transliterated from
   it line by line. Write down what a routine does, then implement that in this engine's own
   structure and style. Cite the address. Short constants and the facts they encode (a range,
   a rate, an id table) are fine, with their address.
3. **No code under an incompatible license.** Everything outside `Codec/` is MIT. Code ported
   from LGPL sources (like FFmpeg) goes only in `Codec/` and keeps its LGPL header. Code
   without a clear license is not accepted. By opening a pull request you agree to license
   your contribution under the license of the files it touches.
4. **Never use the original game's title.** Don't use it in code, comments, identifiers, file
   names, commit messages or docs; say "the game", "the original" or "the exe". Names of its own
   files that the engine needs (the executable, saves, the tuning archive) are read from the
   player's executable at run time (`Game/exe_tables.h`), never spelled in the source. Data
   names such as level folders and script identifiers are fine.

## Before you open a pull request

- [ ] It builds without new warnings, on Linux at least. CI builds it and runs the tests.
- [ ] `ctest --test-dir <build> --output-on-failure` passes with the game data installed, if you
      have it. Say in the pull request whether you ran the data tests.
- [ ] Logic you added or changed has a test, where it can be tested without a GPU (most of
      `Game/` can).
- [ ] If you touched the script compiler or natives, `script_check` still compiles 933 of the
      934 scripts.
- [ ] You ran the game for what you changed, and the pull request says how: level, options,
      what you saw.
- [ ] No unrelated changes: no reformatting of code you didn't touch, no drive-by renames.

## Pull requests

- **One topic per pull request.** A behaviour, a bug, a refactor: not all three. Large work is
  easier to review as a series.
- **The description says:**
  - *What* changed and *why*, including the bug or behaviour, and the level to see it in.
  - *Evidence:* the exe addresses you traced, logs, before and after screenshots.
  - *What you did not do* or are unsure of.
- **Commits:** imperative subject of 72 characters or fewer (`Hunters: step sounds by floor
  material`), with a body explaining why when it isn't obvious. Squash fix-up commits before
  review.
- **Review:** at least one maintainer approval. The author resolves conversations; the
  maintainer merges (squash or rebase, no merge commits on `main`).
- **Disagreements on behaviour** are settled by the original: the exe, the data, or a recording
  of the original game. Not by taste.

## Code style

Match the code around you; the existing code is the style guide. In short:

- C++23. 4 spaces. Opening braces on their own line for functions and types. 120 columns
  soft limit.
- `snake_case` files, `PascalCase` types, `camelCase` functions and variables, `member_`
  trailing underscore, `kConstant` constants, namespaces `Game`, `Renderer`, `Assets`, `Engine`.
- The engine's fixed-width aliases (`int32`, `uint8`, `float32`, from `engine_types.h`).
- Errors as return values (`std::expected`, `std::optional`, `bool`), not exceptions, in engine
  code.
- Comments say *why*, or *what the original does* (with its address). Don't narrate the code.
  Mark the engine's own choices as such ("ours").
- Paths into the game data go through `Engine::Platform::resolvePath`, so they work on
  case-sensitive file systems.
- No new dependencies without discussing it in an issue first.

## Reporting bugs

Open an issue with the level, what you did, what happened, and what the original game does. Attach
the engine's log: it writes to stderr, so run `Reelhunt ... 2> log.txt`. Add your OS and GPU.
Saves are welcome only if they are your own.

## Research notes

Findings that don't become code yet (a routine's role, a structure's layout) are welcome as
issues labelled `research`, with addresses. That keeps the knowledge in the project instead
of in one person's notes.
