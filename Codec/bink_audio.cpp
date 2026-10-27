// SPDX-License-Identifier: LGPL-2.1-or-later
// Codec/bink_audio.cpp - Bink Audio DCT decoder, ported from FFmpeg libavcodec/binkaudio.c.
//
// Per-packet layout (one packet per video frame): [reported_size u32 (skipped)][codec data]. A packet
// holds one or more transform blocks (the first frame's packet is much larger than the rest). Each block
// is decoded per channel: two special leading coefficients (scaled by `root`), a per-band quantization
// index list, then a bit-packed RLE run of de-quantized DCT coefficients. An inverse DCT-III turns each
// channel's 2048 coefficients into 2048 samples; the last 128 (overlap) are crossfaded with the tail of
// the previous block, leaving (frameLen-overlap) new interleaved samples per block.
#include "bink_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Codec
{

    namespace
    {
        // FFmpeg ff_wma_critical_freqs (libavcodec/wma_freqs.c) - band edge frequencies in Hz.
        constexpr std::uint16_t kWmaFreqs[25] = {
            100, 200, 300, 400, 510, 630, 770, 920,
            1080, 1270, 1480, 1720, 2000, 2320, 2700, 3150,
            3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500,
            24500,
        };

        // Bink audio RLE run lengths (binkaudio.c rle_length_tab): value v -> v*8 samples of a run.
        constexpr std::uint8_t kRleLen[16] = {
            2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64,
        };

        constexpr double kPi = 3.14159265358979323846;

        // binkaudio.c get_float: 5-bit exponent + 23-bit mantissa + 1-bit sign -> (mantissa/2^23)*2^exp.
        // The audio bitstream is LITTLE-ENDIAN (binkaudio.c defines BITSTREAM_READER_LE), so it uses the
        // same LE reader as the Bink video track, not the MSB GetBitContext.
        float getFloat(BinkBits* gb)
        {
            const int power = static_cast<int>(bs_read(gb, 5));
            float f = std::ldexp(static_cast<float>(bs_read(gb, 23)), power - 23);
            if (bs_read_bit(gb))
                f = -f;
            return f;
        }
    }

    bool BinkAudioDecoder::configure(int sampleRate, std::uint16_t flags)
    {
        if (sampleRate <= 0)
            return false;
        if (!(flags & BINK_AUD_USEDCT))
            return false; // RDFT variant is not implemented

        sampleRate_ = sampleRate;
        channels_ = (flags & BINK_AUD_STEREO) ? 2 : 1;

        int frameLenBits;
        if (sampleRate < 22050)
            frameLenBits = 9;
        else if (sampleRate < 44100)
            frameLenBits = 10;
        else
            frameLenBits = 11;
        frameLen_ = 1 << frameLenBits;
        overlapLen_ = frameLen_ / 16;
        blockSize_ = (frameLen_ - overlapLen_) * std::min(2, channels_);

        // root: scaling of the first two coefficients (DCT, non-RDFT path).
        root_ = static_cast<float>(frameLen_) /
                (static_cast<float>(std::sqrt(static_cast<double>(frameLen_))) * 32768.0f);

        for (int i = 0; i < 96; ++i)
            quantTable_[i] = std::exp(static_cast<float>(i) * 0.15289164787221953823f) * root_;

        // Number of quantization bands + their coefficient-index edges (mirrors binkaudio.c decode_init).
        const int halfRate = (sampleRate + 1) / 2;
        numBands_ = 1;
        while (numBands_ < 25 && halfRate > static_cast<int>(kWmaFreqs[numBands_ - 1]))
            ++numBands_;
        bands_[0] = 2;
        for (int i = 1; i < numBands_; ++i)
            bands_[i] = (static_cast<std::uint32_t>(kWmaFreqs[i - 1]) * static_cast<std::uint32_t>(frameLen_) /
                         static_cast<std::uint32_t>(halfRate)) & ~1u;
        bands_[numBands_] = static_cast<std::uint32_t>(frameLen_);

        // Precompute the inverse DCT-III cos kernel (matches FFmpeg ff_tx_dctIII):
        // dct_[m*N + k] = cos(pi/N * k * (m + 0.5)). Coefficient k maps to harmonic k (not 2k+1).
        const int n = frameLen_;
        dct_.resize(static_cast<std::size_t>(n) * n);
        for (int m = 0; m < n; ++m)
        {
            const double a = (m + 0.5) * kPi / n;
            float* row = &dct_[static_cast<std::size_t>(m) * n];
            for (int k = 0; k < n; ++k)
                row[k] = static_cast<float>(std::cos(a * k));
        }

        prev_.assign(static_cast<std::size_t>(channels_) * overlapLen_, 0.0f);
        coeffs_.assign(static_cast<std::size_t>(channels_) * frameLen_, 0.0f);
        time_.assign(static_cast<std::size_t>(channels_) * frameLen_, 0.0f);
        first_ = true;
        return true;
    }

    void BinkAudioDecoder::reset()
    {
        std::fill(prev_.begin(), prev_.end(), 0.0f);
        first_ = true;
    }

    bool BinkAudioDecoder::decode(const std::uint8_t* packet, int size, std::vector<float>& out)
    {
        if (!packet || size < 4)
            return false;
        // The LE bit reader refills up to 8 bytes past the payload; pad with zeros so any over-read is safe.
        std::vector<std::uint8_t> pad(static_cast<std::size_t>(size) + 8, 0);
        std::memcpy(pad.data(), packet, static_cast<std::size_t>(size));
        if (bs_init(&bits_, pad.data(), static_cast<unsigned>(size) * 8u) < 0)
            return false;
        bs_skip(&bits_, 32); // discard the leading "reported size" word

        // A packet may hold several transform blocks (the first frame's packet is much larger than the
        // rest). FFmpeg's binkaudio_receive_frame keeps calling decode_block on the same packet's
        // remaining bits (32-bit aligned between blocks) until the packet is exhausted. Mirror that.
        bool produced = false;
        for (;;)
        {
            if (bs_left(&bits_) <= 0)
                break;
            if (!decodeBlock(out))
                break;
            produced = true;
            const int n = (-bs_tell(&bits_)) & 31; // get_bits_align32
            if (n)
                bs_skip(&bits_, static_cast<unsigned>(n));
        }
        return produced;
    }

    bool BinkAudioDecoder::decodeBlock(std::vector<float>& out)
    {
        BinkBits* gb = &bits_;
        const int n = frameLen_;
        const int quantIdxSize = 8; // version_2 == false for BIKi

        bs_skip(gb, 2); // Bink Audio DCT variant: two unused bits before the per-channel data

        for (int ch = 0; ch < channels_; ++ch)
        {
            float* coeffs = &coeffs_[static_cast<std::size_t>(ch) * n];

            if (bs_left(gb) < 58)
                return false;
            coeffs[0] = getFloat(gb) * root_;
            coeffs[1] = getFloat(gb) * root_;

            if (bs_left(gb) < numBands_ * quantIdxSize)
                return false;
            float quant[25];
            for (int i = 0; i < numBands_; ++i)
            {
                const int value = static_cast<int>(bs_read(gb, static_cast<unsigned>(quantIdxSize)));
                quant[i] = quantTable_[std::min(value, 95)];
            }

            // RLE-decode the remaining (frameLen-2) coefficients, de-quantizing per band.
            int k = 0;
            float q = quant[0];
            int i = 2;
            while (i < n)
            {
                int j;
                if (bs_read_bit(gb))
                    j = i + kRleLen[bs_read(gb, 4)] * 8;
                else
                    j = i + 8;
                j = std::min(j, n);

                const int width = static_cast<int>(bs_read(gb, 4));
                if (width == 0)
                {
                    std::memset(coeffs + i, 0, static_cast<std::size_t>(j - i) * sizeof(float));
                    i = j;
                    while (bands_[k] < static_cast<std::uint32_t>(i))
                        q = quant[k++];
                }
                else
                {
                    while (i < j)
                    {
                        if (bands_[k] == static_cast<std::uint32_t>(i))
                            q = quant[k++];
                        const int coeff = static_cast<int>(bs_read(gb, static_cast<unsigned>(width)));
                        if (coeff)
                            coeffs[i] = bs_read_bit(gb) ? -q * static_cast<float>(coeff)
                                                         :  q * static_cast<float>(coeff);
                        else
                            coeffs[i] = 0.0f;
                        ++i;
                    }
                }
            }

            coeffs[0] /= 0.5f; // double the DC coefficient before the inverse DCT (binkaudio.c)

            // Inverse DCT-III (size n): time_[ch][m] = sum_k coeffs[k] * cos(pi/n * k * (m + 0.5)).
            float* t = &time_[static_cast<std::size_t>(ch) * n];
            for (int m = 0; m < n; ++m)
            {
                const float* row = &dct_[static_cast<std::size_t>(m) * n];
                float acc = 0.0f;
                for (int kk = 0; kk < n; ++kk)
                    acc += coeffs[kk] * row[kk];
                t[m] = acc;
            }
        }

        // Overlap-add: linear crossfade between the previous block's tail and this block's head.
        for (int ch = 0; ch < channels_; ++ch)
        {
            const int count = overlapLen_ * channels_;
            float* cur = &time_[static_cast<std::size_t>(ch) * n];
            float* prv = &prev_[static_cast<std::size_t>(ch) * overlapLen_];
            if (!first_)
            {
                int j = ch;
                for (int i = 0; i < overlapLen_; ++i, j += channels_)
                    cur[i] = (prv[i] * static_cast<float>(count - j) + cur[i] * static_cast<float>(j)) /
                             static_cast<float>(count);
            }
            std::memcpy(prv, cur + (n - overlapLen_), static_cast<std::size_t>(overlapLen_) * sizeof(float));
        }
        first_ = false;

        // Emit (frameLen - overlap) interleaved samples.
        const int perCh = n - overlapLen_;
        const std::size_t base = out.size();
        out.resize(base + static_cast<std::size_t>(blockSize_));
        for (int i = 0; i < perCh; ++i)
            for (int ch = 0; ch < channels_; ++ch)
                out[base + static_cast<std::size_t>(i) * channels_ + ch] =
                    time_[static_cast<std::size_t>(ch) * n + i];
        return true;
    }

} // namespace Codec
