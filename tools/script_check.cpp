// tools/script_check.cpp - compile every script of one or more .mls files (dev tool).
//
//   script_check <config/script_constants.txt> <level.mls>...
//
// Prints each record that fails to compile and the natives the scripts call.
#include "Game/script/script_compiler.h"
#include "Assets/mls.h"

#include <cstdio>
#include <map>
#include <set>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: script_check <script_constants.txt> <level.mls>...\n");
        return 2;
    }
    Game::Script::ConstantTable constants;
    if (!Game::Script::loadConstants(argv[1], constants))
    {
        std::fprintf(stderr, "script_check: cannot read %s\n", argv[1]);
        return 1;
    }
    std::size_t total = 0, failed = 0;
    std::map<std::string, int> natives;
    for (int a = 2; a < argc; ++a)
    {
        Assets::MlsFile mls;
        if (!mls.load(argv[a]))
            continue;
        for (std::size_t i = 0; i < mls.scriptCount(); ++i)
        {
            const Assets::MlsScript& s = mls.script(i);
            if (s.source.empty())
                continue;
            ++total;
            auto p = Game::Script::compile(s.source, constants);
            if (!p)
            {
                ++failed;
                std::printf("FAIL %s [%s]: %s\n", argv[a], s.entityName.c_str(), p.error().c_str());
                continue;
            }
            for (const auto& n : p->natives)
                ++natives[n];
        }
    }
    std::printf("%zu scripts, %zu failed, %zu distinct natives\n", total, failed, natives.size());
    if (argc == 3)
        for (const auto& [n, c] : natives)
            std::printf("  native %s\n", n.c_str());
    return failed ? 1 : 0;
}
