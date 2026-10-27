// Game/script/script_compiler.h - compiles the embedded source of a game level script
// (the .mls DBUG/SRCE section, a small Pascal dialect) into a Program.
//
// Grammar covered (measured over every shipped level's scripts):
//   SCRIPTMAIN name; ENTITY name : type;  CONST / TYPE (enumerations) / VAR sections with
//   level_var / game_var scopes and string[N]; PROCEDURE / FUNCTION with (VAR) parameters,
//   forward-declared and defined later; SCRIPT handlers; statements begin..end, :=, calls with
//   or without parentheses, if/then/else, while/do, for..to/downto..do, repeat..until,
//   case..of..else..end; expressions with Pascal precedence (not; * / div mod and; + - or;
//   = <> < > <= >=), field access (.x .y .z .red ...), NIL, TRUE/FALSE, 'strings', reals.
// Identifiers that are none of variable, constant or routine compile to native calls, which the
// engine resolves by name at run time.
#pragma once
#include "engine_types.h"

#include <expected>
#include <string>
#include <unordered_map>

#include "Game/script/script_program.h"

namespace Game::Script {

// Named constants shared by every script (AISCRIPT_HIGHPRIORITY, HUD_STAMINA, ...).
using ConstantTable = std::unordered_map<std::string, int64>;

// Loads "name value" lines (# comments) such as config/script_constants.txt.
bool loadConstants(const std::string& path, ConstantTable& out);

std::expected<Program, std::string> compile(const std::string& source, const ConstantTable& constants);

} // namespace Game::Script
