// tests/bink_test.cpp - Decode asylum.bik with the engine's Bink stack (Codec/*) and compare
// byte-for-byte against an ffmpeg-generated raw YUV420P reference. Verifies bit-exactness of the port.
#include "Codec/bink_container.h"
#include "Codec/bink_decoder.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include "test_paths.h"

namespace
{
    std::vector<std::uint8_t> readAll(const char* path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return {};
        f.seekg(0, std::ios::end);
        const std::streamoff n = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> b(n > 0 ? static_cast<std::size_t>(n) : 0);
        if (n > 0)
            f.read(reinterpret_cast<char*>(b.data()), n);
        return b;
    }
}

int main()
{
    if (!TestPaths::haveGameData())
        return TestPaths::skipNoGameData();
    const std::string bikPathS = TestPaths::level("asylum") + "asylum.bik";
    const std::string refPathS = TestPaths::fixtures() + "bink_verify/ref.yuv";
    const std::string outPathS = TestPaths::scratch() + "my_yuv.yuv";
    const char* bikPath = bikPathS.c_str();
    const char* refPath = refPathS.c_str();
    const char* outPath = outPathS.c_str();
    const int N = 8;

    std::vector<std::uint8_t> file = readAll(bikPath);
    if (file.empty()) { printf("FAIL: cannot read %s\n", bikPath); return 1; }
    printf("loaded %zu bytes\n", file.size());

    Codec::BinkContainer cont;
    if (!cont.open(file.data(), file.size())) { printf("FAIL: container open\n"); return 1; }
    printf("container ok: %dx%d ver='%c' flags=0x%08X frames=%d fps=%.3f\n",
           cont.width(), cont.height(), cont.version(), cont.flags(), cont.numFrames(), cont.fps());

    Codec::BinkDecoder dec;
    dec.configure(cont.width(), cont.height(), cont.version(), cont.flags());

    const int w = cont.width(), h = cont.height();
    const int cw = (w + 1) >> 1, ch = (h + 1) >> 1;
    const std::size_t perFrame = static_cast<std::size_t>(w) * h + static_cast<std::size_t>(cw) * ch + static_cast<std::size_t>(cw) * ch;

    std::vector<std::uint8_t> out(static_cast<std::size_t>(N) * perFrame);
    for (int i = 0; i < N; ++i)
    {
        const std::uint8_t* d; int sz;
        if (!cont.frame(i, &d, &sz)) { printf("FAIL: frame %d lookup\n", i); return 1; }
        Codec::BinkFrame f;
        const bool ok = dec.decode(d, sz, f);
        printf("frame %2d: size=%5d decode=%s\n", i, sz, ok ? "ok" : "FAIL");
        if (!ok) { return 1; }
        std::memcpy(out.data() + static_cast<std::size_t>(i) * perFrame, f.storage.data(), perFrame);
    }

    {
        std::ofstream o(outPath, std::ios::binary | std::ios::trunc);
        if (!o) { printf("FAIL: cannot write %s\n", outPath); return 1; }
        o.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }

    const std::vector<std::uint8_t> ref = readAll(refPath);
    if (ref.empty())
    {
        // The frames decoded; the bit-exact comparison needs the FFmpeg reference (a local fixture).
        printf("SKIP: no reference at %s (set REELHUNT_TEST_FIXTURES)\n", refPath);
        return 77;
    }
    printf("my=%zu bytes ref=%zu bytes\n", out.size(), ref.size());

    const std::size_t total = out.size() < ref.size() ? out.size() : ref.size();
    long mismatches = 0;
    std::size_t firstOff = 0;
    bool haveFirst = false;
    for (std::size_t i = 0; i < total; ++i)
    {
        if (out[i] != ref[i])
        {
            if (!haveFirst) { firstOff = i; haveFirst = true; }
            ++mismatches;
        }
    }

    const bool yuvOk = (mismatches == 0 && out.size() == ref.size());
    if (yuvOk)
        printf("YUV RESULT: BIT-EXACT MATCH (%zu bytes, %d frames)\n", total, N);
    else
    {
        printf("YUV RESULT: %ld mismatched byte(s) of %zu\n", mismatches, total);
        if (haveFirst)
        {
            const std::size_t fr = firstOff / perFrame;
            const std::size_t offInFrame = firstOff % perFrame;
            printf("  first mismatch at global=%zu (frame %zu, in-frame offset %zu): mine=0x%02X ref=0x%02X\n",
                   firstOff, fr, offInFrame, out[firstOff], ref[firstOff]);
        }
    }

    // --- Color path: convert frame 0 YUV->RGBA and compare (with tolerance) to ffmpeg's rgba. ---
    const std::string refRgbaPathS = TestPaths::fixtures() + "bink_verify/ref_rgba.raw";
    const char* refRgbaPath = refRgbaPathS.c_str();
    const std::vector<std::uint8_t> refRgba = readAll(refRgbaPath);
    bool rgbaOk = false;
    if (refRgba.empty())
        printf("RGBA RESULT: SKIP (no %s)\n", refRgbaPath);
    else
    {
        Codec::BinkFrame f0;
        const std::uint8_t* d0; int sz0;
        cont.frame(0, &d0, &sz0);
        Codec::BinkDecoder dec2; // fresh state so frame 0 decodes intra (the main `dec` is past frame 7)
        dec2.configure(cont.width(), cont.height(), cont.version(), cont.flags());
        dec2.decode(d0, sz0, f0);
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4u);
        Codec::yuv420p_to_rgba(f0, rgba.data());

        // ffmpeg's yuv->rgb uses fixed-point coefficients; our float BT.601 differs only by
        // sub-perceptual rounding (a wrong matrix or U/V swap would show diffs in the tens/hundreds).
        const std::size_t n = rgba.size() < refRgba.size() ? rgba.size() : refRgba.size();
        long bad = 0; int maxDiff = 0; long sumDiff = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const int diff = std::abs(static_cast<int>(rgba[i]) - static_cast<int>(refRgba[i]));
            if (diff > maxDiff) maxDiff = diff;
            sumDiff += diff;
            if (diff > 4) ++bad; // allow rounding slack vs ffmpeg's fixed-point path
        }
        const double meanDiff = static_cast<double>(sumDiff) / static_cast<double>(n);
        printf("RGBA RESULT: %ld/%zu channels off by >4, maxDiff=%d meanDiff=%.3f\n", bad, n, maxDiff, meanDiff);
        rgbaOk = (bad == 0 && maxDiff <= 8 && meanDiff < 2.0);
    }

    printf("OVERALL: %s\n", (yuvOk && rgbaOk) ? "PASS" : "FAIL");
    return (yuvOk && rgbaOk) ? 0 : 1;
}
