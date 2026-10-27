// Game/ai/ai_natives.h - the level-script AI functions (AIAddEntity, AIDefineGoal*, ...) bound to
// an AiDirector. Kept apart from the rest of the game's natives so tests can run a level's real
// set-up script against a director alone.
#pragma once
#include "engine_types.h"

#include <functional>

#include "Game/ai/ai_director.h"
#include "Game/script/script_engine.h"

namespace Game {

// `toScene` turns a script position into the scene's frame (identity when not given).
void bindAiNatives(Script::Engine& engine, AiDirector& director,
                   std::function<Math::Vector3(const Math::Vector3&)> toScene = {});

} // namespace Game
