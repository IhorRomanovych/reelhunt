// tests/bink_audio_test.cpp - G00.6: decode asylum.bik's Bink audio track with the engine's
// Codec/bink_audio stack and dump it to a normalized 16-bit WAV for manual listening.
// Prints per-frame stats plus overall duration / peak / RMS so decode health is visible without audio.
#include "Codec/bink_container.h"
#include "Codec/bink_audio.h"


#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
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

    void put32(std::ostream& o, std::uint32_t v)
    {
        o.write(reinterpret_cast<const char*>(&v), 4);
    }
    void put16(std::ostream& o, std::uint16_t v)
    {
        o.write(reinterpret_cast<const char*>(&v), 2);
    }

    // Writes `pcm` (interleaved float) as a 16-bit PCM WAV, linearly normalized to a 0.6 peak.
    void writeWav(const char* path, const std::vector<float>& pcm, int channels, int sampleRate)
    {
        float peak = 0.0f;
        for (float v : pcm)
            peak = std::max(peak, std::fabs(v));
        const float gain = peak > 1e-9f ? (0.6f / peak) : 1.0f;
        printf("raw   : peak=%.5f  norm-gain=%.6f\n", peak, gain);

        std::vector<std::int16_t> s16(pcm.size());
        long long sumSq = 0;
        for (std::size_t i = 0; i < pcm.size(); ++i)
        {
            float v = pcm[i] * gain;
            if (v > 1.0f) v = 1.0f;
            else if (v < -1.0f) v = -1.0f;
            s16[i] = static_cast<std::int16_t>(v * 32767.0f);
            sumSq += static_cast<long long>(s16[i]) * s16[i];
        }

        const std::uint32_t numSamples = static_cast<std::uint32_t>(pcm.size() / channels);
        const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate) * static_cast<std::uint32_t>(channels) * 2u;
        const std::uint32_t blockAlign = static_cast<std::uint32_t>(channels) * 2u;
        const std::uint32_t dataSize = static_cast<std::uint32_t>(s16.size()) * 2u;

        std::ofstream o(path, std::ios::binary | std::ios::trunc);
        if (!o)
            return;
        o << "RIFF";
        put32(o, 36u + dataSize);
        o << "WAVEfmt ";
        put32(o, 16u);                       // PCM fmt chunk size
        put16(o, 1);                          // audio format = PCM
        put16(o, static_cast<std::uint16_t>(channels));
        put32(o, static_cast<std::uint32_t>(sampleRate));
        put32(o, byteRate);
        put16(o, static_cast<std::uint16_t>(blockAlign));
        put16(o, 16);                          // bits per sample
        o << "data";
        put32(o, dataSize);
        o.write(reinterpret_cast<const char*>(s16.data()), static_cast<std::streamsize>(s16.size() * 2));

        const double rms = std::sqrt(static_cast<double>(sumSq) / static_cast<double>(s16.size()));
        const double dur = static_cast<double>(numSamples) / static_cast<double>(sampleRate);
        printf("WAV   : %s\n", path);
        printf("        samples=%u (%.2f s)  channels=%d  rate=%d  peak(0-1)=%.4f  rms(16-bit)=%.1f\n",
               numSamples, dur, channels, sampleRate, peak * gain, rms);
    }
}

