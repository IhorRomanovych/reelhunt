// tools/bsp_dump.cpp - summarise a game world .bsp (dev tool).
//
//   bsp_dump <scene.bsp>
//
// Prints the parse summary and the baked prelight colours: mean, min/max and a brightness
// histogram, which shows whether a level is lit for day or night.
#include "Assets/world.h"

#include <algorithm>
#include <array>
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: bsp_dump <scene.bsp>\n");
        return 2;
    }
    auto world = Assets::World::load(argv[1]);
    if (!world)
    {
        std::fprintf(stderr, "bsp_dump: %s\n", world.error().message.c_str());
        return 1;
    }
    std::printf("leaves %u verts %u tris %u materials %zu\n", world->leafCount, world->totalVerts, world->totalTris,
                world->materialNames.size());

    double sum[4]{};
    float lo[4] = {1, 1, 1, 1}, hi[4]{};
    std::array<std::size_t, 10> histogram{};
    std::size_t n = 0;
    for (const Assets::WorldMesh& m : world->meshes)
        for (const Math::Vector4& c : m.prelight)
        {
            const float v[4] = {c.x, c.y, c.z, c.w};
            for (int k = 0; k < 4; ++k)
            {
                sum[k] += v[k];
                lo[k] = std::min(lo[k], v[k]);
                hi[k] = std::max(hi[k], v[k]);
            }
            const float lum = 0.299f * c.x + 0.587f * c.y + 0.114f * c.z;
            ++histogram[std::min<std::size_t>(9u, static_cast<std::size_t>(lum * 10.0f))];
            ++n;
        }
    if (n == 0)
        return 0;
    std::printf("prelight mean RGBA (%.3f %.3f %.3f %.3f)\n", sum[0] / n, sum[1] / n, sum[2] / n, sum[3] / n);
    std::printf("prelight min (%.3f %.3f %.3f %.3f) max (%.3f %.3f %.3f %.3f)\n", lo[0], lo[1], lo[2], lo[3], hi[0],
                hi[1], hi[2], hi[3]);
    std::printf("luminance histogram (0.0-0.1 ... 0.9-1.0):");
    for (std::size_t h : histogram)
        std::printf(" %.1f%%", 100.0 * static_cast<double>(h) / static_cast<double>(n));
    std::printf("\n");
    return 0;
}
