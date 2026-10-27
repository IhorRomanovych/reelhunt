// Game/save_system.cpp - the game save file (*.SAV) read/write. See save_system.h.
#include "Game/save_system.h"
#include "Core/platform.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace
{
    std::uint32_t readU32(const std::uint8_t* p)
    {
        std::uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }
    float readF32(const std::uint8_t* p)
    {
        const std::uint32_t u = readU32(p);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    void appendU32(std::vector<std::uint8_t>& o, std::uint32_t v)
    {
        o.push_back(static_cast<std::uint8_t>(v & 0xff));
        o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        o.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
        o.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
    }
    void appendF32(std::vector<std::uint8_t>& o, float f)
    {
        std::uint32_t u;
        std::memcpy(&u, &f, 4);
        appendU32(o, u);
    }
    void appendCString(std::vector<std::uint8_t>& o, const std::string& s)
    {
        o.insert(o.end(), s.begin(), s.end());
        o.push_back(0);
        while (o.size() & 3u)
            o.push_back(0);
    }

    // Reads a NUL-terminated C-string at `p` (within `[p, end)`) into `out` and returns the
    // number of bytes consumed INCLUDING padding to the next 4-byte boundary relative to
    // `base`. Returns 0 if no NUL is found (error).
    std::size_t readCString(const std::uint8_t* base, const std::uint8_t* p,
                            const std::uint8_t* end, std::string& out)
    {
        std::size_t n = 0;
        while (p + n < end && p[n] != 0)
            ++n;
        if (p + n >= end)
            return 0;
        ++n; // include the NUL
        out.assign(reinterpret_cast<const char*>(p), n - 1);
        const std::size_t off = static_cast<std::size_t>(p - base);
        const std::size_t padded = (off + n + 3u) & ~std::size_t(3u);
        return padded - off;
    }

    // Finds the 7-byte "wibble\0" marker starting at `p` (within `[p, end)`); returns the
    // offset from `p`, or (end - p) if absent.
    std::size_t findWibble(const std::uint8_t* p, const std::uint8_t* end)
    {
        static const std::uint8_t sig[7] = {'w', 'i', 'b', 'b', 'l', 'e', 0};
        for (const std::uint8_t* q = p; q + 7 <= end; ++q)
        {
            if (std::memcmp(q, sig, 7) == 0)
                return static_cast<std::size_t>(q - p);
        }
        return static_cast<std::size_t>(end - p);
    }

    // Parses the [count][size array][blobs] prefix of the entity block. On success sets
    // `blobEnd` to the offset (relative to `b`) where the blob run ends; any bytes past it
    // are a trailing region preserved raw by the caller.
    bool parseEntityBlock(const std::uint8_t* b, std::size_t size,
                          std::vector<Game::SaveEntityRecord>& out, std::size_t& blobEnd)
    {
        if (size < 4u)
            return false;
        const std::uint32_t count = readU32(b);
        if (size < 4u + static_cast<std::size_t>(count) * 4u)
            return false;
        std::vector<std::uint32_t> sizes(count);
        for (std::uint32_t i = 0; i < count; ++i)
            sizes[i] = readU32(b + 4u + static_cast<std::size_t>(i) * 4u);

        std::size_t off = 4u + static_cast<std::size_t>(count) * 4u;
        out.clear();
        out.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const std::size_t sz = sizes[i];
            if (off + sz > size)
                return false;
            const std::uint8_t* rec = b + off;
            const std::uint8_t* end = rec + sz;
            Game::SaveEntityRecord r;
            const std::uint8_t* p = rec;

            std::size_t c = readCString(rec, p, end, r.name);
            if (c == 0)
                return false;
            p += c;

            // A 4-byte "NUL\0" blob is the null/empty entity marker: no subfields.
            if (p != end)
            {
                c = readCString(rec, p, end, r.instance);
                if (c == 0)
                    return false;
                p += c;

                if (static_cast<std::size_t>(end - p) < 12u)
                    return false;
                for (int k = 0; k < 3; ++k)
                    r.pos[k] = readF32(p + static_cast<std::size_t>(k) * 4u);
                p += 12;

                const std::size_t w = findWibble(p, end);
                const std::uint8_t* planeStart = p;
                p += w;
                for (const std::uint8_t* f = planeStart; f < p; f += 4)
                    r.plane.push_back(readF32(f));

                std::string wibbleMarker;
                c = readCString(rec, p, end, wibbleMarker); // consume the "wibble" marker (discard)
                if (c == 0)
                    return false;
                p += c;

                r.classData.assign(p, end);
            }
            out.push_back(std::move(r));
            off += sz;
        }
        blobEnd = off;
        return true;
    }
}

