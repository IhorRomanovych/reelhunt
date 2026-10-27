// Assets/grf.cpp - AI navigation graph parser. See grf.h.
#include "Assets/grf.h"

#include "Assets/binary_reader.h"

#include <algorithm>
#include <cctype>

namespace Assets {

namespace {

using Engine::EngineError;

bool equalsNoCase(const std::string& a, const std::string& b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}

// A u32 count followed by that many i32 values; refuses counts the remaining bytes cannot hold.
bool readList(ByteReader& r, std::vector<int32>& out)
{
    uint32 n = 0;
    if (!r.readU32(n) || n > r.remaining() / 4u)
        return false;
    out.resize(n);
    for (int32& v : out)
        if (!r.readI32(v))
            return false;
    return true;
}

} // namespace

int32 GrfData::findNode(const std::string& labelOrName) const
{
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (equalsNoCase(nodes[i].label, labelOrName) || equalsNoCase(nodes[i].name, labelOrName))
            return static_cast<int32>(i);
    return -1;
}

int32 GrfData::findPath(const std::string& name) const
{
    for (std::size_t i = 0; i < paths.size(); ++i)
        if (equalsNoCase(paths[i].name, name))
            return static_cast<int32>(i);
    return -1;
}

int32 GrfData::findArea(const std::string& name) const
{
    for (std::size_t i = 0; i < areas.size(); ++i)
        if (equalsNoCase(areas[i], name))
            return static_cast<int32>(i);
    return -1;
}

std::expected<GrfData, EngineError> Grf::load(const std::string& path)
{
    auto bytes = readFileBytes(path);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));
    return parse(*bytes);
}

std::expected<GrfData, EngineError> Grf::parse(const std::vector<uint8_t>& bytes)
{
    auto fail = [](const std::string& what) {
        return std::unexpected(EngineError::failure(EngineError::Code::Invalid, "grf: " + what));
    };
    ByteReader r(bytes);
    GrfData g;
    if (bytes.size() >= 4 && std::equal(bytes.begin(), bytes.begin() + 4, "GNIA"))
        return fail("the sequel graph (GNIA) is not supported");

    uint32 count = 0;
    if (!r.readU32(count) || count > r.remaining() / 16u)
        return fail("bad node count");
    g.nodes.resize(count);
    for (uint32 i = 0; i < count; ++i)
    {
        GrfNode& n = g.nodes[i];
        if (!r.readPaddedString(n.name) || !r.readI32(n.area) || !r.readF32(n.position.x) || !r.readF32(n.position.y) ||
            !r.readF32(n.position.z) || !r.readF32(n.radius) || !r.readPaddedString(n.label) || !readList(r, n.unknown))
            return fail("node " + std::to_string(i) + " truncated");
        uint32 links = 0;
        if (!r.readU32(links) || links > r.remaining() / 12u)
            return fail("node " + std::to_string(i) + " bad link count");
        n.links.resize(links);
        for (GrfLink& l : n.links)
            if (!r.readI32(l.node) || !r.readI32(l.type) || !readList(r, l.extra))
                return fail("node " + std::to_string(i) + " link truncated");
    }

    if (!r.readU32(count) || count > r.remaining() / 8u)
        return fail("bad path count");
    g.paths.resize(count);
    for (GrfPath& p : g.paths)
        if (!r.readPaddedString(p.name) || !readList(r, p.nodes))
            return fail("path truncated");

    if (!r.readU32(count) || count > r.remaining() / 4u)
        return fail("bad area count");
    g.areas.resize(count);
    for (std::string& a : g.areas)
        if (!r.readPaddedString(a))
            return fail("area name truncated");
    return g;
}

} // namespace Assets
