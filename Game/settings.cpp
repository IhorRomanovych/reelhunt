// Game/settings.cpp - the player's settings and Settings.dat. See settings.h.
#include "Game/settings.h"

#include "Game/exe_tables.h"
#include "Game/save_system.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Game {

namespace {

} // namespace

std::string GameSettings::globalTextSuffix() const
{
    static const char* kSuffix[] = {"", "_ger", "_fre", "_ita", "_spa"};
    return language >= 0 && language < 5 ? kSuffix[language] : "";
}

std::string GameSettings::levelTextSuffix() const
{
    static const char* kSuffix[] = {"", "_g", "_f", "_i", "_s"};
    return language >= 0 && language < 5 ? kSuffix[language] : "";
}

SettingsFile SettingsFile::defaults()
{
    SettingsFile f;
    // The key tables come from the game's executable (Game/exe_tables.h); unbound until it is read.
    std::memcpy(f.bindingsA.data(), gameExe().defaultBindings.data(), 0x130);
    std::memcpy(f.bindingsB.data(), gameExe().defaultBindings.data() + 0x130, 0x130);
    return f;
}

bool SettingsFile::load(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // 0x605e50: 0x130, 0x60 and 0x130 bytes, nothing after.
    if (bytes.size() != 0x130 + sizeof(GameSettings) + 0x130)
        return false;
    std::memcpy(bindingsA.data(), bytes.data(), 0x130);
    std::memcpy(&settings, bytes.data() + 0x130, sizeof(GameSettings));
    std::memcpy(bindingsB.data(), bytes.data() + 0x130 + sizeof(GameSettings), 0x130);
    return true;
}

bool SettingsFile::save(const std::string& path) const
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    keepBackup(path);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(bindingsA.data()), 0x130);
    out.write(reinterpret_cast<const char*>(&settings), sizeof(GameSettings));
    out.write(reinterpret_cast<const char*>(bindingsB.data()), 0x130);
    return static_cast<bool>(out);
}

} // namespace Game