std::vector<std::uint8_t> Game::SaveFile::encodeEntityBlock() const
{
    std::vector<std::vector<std::uint8_t>> blobs;
    blobs.reserve(entities.size());
    for (const SaveEntityRecord& e : entities)
    {
        std::vector<std::uint8_t> blob;
        appendCString(blob, e.name);
        if (e.name != "NUL")
        {
            appendCString(blob, e.instance);
            for (int k = 0; k < 3; ++k)
                appendF32(blob, e.pos[k]);
            for (float f : e.plane)
                appendF32(blob, f);
            appendCString(blob, "wibble");
            blob.insert(blob.end(), e.classData.begin(), e.classData.end());
        }
        blobs.push_back(std::move(blob));
    }

    std::vector<std::uint8_t> out;
    std::size_t total = 4u + blobs.size() * 4u + entityTail.size();
    for (const auto& b : blobs)
        total += b.size();
    out.reserve(total);
    appendU32(out, static_cast<std::uint32_t>(blobs.size()));
    for (const auto& b : blobs)
        appendU32(out, static_cast<std::uint32_t>(b.size()));
    for (const auto& b : blobs)
        out.insert(out.end(), b.begin(), b.end());
    out.insert(out.end(), entityTail.begin(), entityTail.end());
    return out;
}

std::uint32_t Game::SaveFile::headerWord(std::size_t off) const
{
    if (off + 4u > header.size())
        return 0;
    return readU32(header.data() + off);
}

void Game::SaveFile::setHeaderWord(std::size_t off, std::uint32_t v)
{
    if (off + 4u > header.size())
        return;
    std::memcpy(header.data() + off, &v, 4);
}

std::string Game::SaveFile::title() const
{
    std::string t;
    for (std::size_t i = kTitleOffset; i + 1 < kTitleOffset + 0x30u && i + 1 < header.size(); i += 2)
    {
        const std::uint16_t c = static_cast<std::uint16_t>(header[i]) |
                                (static_cast<std::uint16_t>(header[i + 1]) << 8);
        if (c == 0)
            break;
        t.push_back(static_cast<char>(c)); // ASCII slot titles
    }
    return t;
}

void Game::SaveFile::setTitle(const std::string& t)
{
    if (header.size() < kTitleOffset + 0x30u)
        return;
    for (std::size_t i = kTitleOffset; i < kTitleOffset + 0x30u; ++i)
        header[i] = 0;
    // 24 UTF-16 code units including the terminator: at most 23 characters.
    for (std::size_t i = 0; i < t.size() && i < 23u; ++i)
    {
        header[kTitleOffset + i * 2u] = static_cast<std::uint8_t>(t[i]);
        header[kTitleOffset + i * 2u + 1u] = 0;
    }
}

const Game::SaveEntityRecord* Game::SaveFile::findEntity(const std::string& name,
                                                        const std::string& instance) const
{
    for (const auto& e : entities)
    {
        if (e.name == name && (instance.empty() || e.instance == instance))
            return &e;
    }
    return nullptr;
}

Game::SaveEntityRecord* Game::SaveFile::findEntity(const std::string& name,
                                                   const std::string& instance)
{
    for (auto& e : entities)
    {
        if (e.name == name && (instance.empty() || e.instance == instance))
            return &e;
    }
    return nullptr;
}

std::size_t Game::SaveFile::staticPropsCount() const
{
    static const std::uint8_t sig[7] = {'w', 'i', 'b', 'b', 'l', 'e', 0};
    std::size_t n = 0;
    for (std::size_t i = 0; i + 7u <= entityTail.size(); ++i)
        if (std::memcmp(entityTail.data() + i, sig, 7) == 0)
            ++n;
    return n;
}

bool Game::loadSaveFile(const std::string& path, SaveFile& out, std::vector<std::uint8_t>* rawOut)
{
    std::ifstream f(Engine::Platform::resolvePath(path), std::ios::binary | std::ios::ate);
    if (!f)
        return false;
    const std::streamoff n = f.tellg();
    if (n != static_cast<std::streamoff>(SaveLayout::total))
        return false;
    std::vector<std::uint8_t> data(SaveLayout::total);
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(data.data()), n))
        return false;

    std::size_t off = 0;
    auto slice = [&](std::vector<std::uint8_t>& dst, std::size_t len)
    {
        dst.assign(data.begin() + static_cast<std::ptrdiff_t>(off), data.begin() + static_cast<std::ptrdiff_t>(off + len));
        off += len;
    };

    // Parse into a local and only publish it on success, so `out` is untouched on failure.
    SaveFile parsed;
    slice(parsed.header, SaveLayout::buffer1);

    const std::uint8_t* eb = data.data() + off;
    std::size_t blobEnd = 0;
    if (!parseEntityBlock(eb, SaveLayout::buffer2, parsed.entities, blobEnd))
        return false;
    // Preserve the trailing region of buffer 2 (after the blob run) verbatim.
    parsed.entityTail.assign(eb + blobEnd, eb + SaveLayout::buffer2);
    off += SaveLayout::buffer2;

    slice(parsed.buffer3, SaveLayout::buffer3);
    slice(parsed.buffer4, SaveLayout::buffer4);
    slice(parsed.buffer5, SaveLayout::buffer5);
    slice(parsed.buffer6, SaveLayout::buffer6);
    slice(parsed.buffer7, SaveLayout::buffer7);

    out = std::move(parsed);
    if (rawOut)
        *rawOut = std::move(data);
    return true;
}

