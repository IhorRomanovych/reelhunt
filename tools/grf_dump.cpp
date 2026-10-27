// tools/grf_dump.cpp - summarise a game AI navigation graph (mapAI.grf) (dev tool).
//
//   grf_dump <mapAI.grf> [-v]
//
// Counts, the areas, the paths and every labelled node; -v lists every node with its links.
#include "Assets/grf.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: grf_dump <mapAI.grf> [-v]\n");
        return 2;
    }
    auto grf = Assets::Grf::load(argv[1]);
    if (!grf)
    {
        std::fprintf(stderr, "grf_dump: %s\n", grf.error().message.c_str());
        return 1;
    }
    const bool verbose = argc > 2 && std::strcmp(argv[2], "-v") == 0;
    std::size_t links = 0;
    for (const auto& n : grf->nodes)
        links += n.links.size();
    std::printf("nodes %zu links %zu paths %zu areas %zu\n", grf->nodes.size(), links, grf->paths.size(), grf->areas.size());
    for (std::size_t i = 0; i < grf->areas.size(); ++i)
        std::printf("area %zu '%s'\n", i, grf->areas[i].c_str());
    for (const auto& p : grf->paths)
    {
        std::printf("path '%s':", p.name.c_str());
        for (int32_t n : p.nodes)
            std::printf(" %d", n);
        std::printf("\n");
    }
    for (std::size_t i = 0; i < grf->nodes.size(); ++i)
    {
        const auto& n = grf->nodes[i];
        if (!verbose && n.label.empty())
            continue;
        std::printf("node %zu '%s' label '%s' area %d pos (%.2f %.2f %.2f) r %.2f links", i, n.name.c_str(), n.label.c_str(),
                    n.area, n.position.x, n.position.y, n.position.z, n.radius);
        for (const auto& l : n.links)
            std::printf(" %d/%d", l.node, l.type);
        std::printf("\n");
    }
    return 0;
}
