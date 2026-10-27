// tools/ifp_dump.cpp - list the clips of an .ifp animation file (dev tool).
//
//   ifp_dump <file.ifp> [name-filter]
//
// One line per clip: block, name, duration, bone-track count and, for every track with positions,
// the displacement between its first and last key (root motion shows up as the one track that
// travels).
#include "Assets/ifp/ifp.h"

#include <cmath>
#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: ifp_dump <file.ifp> [name-filter]\n");
        return 2;
    }
    const std::string filter = argc > 2 ? argv[2] : "";
    auto ifp = Assets::loadIfp(argv[1]);
    if (!ifp)
    {
        std::fprintf(stderr, "ifp_dump: cannot read %s\n", argv[1]);
        return 1;
    }
    for (const Assets::IfpBlock& block : ifp->blocks)
        for (const Assets::IfpAnimation& anim : block.animations)
        {
            if (!filter.empty() && anim.name.find(filter) == std::string::npos)
                continue;
            std::printf("%-16s %-40s %6.3fs %3zu tracks", block.name.c_str(), anim.name.c_str(), anim.duration, anim.bones.size());
            for (const Assets::IfpBoneTrack& t : anim.bones)
            {
                if (t.frames.empty() || !t.frames.front().hasPos)
                    continue;
                const float* a = t.frames.front().pos;
                const float* b = t.frames.back().pos;
                const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
                if (std::sqrt(dx * dx + dy * dy + dz * dz) <= 0.01f)
                {
                    if (t.boneId == 1000) // the root, even when it holds still
                        std::printf("  bone 1000 at (%.3f %.3f %.3f)", a[0], a[1], a[2]);
                }
                else
                    std::printf("  bone %d from (%.3f %.3f %.3f) moves (%.3f %.3f %.3f) over %zu keys %.3f..%.3fs", t.boneId,
                                a[0], a[1], a[2], dx, dy, dz, t.frames.size(), t.frames.front().time, t.frames.back().time);
            }
            std::printf("\n");
        }
    return 0;
}
