// tools/col_dump.cpp - list the entries of a game .col collision archive (dev tool).
//
//   col_dump <collisions.col> [entry-name]
//
// One line per entry: name, bounding radius and its sphere / line / vertex / face counts. With
// an entry name, also that entry's bounds, spheres and lines.
#include "Assets/col.h"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: col_dump <file.col> [entry-name]\n");
        return 2;
    }
    auto col = Assets::Col::load(argv[1]);
    if (!col)
    {
        std::fprintf(stderr, "col_dump: %s\n", col.error().message.c_str());
        return 1;
    }
    for (const Assets::ColEntry& e : col->entries)
    {
        std::printf("%-32s r %6.2f  spheres %3zu lines %3zu verts %4zu faces %4zu\n", e.name.c_str(), e.radius,
                    e.spheres.size(), e.lines.size(), e.verticals.size(), e.faces.size());
        if (argc < 3 || e.name != argv[2])
            continue;
        std::printf("  centre (%.3f %.3f %.3f) box (%.3f %.3f %.3f)..(%.3f %.3f %.3f)\n", e.center.x, e.center.y,
                    e.center.z, e.min.x, e.min.y, e.min.z, e.max.x, e.max.y, e.max.z);
        for (const Assets::ColSphere& sp : e.spheres)
            std::printf("  sphere (%.3f %.3f %.3f) r %.3f\n", sp.center.x, sp.center.y, sp.center.z, sp.radius);
        for (const Assets::ColLine& ln : e.lines)
            std::printf("  line (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n", ln.a.x, ln.a.y, ln.a.z, ln.b.x, ln.b.y, ln.b.z);
    }
    std::printf("%zu entries: %u spheres, %u lines, %u verts, %u faces\n", col->entries.size(), col->totalSpheres,
                col->totalLines, col->totalVerticals, col->totalFaces);
    return 0;
}
