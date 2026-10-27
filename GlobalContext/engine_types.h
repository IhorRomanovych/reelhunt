// GlobalContext/engine_types.h - foundational engine types (self-contained).
// Primitive aliases are global for convenience; engine-specific types live
// in the Engine namespace. Included by every translation unit of the engine.
#pragma once
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

// --- Global primitive aliases ---
using uint8  = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int8   = std::int8_t;
using int16  = std::int16_t;
using int32  = std::int32_t;
using int64  = std::int64_t;
using float32  = float;
using double64 = double;
using ObjectId = uint32;

namespace Engine {

enum class RenderMode : uint32 {
    None = 0u, Diffuse = 1u << 0, Specular = 1u << 1, Ambient = 1u << 2,
    Gouraud = 1u << 3, Flat = 1u << 4, BackCull = 1u << 5, FrontCull = 1u << 6,
    DoubleSided = 1u << 7, Masked = 1u << 8, Translucent = 1u << 9,
    Additive = 1u << 10, ZWrite = 1u << 11, ZTestOn = 1u << 12, ZTestOff = 1u << 13,
    BlendOn = 1u << 14, AlphaTest = 1u << 15, Shadow = 1u << 16,
    Dynamic = 1u << 17, Static = 1u << 18, Occluded = 1u << 19,
    Visible = 1u << 20, Invisible = 1u << 21
};

enum class EntityFlag : uint32 {
    None = 0u, Active = 1u << 0, Inactive = 1u << 1, Visible = 1u << 2,
    Invisible = 1u << 3, Solid = 1u << 4, NonSolid = 1u << 5,
    PlayerOwned = 1u << 6, AIControlled = 1u << 7, Physics = 1u << 8,
    NoPhysics = 1u << 9, Scripted = 1u << 10, Animated = 1u << 11,
    Rigid = 1u << 12, Deformable = 1u << 13, Static = 1u << 14,
    Dynamic = 1u << 15, Trigger = 1u << 16, Locked = 1u << 17,
    Unlocked = 1u << 18, Detached = 1u << 19, Attached = 1u << 20,
    Root = 1u << 21, Leaf = 1u << 22
};

[[nodiscard]] constexpr bool hasFlag(RenderMode mode, uint32 flag) {
    return (static_cast<uint32>(mode) & flag) != 0;
}
[[nodiscard]] constexpr bool hasFlag(EntityFlag flags, EntityFlag flag) {
    return (static_cast<uint32>(flags) & static_cast<uint32>(flag)) != 0;
}
constexpr EntityFlag operator|(EntityFlag lhs, EntityFlag rhs) {
    return static_cast<EntityFlag>(static_cast<uint32>(lhs) | static_cast<uint32>(rhs));
}
constexpr EntityFlag operator&(EntityFlag lhs, EntityFlag rhs) {
    return static_cast<EntityFlag>(static_cast<uint32>(lhs) & static_cast<uint32>(rhs));
}
constexpr EntityFlag operator~(EntityFlag lhs) {
    return static_cast<EntityFlag>(~static_cast<uint32>(lhs));
}

struct EngineError final {
    enum class Code : uint32 {
        None = 0u, Generic = 1u, Invalid = 2u, NotFound = 3u, Failed = 4u,
        Memory = 5u, Timeout = 6u, Unsupported = 7u, Interrupted = 8u
    };
    Code code = Code::None;
    // Owned (not a string_view): failure messages are often built at runtime
    // ("... at index " + std::to_string(i)), and a view of that temporary dangled.
    std::string message;
    [[nodiscard]] bool isOk() const noexcept { return code == Code::None; }
    [[nodiscard]] static EngineError failure(Code c, std::string msg) { return {c, std::move(msg)}; }
};

// Canonical "success" value.
inline const EngineError okError{};

} // namespace Engine
