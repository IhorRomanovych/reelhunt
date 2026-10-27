// Assets/gxt.h - the game text (levels/GLOBAL/PC_TEXT/pc_game*.gxt).
//
// Layout (little-endian): "TKEY" u32 keyBytes, then keyBytes / 12 entries of
// { u32 offset into TDAT body, char key[8] (NUL-padded) }; then "TDAT" u32 dataBytes and
// NUL-terminated UTF-16LE strings. Button glyphs are inline tokens such as "~cross~".
#pragma once
#include "engine_types.h"

#include <expected>
#include <string>
#include <unordered_map>

namespace Assets {

class Gxt {
public:
    static std::expected<Gxt, Engine::EngineError> load(const std::string& path);

    // The string for `key`, converted to Latin-1/ASCII (non-Latin-1 code points become '?').
    // Returns `key` itself when it is missing, so a gap is visible instead of blank.
    [[nodiscard]] std::string text(const std::string& key) const;
    [[nodiscard]] bool contains(const std::string& key) const { return strings_.contains(key); }
    [[nodiscard]] std::size_t size() const noexcept { return strings_.size(); }

private:
    std::unordered_map<std::string, std::string> strings_;
};

} // namespace Assets
