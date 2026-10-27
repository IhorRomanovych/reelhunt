// tools/exe_tables.cpp - write the tables the engine reads from the exe to a folder (dev tool).
//
//   exe_tables <game root> <out dir>
//
// The engine does this itself at start-up (into its user folder); this shows the same tables.
#include "Game/exe_tables.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: exe_tables <game root> <out dir>\n");
        return 2;
    }
    std::string root = argv[1];
    if (!root.empty() && root.back() != '/' && root.back() != '\\')
        root += '/';
    const auto tables = Game::findGameExe(root);
    if (!tables)
    {
        std::fprintf(stderr, "exe_tables: %s\n", tables.error().c_str());
        return 1;
    }
    if (!Game::writeExeTables(*tables, argv[2]))
    {
        std::fprintf(stderr, "exe_tables: cannot write to %s\n", argv[2]);
        return 1;
    }
    std::printf("wrote director_speech.txt, hunter_speech.txt, ped_anims.txt to %s\n", argv[2]);
    return 0;
}
