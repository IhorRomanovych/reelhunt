#include "Game/script_vm.h"
#include <cstring>
#include <initializer_list>

namespace Game {

void ScriptVM::bind(uint32 functionId, FuncCallback fn)
{
    functions_[functionId] = std::move(fn);
}

bool ScriptVM::popValue(int64& out)
{
    if (evalStack_.empty())
        return false;
    out = evalStack_.back();
    evalStack_.pop_back();
    return true;
}

void ScriptVM::execute(const Assets::MlsScript& script, uint32 startWord)
{
    frame_.script = &script;
    frame_.pc = startWord;
    evalStack_.clear();
    halted_ = false;
    jumped_ = false;

    const auto& code = script.code;
    uint32 steps = 0;

    while (!halted_ && frame_.pc < code.size() && steps++ < kMaxSteps)
    {
        uint32 consumed = 0;

        if (match(frame_.pc, {0x10, 0x0a, 0x11, 0x0a, 0x09}))
            consumed = execScriptStart();
        else if (match(frame_.pc, {0x11, 0x09, 0x0a, 0x0f, 0x0a, 0x3b, 0x00}))
            consumed = execScriptEnd();
        else if (match(frame_.pc, {0x23, 0x04, 0x01, 0x12, 0x01, 0x01}))
            consumed = execIfStatement();
        else if (match(frame_.pc, {0x24, 0x01, 0x00}))
            consumed = execBranchIfFalse();
        else if (match(frame_.pc, {0x1e}))
            consumed = execReadGameVar();
        else if (match(frame_.pc, {0x1d, 0x01}))
            consumed = execWriteGameVar();
        else if (match(frame_.pc, {0x14, 0x01, 0x04}))
            consumed = execReadHeaderVar();
        else if (match(frame_.pc, {0x13, 0x01, 0x04}))
            consumed = execReadHeaderVar();
        else if (match(frame_.pc, {0x12, 0x01}))
            consumed = execPushConstInt();
        else if (match(frame_.pc, {0x10, 0x01}))
            consumed = execNestedReturn();
        else if (match(frame_.pc, {0x34, 0x09}))
            consumed = execReserveBytes();
        else if (peek(0) == 0x3f || peek(0) == 0x3c)
            consumed = execLineMarker();
        else
            consumed = execFunctionCall();

        if (jumped_)
        {
            jumped_ = false; // pc already points at the branch target
            continue;
        }
        if (consumed == 0)
            consumed = 1; // safety: always advance
        frame_.pc += consumed;
    }

    if (steps >= kMaxSteps)
        std::cerr << "[vm] step limit reached (possible infinite loop)\n";
}

bool ScriptVM::match(uint32 pc, std::initializer_list<uint32> words) const
{
    const auto& code = cur()->code;
    if (pc + words.size() > code.size())
        return false;
    uint32 i = 0;
    for (uint32 w : words)
    {
        if (code[pc + i] != w)
            return false;
        ++i;
    }
    return true;
}

uint32 ScriptVM::peek(uint32 offset) const
{
    const auto& code = cur()->code;
    uint32 idx = frame_.pc + offset;
    if (idx >= code.size())
        return 0;
    return code[idx];
}

int64 ScriptVM::getVar(const std::string& name) const
{
    auto it = vars_.find(name);
    return (it != vars_.end()) ? it->second : 0;
}

void ScriptVM::setVar(const std::string& name, int64 value)
{
    vars_[name] = value;
}

uint32 ScriptVM::execScriptStart()
{
    // "10 0a 11 0a 09" — script initialization marker, no action needed
    return 5;
}

uint32 ScriptVM::execScriptEnd()
{
    // "11 09 0a 0f 0a 3b 00" — halt execution
    halted_ = true;
    return 7;
}

uint32 ScriptVM::execReadHeaderVar()
{
    // "14 01 04 [offset]" or "13 01 04 [offset]" — read variable at DATA offset
    const uint32 dataOffset = peek(3);
    // Store in eval stack; the actual value comes from our vars_ map or defaults to 0
    // For game_vars with offset 0xFFFFFFFF, use a synthetic name
    std::string varName;
    if (dataOffset == 0xFFFFFFFF)
        varName = "global_var";
    else
        varName = "hdr_" + std::to_string(dataOffset);

    evalStack_.push_back(getVar(varName));
    return 4;
}

uint32 ScriptVM::execPushConstInt()
{
    // "12 01 [value] 0f 04" or "12 01 [value] 10 01" — push constant
    const uint32 value = peek(2);
    evalStack_.push_back(static_cast<int64>(value));

    // Determine terminator: 0f 04 (5 words total) or 10 01 (5 words total)
    if (frame_.pc + 4 < cur()->code.size())
    {
        uint32 w4 = cur()->code[frame_.pc + 4];
        uint32 w3 = cur()->code[frame_.pc + 3];
        if ((w3 == 0x0f && w4 == 0x04) || (w3 == 0x10 && w4 == 0x01))
            return 5;
    }
    // Fallback: just the value word
    return 3;
}

uint32 ScriptVM::execIfStatement()
{
    // "23 04 01 12 01 01 [op] [branchOffset] 33 01 01" — 11 words total
    const uint32 op = peek(6);
    const uint32 branchByteOffset = peek(7);

    // Pop two values from eval stack (right operand first, then left)
    int64 rhs = 0, lhs = 0;
    if (evalStack_.size() >= 2)
    {
        rhs = evalStack_.back(); evalStack_.pop_back();
        lhs = evalStack_.back(); evalStack_.pop_back();
    }

    bool result = false;
    switch (op)
    {
    case 0x3f: result = (lhs == rhs); break;   // equal
    case 0x40: result = (lhs != rhs); break;   // not equal
    case 0x3d: result = (lhs < rhs); break;    // less than
    case 0x42: result = (lhs > rhs); break;    // greater than
    default:
        std::cerr << "[vm] unknown compare op 0x" << std::hex << op << "\n";
        result = false;
    }

    if (!result)
    {
        // Taken branch: jump to the target. (Returning the instruction length here as well used
        // to add 11 words on top of the target, skipping the start of the else/after block.)
        frame_.pc = branchByteOffset / 4;
        jumped_ = true;
        return 0;
    }

    return 11;
}

uint32 ScriptVM::execReadGameVar()
{
    // "1e [varId] 04 [flag]" — read game_var by ID, push onto eval stack
    const uint32 varId = peek(1);
    std::string name = "gamevar_" + std::to_string(varId);
    evalStack_.push_back(getVar(name));
    return 4;
}

uint32 ScriptVM::execWriteGameVar()
{
    // "1d 01 [varId] 04" — pop eval stack, write to game_var
    const uint32 varId = peek(2);
    std::string name = "gamevar_" + std::to_string(varId);
    if (!evalStack_.empty())
    {
        setVar(name, evalStack_.back());
        evalStack_.pop_back();
    }
    return 4;
}

uint32 ScriptVM::execBranchIfFalse()
{
    // "24 01 00" — if-end marker or while-loop branch-if-false
    // Check if followed by "3f [offset]" which would make it a branch target
    if (frame_.pc + 5 < cur()->code.size())
    {
        uint32 next0 = cur()->code[frame_.pc + 3];
        if (next0 == 0x3f)
        {
            // This is a while-loop exit: "24 01 00 3f [offset]"
            // The branch was already handled by the condition; just skip all 5 words
            return 5;
        }
    }
    // Plain if-end terminator
    return 3;
}

uint32 ScriptVM::execLineMarker()
{
    // "3f [byteOffset]" or "3c [byteOffset]" — line number / loop-back marker
    // For now, just skip (2 words). Loop-backs would need context tracking.
    return 2;
}

uint32 ScriptVM::execFunctionCall()
{
    // Single u32 word = function ID from the game's function map
    const uint32 funcId = peek(0);

    if (const auto it = functions_.find(funcId); it != functions_.end())
    {
        it->second(*this);
        return 1;
    }
    std::cout << "[vm] unbound func=0x" << std::hex << funcId << std::dec
              << " (pc=" << frame_.pc << ")\n";
    return 1;
}

uint32 ScriptVM::execNestedReturn()
{
    // "10 01" — consume return value from previous read/call into eval stack
    // The value was already pushed by the read instruction; this is just a marker.
    return 2;
}

uint32 ScriptVM::execReserveBytes()
{
    // "34 09 [byteCount]" — reserve local variable space, no runtime action
    return 3;
}

} // namespace Game
