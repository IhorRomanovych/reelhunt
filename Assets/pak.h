// Assets/pak.h - the game data container (levels/GLOBAL/DATA/*.pak).
//
// Layout (little-endian), from Sor3nt's file-format toolkit Archive/Pak.php:
//   char[4] "MHPK", u32 version (0x00020000), u32 count
//   count x { char name[260], u32 size, u32 offset, u32 unknown, u32 crc }
//   file data at each offset, every byte XORed with 0x7F.
// It holds the game's tuning files: levels/global/data/{aiTypeData, AISounds, WeaponTypeData,
// ShotTypeData, ...}.ini and, per level, entityTypeData.ini / levelSetup.ini / spl/splines.ini.
#pragma once
#include "engine_types.h"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace Assets {

class PakFile {
public:
    static std::expected<PakFile, Engine::EngineError> load(const std::string& path);
    static std::expected<PakFile, Engine::EngineError> parse(const std::vector<uint8_t>& bytes);

    // The decoded file, looked up case-insensitively by its path inside the pak
    // ("levels/global/data/aiTypeData.ini"; a leading "./" is optional).
    [[nodiscard]] std::optional<std::string> file(const std::string& name) const;
    [[nodiscard]] std::vector<std::string> names() const;

private:
    struct Entry {
        std::string key;  // lower-case, without "./"
        std::string name; // as stored
        std::string data; // decoded
    };
    std::vector<Entry> entries_;
};

} // namespace Assets
