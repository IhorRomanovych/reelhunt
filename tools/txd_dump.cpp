// tools/txd_dump.cpp - dump every texture of a RenderWare .txd to PNG files (dev tool).
//
//   txd_dump <file.txd> <output-dir>
//
// Prints one line per texture (name, size, cut-out alpha) and writes <output-dir>/<name>.png.
#include "Assets/txd.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    uint32_t crc32(const uint8_t* p, std::size_t n, uint32_t crc = 0)
    {
        static const auto table = [] {
            std::array<uint32_t, 256> t{};
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[i] = c;
            }
            return t;
        }();
        crc = ~crc;
        for (std::size_t i = 0; i < n; ++i)
            crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
        return ~crc;
    }

    void put32(std::vector<uint8_t>& v, uint32_t x)
    {
        for (int s = 24; s >= 0; s -= 8)
            v.push_back(static_cast<uint8_t>(x >> s));
    }

    void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data)
    {
        put32(png, static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        png.insert(png.end(), body.begin(), body.end());
        put32(png, crc32(body.data(), body.size()));
    }

    // Minimal RGBA8 PNG writer: zlib stream of uncompressed (stored) deflate blocks.
    bool writePng(const std::filesystem::path& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba)
    {
        std::vector<uint8_t> raw;
        raw.reserve((static_cast<std::size_t>(w) * 4 + 1) * h);
        for (uint32_t y = 0; y < h; ++y)
        {
            raw.push_back(0); // filter: none
            raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(y) * w * 4,
                       rgba.begin() + static_cast<std::ptrdiff_t>(y + 1) * w * 4);
        }
        std::vector<uint8_t> z{0x78, 0x01};
        uint32_t a = 1, b = 0;
        for (uint8_t c : raw)
        {
            a = (a + c) % 65521u;
            b = (b + a) % 65521u;
        }
        for (std::size_t off = 0; off < raw.size() || off == 0;)
        {
            const std::size_t len = std::min<std::size_t>(65535, raw.size() - off);
            const bool last = off + len >= raw.size();
            z.push_back(last ? 1 : 0);
            z.push_back(static_cast<uint8_t>(len));
            z.push_back(static_cast<uint8_t>(len >> 8));
            z.push_back(static_cast<uint8_t>(~len));
            z.push_back(static_cast<uint8_t>(~len >> 8));
            z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(off), raw.begin() + static_cast<std::ptrdiff_t>(off + len));
            off += len;
            if (last)
                break;
        }
        put32(z, (b << 16) | a);

        std::vector<uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        std::vector<uint8_t> ihdr;
        put32(ihdr, w);
        put32(ihdr, h);
        ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
        chunk(png, "IHDR", ihdr);
        chunk(png, "IDAT", z);
        chunk(png, "IEND", {});
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        return f.good();
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: txd_dump <file.txd> <output-dir>\n");
        return 2;
    }
    auto txd = Assets::Txd::load(argv[1]);
    if (!txd)
    {
        std::fprintf(stderr, "txd_dump: %s\n", txd.error().message.c_str());
        return 1;
    }
    const std::filesystem::path out(argv[2]);
    std::filesystem::create_directories(out);
    for (const auto& t : *txd)
    {
        std::printf("%-32s %4ux%-4u mips=%u cutout=%d%s\n", t.name.c_str(), t.width, t.height, t.mipLevels,
                    t.hasCutoutAlpha() ? 1 : 0, t.hasPixels() ? "" : " (no pixels)");
        if (t.hasPixels())
            writePng(out / (t.name + ".png"), t.width, t.height, t.rgba);
    }
    return 0;
}
