// Assets/font.cpp - the game bitmap font parser. See font.h.
#include "Assets/font.h"

#include "Assets/binary_reader.h"

#include <cstring>

namespace Assets {

std::expected<std::vector<Font>, Engine::EngineError> loadFonts(const std::string& path)
{
    using Engine::EngineError;
    auto bytes = readFileBytes(path);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));

    ByteReader r(*bytes);
    std::vector<Font> fonts;
    while (!r.eof())
    {
        const auto fail = [&](const char* what) {
            return std::unexpected(EngineError::failure(
                EngineError::Code::Invalid, std::string("font.dat: ") + what + " at offset " + std::to_string(r.offset())));
        };
        if (r.remaining() < 6 || std::memcmp(bytes->data() + r.offset(), "<FONT>", 6) != 0)
            return fail("missing <FONT> tag");
        r.skip(6);
        Font f;
        uint32 mapSize = 0, reserved = 0, glyphCount = 0;
        if (!r.readU32(mapSize) || !r.readU32(reserved) || !r.readU32(f.id) || !r.readU32(glyphCount) ||
            !r.readU32(f.pixelHeight) || !r.readF32(f.lineHeight))
            return fail("truncated header");
        if (mapSize > r.remaining() / 4)
            return fail("map too large");
        f.map.resize(mapSize);
        for (int32& m : f.map)
            r.readI32(m);
        if (glyphCount > r.remaining() / 24)
            return fail("glyph table too large");
        f.glyphs.resize(glyphCount);
        for (FontGlyph& g : f.glyphs)
            if (!r.readU32(g.code) || !r.readF32(g.advance) || !r.readF32(g.u0) || !r.readF32(g.v0) ||
                !r.readF32(g.u1) || !r.readF32(g.v1))
                return fail("truncated glyph");
        fonts.push_back(std::move(f));
    }
    return fonts;
}

} // namespace Assets
