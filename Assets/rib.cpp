// Assets/rib.cpp - RIB ADPCM audio. See rib.h.
#include "Assets/rib.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "Assets/binary_reader.h"

namespace Assets {

namespace {

constexpr std::array<int32, 89> kStep = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,    31,
    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,
    157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr std::array<int32, 16> kIndex = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::size_t kInterleave = 0x10000;

void decodeChunk(const uint8_t* in, std::size_t size, std::vector<int16_t>& out)
{
    int16_t first = 0;
    std::memcpy(&first, in, 2);
    int32 predictor = first;
    int32 index = std::clamp<int32>(in[2], 0, 88);
    out.push_back(first);
    auto expand = [&](int32 nibble) {
        const int32 step = kStep[static_cast<std::size_t>(index)];
        int32 diff = step >> 3;
        if (nibble & 4)
            diff += step;
        if (nibble & 2)
            diff += step >> 1;
        if (nibble & 1)
            diff += step >> 2;
        predictor = std::clamp(nibble & 8 ? predictor - diff : predictor + diff, -32768, 32767);
        index = std::clamp(index + kIndex[static_cast<std::size_t>(nibble)], 0, 88);
        out.push_back(static_cast<int16_t>(predictor));
    };
    for (std::size_t i = 4; i < size; ++i)
    {
        expand(in[i] & 0x0f);
        expand(in[i] >> 4);
    }
}

} // namespace

RibAudio decodeRib(const std::vector<uint8_t>& bytes, uint32 channels, uint32 chunk, uint32 sampleRate)
{
    RibAudio a;
    a.channels = std::max(1u, channels);
    a.sampleRate = sampleRate;
    std::vector<std::vector<int16_t>> per(a.channels);
    for (std::size_t o = 0; o + a.channels * kInterleave <= bytes.size();)
        for (uint32 c = 0; c < a.channels; ++c, o += kInterleave)
            for (std::size_t k = 0; k + chunk <= kInterleave; k += chunk)
                decodeChunk(bytes.data() + o + k, chunk, per[c]);
    const std::size_t frames = per[0].size();
    a.samples.reserve(frames * a.channels);
    for (std::size_t i = 0; i < frames; ++i)
        for (uint32 c = 0; c < a.channels; ++c)
            a.samples.push_back(i < per[c].size() ? per[c][i] : int16_t{0});
    return a;
}

std::expected<RibAudio, Engine::EngineError> loadExecutionSound(const std::string& path)
{
    auto bytes = readFileBytes(path);
    if (!bytes)
        return std::unexpected(bytes.error());
    RibAudio out = decodeRib(*bytes);
    // Drop the silent padding at the end (below a whisper).
    std::size_t frames = out.samples.size();
    while (frames > 0 && std::abs(out.samples[frames - 1]) < 64)
        --frames;
    out.samples.resize(frames);
    return out;
}

} // namespace Assets
