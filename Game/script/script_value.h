// Game/script/script_value.h - a value in the game level-script language.
#pragma once
#include "engine_types.h"

#include <cstdint>
#include <string>

#include "Math/vector.h"

namespace Game::Script {

enum class ValueType : uint8 { Nil, Int, Real, Bool, String, Entity, Vec3, Ref };

// Where a variable lives, for VAR (by-reference) parameters.
struct VarRef {
    uint8 scope = 0;    // see Scope in script_program.h
    uint32 frame = 0;   // call-frame index for locals
    uint32 slot = 0;
};

struct Value {
    ValueType type = ValueType::Nil;
    int64 i = 0;             // Int, Bool (0/1), Entity (handle, 0 = nil)
    double64 r = 0.0;         // Real
    std::string s;           // String
    Math::Vector3 v{0.0f, 0.0f, 0.0f}; // Vec3 (also rgbaint as x/y/z + i for alpha)
    VarRef ref;              // Ref

    static Value nil() { return {}; }
    static Value integer(int64 x) { Value v; v.type = ValueType::Int; v.i = x; return v; }
    static Value real(double64 x) { Value v; v.type = ValueType::Real; v.r = x; return v; }
    static Value boolean(bool b) { Value v; v.type = ValueType::Bool; v.i = b ? 1 : 0; return v; }
    static Value string(std::string x) { Value v; v.type = ValueType::String; v.s = std::move(x); return v; }
    static Value entity(uint32 handle) { Value v; v.type = handle ? ValueType::Entity : ValueType::Nil; v.i = handle; return v; }
    static Value vec3(const Math::Vector3& p) { Value v; v.type = ValueType::Vec3; v.v = p; return v; }

    [[nodiscard]] int64 asInt() const { return type == ValueType::Real ? static_cast<int64>(r) : i; }
    [[nodiscard]] double64 asReal() const { return type == ValueType::Real ? r : static_cast<double64>(i); }
    [[nodiscard]] float32 asFloat() const { return static_cast<float32>(asReal()); }
    [[nodiscard]] bool asBool() const { return type == ValueType::Real ? r != 0.0 : i != 0; }
    [[nodiscard]] uint32 asEntity() const { return type == ValueType::Entity ? static_cast<uint32>(i) : 0u; }
    [[nodiscard]] const std::string& asString() const { return s; }
    [[nodiscard]] bool isNil() const { return type == ValueType::Nil || (type == ValueType::Entity && i == 0); }
};

} // namespace Game::Script
