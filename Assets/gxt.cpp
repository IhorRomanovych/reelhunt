// Assets/gxt.cpp - the game text parser. See gxt.h.
#include "Assets/gxt.h"

#include "Assets/binary_reader.h"

#include <cstring>

namespace Assets {

std::expected<Gxt, Engine::EngineError> Gxt::load(const std::string& path)
{
    using Engine::EngineError;
    auto bytes = readFileBytes(path);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));
    const std::span<const uint8_t> d(*bytes);

    auto invalid = [&](const char* what) {
        return std::unexpected(EngineError::failure(EngineError::Code::Invalid, std::string("gxt: ") + what + " in " + path));
    };
    uint32 keyBytes = 0, dataBytes = 0;
    if (d.size() < 8 || std::memcmp(d.data(), "TKEY", 4) != 0 || !readAt(d, 4, keyBytes))
        return invalid("missing TKEY");
    const std::size_t tdat = 8u + keyBytes;
    if (tdat + 8 > d.size() || std::memcmp(d.data() + tdat, "TDAT", 4) != 0 || !readAt(d, tdat + 4, dataBytes) ||
        dataBytes > d.size() - tdat - 8)
        return invalid("missing TDAT");
    const std::span<const uint8_t> body = d.subspan(tdat + 8, dataBytes);

    Gxt gxt;
    for (std::size_t e = 0; e + 12 <= keyBytes; e += 12)
    {
        uint32 off = 0;
        readAt(d, 8 + e, off);
        const char* k = reinterpret_cast<const char*>(d.data() + 8 + e + 4);
        const std::string key(k, strnlen(k, 8));
        std::string text;
        for (std::size_t i = off; i + 1 < body.size(); i += 2)
        {
            const uint16 c = static_cast<uint16>(body[i] | (body[i + 1] << 8));
            if (c == 0)
                break;
            text.push_back(c < 256 ? static_cast<char>(c) : '?');
        }
        gxt.strings_[key] = std::move(text);
    }
    return gxt;
}

std::string Gxt::text(const std::string& key) const
{
    const auto it = strings_.find(key);
    return it == strings_.end() ? key : it->second;
}

} // namespace Assets
