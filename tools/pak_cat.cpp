// tools/pak_cat.cpp - list or print the files of a game .pak archive (dev tool).
//
//   pak_cat <the tuning pak>            every file's path
//   pak_cat <the tuning pak> <path>     that file, decoded ("levels/jury_turf/entityTypeData.ini")
#include "Assets/pak.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: pak_cat <the tuning pak> [path]\n");
        return 2;
    }
    const auto pak = Assets::PakFile::load(argv[1]);
    if (!pak)
    {
        std::fprintf(stderr, "pak_cat: cannot read %s: %s\n", argv[1], pak.error().message.c_str());
        return 1;
    }
    if (argc < 3)
    {
        for (const std::string& name : pak->names())
            std::printf("%s\n", name.c_str());
        return 0;
    }
    const auto file = pak->file(argv[2]);
    if (!file)
    {
        std::fprintf(stderr, "pak_cat: no %s in the pak\n", argv[2]);
        return 1;
    }
    std::fwrite(file->data(), 1, file->size(), stdout);
    return 0;
}
