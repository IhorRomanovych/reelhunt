// Game/texture_set.h - decoded TXD textures with name lookup and alpha-test classification.
#pragma once
#include "engine_types.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/txd.h"
#include "Renderer/material.h"
#include "Renderer/texture.h"

namespace Game {

// Owns the textures of one or more texture dictionaries. Materials and meshes hold raw pointers
// into it, so entries are stored in a deque (stable addresses) and never removed.
class TextureSet {
public:
    // Lower-cased name without a file extension: the key every lookup uses.
    [[nodiscard]] static std::string normalizeName(const std::string& name);

    // Add every texture of a decoded dictionary. Entries without pixels keep their name but
    // render untextured. When two dictionaries share a name, the larger texture wins lookups.
    void add(const std::vector<Assets::TxdTexture>& entries);

    // Textures the original renders opaque even though their alpha channel has cut-out pixels.
    void setForceOpaque(std::vector<std::string> names);

    [[nodiscard]] const Renderer::Texture* find(const std::string& name) const;
    // Exact match first, then the first texture whose name contains `name` or vice versa.
    [[nodiscard]] const Renderer::Texture* findFuzzy(const std::string& name) const;

    // Bind `tex` to `material` and set its alpha mode: cut-outs alpha-test (AlphaTest), real
    // partial alpha blends (BlendOn: drawn in the forward translucent pass).
    void apply(Renderer::Material& material, const Renderer::Texture* tex) const;

    [[nodiscard]] Assets::AlphaClass alphaClass(const Renderer::Texture* tex) const;
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t withPixels() const noexcept;
    [[nodiscard]] std::vector<uint32> handles() const;

    // Iteration over (normalized name, texture) for heuristics that need every entry.
    template <typename F>
    void forEach(F&& f) const {
        for (const Entry& e : entries_)
            f(e.key, &e.texture);
    }

private:
    struct Entry {
        std::string key;              // normalized name
        Renderer::Texture texture;
        Assets::AlphaClass alpha = Assets::AlphaClass::Opaque;
        std::size_t area = 0;
    };

    std::deque<Entry> entries_;
    std::unordered_map<std::string, const Entry*> byName_;
    std::unordered_map<const Renderer::Texture*, const Entry*> byTexture_;
    std::unordered_set<std::string> forceOpaque_;
};

} // namespace Game
