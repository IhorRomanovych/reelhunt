// Game/save_slots.cpp - the save slots (see save_slots.h).
#include "Game/save_slots.h"

#include "Game/exe_tables.h"
#include "Game/progress.h"
#include "Game/save_system.h"
#include "Game/settings.h"

#include <cstring>
#include <filesystem>

namespace Game {

namespace {

constexpr std::size_t kSceneWord = 0x90;
constexpr std::size_t kStateWord = 0x94;
constexpr std::size_t kDifficulty = 0x5c; // in the state block: the settings block's last field

// The slot's file name, as the game's executable names it (Game/exe_tables.h).
const std::string& slotName(int32 i)
{
    return gameExe().saveNames[static_cast<std::size_t>(i)];
}

} // namespace

void SaveSlots::setDirs(std::string ownDir, std::string originalDir)
{
    ownDir_ = std::move(ownDir);
    originalDir_ = std::move(originalDir);
}

void SaveSlots::refresh()
{
    namespace fs = std::filesystem;
    for (int32 i = 0; i < kSlots; ++i)
    {
        SaveSlotInfo& s = slots_[static_cast<std::size_t>(i)];
        s = SaveSlotInfo{};
        const std::string own = (fs::path(ownDir_) / slotName(i)).string();
        const std::string original = originalDir_.empty() ? std::string() : (fs::path(originalDir_) / slotName(i)).string();
        std::error_code ec;
        s.path = fs::exists(own, ec) || original.empty() || !fs::exists(original, ec) ? own : original;
        if (!fs::exists(s.path, ec))
            continue;
        SaveFile f;
        if (!loadSaveFile(s.path, f) || !f.stateBlock())
        {
            s.status = SaveSlotInfo::Status::Corrupt;
            continue;
        }
        s.status = SaveSlotInfo::Status::Saved;
        s.scene = static_cast<int32>(f.headerWord(kSceneWord));
        s.state = static_cast<int32>(f.headerWord(kStateWord));
        std::memcpy(&s.difficulty, f.stateBlock() + kDifficulty, sizeof(s.difficulty));
    }
}

bool SaveSlots::load(int32 i, GameProgress& progress, GameSettings& settings) const
{
    SaveFile f;
    if (i < 0 || i >= kSlots || !loadSaveFile(slots_[static_cast<std::size_t>(i)].path, f) || !f.stateBlock())
        return false;
    progress.readStateBlock(f.stateBlock(), SaveFile::kStateBlockSize);
    progress.setLast(static_cast<int32>(f.headerWord(kSceneWord)), static_cast<int32>(f.headerWord(kStateWord)));
    std::memcpy(&settings.difficulty, f.stateBlock() + kDifficulty, sizeof(settings.difficulty));
    return true;
}

bool SaveSlots::save(int32 i, const GameProgress& progress, const GameSettings& settings, const std::string& title,
                     const SaveFile* checkpoint)
{
    if (i < 0 || i >= kSlots)
        return false;
    SaveFile f;
    if (checkpoint)
        f = *checkpoint;
    f.header.assign(SaveLayout::buffer1, 0);
    f.buffer3.resize(SaveLayout::buffer3, 0);
    f.buffer4.resize(SaveLayout::buffer4, 0);
    f.buffer5.resize(SaveLayout::buffer5, 0);
    f.buffer6.resize(SaveLayout::buffer6, 0);
    f.buffer7.resize(SaveLayout::buffer7, 0);
    if (bindings_)
        std::memcpy(f.buffer7.data(), bindings_->data(), SaveLayout::buffer7);
    // The first words as the game's own saves have them.
    f.setHeaderWord(0x4, 0x82);
    f.setHeaderWord(0x8, 0x41);
    f.setTitle(title);
    f.setResolution(640, 480);
    f.setHeaderWord(kSceneWord, static_cast<std::uint32_t>(progress.lastScene()));
    f.setHeaderWord(kStateWord, static_cast<std::uint32_t>(progress.sceneState()));
    static_assert(sizeof(GameSettings) == 0x60);
    std::memcpy(f.stateBlock(), &settings, sizeof(GameSettings));
    progress.writeStateBlock(f.stateBlock(), SaveFile::kStateBlockSize);
    std::error_code ec;
    std::filesystem::create_directories(ownDir_, ec);
    const std::string path = (std::filesystem::path(ownDir_) / slotName(i)).string();
    keepBackup(path);
    if (!writeSaveFile(path, f))
        return false;
    refresh();
    return true;
}

} // namespace Game
