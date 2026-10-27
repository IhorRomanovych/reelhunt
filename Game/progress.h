// Game/progress.h - which scenes are done and their best ratings, and what that unlocks.
//
// The exe keeps a record per scene (24: 20 story scenes, 4 bonus) at 0x7d6b50 + 0xb0 * i:
// +0 done (byte), +4 the best rating in stars, +8 the inventory carried on (0x5d24c0). They
// follow the settings block 0x7d6af0, and the two together (0x10e0 bytes) are what a save's
// state block copies (Game/save_system.h): the records at 0x60 + 0xb0 * i there.
//   0x5d2320: a won scene is done; its best rating is the first one, then the highest.
//   0x5d20b0: scene i can be played when i is 0 or scene i - 1 is done.
//   0x5d6d60 / 0x5d6da0 / 0x5d6de0 / 0x5d6e20: the scene list's groups 5-8 / 9-12 / 13-16 /
//   17-20 open when the four scenes before them are done.
//   0x5d6af0: bonus scene 20 + k opens when story scenes 5k .. 5k + 4 all have 3 stars or more
//   (0x7c8f30, a cheat, opens them all); 0x5d6c70: the BONUS group when any is open.
// With them go the scene last played, 0x7d6ae4 (-1 before any game), and its state, 0x7d6ae8:
// -2 a new game, -1 the scene was finished, a scene index for a save point's checkpoint. A
// save keeps the two in its header (0x90, 0x94). Nothing of this outlives the program but a save.
#pragma once
#include "engine_types.h"

#include <array>
#include <cstdint>
#include <vector>

#include "Game/save_system.h"

namespace Game {

class GameProgress {
public:
    static constexpr int32 kScenes = 24;
    static constexpr int32 kStory = 20;
    static constexpr int32 kNewGame = -2;      // 0x7d6ae8 values
    static constexpr int32 kSceneDone = -1;

    struct Scene {
        bool done = false;
        int32 best = 0; // stars
        // The inventory the scene was won with (record +8, 0x5d24a0), and whether this record
        // holds one (ours: a record never stored keeps the bytes it was read with).
        std::array<SaveInventorySlot, 4> carried{};
        bool hasCarried = false;
    };

    [[nodiscard]] const Scene& scene(int32 i) const noexcept { return scenes_[static_cast<std::size_t>(i)]; }
    // A scene was won with `stars` (0x5d2320).
    void complete(int32 i, int32 stars) noexcept;
    // The inventory a won scene ends with (0x5d24a0, after 0x5d2320).
    void storeInventory(int32 i, const std::array<SaveInventorySlot, 4>& slots);
    // The inventory scene i starts with when it is not resumed from its checkpoint (0x5d2590):
    // scenes 1, 2, 4, 6, 12, 15, 16 and 18 (0-based) take the one scene i - 1 was won with.
    [[nodiscard]] const std::array<SaveInventorySlot, 4>* carriedInto(int32 i) const noexcept;

    [[nodiscard]] bool playable(int32 i) const noexcept;   // 0x5d20b0 / 0x5d6af0
    [[nodiscard]] bool groupOpen(int32 g) const noexcept;  // scene list group 0..5 (5 = BONUS)
    [[nodiscard]] bool bonusOpen(int32 i) const noexcept;  // 0x5d6af0, i = 20..23
    // The sum of the story scenes' best ratings (0x5daac0, GAME COMPLETED's "n / 100").
    [[nodiscard]] int32 totalStars() const noexcept;
    // Ours (--unlock-all): every scene playable, as the exe's 0x7c8f30 does for the bonus ones.
    void setUnlockAll(bool on) noexcept { unlockAll_ = on; }

    // The scene last played and its state (0x7d6ae4 / 0x7d6ae8).
    [[nodiscard]] int32 lastScene() const noexcept { return lastScene_; }
    [[nodiscard]] int32 sceneState() const noexcept { return sceneState_; }
    void setLast(int32 scene, int32 state) noexcept { lastScene_ = scene; sceneState_ = state; }
    // A new game (0x5d2010, then scene 0 in state -2): every record cleared.
    void newGame() noexcept;
    // What PLAY continues (0x5d6900): -1 before any game, else the later of the scene last played
    // and the last one open (scene 20 once 19 is open and not done).
    [[nodiscard]] int32 continueScene() const noexcept;
    // The scene a loaded save starts (0x606b00): the next one after a finished scene, the first
    // of a new game, else its own.
    [[nodiscard]] int32 resumeScene() const noexcept;

    // The records in a save's 0x10e0-byte state block.
    void readStateBlock(const std::uint8_t* state, std::size_t size);
    void writeStateBlock(std::uint8_t* state, std::size_t size) const;

private:
    std::array<Scene, kScenes> scenes_{};
    int32 lastScene_ = -1;
    int32 sceneState_ = kSceneDone;
    bool unlockAll_ = false;
};

} // namespace Game
