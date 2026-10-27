// tools/dff_dump.cpp - list the clumps of a RenderWare .dff (dev tool).
//
//   dff_dump <file.dff> [clump-index]
//
// One line per clump: atomics, vertices, skinning and bone count; with an index, also that
// clump's bone ids and per-atomic material texture names.
#include "Assets/dff.h"
#include "Assets/librw_engine.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: dff_dump <file.dff> [clump-index]\n");
        return 2;
    }
    Assets::ensureLibrwEngine();
    auto clumps = Assets::Dff::load(argv[1]);
    if (!clumps)
    {
        std::fprintf(stderr, "dff_dump: %s\n", clumps.error().message.c_str());
        return 1;
    }
    const long only = argc > 2 ? std::strtol(argv[2], nullptr, 10) : -1;
    for (std::size_t c = 0; c < clumps->size(); ++c)
    {
        const Assets::DffClump& clump = (*clumps)[c];
        std::size_t verts = 0, skinned = 0, prelit = 0;
        for (const auto& a : clump.atomics)
        {
            verts += a.vertices.size();
            skinned += a.skinned ? 1u : 0u;
            prelit += a.prelit ? 1u : 0u;
        }
        std::printf("clump %3zu: atomics %zu (skinned %zu, prelit %zu) verts %zu bones %zu", c, clump.atomics.size(),
                    skinned, prelit, verts, clump.bones.size());
        for (const auto& a : clump.atomics)
            std::printf(" [%s]", a.frameName.c_str());
        std::printf("\n");
        if (static_cast<long>(c) != only)
            continue;
        std::printf("  bone ids:");
        for (const auto& b : clump.bones)
            std::printf(" %d", b.id);
        std::printf("\n");
        // Each bone's frame-list position (restLocal, through the hierarchy), its skin bind
        // position (inverseBind inverted; rigid, column-major) and its local frame offset.
        for (const auto& b : clump.bones)
        {
            const float* m = b.inverseBind;
            const float tx = m[12], ty = m[13], tz = m[14];
            const float bx = -(m[0] * tx + m[1] * ty + m[2] * tz);
            const float by = -(m[4] * tx + m[5] * ty + m[6] * tz);
            const float bz = -(m[8] * tx + m[9] * ty + m[10] * tz);
            std::printf("  bone %5d frame (%.3f %.3f %.3f)  bind (%.3f %.3f %.3f)  local (%.3f %.3f %.3f)\n", b.id,
                        b.restLocal[12], b.restLocal[13], b.restLocal[14], bx, by, bz, b.localRest[12], b.localRest[13],
                        b.localRest[14]);
        }
        for (std::size_t a = 0; a < clump.atomics.size(); ++a)
            for (const auto& m : clump.atomics[a].materials)
                std::printf("  atomic %zu material '%s'\n", a, m.textureName.c_str());
    }
    return 0;
}
