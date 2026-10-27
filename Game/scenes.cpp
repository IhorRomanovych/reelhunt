// Game/scenes.cpp - the game's scene (level) list. See scenes.h.
#include "Game/scenes.h"
#include "Core/platform.h"

#include "Assets/gxt.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace Game {

std::expected<std::vector<SceneInfo>, Engine::EngineError> loadScenes(const std::string& levelsTxtPath,
                                                                      const Assets::Gxt* gxt)
{
    std::ifstream f(Engine::Platform::resolvePath(levelsTxtPath));
    if (!f)
        return std::unexpected(Engine::EngineError::failure(Engine::EngineError::Code::NotFound,
                                                            "cannot open " + levelsTxtPath));
    std::vector<SceneInfo> scenes;
    std::string line;
    while (std::getline(f, line))
    {
        std::istringstream in(line);
        std::string tag, folder;
        if (!(in >> tag >> folder) || tag != "LEVEL")
            continue;
        SceneInfo s;
        s.number = static_cast<int>(scenes.size()) + 1;
        for (char& c : folder)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        s.folder = folder;
        if (gxt)
        {
            const std::string key = "LVL_" + std::to_string(s.number);
            if (gxt->contains(key))
            {
                s.title = gxt->text(key);
                const std::string second = gxt->text(key + "A");
                if (gxt->contains(key + "A") && second != "-")
                    s.title += " " + second;
            }
        }
        scenes.push_back(std::move(s));
    }
    if (scenes.empty())
        return std::unexpected(Engine::EngineError::failure(Engine::EngineError::Code::Invalid,
                                                            "no LEVEL lines in " + levelsTxtPath));
    return scenes;
}

int sceneNumberFromSaveTitle(const std::string& title)
{
    int n = 0;
    std::size_t i = 0;
    while (i < title.size() && std::isdigit(static_cast<unsigned char>(title[i])))
        n = n * 10 + (title[i++] - '0');
    return (i > 0 && i < title.size() && title[i] == ')') ? n : 0;
}

} // namespace Game
