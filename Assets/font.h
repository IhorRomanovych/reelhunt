// Assets/font.h - the game bitmap fonts (initscripts/FONTS/font.dat).
//
// The file is a sequence of font blocks, each:
//   "<FONT>"  u32 mapSize  u32 reserved  u32 fontId  u32 glyphCount  u32 pixelHeight  f32 lineHeight
//   i32 map[mapSize]                    character code -> glyph index (-1 = no glyph)
//   glyphCount x { u32 code, f32 advance, f32 u0, f32 v0, f32 u1, f32 v1 }
// Advances and line heights are in "frontend units" where the screen is 1.0 wide and tall; a
// glyph's on-screen size is its atlas pixel size / 512 on both axes. Font id 2 is the menu's
// LCD font (texture "lcd" in pictures/MENU/*_ep.txd); font id 0 is the console font.
#pragma once
#include "engine_types.h"

#include <expected>
#include <string>
#include <vector>

namespace Assets {

struct FontGlyph {
    uint32 code = 0;
    float32 advance = 0.0f;
    float32 u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

struct Font {
    uint32 id = 0;
    uint32 pixelHeight = 0;
    float32 lineHeight = 0.0f;
    std::vector<int32> map;            // code -> index into glyphs, -1 when absent
    std::vector<FontGlyph> glyphs;

    [[nodiscard]] const FontGlyph* glyph(unsigned char c) const noexcept {
        if (c >= map.size() || map[c] < 0 || static_cast<std::size_t>(map[c]) >= glyphs.size())
            return nullptr;
        return &glyphs[static_cast<std::size_t>(map[c])];
    }
};

std::expected<std::vector<Font>, Engine::EngineError> loadFonts(const std::string& path);

} // namespace Assets
