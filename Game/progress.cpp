// Game/progress.cpp - scene progress (see progress.h).
#include "Game/progress.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace Game {

namespace {

constexpr std::size_t kRecords = 0x60;  // after the settings block
constexpr std::size_t kRecordSize = 0xb0;

} // namespace

void GameProgress::complete(int32 i, int32 stars) noexcept
{
    if (i < 0 || i >= kScenes)
        return;
    Scene& s = scenes_[static_cast<std::size_t>(i)];
    s.best = s.done ? std::max(s.best, stars) : stars;
    s.done = true;
}

void GameProgress::storeInventory(int32 i, const std::array<SaveInventorySlot, 4>& slots)
{
    if (i < 0 || i >= kScenes)
        return;
    Scene& s = scenes_[static_cast<std::size_t>(i)];
    s.carried = slots;
    s.hasCarried = true;
}

const std::array<SaveInventorySlot, 4>* GameProgress::carriedInto(int32 i) const noexcept
{
    constexpr int32 kCarry[] = {1, 2, 4, 6, 12, 15, 16, 18};
    if (std::find(std::begin(kCarry), std::end(kCarry), i) == std::end(kCarry))
        return nullptr;
    const Scene& prev = scenes_[static_cast<std::size_t>(i - 1)];
    return prev.done ? &prev.carried : nullptr;
}

bool GameProgress::bonusOpen(int32 i) const noexcept
{
    if (unlockAll_)
        return true;
    if (i < kStory || i >= kScenes)
        return false;
    const int32 first = (i - kStory) * 5;
    for (int32 k = first; k < first + 5; ++k)
        if (scenes_[static_cast<std::size_t>(k)].best <= 2)
            return false;
    return true;
}

bool GameProgress::playable(int32 i) const noexcept
{
    if (i >= kStory)
        return bonusOpen(i);
    return unlockAll_ || i == 0 || (i > 0 && i < kScenes && scenes_[static_cast<std::size_t>(i - 1)].done);
}

bool GameProgress::groupOpen(int32 g) const noexcept
{
    if (g == 0)
        return true;
    if (g == 5)
    {
        for (int32 i = kStory; i < kScenes; ++i)
            if (bonusOpen(i))
                return true;
        return false;
    }
    if (unlockAll_)
        return true;
    for (int32 i = (g - 1) * 4; i < g * 4; ++i)
        if (!scenes_[static_cast<std::size_t>(i)].done)
            return false;
    return true;
}

int32 GameProgress::totalStars() const noexcept
{
    int32 n = 0;
    for (int32 i = 0; i < kStory; ++i)
        n += scenes_[static_cast<std::size_t>(i)].best;
    return n;
}

void GameProgress::newGame() noexcept
{
    scenes_ = {};
    lastScene_ = 0;
    sceneState_ = kNewGame;
}

int32 GameProgress::continueScene() const noexcept
{
    int32 scene = lastScene_;
    if (scene != -1)
    {
        for (int32 i = 0; i < kStory; ++i)
            if (!playable(i))
            {
                scene = std::max(scene, i - 1);
                break;
            }
    }
    if (scene == 18 && playable(19) && !scenes_[19].done)
        scene = 19;
    return scene;
}

int32 GameProgress::resumeScene() const noexcept
{
    if (sceneState_ < 0 && lastScene_ != 19 && lastScene_ != 23)
        return sceneState_ == kNewGame ? 0 : lastScene_ + 1;
    return lastScene_;
}

void GameProgress::readStateBlock(const std::uint8_t* state, std::size_t size)
{
    for (int32 i = 0; i < kScenes; ++i)
    {
        const std::size_t at = kRecords + kRecordSize * static_cast<std::size_t>(i);
        if (at + 8 > size)
            break;
        Scene& s = scenes_[static_cast<std::size_t>(i)];
        s.done = state[at] != 0;
        std::int32_t best = 0;
        std::memcpy(&best, state + at + 4, sizeof(best));
        s.best = best;
        if (at + kRecordSize <= size)
        {
            s.carried = readInventory(state + at + 8, kRecordSize - 8);
            s.hasCarried = true;
        }
    }
}

void GameProgress::writeStateBlock(std::uint8_t* state, std::size_t size) const
{
    for (int32 i = 0; i < kScenes; ++i)
    {
        const std::size_t at = kRecords + kRecordSize * static_cast<std::size_t>(i);
        if (at + 8 > size)
            break;
        const Scene& s = scenes_[static_cast<std::size_t>(i)];
        state[at] = s.done ? 1 : 0;
        const std::int32_t best = s.best;
        std::memcpy(state + at + 4, &best, sizeof(best));
        if (s.hasCarried && at + kRecordSize <= size)
            writeInventory(state + at + 8, kRecordSize - 8, s.carried);
    }
}

} // namespace Game
