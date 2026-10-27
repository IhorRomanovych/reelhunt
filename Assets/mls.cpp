#include "Assets/mls.h"
#include "Assets/binary_reader.h"
#include <cstring>
#include <fstream>
#include <iostream>

namespace Assets {

uint32 MlsFile::readU32LE(const uint8_t* p)
{
    return static_cast<uint32>(p[0]) | (static_cast<uint32>(p[1]) << 8) |
           (static_cast<uint32>(p[2]) << 16) | (static_cast<uint32>(p[3]) << 24);
}

bool MlsFile::load(const std::string& path)
{
    scripts_.clear();
    auto bytes = readFileBytes(path);
    if (!bytes)
    {
        std::cerr << "[mls] " << bytes.error().message << "\n";
        return false;
    }
    const std::vector<uint8_t>& buf = *bytes;
    const size_t fileSize = buf.size();

    // MHLS header: "MHLS" + u32 version
    if (fileSize < 8 || std::memcmp(buf.data(), "MHLS", 4) != 0)
    {
        std::cerr << "[mls] bad magic\n";
        return false;
    }

    size_t pos = 8;
    while (pos + 8 <= fileSize)
    {
        // Each block: "MHSC" label + u32 payload size
        if (std::memcmp(buf.data() + pos, "MHSC", 4) != 0)
            break;
        const uint32 payloadSize = readU32LE(buf.data() + pos + 4);
        pos += 8;
        if (payloadSize > fileSize - pos)
            break;

        MlsScript script;
        parseSections(buf.data() + pos, payloadSize, script);
        scripts_.push_back(std::move(script));
        pos += payloadSize;
    }

    std::cout << "[mls] loaded " << scripts_.size() << " scripts from " << path << "\n";
    return !scripts_.empty();
}

void MlsFile::parseSections(const uint8_t* data, size_t size, MlsScript& out)
{
    size_t pos = 0;
    while (pos + 8 <= size)
    {
        char tag[5]{};
        std::memcpy(tag, data + pos, 4);
        const uint32 secSize = readU32LE(data + pos + 4);
        pos += 8;
        if (secSize > size - pos)
            break;

        const uint8_t* d = data + pos;
        if (std::memcmp(tag, "SCPT", 4) == 0)
            parseSCPT(d, secSize, out);
        else if (std::memcmp(tag, "NAME", 4) == 0)
            out.name.assign(reinterpret_cast<const char*>(d), strnlen(reinterpret_cast<const char*>(d), secSize));
        else if (std::memcmp(tag, "ENTT", 4) == 0)
        {
            if (secSize >= 4)
            {
                out.entityType = readU32LE(d);
                const char* nameStart = reinterpret_cast<const char*>(d + 4);
                size_t nameLen = strnlen(nameStart, secSize - 4);
                out.entityName.assign(nameStart, nameLen);
            }
        }
        else if (std::memcmp(tag, "CODE", 4) == 0)
            parseCODE(d, secSize, out);
        else if (std::memcmp(tag, "DATA", 4) == 0)
            out.data.assign(d, d + secSize);
        else if (std::memcmp(tag, "SMEM", 4) == 0 && secSize >= 4)
            out.smemSize = readU32LE(d);
        else if (std::memcmp(tag, "DMEM", 4) == 0 && secSize >= 4)
            out.dmemSize = readU32LE(d);
        else if (std::memcmp(tag, "DBUG", 4) == 0)
            parseDBUG(d, secSize, out);
        else if (std::memcmp(tag, "STAB", 4) == 0)
            parseSTAB(d, secSize, out);

        pos += secSize;
    }
}

void MlsFile::parseSCPT(const uint8_t* d, size_t n, MlsScript& s)
{
    const size_t count = n / 72;
    s.events.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t* rec = d + i * 72;
        std::memcpy(s.events[i].name, rec, 64);
        s.events[i].name[63] = '\0';
        s.events[i].onTrigger = readU32LE(rec + 64);
        s.events[i].scriptStart = readU32LE(rec + 68);
    }
}

void MlsFile::parseCODE(const uint8_t* d, size_t n, MlsScript& s)
{
    const size_t wordCount = n / 4;
    s.code.resize(wordCount);
    for (size_t i = 0; i < wordCount; ++i)
        s.code[i] = readU32LE(d + i * 4);
}

void MlsFile::parseSTAB(const uint8_t* d, size_t n, MlsScript& s)
{
    // MH1 STAB entry: name[32] + u32 offset + u32 size + u32 valueType + u32 occCount + occCount×u32 offsets
    const size_t entryBase = 32 + 4 * 4; // 48 bytes minimum
    if (n < entryBase)
        return;

    size_t pos = 0;
    while (pos + entryBase <= n)
    {
        MlsSymbol sym;
        std::memcpy(sym.name, d + pos, 32);
        sym.name[31] = '\0';

        const uint8_t* fields = d + pos + 32;
        sym.offset = readU32LE(fields);
        sym.size = readU32LE(fields + 4);
        sym.valueType = readU32LE(fields + 8);
        sym.occCount = readU32LE(fields + 12);

        // The occurrence list must fit; otherwise the table is corrupt and every later entry
        // would be misaligned, so stop here.
        if (sym.occCount > (n - pos - entryBase) / 4)
            break;
        const size_t entrySize = entryBase + static_cast<size_t>(sym.occCount) * 4;
        sym.occurrences.resize(sym.occCount);
        const uint8_t* occs = fields + 16;
        for (uint32 i = 0; i < sym.occCount; ++i)
            sym.occurrences[i] = readU32LE(occs + static_cast<size_t>(i) * 4);

        s.symbols.push_back(std::move(sym));
        pos += entrySize;
    }
}

void MlsFile::parseDBUG(const uint8_t* d, size_t n, MlsScript& s)
{
    // DBUG contains nested tagged sub-sections: SRCE, LINE, TRCE
    size_t pos = 0;
    while (pos + 8 <= n)
    {
        char tag[5]{};
        std::memcpy(tag, d + pos, 4);
        const uint32 subSize = readU32LE(d + pos + 4);
        pos += 8;
        if (subSize > n - pos)
            break;

        if (std::memcmp(tag, "SRCE", 4) == 0)
            s.source.assign(reinterpret_cast<const char*>(d + pos), subSize);

        pos += subSize;
    }
}

int MlsFile::findScriptByEntity(const std::string& entityName) const
{
    for (size_t i = 0; i < scripts_.size(); ++i)
    {
        if (scripts_[i].entityName == entityName)
            return static_cast<int>(i);
    }
    return -1;
}

int MlsFile::findEvent(const MlsScript& s, const std::string& eventName) const
{
    for (size_t i = 0; i < s.events.size(); ++i)
    {
        if (std::strcmp(s.events[i].name, eventName.c_str()) == 0)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace Assets
