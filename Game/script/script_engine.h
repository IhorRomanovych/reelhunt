// Game/script/script_engine.h - runs a level's scripts.
//
// Every .mls record is compiled from its embedded source into a Program bound to one entity
// (the level, the player, a hunter, a trigger...). The engine keeps the shared level variables,
// starts event handlers (OnCreate, OnDeath, OnEnterTrigger, ... and RunScript) as tasks and
// runs them cooperatively: a task runs until it sleeps (Sleep(ms)), finishes or exhausts its
// per-frame instruction budget. Native functions are looked up by name in a table the game
// fills (ScriptNatives); an unknown native logs once and returns 0/nil.
#pragma once
#include "engine_types.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/mls.h"
#include "Game/script/script_compiler.h"
#include "Game/script/script_program.h"

namespace Game::Script {

class Engine;
struct Task;

// What a native function sees: its arguments (by value, with write-back for variables passed
// directly, e.g. SetVector(v, ...)), the calling task and the engine.
class NativeCall {
public:
    NativeCall(Engine& engine, Task& task, std::span<Value> args, std::span<const VarRef*> refs)
        : engine_(engine), task_(task), args_(args), refs_(refs) {}

    [[nodiscard]] std::size_t count() const noexcept { return args_.size(); }
    [[nodiscard]] const Value& arg(std::size_t i) const;
    [[nodiscard]] int64 integer(std::size_t i) const { return arg(i).asInt(); }
    [[nodiscard]] float32 real(std::size_t i) const { return arg(i).asFloat(); }
    [[nodiscard]] bool boolean(std::size_t i) const { return arg(i).asBool(); }
    [[nodiscard]] std::string string(std::size_t i) const; // lower case
    [[nodiscard]] const std::string& rawString(std::size_t i) const { return arg(i).s; }
    [[nodiscard]] uint32 entity(std::size_t i) const { return arg(i).asEntity(); }
    [[nodiscard]] Math::Vector3 vec3(std::size_t i) const { return arg(i).v; }
    // Write an out-argument (only when the caller passed a variable).
    void set(std::size_t i, const Value& v);

    void sleep(uint32 ms);                // suspend the calling task
    void stopTask();                      // RemoveThisScript / end the handler
    void endTask();                       // KillThisScript: end the handler only
    [[nodiscard]] uint32 self() const;    // the entity the program is bound to
    [[nodiscard]] Engine& engine() noexcept { return engine_; }

private:
    Engine& engine_;
    Task& task_;
    std::span<Value> args_;
    std::span<const VarRef*> refs_;
};

using NativeFn = std::function<Value(NativeCall&)>;

struct Frame {
    uint32 routine = 0;
    uint32 pc = 0;
    std::vector<Value> slots;
};

struct Task {
    uint32 program = 0;
    std::string label;          // "entity:script", for logs
    std::vector<Frame> frames;
    std::vector<Value> stack;
    double64 wakeAt = 0.0;      // engine time (seconds)
    bool done = false;
};

class Engine {
public:
    // Compiles every record of `mls`; records whose source fails to compile are logged and skipped.
    void load(const Assets::MlsFile& mls, const ConstantTable& constants);
    // Compile one program from source text, bound to `entity` (tests, tools). False on error.
    bool loadSource(const std::string& entity, const std::string& source, const ConstantTable& constants);

    // Native functions by lower-case name.
    void bind(const std::string& name, NativeFn fn);
    // The natives the loaded programs call that nothing binds (sorted, each once).
    [[nodiscard]] std::vector<std::string> unboundNatives() const;
    // Entity name (lower case) -> the handle `this` gives inside that entity's programs.
    void setEntityHandle(const std::string& entity, uint32 handle);

