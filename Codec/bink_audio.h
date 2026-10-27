// SPDX-License-Identifier: LGPL-2.1-or-later
// Codec/bink_audio.h - Bink Audio DCT decoder, ported from FFmpeg libavcodec/binkaudio.c.
// Decodes per-frame audio packets from a Bink1 .bik into interleaved 32-bit float PCM.
// Supports the DCT variant (BINK_AUD_USEDCT), mono or stereo; the RDFT variant is not supported.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "bink_bitreader.h"

namespace Codec
{

    // Bink audio track flag bits (FFmpeg libavformat/bink.c).
    constexpr std::uint16_t BINK_AUD_16BITS = 0x4000; // prefer 16-bit output
    constexpr std::uint16_t BINK_AUD_STEREO = 0x2000;
    constexpr std::uint16_t BINK_AUD_USEDCT = 0x1000;

    class BinkAudioDecoder
    {
    public:
        // Configure from the container's audio-track parameters. `flags` is the 2-byte audio_flags
        // word. Returns false when the variant is unsupported (RDFT) or there is no valid sample rate.
        bool configure(int sampleRate, std::uint16_t flags);

        int sampleRate() const { return sampleRate_; }
        int channels()   const { return channels_; }

        // Decode one per-frame audio packet (the raw payload, including its 4-byte "reported size"
        // prefix) and append the resulting interleaved float PCM to `out`. Returns false on a decode
        // error (malformed data / over-read). The decoder is stateful (overlap-add across packets).
        bool decode(const std::uint8_t* packet, int size, std::vector<float>& out);

        // Clear the overlap state (call once at the start of each cutscene).
        void reset();

    private:
        bool decodeBlock(std::vector<float>& out);

        int sampleRate_ = 0;
        int channels_ = 1;
        int frameLen_ = 0;   // transform size (samples per channel)
        int overlapLen_ = 0; // overlap size (samples per channel)
        int blockSize_ = 0;  // interleaved samples emitted per packet = (frameLen-overlap)*channels
        int numBands_ = 0;
        float root_ = 0.0f;  // scaling for the first two (DC/low) coefficients

        std::uint32_t bands_[26] = {};   // coefficient-index where each quantization band begins
        float quantTable_[96] = {};      // quant index -> gain
        std::vector<float> dct_;         // DCT-III cos kernel, frameLen_*frameLen_ (precomputed)

        std::vector<float> prev_;   // overlap carry: channels_ * overlapLen_
        std::vector<float> coeffs_; // per-channel coefficients: channels_ * frameLen_
        std::vector<float> time_;   // per-channel time domain:  channels_ * frameLen_

        BinkBits bits_;
        bool first_ = true;
    };

} // namespace Codec
