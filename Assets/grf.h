// Assets/grf.h - the game AI navigation graph (levels/<level>/mapAI.grf).
//
// Layout (little-endian; strings are NUL-terminated and padded to a 4-byte block of
// len + (4 - len % 4) bytes, like .inst/.col), from Sor3nt's file-format toolkit Archive/Grf/Extract.php:
//   u32 nodeCount, then per node:
//     string  name            the node's id (often numeric)
//     i32     area            index into the area names
//     f32[3]  position        level (Y-up script) coordinates
//     f32     radius          how close counts as "at the node"
//     string  label           script-visible node name ('wilson', 'attak1', ... or empty)
//     u32 n, i32[n]           unknown list
//     u32 m, m x { i32 node, i32 type, u32 k, i32[k] }  links to other nodes
//   u32 pathCount, then per path: string name, u32 n, i32[n] node indices   ('AIPath03', ...)
//   u32 areaCount, then per area: string name                              ('aiStart', ...)
#pragma once
#include "engine_types.h"

#include <expected>
#include <string>
#include <vector>

#include "Math/vector.h"

namespace Assets {

struct GrfLink {
    int32 node = -1;   // index of the linked node
    int32 type = 0;    // link kind (not decoded; doors, jumps, ...)
    std::vector<int32> extra;
};

struct GrfNode {
    std::string name;
    std::string label; // the name level scripts use, may be empty
    int32 area = -1;
    Math::Vector3 position{0.0f, 0.0f, 0.0f};
    float32 radius = 0.0f;
    std::vector<int32> unknown;
    std::vector<GrfLink> links;
};

struct GrfPath {
    std::string name;
    std::vector<int32> nodes;
};

struct GrfData {
    std::vector<GrfNode> nodes;
    std::vector<GrfPath> paths;
    std::vector<std::string> areas;

    // Case-insensitive lookups (scripts are not consistent about case); -1 when absent.
    [[nodiscard]] int32 findNode(const std::string& labelOrName) const;
    [[nodiscard]] int32 findPath(const std::string& name) const;
    [[nodiscard]] int32 findArea(const std::string& name) const;
};

class Grf {
public:
    static std::expected<GrfData, Engine::EngineError> load(const std::string& path);
    static std::expected<GrfData, Engine::EngineError> parse(const std::vector<uint8_t>& bytes);
};

} // namespace Assets
