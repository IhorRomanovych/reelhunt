#include "Assets/librw_engine.h"

#include "rw.h"

#include <cstddef>
#include <mutex>

namespace {

bool ensureLibrwEngineImpl() noexcept {
    static std::once_flag flag;
    std::call_once(flag, [] {
        rw::Engine::init();
        rw::Engine::open(nullptr);
        rw::Engine::start();
        // DFF materials reference their texture by name only (the real raster
        // lives in the .txd, loaded separately and bound as a GPU texture). Disable
        // disk texture loading so names are never resolved via file reads, and enable
        // dummy textures so every material keeps a name-only Texture handle. That name
        // is what we match against the pre-loaded txd map when building meshes; without
        // dummies an unmatched name would leave Material::texture == nil (empty name).
        rw::Texture::setLoadTextures(false);
        rw::Texture::setCreateDummies(true);
        // Register the skin + hanim streaming plugins so DFF loads populate per-vertex bone
        // indices/weights (Skin on geometry) and the animation hierarchy (HAnimHierarchy via
        // the Frame tree). Neither is auto-registered by Engine::start(); both are safe under
        // the NULL (parse-only) platform.
        rw::registerSkinPlugin();
        rw::registerHAnimPlugin();
    });
    return rw::Engine::state == rw::Engine::State::Started;
}

}  // namespace anonymous

namespace Assets {

bool ensureLibrwEngine() {
    return ensureLibrwEngineImpl();
}

}  // namespace Assets
