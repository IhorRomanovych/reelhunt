// Game/script/script_engine.cpp - level-script execution. See script_engine.h.
#include "Game/script/script_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

namespace Game::Script {

namespace {

constexpr uint32 kBudgetPerRun = 50000u; // instructions before a busy task yields to the frame

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct ScriptError {
    std::string message;
};

// Assignment with the coercions the language performs (integer -> real).
void assign(Value& target, const Value& v)
{
    if (target.type == ValueType::Real && (v.type == ValueType::Int || v.type == ValueType::Bool))
        target = Value::real(static_cast<double64>(v.i));
    else if (target.type == ValueType::Int && v.type == ValueType::Real)
        target = Value::integer(static_cast<int64>(v.r));
    else
        target = v;
}

float32& component(Value& v, Field f)
{
    switch (f)
    {
    case Field::X: return v.v.x;
    case Field::Y: return v.v.y;
    default: return v.v.z;
    }
}

bool isNumber(const Value& v)
{
    return v.type == ValueType::Int || v.type == ValueType::Real || v.type == ValueType::Bool;
}

Value binary(Op op, const Value& a, const Value& b)
{
    // Strings: concatenation and case-insensitive comparison.
    if (a.type == ValueType::String || b.type == ValueType::String)
    {
        const std::string x = lower(a.s), y = lower(b.s);
        switch (op)
        {
        case Op::Add: return Value::string(a.s + b.s);
        case Op::Eq: return Value::boolean(x == y);
        case Op::Ne: return Value::boolean(x != y);
        case Op::Lt: return Value::boolean(x < y);
        case Op::Gt: return Value::boolean(x > y);
        case Op::Le: return Value::boolean(x <= y);
        case Op::Ge: return Value::boolean(x >= y);
        default: throw ScriptError{"bad string operation"};
        }
    }
    // Entities and NIL compare by handle.
    if (!isNumber(a) || !isNumber(b))
    {
        if (a.type == ValueType::Vec3 && b.type == ValueType::Vec3)
        {
            if (op == Op::Add)
                return Value::vec3(a.v + b.v);
            if (op == Op::Sub)
                return Value::vec3(a.v - b.v);
        }
        const bool same = a.asEntity() == b.asEntity() && a.isNil() == b.isNil();
        if (op == Op::Eq)
            return Value::boolean(same);
        if (op == Op::Ne)
            return Value::boolean(!same);
        throw ScriptError{"bad operands"};
    }
    const bool real = a.type == ValueType::Real || b.type == ValueType::Real || op == Op::Div;
    if (op == Op::And || op == Op::Or)
    {
        if (a.type == ValueType::Bool && b.type == ValueType::Bool)
            return Value::boolean(op == Op::And ? (a.i && b.i) : (a.i || b.i));
        return Value::integer(op == Op::And ? (a.asInt() & b.asInt()) : (a.asInt() | b.asInt()));
    }
    if (real)
    {
        const double64 x = a.asReal(), y = b.asReal();
        switch (op)
        {
        case Op::Add: return Value::real(x + y);
        case Op::Sub: return Value::real(x - y);
        case Op::Mul: return Value::real(x * y);
        case Op::Div: return Value::real(y != 0.0 ? x / y : 0.0);
        case Op::Eq: return Value::boolean(x == y);
        case Op::Ne: return Value::boolean(x != y);
        case Op::Lt: return Value::boolean(x < y);
        case Op::Gt: return Value::boolean(x > y);
        case Op::Le: return Value::boolean(x <= y);
        case Op::Ge: return Value::boolean(x >= y);
        default: break;
        }
    }
    const int64 x = a.asInt(), y = b.asInt();
    switch (op)
    {
    case Op::Add: return Value::integer(x + y);
    case Op::Sub: return Value::integer(x - y);
    case Op::Mul: return Value::integer(x * y);
    case Op::IntDiv: return Value::integer(y ? x / y : 0);
    case Op::Mod: return Value::integer(y ? x % y : 0);
    case Op::Eq: return Value::boolean(x == y);
    case Op::Ne: return Value::boolean(x != y);
    case Op::Lt: return Value::boolean(x < y);
    case Op::Gt: return Value::boolean(x > y);
    case Op::Le: return Value::boolean(x <= y);
    case Op::Ge: return Value::boolean(x >= y);
    default: throw ScriptError{"bad integer operation"};
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// NativeCall

const Value& NativeCall::arg(std::size_t i) const
{
    static const Value kNil;
    return i < args_.size() ? args_[i] : kNil;
}

std::string NativeCall::string(std::size_t i) const
{
    return lower(arg(i).s);
}

void NativeCall::set(std::size_t i, const Value& v)
{
    if (i >= refs_.size() || !refs_[i])
        return;
    const VarRef& r = *refs_[i];
    assign(engine_.deref(task_, engine_.slot(task_, static_cast<Scope>(r.scope), r.slot, r.frame)), v);
    if (i < args_.size())
        args_[i] = v;
}

void NativeCall::sleep(uint32 ms)
{
    task_.wakeAt = engine_.time_ + static_cast<double64>(ms) / 1000.0;
}

void NativeCall::stopTask()
{
    task_.done = true;
    engine_.programs_[task_.program].removed = true;
}

void NativeCall::endTask()
{
    task_.done = true;
}

uint32 NativeCall::self() const
{
    return engine_.programs_[task_.program].entityHandle;
}

// ---------------------------------------------------------------------------------------------
// Engine

void Engine::load(const Assets::MlsFile& mls, const ConstantTable& constants)
{
    for (std::size_t i = 0; i < mls.scriptCount(); ++i)
    {
        const Assets::MlsScript& rec = mls.script(i);
        if (!rec.source.empty())
            add(rec.entityName, rec.source, constants);
    }
    registerBuiltins();
    std::cerr << "[script] compiled " << programs_.size() << " programs (" << errors_.size() << " failed), "
              << levelVars_.size() << " level variables" << std::endl;
}

bool Engine::loadSource(const std::string& entity, const std::string& source, const ConstantTable& constants)
{
    const bool ok = add(entity, source, constants);
    registerBuiltins();
    return ok;
}

bool Engine::add(const std::string& entity, const std::string& source, const ConstantTable& constants)
{
    auto program = compile(source, constants);
    if (!program)
    {
        errors_.push_back(entity + ": " + program.error());
        std::cerr << "[script] " << entity << ": " << program.error() << std::endl;
        return false;
    }
    Loaded l;
    l.program = std::move(*program);
    if (!entity.empty())
        l.program.entity = lower(entity);
    for (const std::string& type : l.program.globalTypes)
        l.globals.push_back(defaultValue(type));
    // The level program's VARs are the level's variables: another program's "x : level_var"
    // names the level program's x (exe keeps them in its memory, 0x71a860, at the
    // level program's symbol offsets; a save's script data is that memory).
    if (l.program.entityType == "et_level")
        for (std::size_t g = 0; g < l.program.globalNames.size(); ++g)
        {
            auto [it, added] = levelVarIndex_.try_emplace(l.program.globalNames[g], static_cast<uint32>(levelVars_.size()));
            if (added)
                levelVars_.push_back(l.globals[g]);
            l.globalLevel.push_back(it->second);
        }
    for (std::size_t v = 0; v < l.program.levelVars.size(); ++v)
    {
        const std::string& name = l.program.levelVars[v];
        auto [it, added] = levelVarIndex_.try_emplace(name, static_cast<uint32>(levelVars_.size()));
        if (added)
            levelVars_.push_back(defaultValue(l.program.levelVarTypes[v]));
        l.levelSlots.push_back(it->second);
    }
    if (const auto h = entityHandles_.find(l.program.entity); h != entityHandles_.end())
        l.entityHandle = h->second;
    byEntity_[l.program.entity].push_back(static_cast<uint32>(programs_.size()));
    programs_.push_back(std::move(l));
    return true;
}

void Engine::registerBuiltins()
{
    // Built-ins of the language runtime (bind() may override any of them).
    natives_.try_emplace("this", [](NativeCall& c) { return Value::entity(c.self()); });
    natives_.try_emplace("sleep", [](NativeCall& c) { c.sleep(static_cast<uint32>(std::max<int64>(0, c.integer(0)))); return Value::nil(); });
    natives_.try_emplace("removethisscript", [](NativeCall& c) { c.stopTask(); return Value::nil(); });
    natives_.try_emplace("runscript", [this](NativeCall& c) { start(c.string(0), c.string(1)); return Value::nil(); });
    natives_.try_emplace("writedebug", [](NativeCall&) { return Value::nil(); });
    natives_.try_emplace("assert", [](NativeCall&) { return Value::nil(); });
    natives_.try_emplace("setvector", [](NativeCall& c) {
        c.set(0, Value::vec3({c.real(1), c.real(2), c.real(3)}));
        return Value::nil();
    });
}

void Engine::bind(const std::string& name, NativeFn fn)
{
    natives_[lower(name)] = std::move(fn);
}

void Engine::setEntityHandle(const std::string& entity, uint32 handle)
{
    entityHandles_[lower(entity)] = handle;
    if (const auto it = byEntity_.find(lower(entity)); it != byEntity_.end())
        for (uint32 p : it->second)
            programs_[p].entityHandle = handle;
}

int Engine::findProgram(const std::string& entity) const
{
    const auto it = byEntity_.find(lower(entity));
    return it == byEntity_.end() || it->second.empty() ? -1 : static_cast<int>(it->second.front());
}

bool Engine::has(const std::string& entity, const std::string& script) const
{
    const auto it = byEntity_.find(lower(entity));
    if (it == byEntity_.end())
        return false;
    for (uint32 p : it->second)
        if (!programs_[p].removed && !programs_[p].removedScripts.contains(lower(script)) &&
            programs_[p].program.findScript(lower(script)))
            return true;
    return false;
}

bool Engine::start(const std::string& entity, const std::string& script)
{
    const auto it = byEntity_.find(lower(entity));
    if (it == byEntity_.end())
        return false;
    bool started = false;
    const std::string name = lower(script);
    for (uint32 p : it->second)
    {
        Loaded& l = programs_[p];
        if (l.removed || l.removedScripts.contains(name))
            continue;
        const auto r = l.program.routineIndex.find(name);
        if (r == l.program.routineIndex.end() || !l.program.routines[r->second].isScript)
            continue;
        auto task = std::make_unique<Task>();
        task->program = p;
        task->label = l.program.entity + ":" + name;
        task->wakeAt = time_;
        Frame f;
        f.routine = r->second;
        for (const std::string& type : l.program.routines[r->second].slotTypes)
            f.slots.push_back(defaultValue(type));
        task->frames.push_back(std::move(f));
        tasks_.push_back(std::move(task));
        started = true;
    }
    return started;
}

void Engine::detach(const std::string& entity)
{
    const auto it = byEntity_.find(lower(entity));
    if (it == byEntity_.end())
        return;
    for (uint32 p : it->second)
        programs_[p].removed = true;
    for (auto& t : tasks_)
        if (std::find(it->second.begin(), it->second.end(), t->program) != it->second.end())
            t->done = true;
}

void Engine::attach(const std::string& entity)
{
    const auto it = byEntity_.find(lower(entity));
    if (it == byEntity_.end())
        return;
    for (uint32 p : it->second)
    {
        programs_[p].removed = false;
        programs_[p].removedScripts.clear();
    }
}

void Engine::removeScript(const std::string& entity, const std::string& script)
{
    const auto it = byEntity_.find(lower(entity));
    if (it == byEntity_.end())
        return;
    const std::string name = lower(script);
    for (uint32 p : it->second)
    {
        Loaded& l = programs_[p];
        l.removedScripts.insert(name);
        const std::string label = l.program.entity + ":" + name;
        for (auto& t : tasks_)
            if (t->program == p && t->label == label)
                t->done = true;
    }
}

bool Engine::copyScripts(const std::string& from, const std::string& to)
{
    const auto it = byEntity_.find(lower(from));
    if (it == byEntity_.end() || it->second.empty())
        return false;
    const std::vector<uint32> sources = it->second; // (programs_ grows below)
    for (uint32 p : sources)
    {
        Loaded l;
        l.program = programs_[p].program;
        l.program.entity = lower(to);
        for (const std::string& type : l.program.globalTypes)
            l.globals.push_back(defaultValue(type));
        l.levelSlots = programs_[p].levelSlots;
        if (const auto h = entityHandles_.find(l.program.entity); h != entityHandles_.end())
            l.entityHandle = h->second;
        byEntity_[l.program.entity].push_back(static_cast<uint32>(programs_.size()));
        programs_.push_back(std::move(l));
    }
    return true;
}

void Engine::killScript(const std::string& entity, const std::string& script)
{
    const auto it = byEntity_.find(lower(entity));
    if (it == byEntity_.end())
        return;
    for (uint32 p : it->second)
    {
        const std::string label = programs_[p].program.entity + ":" + lower(script);
        for (auto& t : tasks_)
            if (t->program == p && t->label == label)
                t->done = true;
    }
}

void Engine::startAll(const std::string& script)
{
    // The level's own program first, then every entity's, as the game creates the level first.
    std::vector<std::string> entities;
    for (const Loaded& l : programs_)
        if (l.program.entityType == "et_level" && std::find(entities.begin(), entities.end(), l.program.entity) == entities.end())
            entities.push_back(l.program.entity);
    for (const Loaded& l : programs_)
        if (std::find(entities.begin(), entities.end(), l.program.entity) == entities.end())
            entities.push_back(l.program.entity);
    for (const std::string& e : entities)
        start(e, script);
}

Value Engine::global(const std::string& entity, const std::string& name) const
{
    const int p = findProgram(entity);
    if (p < 0)
        return Value::nil();
    const Loaded& l = programs_[static_cast<std::size_t>(p)];
    for (std::size_t i = 0; i < l.program.globalNames.size() && i < l.globals.size(); ++i)
        if (l.program.globalNames[i] == lower(name))
            return globalSlot(l, i);
    return Value::nil();
}

Value& Engine::globalSlot(Loaded& l, std::size_t index)
{
    return index < l.globalLevel.size() ? levelVars_[l.globalLevel[index]] : l.globals[index];
}

const Value& Engine::globalSlot(const Loaded& l, std::size_t index) const
{
    return index < l.globalLevel.size() ? levelVars_[l.globalLevel[index]] : l.globals[index];
}

bool Engine::setGlobal(const std::string& entity, const std::string& name, const Value& v)
{
    const int p = findProgram(entity);
    if (p < 0)
        return false;
    Loaded& l = programs_[static_cast<std::size_t>(p)];
    for (std::size_t i = 0; i < l.program.globalNames.size() && i < l.globals.size(); ++i)
        if (l.program.globalNames[i] == lower(name))
        {
            globalSlot(l, i) = v;
            return true;
        }
    return false;
}

Value Engine::levelVar(const std::string& name) const
{
    const auto it = levelVarIndex_.find(lower(name));
    return it == levelVarIndex_.end() ? Value::nil() : levelVars_[it->second];
}

void Engine::update(double64 dt)
{
    time_ += dt;
    for (std::size_t i = 0; i < tasks_.size(); ++i) // tasks started meanwhile run in this frame too
    {
        Task& t = *tasks_[i];
        if (!t.done && t.wakeAt <= time_)
            run(t);
    }
    std::erase_if(tasks_, [](const std::unique_ptr<Task>& t) { return t->done; });
}

Value& Engine::slot(Task& task, Scope scope, uint32 index, uint32 frameIndex)
{
    Loaded& l = programs_[task.program];
    switch (scope)
    {
    case Scope::Local:
        if (frameIndex >= task.frames.size() || index >= task.frames[frameIndex].slots.size())
            throw ScriptError{"bad local slot"};
        return task.frames[frameIndex].slots[index];
    case Scope::Global:
        if (index >= l.globals.size())
            throw ScriptError{"bad global slot"};
        return globalSlot(l, index);
    case Scope::Level:
        if (index >= l.levelSlots.size())
            throw ScriptError{"bad level variable"};
        return levelVars_[l.levelSlots[index]];
    }
    throw ScriptError{"bad scope"};
}

Value& Engine::deref(Task& task, Value& v)
{
    Value* p = &v;
    for (int depth = 0; p->type == ValueType::Ref && depth < 16; ++depth)
        p = &slot(task, static_cast<Scope>(p->ref.scope), p->ref.slot, p->ref.frame);
    return *p;
}

std::vector<std::string> Engine::unboundNatives() const
{
    std::vector<std::string> out;
    for (const Loaded& l : programs_)
        for (const std::string& n : l.program.natives)
        {
            // A name the linker made a call of the program's own routine is no native.
            const auto r = l.program.routineIndex.find(n);
            if (!natives_.contains(n) && (r == l.program.routineIndex.end() || l.program.routines[r->second].isScript))
                out.push_back(n);
        }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

Value Engine::callNative(Task& task, uint32 nameIndex, uint32 argc)
{
    const std::string& name = programs_[task.program].program.natives[nameIndex];
    if (task.stack.size() < argc)
        throw ScriptError{"stack underflow calling " + name};
    std::vector<Value> args(task.stack.end() - argc, task.stack.end());
    task.stack.resize(task.stack.size() - argc);
    std::vector<VarRef> refStore(argc);
    std::vector<const VarRef*> refs(argc, nullptr);
    for (uint32 i = 0; i < argc; ++i)
        if (args[i].type == ValueType::Ref)
        {
            refStore[i] = args[i].ref;
            refs[i] = &refStore[i];
            args[i] = deref(task, args[i]);
        }
    const auto it = natives_.find(name);
    if (it == natives_.end())
    {
        if (missing_.insert(name).second)
            std::cerr << "[script] unimplemented native '" << name << "' (" << task.label << ")\n";
        return Value::integer(0);
    }
    NativeCall call(*this, task, args, refs);
    return it->second(call);
}

void Engine::enterRoutine(Task& task, uint32 routine, uint32 argc)
{
    const Routine& r = programs_[task.program].program.routines[routine];
    if (task.stack.size() < argc)
        throw ScriptError{"stack underflow calling " + r.name};
    if (task.frames.size() > 256)
        throw ScriptError{"call depth exceeded in " + r.name};
    Frame f;
    f.routine = routine;
    for (const std::string& type : r.slotTypes)
        f.slots.push_back(defaultValue(type));
    const std::size_t base = task.stack.size() - argc;
    for (uint32 i = 0; i < argc && i < r.paramCount; ++i)
    {
        Value& a = task.stack[base + i];
        const bool byRef = i < r.paramByRef.size() && r.paramByRef[i];
        if (byRef && a.type == ValueType::Ref)
            f.slots[i] = a;
        else
            assign(f.slots[i], a.type == ValueType::Ref ? deref(task, a) : a);
    }
    task.stack.resize(base);
    task.frames.push_back(std::move(f));
}

void Engine::run(Task& task)
{
    uint32 budget = kBudgetPerRun;
    try
    {
        while (!task.done && task.wakeAt <= time_)
        {
            if (budget-- == 0)
            {
                task.wakeAt = time_ + 1e-6; // a loop without Sleep: continue next frame
                return;
            }
            const uint32 fi = static_cast<uint32>(task.frames.size() - 1);
            Frame& f = task.frames.back();
            // Fetched again each step: a native that makes an entity adds a program, and
            // programs_ may move.
            const Program& prog = programs_[task.program].program;
            const Routine& r = prog.routines[f.routine];
            if (f.pc >= r.code.size())
                throw ScriptError{"ran off the end of " + r.name};
            const Instr& in = r.code[f.pc++];
            auto pop = [&] {
                if (task.stack.empty())
                    throw ScriptError{"stack underflow"};
                Value v = std::move(task.stack.back());
                task.stack.pop_back();
                return v;
            };
            switch (in.op)
            {
            case Op::PushConst: task.stack.push_back(prog.constants[in.a]); break;
            case Op::Load: task.stack.push_back(deref(task, slot(task, in.scope, in.a, fi))); break;
            case Op::Store: { Value v = pop(); assign(deref(task, slot(task, in.scope, in.a, fi)), v); break; }
            case Op::LoadField:
            {
                Value v = pop();
                task.stack.push_back(in.field == Field::W ? Value::integer(v.i) : Value::real(component(v, in.field)));
                break;
            }
            case Op::StoreField:
            {
                Value v = pop();
                Value& target = deref(task, slot(task, in.scope, in.a, fi));
                if (target.type != ValueType::Vec3)
                    target = Value::vec3({0.0f, 0.0f, 0.0f});
                if (in.field == Field::W)
                    target.i = v.asInt();
                else
                    component(target, in.field) = v.asFloat();
                break;
            }
            case Op::PushRef:
            {
                Value v;
                v.type = ValueType::Ref;
                v.ref = VarRef{static_cast<uint8>(in.scope), fi, in.a};
                task.stack.push_back(v);
                break;
            }
            case Op::Neg:
            {
                Value v = pop();
                task.stack.push_back(v.type == ValueType::Real ? Value::real(-v.r)
                                     : v.type == ValueType::Vec3 ? Value::vec3(v.v * -1.0f)
                                                                 : Value::integer(-v.i));
                break;
            }
            case Op::Not:
            {
                Value v = pop();
                task.stack.push_back(v.type == ValueType::Int ? Value::integer(~v.i) : Value::boolean(!v.asBool()));
                break;
            }
            case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::IntDiv: case Op::Mod:
            case Op::And: case Op::Or: case Op::Eq: case Op::Ne: case Op::Lt: case Op::Gt: case Op::Le: case Op::Ge:
            {
                Value b = pop();
                Value a = pop();
                task.stack.push_back(binary(in.op, a, b));
                break;
            }
            case Op::Jump: f.pc = in.a; break;
            case Op::JumpIfFalse:
                if (!pop().asBool())
                    f.pc = in.a;
                break;
            case Op::CallNative: task.stack.push_back(callNative(task, in.a, in.b)); break;
            case Op::CallUser: enterRoutine(task, in.a, in.b); break;
            case Op::Return:
            {
                Value result = r.isFunction ? deref(task, f.slots[r.resultSlot]) : Value::nil();
                task.frames.pop_back();
                if (task.frames.empty())
                    task.done = true;
                else
                    task.stack.push_back(std::move(result));
                break;
            }
            case Op::Pop: pop(); break;
            }
        }
    }
    catch (const ScriptError& e)
    {
        const Frame& f = task.frames.back();
        const auto& code = programs_[task.program].program.routines[f.routine].code;
        const uint32 line = f.pc > 0 && f.pc - 1 < code.size() ? code[f.pc - 1].line : 0;
        std::cerr << "[script] " << task.label << " line " << line << ": " << e.message << " (task stopped)\n";
        task.done = true;
    }
}

} // namespace Game::Script
