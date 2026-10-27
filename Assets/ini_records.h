// Assets/ini_records.h - the record files in the tuning pak (aiTypeData.ini, WeaponTypeData.ini,
// entityTypeData.ini, ...):
//   # comment
//   RECORD <name>
//       KEY  value[, value ...]     values are separated by blanks and/or commas
//       ...
//   END
// A record may repeat a key (SET_ALERT_LEVEL, ACTION_TRANSITION, SKIN_TEX); lines() keeps them
// all in order. Numbers may carry a C float suffix ("1.3f").
#pragma once
#include "engine_types.h"

#include <optional>
#include <string>
#include <vector>

namespace Assets {

struct IniRecord {
    std::string name;
    std::vector<std::vector<std::string>> lines; // tokens; [0] is the key as written

    // The first line whose key matches (case-insensitively), or nullptr.
    [[nodiscard]] const std::vector<std::string>* line(const std::string& key) const;
    // Every line whose key matches, in file order.
    [[nodiscard]] std::vector<const std::vector<std::string>*> all(const std::string& key) const;
    // Value `index` (1 = the first after the key) of `key` as a number / text.
    [[nodiscard]] std::optional<float32> number(const std::string& key, std::size_t index = 1) const;
    [[nodiscard]] std::optional<std::string> text(const std::string& key, std::size_t index = 1) const;
};

std::vector<IniRecord> parseIniRecords(const std::string& text);
// The first record called `name` (case-insensitive), or nullptr.
const IniRecord* findRecord(const std::vector<IniRecord>& records, const std::string& name);
// "12.5", "1.3f", "06" -> number.
std::optional<float32> parseIniNumber(const std::string& token);
bool iequals(const std::string& a, const std::string& b);

} // namespace Assets
