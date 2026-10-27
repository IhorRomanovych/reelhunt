// Assets/pak.cpp - the tuning pak reader. See pak.h.
#include "Assets/pak.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "Assets/binary_reader.h"

namespace Assets {

using Engine::EngineError;

namespace {

std::string pakKey(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    std::string out;
    for (char c : name)
        if (!(c == '/' && !out.empty() && out.back() == '/')) // "//" -> "/"
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (out.rfind("./", 0) == 0)
        out.erase(0, 2);
    while (!out.empty() && out.front() == '/')
        out.erase(0, 1);
    return out;
}

} // namespace

std::expected<PakFile, EngineError> PakFile::load(const std::string& path)
{
    auto bytes = readFileBytes(path);
    if (!bytes)
        return std::unexpected(bytes.error());
    return parse(*bytes);
}

std::expected<PakFile, EngineError> PakFile::parse(const std::vector<uint8_t>& bytes)
{
    constexpr std::size_t kEntrySize = 276;
    ByteReader r(bytes.data(), bytes.size());
    uint32 magic = 0, version = 0, count = 0;
    if (!r.read(magic) || magic != 0x4B50484Du /* "MHPK" */ || !r.read(version) || !r.read(count))
        return std::unexpected(EngineError::failure(EngineError::Code::Invalid, "not an MHPK file"));
    if (count > (bytes.size() - 12) / kEntrySize)
        return std::unexpected(EngineError::failure(EngineError::Code::Invalid, "MHPK index runs past the file"));

    PakFile pak;
    for (uint32 i = 0; i < count; ++i)
    {
        const std::size_t at = 12 + i * kEntrySize;
        const char* raw = reinterpret_cast<const char*>(bytes.data() + at);
        std::string name(raw, strnlen(raw, 260));
        uint32 size = 0, offset = 0;
        std::memcpy(&size, bytes.data() + at + 260, 4);
        std::memcpy(&offset, bytes.data() + at + 264, 4);
        if (offset > bytes.size() || size > bytes.size() - offset)
            return std::unexpected(EngineError::failure(EngineError::Code::Invalid, "MHPK entry '" + name + "' runs past the file"));
        Entry e;
        e.key = pakKey(name);
        e.name = std::move(name);
        e.data.resize(size);
        for (uint32 b = 0; b < size; ++b)
            e.data[b] = static_cast<char>(bytes[offset + b] ^ 0x7Fu);
        pak.entries_.push_back(std::move(e));
    }
    return pak;
}

std::optional<std::string> PakFile::file(const std::string& name) const
{
    const std::string key = pakKey(name);
    for (const Entry& e : entries_)
        if (e.key == key)
            return e.data;
    return std::nullopt;
}

std::vector<std::string> PakFile::names() const
{
    std::vector<std::string> out;
    for (const Entry& e : entries_)
        out.push_back(e.name);
    return out;
}

} // namespace Assets