namespace
{
    // Fit an encoded entity block to the fixed buffer-2 size. The tail ends in zero padding, so
    // a block that grew (e.g. a longer name) may absorb trailing zeros and one that shrank is
    // zero-padded. Fails only if trimming would drop non-zero tail data.
    bool fitEntityBlock(std::vector<std::uint8_t>& block)
    {
        const std::size_t want = Game::SaveLayout::buffer2;
        if (block.size() < want)
        {
            block.resize(want, 0u);
            return true;
        }
        for (std::size_t i = want; i < block.size(); ++i)
            if (block[i] != 0u)
                return false;
        block.resize(want);
        return true;
    }
}

void Game::keepBackup(const std::string& path)
{
    std::error_code ec;
    const std::string bak = path + ".bak";
    if (std::filesystem::exists(path, ec) && !std::filesystem::exists(bak, ec))
        std::filesystem::copy_file(path, bak, ec);
}

bool Game::writeSaveFile(const std::string& path, const SaveFile& in)
{
    std::vector<std::uint8_t> entityBlock = in.encodeEntityBlock();
    if (!fitEntityBlock(entityBlock))
        return false;

    std::vector<std::uint8_t> out;
    out.reserve(SaveLayout::total);
    out.insert(out.end(), in.header.begin(), in.header.end());
    out.insert(out.end(), entityBlock.begin(), entityBlock.end());
    out.insert(out.end(), in.buffer3.begin(), in.buffer3.end());
    out.insert(out.end(), in.buffer4.begin(), in.buffer4.end());
    out.insert(out.end(), in.buffer5.begin(), in.buffer5.end());
    out.insert(out.end(), in.buffer6.begin(), in.buffer6.end());
    out.insert(out.end(), in.buffer7.begin(), in.buffer7.end());

    if (out.size() != SaveLayout::total)
        return false;

    const std::filesystem::path target(path);
    std::filesystem::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o)
            return false;
        o.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!o.good())
        {
            o.close();
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec); // replaces an existing file on both Windows and POSIX
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

std::array<Game::SaveInventorySlot, 4> Game::readInventory(const std::uint8_t* base, std::size_t size)
{
    constexpr std::size_t kRecord = SaveFile::kInventoryRecord;
    std::array<SaveInventorySlot, 4> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const std::size_t at = kRecord * (i + 1);
        if (size < at + kRecord)
            break;
        const char* name = reinterpret_cast<const char*>(base + at);
        std::size_t len = 0;
        while (len < 0x18 && name[len] != '\0')
            ++len;
        out[i].record.assign(name, len);
        std::int32_t rounds = 0;
        std::memcpy(&rounds, base + at + 0x18, 4);
        out[i].rounds = rounds;
    }
    return out;
}

void Game::writeInventory(std::uint8_t* base, std::size_t size, const std::array<SaveInventorySlot, 4>& slots)
{
    constexpr std::size_t kRecord = SaveFile::kInventoryRecord;
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        const std::size_t at = kRecord * (i + 1);
        if (size < at + kRecord)
            break;
        const std::string& name = slots[i].record;
        const std::size_t len = std::min<std::size_t>(name.size(), 0x17);
        std::memcpy(base + at, name.data(), len);
        base[at + len] = 0u;
        const std::int32_t rounds = name.empty() ? -1 : slots[i].rounds;
        std::memcpy(base + at + 0x18, &rounds, 4);
    }
}

std::array<Game::SaveInventorySlot, Game::SaveFile::kInventorySlots> Game::SaveFile::inventory() const
{
    return readInventory(buffer3.data(), buffer3.size());
}

void Game::SaveFile::setInventory(const std::array<SaveInventorySlot, kInventorySlots>& slots)
{
    if (buffer3.size() < Game::SaveLayout::buffer3)
        buffer3.resize(Game::SaveLayout::buffer3, 0u);
    writeInventory(buffer3.data(), buffer3.size(), slots);
}
