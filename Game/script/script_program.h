// Game/script/script_program.h - a compiled level-script program: the routines of one
// SCRIPTMAIN block (one .mls record, bound to one entity) as stack-machine code.
#pragma once
#include "engine_types.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "Game/script/script_value.h"

namespace Game::Script {

enum class Scope : uint8 { Local = 0, Global = 1, Level = 2 };

enum class Op : uint8 {
    PushConst,   // a = constant index
    Load,        // scope, a = slot (Level: a = level-variable name index)
    Store,       // scope, a = slot; pops the value
    LoadField,   // field: replaces the top value by one of its components
    StoreField,  // scope, a = slot, field; pops the value into that component
    PushRef,     // scope, a = slot: a reference to the variable (VAR parameters, native out-args)
    Neg, Not,
    Add, Sub, Mul, Div, IntDiv, Mod, And, Or,
    Eq, Ne, Lt, Gt, Le, Ge,
    Jump,        // a = target
    JumpIfFalse, // a = target; pops the condition
    CallNative,  // a = native name index, b = argument count; pushes the result
    CallUser,    // a = routine index, b = argument count; pushes the result
    Return,      // functions push their result slot
    Pop,
};

// Components addressable with '.': vec3d x/y/z, rgbaint red/green/blue/alpha.
enum class Field : uint8 { X = 0, Y = 1, Z = 2, W = 3 };

struct Instr {
    Op op = Op::Pop;
    Scope scope = Scope::Local;
    Field field = Field::X;
    uint32 a = 0;
    uint32 b = 0;
    uint32 line = 0; // source line, for diagnostics
};

struct Routine {
    std::string name;            // lower case
    bool isScript = false;       // an event handler (SCRIPT name;)
    bool isFunction = false;     // returns slots[resultSlot]
    bool defined = false;        // has a body (not only a forward declaration)
    uint32 paramCount = 0;
    std::vector<bool> paramByRef;
    std::vector<std::string> slotTypes; // params, then result (functions), then locals
    uint32 resultSlot = 0;
    std::vector<Instr> code;
};

struct Program {
    std::string name;        // SCRIPTMAIN name
    std::string entity;      // the ENTITY it is bound to (lower case)
    std::string entityType;  // et_name, et_level, ...
    std::vector<Value> constants;
    std::vector<std::string> natives;    // lower-case native function names
    std::vector<std::string> levelVars;  // names of level/game variables this program uses
    std::vector<std::string> levelVarTypes;
    std::vector<std::string> globalTypes; // program VAR section
    std::vector<std::string> globalNames; // their names (lower case), for a save's data segment
    std::vector<Routine> routines;
    std::unordered_map<std::string, uint32> routineIndex;

    [[nodiscard]] const Routine* findScript(const std::string& lowerName) const {
        const auto it = routineIndex.find(lowerName);
        return it != routineIndex.end() && routines[it->second].isScript ? &routines[it->second] : nullptr;
    }
};

// The initial value of a variable of `type` (integer 0, string '', entityptr nil, vec3d 0...).
Value defaultValue(const std::string& type);

} // namespace Game::Script
