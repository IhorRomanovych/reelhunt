// tools/mls_dump.cpp - list the scripts of a game .mls level script file (dev tool).
//
//   mls_dump <level.mls> [script-or-entity-name | *]
//
// One line per script: name, entity, code size and event handlers. With a name, also that
// script's embedded source (the DBUG/SRCE section) and its symbol table.
#include "Assets/mls.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: mls_dump <file.mls> [script-or-entity-name]\n");
        return 2;
    }
    Assets::MlsFile mls;
    if (!mls.load(argv[1]))
    {
        std::fprintf(stderr, "mls_dump: cannot read %s\n", argv[1]);
        return 1;
    }
    const std::string only = argc > 2 ? argv[2] : "";
    for (std::size_t i = 0; i < mls.scriptCount(); ++i)
    {
        const Assets::MlsScript& s = mls.script(i);
        const bool all = only == "*";
        if (!only.empty() && !all && s.name != only && s.entityName != only)
            continue;
        std::printf("%3zu %-28s entity '%s' type %u code %zu words data %zu bytes events:", i, s.name.c_str(),
                    s.entityName.c_str(), s.entityType, s.code.size(), s.data.size());
        for (const Assets::MlsEvent& e : s.events)
            std::printf(" %s#%u@%u", e.name, e.onTrigger, e.scriptStart);
        std::printf("\n");
        if (only.empty())
            continue;
        for (const Assets::MlsSymbol& sym : s.symbols)
            std::printf("  symbol %-24s offset %u size %u type %u\n", sym.name, sym.offset, sym.size, sym.valueType);
        std::printf("----- source -----\n%s\n", s.source.c_str());
        // "code" as a third argument: the compiled words too (native calls are function ids).
        if (argc > 3 && std::string(argv[3]) == "code")
            for (std::size_t w = 0; w < s.code.size(); ++w)
                std::printf("%s%6zu: %08x", w % 8 == 0 ? "\n" : " ", w, s.code[w]);
    }
    return 0;
}