int main()
{
    if (!TestPaths::haveGameData())
        return TestPaths::skipNoGameData();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string bikPathS = TestPaths::level("asylum") + "asylum.bik";
    const std::string wavPathS = TestPaths::scratch() + "asylum_audio.wav";
    const char* bikPath = bikPathS.c_str();
    const char* wavPath = wavPathS.c_str();

    const std::vector<std::uint8_t> file = readAll(bikPath);
    if (file.empty()) { printf("FAIL: cannot read %s\n", bikPath); return 1; }
    printf("file  : %s (%zu bytes)\n", bikPath, file.size());

    Codec::BinkContainer cont;
    if (!cont.open(file.data(), file.size())) { printf("FAIL: container open\n"); return 1; }
    printf("video : %dx%d ver='%c' frames=%d fps=%.3f\n",
           cont.width(), cont.height(), cont.version(), cont.numFrames(), cont.fps());
    if (!cont.hasAudio()) { printf("FAIL: no audio track in file\n"); return 1; }
    printf("audio : rate=%d Hz flags=0x%04X\n", cont.audioSampleRate(), cont.audioFlags());

    Codec::BinkAudioDecoder dec;
    if (!dec.configure(cont.audioSampleRate(), cont.audioFlags()))
    {
        printf("FAIL: audio configure - unsupported variant (need BINK_AUD_USEDCT)\n");
        return 1;
    }
    printf("dec   : rate=%d Hz channels=%d\n", dec.sampleRate(), dec.channels());

    // Decode every frame's audio packet (each packet may hold several transform blocks) and append
    // the interleaved float PCM.
    std::vector<float> pcm;
    int decoded = 0, empty = 0, failed = 0;
    for (int i = 0; i < cont.numFrames(); ++i)
    {
        const std::uint8_t* a; int asz = 0;
        if (!cont.audioFrame(i, &a, &asz) || asz < 4) { ++empty; continue; }
        if (dec.decode(a, asz, pcm)) ++decoded; else ++failed;
    }
    printf("decode: total=%d  with-audio=%d  empty=%d  failed=%d\n", cont.numFrames(), decoded, empty, failed);
    if (pcm.empty()) { printf("FAIL: no PCM produced\n"); return 1; }

    // Regression check: scale-invariant Pearson correlation against the FFmpeg reference decode. A
    // correct decoder matches the reference within floating-point noise (corr ~ 1.0, offset 0).
    {
        const std::string refPathS = TestPaths::fixtures() + "bink_audio/reference.wav";
        const char* refPath = refPathS.c_str();
        const std::vector<std::uint8_t> rb = readAll(refPath);
        long dataOff = -1, dataSize = 0;
        if (rb.size() >= 12 && std::memcmp(rb.data(), "RIFF", 4) == 0)
        {
            long pos = 12;
            while (pos + 8 <= static_cast<long>(rb.size()))
            {
                char id[5] = { static_cast<char>(rb[pos]), static_cast<char>(rb[pos + 1]),
                               static_cast<char>(rb[pos + 2]), static_cast<char>(rb[pos + 3]), 0 };
                long sz = static_cast<long>(rb[pos + 4]) | (static_cast<long>(rb[pos + 5]) << 8) |
                          (static_cast<long>(rb[pos + 6]) << 16) | (static_cast<long>(rb[pos + 7]) << 24);
                if (std::strcmp(id, "data") == 0) { dataOff = pos + 8; dataSize = sz; break; }
                pos += 8 + sz + (sz & 1);
            }
        }
        const long nRef = dataSize / 2;
        if (nRef > 0)
        {
            const long mlen = static_cast<long>(pcm.size());
            const long span = 150000;
            long cnt = 0; double sA = 0, sB = 0, sAA = 0, sBB = 0, sAB = 0;
            for (long i = 0; i < span; ++i)
            {
                if (i >= mlen || i >= nRef) break;
                const double x = pcm[i];
                const double y = static_cast<double>(static_cast<std::int16_t>((rb[dataOff + i * 2] | (rb[dataOff + i * 2 + 1] << 8)) & 0xFFFFu));
                sA += x; sB += y; sAA += x * x; sBB += y * y; sAB += x * y; ++cnt;
            }
            if (cnt > 1000)
            {
                const double cov = sAB - sA * sB / cnt, va = sAA - sA * sA / cnt, vb = sBB - sB * sB / cnt;
                const double corr = (va > 0 && vb > 0) ? cov / std::sqrt(va * vb) : -2.0;
                printf("ref   : %ld samples (%.2f s)  correlation=%.4f\n", nRef, static_cast<double>(nRef) / 2 / dec.sampleRate(), corr);
            }
        }
        else
            printf("ref   : (no reference.wav found; skipping correlation check)\n");
    }

    writeWav(wavPath, pcm, dec.channels(), dec.sampleRate());
    printf("DONE  : decoded asylum.bik Bink audio -> WAV (listen to verify it is dialogue, not noise/silence)\n");
    return failed == 0 ? 0 : 2;
}
