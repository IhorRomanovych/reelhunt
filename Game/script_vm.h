#pragma once
#include "engine_types.h"
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "Assets/mls.h"

namespace Game {

class ScriptVM {
public:
    using FuncCallback = std::function<void(ScriptVM&)>;

    // Register a native implementation for a script function, keyed by the function ID word
    // that appears in the bytecode (see Sor3nt's file-format toolkit CompilerV2/the game.php for the ID map).
    void bind(uint32 functionId, FuncCallback fn);

    // Values pushed by the script that a bound function can consume.
    [[nodiscard]] bool popValue(int64& out);
    void pushValue(int64 v) { evalStack_.push_back(v); }

    // Execute a script's event handler. startWord is the word offset into CODE.
    void execute(const Assets::MlsScript& script, uint32 startWord);

    [[nodiscard]] int64 getVar(const std::string& name) const;
    void setVar(const std::string& name, int64 value);

private:
    struct Frame {
        const Assets::MlsScript* script = nullptr;
        uint32 pc = 0;
    };

    // Pattern matching helpers
    bool match(uint32 pc, std::initializer_list<uint32> words) const;
    uint32 peek(uint32 offset = 0) const;
    [[nodiscard]] const Assets::MlsScript* cur() const { return frame_.script; }

    // Instruction executors (return words consumed)
    uint32 execScriptStart();
    uint32 execScriptEnd();
    uint32 execReadHeaderVar();
    uint32 execReadGameVar();
    uint32 execWriteGameVar();
    uint32 execPushConstInt();
    uint32 execIfStatement();
    uint32 execBranchIfFalse();
    uint32 execLineMarker();
    uint32 execFunctionCall();
    uint32 execNestedReturn();
    uint32 execReserveBytes();

    // State
    Frame frame_;
    std::vector<int64> evalStack_;
    std::unordered_map<std::string, int64> vars_;
    std::unordered_map<uint32, FuncCallback> functions_;
    bool halted_ = false;
    bool jumped_ = false; // set by an instruction that moved frame_.pc itself (taken branch)

    static constexpr uint32 kMaxSteps = 100000;
};

} // namespace Game
