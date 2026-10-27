// Game/save_slots.h - the eight save slots of LOAD GAME and SAVE GAME (the eight slot .SAV files).
//
// The exe lists the slots it found (0x7d8f8c + 0x54 * i: -1 corrupt, 0 a save, 1 empty) with
// the scene each continues (0x606ac0), its state (0x606aa0: COMPLETE for a finished scene, else
// IN PROGRESS) and difficulty (0x606a80). A save's header carries the slot title
// ("0)SCENE 1-(IN PROGRESS)"), the scene (0x90) and its state (0x94, 0x7d6ae8); the state block
// after it is the settings block and the scene records (Game/progress.h).
//
// The slots live in the game's save folder as the original's do (`ownDir`; a second folder,
// `originalDir`, is read for a slot `ownDir` has no file for). The first write over an existing
// file keeps a copy of it as .bak (ours, keepBackup).
#pragma once
#include "engine_types.h"

#include <array>
#include <string>

namespace Game {

class GameProgress;
struct SaveFile;
struct GameSettings;

struct SaveSlotInfo {
    enum class Status { Empty, Corrupt, Saved };
    Status status = Status::Empty;
    std::string path;       // the file this slot reads
    int32 scene = 0;        // the save's scene (0x90)
    int32 state = -1;       // its state (0x94): -2 new game, -1 finished, else a checkpoint
    int32 difficulty = 0;   // 0 FETISH, 1 HARDCORE
};

class SaveSlots {
public:
    static constexpr int32 kSlots = 8;

    void setDirs(std::string ownDir, std::string originalDir);
    // The key bindings every save keeps as its last buffer (Settings.dat's first 0x130 bytes,
    // 0x710bf4); owned by the caller.
    void setBindings(const std::array<uint8, 0x130>* bindings) noexcept { bindings_ = bindings; }
    // Read every slot's file again.
    void refresh();
    [[nodiscard]] const SaveSlotInfo& slot(int32 i) const noexcept { return slots_[static_cast<std::size_t>(i)]; }

    // Slot `i` into the progress (records, scene and state) and the difficulty setting.
    [[nodiscard]] bool load(int32 i, GameProgress& progress, GameSettings& settings) const;
    // Write slot `i` (into ownDir): the progress, the settings block and its header. `title` is the
    // slot title the menu shows ("1)SCENE 2-(COMPLETE)").
    // `checkpoint`: a save point's level (entity block and buffers 3..7); none: a save between
    // scenes, with an empty level as the game's own.
    [[nodiscard]] bool save(int32 i, const GameProgress& progress, const GameSettings& settings, const std::string& title,
                            const SaveFile* checkpoint = nullptr);

private:
    std::string ownDir_, originalDir_;
    std::array<SaveSlotInfo, kSlots> slots_{};
    const std::array<uint8, 0x130>* bindings_ = nullptr;
};

} // namespace Game
