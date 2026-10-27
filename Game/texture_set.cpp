// Game/texture_set.cpp - decoded TXD textures with name lookup and alpha-test classification.
#include "Game/texture_set.h"

#include <span>

namespace Game {

std::string TextureSet::normalizeName(const std::string& name) {
    std::string key = name;
    if (const auto dot = key.find_last_of('.'); dot != std::string::npos)
        key.erase(dot);
    for (char& c : key)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return key;
}

void TextureSet::add(const std::vector<Assets::TxdTexture>& entries) {
    for (const Assets::TxdTexture& t : entries) {
        Entry e;
        e.key = normalizeName(t.name);
        e.area = static_cast<std::size_t>(t.width) * t.height;
        if (t.hasPixels() && e.texture.create(t.width, t.height)) {
            e.texture.setPixelsRGBA8(std::span<const uint8_t>(t.rgba));
            e.alpha = t.alphaClass();
        }
        entries_.push_back(std::move(e));
        const Entry* stored = &entries_.back();
        byTexture_[&stored->texture] = stored;
        auto [it, inserted] = byName_.try_emplace(stored->key, stored);
        if (!inserted && stored->area > it->second->area)
            it->second = stored;
    }
}

void TextureSet::setForceOpaque(std::vector<std::string> names) {
    forceOpaque_.clear();
    for (const std::string& n : names)
        forceOpaque_.insert(normalizeName(n));
}

const Renderer::Texture* TextureSet::find(const std::string& name) const {
    const auto it = byName_.find(normalizeName(name));
    if (it == byName_.end() || !it->second->texture.hasPixels())
        return nullptr;
    return &it->second->texture;
}

const Renderer::Texture* TextureSet::findFuzzy(const std::string& name) const {
    if (const Renderer::Texture* exact = find(name))
        return exact;
    const std::string key = normalizeName(name);
    if (key.empty())
        return nullptr;
    for (const Entry& e : entries_)
        if (e.texture.hasPixels() && !e.key.empty() &&
            (e.key.find(key) != std::string::npos || key.find(e.key) != std::string::npos))
            return &e.texture;
    return nullptr;
}

void TextureSet::apply(Renderer::Material& material, const Renderer::Texture* tex) const {
    if (!tex)
        return;
    material.setTexture(*tex);
    const Assets::AlphaClass a = alphaClass(tex);
    material.setFlag(Engine::RenderMode::AlphaTest, a == Assets::AlphaClass::Cutout);
    material.setFlag(Engine::RenderMode::BlendOn, a == Assets::AlphaClass::Blend);
}

Assets::AlphaClass TextureSet::alphaClass(const Renderer::Texture* tex) const {
    const auto it = byTexture_.find(tex);
    if (it == byTexture_.end() || forceOpaque_.contains(it->second->key))
        return Assets::AlphaClass::Opaque;
    return it->second->alpha;
}

std::size_t TextureSet::withPixels() const noexcept {
    std::size_t n = 0;
    for (const Entry& e : entries_)
        n += e.texture.hasPixels() ? 1u : 0u;
    return n;
}

std::vector<uint32> TextureSet::handles() const {
    std::vector<uint32> out;
    out.reserve(entries_.size());
    for (const Entry& e : entries_)
        if (e.texture.valid())
            out.push_back(e.texture.handle());
    return out;
}

} // namespace Game
