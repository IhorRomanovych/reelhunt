#pragma once

#include "engine_types.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace Assets {

// One texture from a RenderWare texture dictionary, decoded to RGBA8.
// How a texture uses its alpha channel.
enum class AlphaClass : uint8 {
    Opaque, // no transparent texels
    Cutout, // on/off holes (chain link, barbed wire): alpha-tested
    Blend,  // real partial alpha (glass, decals, soft mesh): alpha-blended
};

struct TxdTexture {
    std::string name;
    uint32 width = 0;
    uint32 height = 0;
    uint32 mipLevels = 0;          // mip count stored in the file (informational; only level 0 is decoded)
    uint32 format = 0;             // raw RW raster format flags
    std::vector<uint8_t> rgba;     // level 0, width*height*4 bytes, row 0 = top of the image

    [[nodiscard]] bool hasPixels() const noexcept {
        return width > 0u && height > 0u && rgba.size() == static_cast<std::size_t>(width) * height * 4u;
    }
    // True if any pixel has alpha < 0.5, i.e. the alpha-test cutout would discard it.
    [[nodiscard]] bool hasCutoutAlpha() const noexcept;
    // Opaque / Cutout / Blend from the alpha histogram: more than kBlendShare of the texels
    // between 16 and 239 means the alpha is a real opacity, not just holes.
    static constexpr float kBlendShare = 0.10f;
    [[nodiscard]] AlphaClass alphaClass() const noexcept;
};

class Txd {
public:
    // Decode every texture in the dictionary.
    static std::expected<std::vector<TxdTexture>, Engine::EngineError> load(const std::string& path);

    // As load(), and also make the dictionary librw's current TexDictionary so subsequent DFF
    // loads resolve material texture names against it. The dictionary stays alive for the
    // session (DFF materials may reference it).
    static std::expected<std::vector<TxdTexture>, Engine::EngineError> loadAndBind(const std::string& path);
};

}  // namespace Assets
