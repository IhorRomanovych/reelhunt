// Assets/txd.cpp - RenderWare texture dictionary (.txd) loading via librw, decoded to RGBA8.
#include "Assets/txd.h"
#include "Core/platform.h"

#include "Assets/librw_engine.h"

#include "rw.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using Engine::EngineError;

// Dictionaries made current by loadAndBind. Kept alive for the session: DFF materials loaded
// while a dictionary is current may hold references into it.
std::vector<rw::TexDictionary*> g_boundDictionaries;

uint8_t expand5(uint32 v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
uint8_t expand6(uint32 v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }
uint8_t expand4(uint32 v) { return static_cast<uint8_t>(v * 17u); }

// Fallback decode straight from the locked raster, used when librw cannot convert the raster
// to an Image. Handles the D3D8 PC formats (little-endian, BGRA byte order for 32/24-bit).
bool decodeLockedRaster(rw::Raster* raster, std::vector<uint8_t>& out) {
    rw::uint8* px = raster->lock(0, rw::Raster::LOCKREAD);
    if (!px)
        return false;

    const uint32 w = static_cast<uint32>(raster->width);
    const uint32 h = static_cast<uint32>(raster->height);
    const uint32 stride = static_cast<uint32>(raster->stride);
    const uint32 fmt = static_cast<uint32>(raster->format) & 0xF00u;
    const bool pal4 = (raster->format & rw::Raster::PAL4) != 0;
    const bool pal8 = (raster->format & rw::Raster::PAL8) != 0;
    const rw::uint8* palette = raster->palette;
    out.assign(static_cast<std::size_t>(w) * h * 4u, 0u);

    auto put = [&](uint32 x, uint32 y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        uint8_t* o = &out[(static_cast<std::size_t>(y) * w + x) * 4u];
        o[0] = r; o[1] = g; o[2] = b; o[3] = a;
    };
    auto putIndex = [&](uint32 x, uint32 y, uint32 idx) {
        if (palette)
            put(x, y, palette[idx * 4u], palette[idx * 4u + 1u], palette[idx * 4u + 2u], palette[idx * 4u + 3u]);
    };
    auto u16 = [&](uint32 x, uint32 y) {
        const rw::uint8* p = px + static_cast<std::size_t>(y) * stride + x * 2u;
        return static_cast<uint32>(p[0]) | (static_cast<uint32>(p[1]) << 8);
    };

    bool ok = true;
    for (uint32 y = 0; y < h; ++y) {
        const rw::uint8* row = px + static_cast<std::size_t>(y) * stride;
        for (uint32 x = 0; x < w; ++x) {
            if (pal8 || (pal4 && stride >= w)) { // PAL4 is stored one index per byte on PC
                putIndex(x, y, row[x]);
            } else if (pal4) {                   // packed nibbles, high nibble first
                putIndex(x, y, (x & 1u) ? (row[x / 2u] & 0x0Fu) : (row[x / 2u] >> 4));
            } else if (fmt == rw::Raster::C8888) {
                const rw::uint8* p = row + x * 4u;
                put(x, y, p[2], p[1], p[0], p[3]);
            } else if (fmt == rw::Raster::C888) {
                const uint32 bytesPerPixel = (stride >= w * 4u) ? 4u : 3u; // X8R8G8B8 or packed R8G8B8
                const rw::uint8* p = row + x * bytesPerPixel;
                put(x, y, p[2], p[1], p[0], 255u);
            } else if (fmt == rw::Raster::C1555) {
                const uint32 v = u16(x, y);
                put(x, y, expand5((v >> 10) & 31u), expand5((v >> 5) & 31u), expand5(v & 31u), (v & 0x8000u) ? 255u : 0u);
            } else if (fmt == rw::Raster::C565) {
                const uint32 v = u16(x, y);
                put(x, y, expand5((v >> 11) & 31u), expand6((v >> 5) & 63u), expand5(v & 31u), 255u);
            } else if (fmt == rw::Raster::C555) {
                const uint32 v = u16(x, y);
                put(x, y, expand5((v >> 10) & 31u), expand5((v >> 5) & 31u), expand5(v & 31u), 255u);
            } else if (fmt == rw::Raster::C4444) {
                const uint32 v = u16(x, y);
                put(x, y, expand4((v >> 8) & 15u), expand4((v >> 4) & 15u), expand4(v & 15u), expand4(v >> 12));
            } else if (fmt == rw::Raster::LUM8) {
                put(x, y, row[x], row[x], row[x], 255u);
            } else {
                ok = false;
            }
        }
    }
    raster->unlock(0);
    return ok;
}

// Level-0 pixels of `raster` as RGBA8, plus the decoded size (which can differ from the raster
// header for some files; the decoded size is authoritative).
bool decodeRaster(rw::Raster* raster, std::vector<uint8_t>& out, uint32& width, uint32& height) {
    if (rw::Image* img = raster->toImage()) {
        img->convertTo32(); // palettised / 16 / 24-bit -> RGBA8888
        width = static_cast<uint32>(img->width);
        height = static_cast<uint32>(img->height);
        out.resize(static_cast<std::size_t>(width) * height * 4u);
        for (uint32 y = 0; y < height; ++y) {
            const uint8* src = img->pixels + static_cast<std::size_t>(y) * img->stride;
            std::copy(src, src + width * 4u, out.begin() + static_cast<std::ptrdiff_t>(y) * width * 4);
        }
        const bool ok = img->depth == 32;
        img->destroy();
        return ok;
    }
    width = static_cast<uint32>(raster->width);
    height = static_cast<uint32>(raster->height);
    return decodeLockedRaster(raster, out);
}

std::expected<std::vector<Assets::TxdTexture>, EngineError> readDictionary(const std::string& path, bool bind) {
    if (!Assets::ensureLibrwEngine())
        return std::unexpected(EngineError::failure(EngineError::Code::Failed, "librw engine not started"));

    rw::uint32 len = 0;
    rw::uint8* data = rw::getFileContents(Engine::Platform::resolvePath(path).c_str(), &len);
    if (!data)
        return std::unexpected(EngineError::failure(EngineError::Code::NotFound, "failed to open txd " + path));

    rw::StreamMemory in;
    in.open(data, len);
    rw::TexDictionary* txd = nullptr;
    if (rw::findChunk(&in, rw::ID_TEXDICTIONARY, nullptr, nullptr))
        txd = rw::TexDictionary::streamRead(&in);
    in.close();
    rwFree(data);
    if (!txd)
        return std::unexpected(EngineError::failure(EngineError::Code::Failed, "failed to parse txd " + path));

    std::vector<Assets::TxdTexture> out;
    FORLIST(lnk, txd->textures) {
        rw::Texture* tex = LLLinkGetData(lnk, rw::Texture, inDict);
        if (!tex || !tex->raster)
            continue;

        Assets::TxdTexture entry;
        entry.name = tex->name;
        entry.mipLevels = static_cast<uint32>(std::max(tex->raster->getNumLevels(), 1));
        entry.format = static_cast<uint32>(tex->raster->format);
        uint32 w = 0, h = 0;
        if (!decodeRaster(tex->raster, entry.rgba, w, h)) {
            // Keep the entry (its name may be referenced) but without pixels.
            entry.rgba.clear();
            w = static_cast<uint32>(tex->raster->width);
            h = static_cast<uint32>(tex->raster->height);
        }
        entry.width = w;
        entry.height = h;
        out.push_back(std::move(entry));
    }

    if (bind) {
        rw::TexDictionary::setCurrent(txd);
        g_boundDictionaries.push_back(txd);
    } else {
        txd->destroy();
    }
    return out;
}

} // namespace

namespace Assets {

bool TxdTexture::hasCutoutAlpha() const noexcept {
    for (std::size_t i = 3; i < rgba.size(); i += 4)
        if (rgba[i] < 128u)
            return true;
    return false;
}

AlphaClass TxdTexture::alphaClass() const noexcept {
    std::size_t mid = 0, n = 0;
    bool holes = false;
    for (std::size_t i = 3; i < rgba.size(); i += 4, ++n) {
        const uint8_t a = rgba[i];
        if (a >= 16u && a < 240u)
            ++mid;
        if (a < 128u)
            holes = true;
    }
    if (n > 0 && static_cast<float>(mid) > kBlendShare * static_cast<float>(n))
        return AlphaClass::Blend;
    return holes ? AlphaClass::Cutout : AlphaClass::Opaque;
}

std::expected<std::vector<TxdTexture>, Engine::EngineError> Txd::load(const std::string& path) {
    return readDictionary(path, false);
}

std::expected<std::vector<TxdTexture>, Engine::EngineError> Txd::loadAndBind(const std::string& path) {
    return readDictionary(path, true);
}

}  // namespace Assets