    // Start a handler of the program bound to `entity`; false when there is none (or it was
    // removed with RemoveThisScript). Event names are case-insensitive.
    bool start(const std::string& entity, const std::string& script);
    [[nodiscard]] bool has(const std::string& entity, const std::string& script) const;
    // Any program bound to `entity` (exe 0x480e50: the entity has a script).
    [[nodiscard]] bool hasProgram(const std::string& entity) const { return findProgram(entity) >= 0; }
    // The entity left the level (DestroyEntity): its programs take no more events and their
    // running handlers end. attach(): it is made again - a new instance, every handler back.
    void detach(const std::string& entity);
    void attach(const std::string& entity);
    // The entities programs are bound to (lower case, each once).
    [[nodiscard]] std::vector<std::string> programEntities() const
    {
        std::vector<std::string> out;
        for (const auto& [entity, programs] : byEntity_)
            if (!programs.empty())
                out.push_back(entity);
        return out;
    }
    // RemoveScript(entity, handler): ends its running handler and keeps it from starting again.
    void removeScript(const std::string& entity, const std::string& script);
    // KillScript(entity, handler): ends its running handler (it may start again).
    void killScript(const std::string& entity, const std::string& script);
    // SetEntityScriptsFromEntity(from, to): `to` gets its own copy of `from`'s programs (fresh
    // globals, `to` as `this`). False when `from` has none.
    bool copyScripts(const std::string& from, const std::string& to);
    // Start `script` on every program that has it (e.g. OnCreate at level start).
    void startAll(const std::string& script);

    // Run every task that is awake until it sleeps or finishes.
    void update(double64 dt);

    [[nodiscard]] double64 time() const noexcept { return time_; }
    [[nodiscard]] std::size_t programCount() const noexcept { return programs_.size(); }
    [[nodiscard]] std::size_t runningTasks() const noexcept { return tasks_.size(); }
    [[nodiscard]] const std::vector<std::string>& compileErrors() const noexcept { return errors_; }
    // Natives called by the scripts that nothing implements (each logged once).
    [[nodiscard]] const std::unordered_set<std::string>& missingNatives() const noexcept { return missing_; }

    [[nodiscard]] Value levelVar(const std::string& name) const;
    // A program's VAR by name (the entity's first program): what a save's script data keeps.
    [[nodiscard]] Value global(const std::string& entity, const std::string& name) const;
    bool setGlobal(const std::string& entity, const std::string& name, const Value& v);

private:
    friend class NativeCall;
    struct Loaded {
        Program program;
        std::vector<Value> globals;
        std::vector<uint32> levelSlots; // program level-var index -> levelVars_ slot
        // The level program (et_level): its VAR i is levelVars_[globalLevel[i]], the variable
        // the other programs' "level_var" of that name share.
        std::vector<uint32> globalLevel;
        uint32 entityHandle = 0;
        bool removed = false;           // RemoveThisScript ran: no more events
        std::unordered_set<std::string> removedScripts; // RemoveScript: these handlers no more
    };

    void run(Task& task);
    Value& slot(Task& task, Scope scope, uint32 index, uint32 frameIndex);
    Value& deref(Task& task, Value& v);
    Value& globalSlot(Loaded& l, std::size_t index);
    [[nodiscard]] const Value& globalSlot(const Loaded& l, std::size_t index) const;
    Value callNative(Task& task, uint32 nameIndex, uint32 argc);
    void enterRoutine(Task& task, uint32 routine, uint32 argc);
    int findProgram(const std::string& entity) const;
    bool add(const std::string& entity, const std::string& source, const ConstantTable& constants);
    void registerBuiltins();

    std::vector<Loaded> programs_;
    std::unordered_map<std::string, std::vector<uint32>> byEntity_; // entity -> programs
    std::vector<Value> levelVars_;
    std::unordered_map<std::string, uint32> levelVarIndex_;
    std::unordered_map<std::string, NativeFn> natives_;
    std::unordered_map<std::string, uint32> entityHandles_;
    std::vector<std::unique_ptr<Task>> tasks_;
    std::vector<std::string> errors_;
    std::unordered_set<std::string> missing_;
    double64 time_ = 0.0;
};

} // namespace Game::Script
