// tests/script_test.cpp - the level-script compiler and engine: language semantics on a synthetic
// program, and every shipped level's scripts compiling.
#include "Assets/mls.h"
#include "Game/script/script_engine.h"
#include "test_paths.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (!ok)
        ++g_failures;
}

const char* kProgram = R"(
{$t-} {a comment}
SCRIPTMAIN Test;
ENTITY
    Tester : et_name;
CONST
    LIMIT = 4;
TYPE
    tState = (S_IDLE, S_WALK, S_RUN);
VAR
    total : level_var integer;
    v : vec3d;
    st : tState;

FUNCTION Twice(x : integer) : integer;
BEGIN
    Twice := x * 2;
END;

PROCEDURE Bump(VAR n : integer);
BEGIN
    n := n + 10;
END;

SCRIPT OnCreate;
VAR
    i, acc : integer;
    f : real;
BEGIN
    acc := 0;
    for i := 1 to LIMIT do acc := acc + Twice(i);
    Bump(acc);
    st := S_RUN;
    case st of
        S_IDLE: total := 1;
        S_WALK, S_RUN: total := acc;
    else
        total := -1;
    end;
    SetVector(v, 1.0, 2.5, -3.0);
    v.y := v.y * 2.0;
    f := 7 / 2;
    if (f > 3.0) and (v.y = 5.0) and NOT (GetEntity('nobody') <> NIL) then Record(total, f, v.y);
    i := 0;
    repeat i := i + 1 until i >= 3;
    while i < 6 do i := i + 1;
    Record(i, 0, 0);
    RunScript('reader', 'Show');
    Sleep(500);
    Record(99, 0, 0);
    RemoveThisScript;
END;

END.
)";

const char* kReader = R"(
SCRIPTMAIN Reader;
ENTITY reader : et_name;
VAR total : level_var integer;
SCRIPT Show;
BEGIN
    if 'Hello' = 'HELLO' then Record(total + 1, 0, 0);
END;
END.
)";

} // namespace

int main()
{
    std::puts("Language semantics");
    Game::Script::ConstantTable constants;
    Game::Script::Engine engine;
    check(engine.loadSource("tester", kProgram, constants), "the test program compiles");
    check(engine.loadSource("reader", kReader, constants), "a second program compiles");
    std::vector<std::vector<double>> records;
    engine.bind("record", [&](Game::Script::NativeCall& c) {
        records.push_back({c.arg(0).asReal(), c.arg(1).asReal(), c.arg(2).asReal()});
        return Game::Script::Value::nil();
    });
    engine.bind("getentity", [](Game::Script::NativeCall&) { return Game::Script::Value::nil(); });
    check(engine.start("TESTER", "oncreate"), "an event handler starts by case-insensitive name");
    engine.update(0.016);
    check(records.size() == 3, "three records before the sleep (got " + std::to_string(records.size()) + ")");
    if (records.size() >= 3)
    {
        check(records[0][0] == 30.0, "for + function + VAR parameter: 2+4+6+8 then +10 = 30");
        check(std::fabs(records[0][1] - 3.5) < 1e-9, "'/' divides as reals");
        check(records[0][2] == 5.0, "SetVector out-argument and field assignment");
        check(records[1][0] == 6.0, "repeat..until and while loops");
        check(records[2][0] == 31.0, "RunScript starts another program; level variables are shared; strings compare case-insensitively");
    }
    engine.update(0.3);
    check(records.size() == 3, "Sleep(500) suspends the task");
    engine.update(0.3);
    check(records.size() == 4 && records.back()[0] == 99.0, "the task resumes after its sleep");
    check(!engine.start("tester", "oncreate"), "RemoveThisScript disables the program's handlers");
    check(engine.levelVar("total").asInt() == 30, "case with several labels picks the right branch");

    // A level_var is the level program's VAR of that name (the exe keeps both at the level
    // program's symbol offset, 0x71a860): what a trigger sets, the level script reads.
    {
        Game::Script::Engine level;
        const char* kTrigger = R"(
SCRIPTMAIN TriggerScript;
ENTITY trigger_01 : et_name;
VAR status : level_var integer;
SCRIPT OnEnterTrigger;
BEGIN
    status := 3;
END;
END.
)";
        const char* kLevel = R"(
SCRIPTMAIN LevelScripts;
ENTITY Some_Level : et_level;
VAR status : integer;
SCRIPT OnCreate;
BEGIN
    status := 1;
END;
END.
)";
        check(level.loadSource("trigger_01", kTrigger, constants) && level.loadSource("some_level", kLevel, constants),
              "a trigger script and a level script compile");
        level.start("some_level", "oncreate");
        level.update(0.016);
        const bool seen = level.levelVar("status").asInt() == 1;
        level.start("trigger_01", "onentertrigger");
        level.update(0.016);
        check(seen && level.global("some_level", "status").asInt() == 3,
              "a trigger's level_var is the level program's VAR (both ways)");
        level.setGlobal("some_level", "status", Game::Script::Value::integer(7));
        check(level.levelVar("status").asInt() == 7, "a restored level VAR is what the level_vars read");
        // DestroyEntity takes the entity's script instance with it; made again, it has a new one.
        level.detach("trigger_01");
        const bool gone = !level.start("trigger_01", "onentertrigger");
        level.attach("trigger_01");
        check(gone && level.start("trigger_01", "onentertrigger"), "a destroyed entity's script takes no events until it is made again");
    }

    std::puts("Shipped scripts");
    const std::string levels = TestPaths::gameRoot() + "levels/";
    if (!std::filesystem::exists(levels))
    {
        std::puts("SKIP (no game data)");
        return 77;
    }
    std::size_t total = 0, failed = 0;
    for (const auto& dir : std::filesystem::directory_iterator(levels))
    {
        const std::string name = dir.path().filename().string();
        const std::string path = dir.path().string() + "/scripts/" + name + ".mls";
        Assets::MlsFile mls;
        if (!std::filesystem::exists(path) || !mls.load(path))
            continue;
        for (std::size_t i = 0; i < mls.scriptCount(); ++i)
        {
            if (mls.script(i).source.empty())
                continue;
            ++total;
            if (!Game::Script::compile(mls.script(i).source, constants))
                ++failed;
        }
    }
    std::printf("  %zu scripts, %zu fail to compile\n", total, failed);
    check(total > 900, "every level's scripts are found");
    // Estate_Ext's willies_door_(sd)01 ships with prose outside any comment; nothing compiles it.
    check(failed <= 1, "all shipped scripts compile except the one with a broken source");

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
