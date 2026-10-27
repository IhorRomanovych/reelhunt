// Renderer/scene.cpp - owning entity list with id lookup.
#include "Renderer/scene.h"
#include "Renderer/camera.h"

#include <algorithm>
#include <unordered_set>

namespace Renderer {

Engine::Rw::Entity* Scene::addEntity(std::unique_ptr<Engine::Rw::Entity> e) {
    if (!e)
        return nullptr;
    Engine::Rw::Entity* raw = e.get();
    if (!activeCamera_)
        if (const auto* cam = dynamic_cast<const Camera*>(raw))
            activeCamera_ = cam;
    byId_[raw->id()] = raw;
    entities_.push_back(std::move(e));
    return raw;
}

void Scene::removeEntity(ObjectId id) {
    removeEntities({id});
}

void Scene::removeEntities(const std::vector<ObjectId>& ids) {
    if (ids.empty())
        return;
    const std::unordered_set<ObjectId> doomed(ids.begin(), ids.end());
    for (ObjectId id : ids) {
        auto it = byId_.find(id);
        if (it == byId_.end())
            continue;
        if (it->second == activeCamera_)
            activeCamera_ = nullptr;
        byId_.erase(it);
    }
    std::erase_if(entities_, [&](const std::unique_ptr<Engine::Rw::Entity>& e) {
        return e && doomed.contains(e->id());
    });
}

Engine::Rw::Entity* Scene::find(ObjectId id) const noexcept {
    const auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : it->second;
}

} // namespace Renderer
