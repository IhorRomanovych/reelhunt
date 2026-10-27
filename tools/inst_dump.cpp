// tools/inst_dump.cpp - list the placements of a game .inst file (dev tool).
//
//   inst_dump <entity.inst> [entity-class]
//
// One line per placement: class, record, instance name, position, rotation and its raw
// parameters (each shown as int and as float - MH1 stores no names or types for them).
#include "Assets/inst.h"

#include <bit>
#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: inst_dump <entity.inst> [entity-class]\n");
        return 2;
    }
    const std::string only = argc > 2 ? argv[2] : "";
    auto inst = Assets::Inst::load(argv[1]);
    if (!inst)
    {
        std::fprintf(stderr, "inst_dump: %s\n", inst.error().message.c_str());
        return 1;
    }
    for (const Assets::InstPlacement& p : *inst)
    {
        if (!only.empty() && p.entityClass != only)
            continue;
        std::printf("%-12s %-28s %-28s pos (%.2f %.2f %.2f) rot (%.3f %.3f %.3f %.3f)\n", p.entityClass.c_str(),
                    p.record.c_str(), p.internalName.c_str(), p.position.x, p.position.y, p.position.z, p.rotation.x,
                    p.rotation.y, p.rotation.z, p.rotation.w);
        for (std::size_t i = 0; i < p.params.size(); ++i)
            std::printf("    [%zu] %d / %g\n", i, p.params[i], std::bit_cast<float>(p.params[i]));
    }
    return 0;
}
