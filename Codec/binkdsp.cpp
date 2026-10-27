// SPDX-License-Identifier: LGPL-2.1-or-later
// Codec/binkdsp.cpp - Scalar Bink DSP routines, ported verbatim from FFmpeg libavcodec/binkdsp.c.
#include "binkdsp.h"

namespace Codec
{
namespace
{

constexpr int A1 = 2896; // (1/sqrt(2))<<12
constexpr int A2 = 2217;
constexpr int A3 = 3784;
constexpr int A4 = -5352;

inline int MUL(int X, int Y)
{
    return static_cast<int>(static_cast<unsigned>(X) * static_cast<unsigned>(Y)) >> 11;
}

#define IDCT_TRANSFORM(dest, s0, s1, s2, s3, s4, s5, s6, s7, d0, d1, d2, d3, d4, d5, d6, d7, munge, src) \
    {                                                                                                    \
        const int a0 = (src)[s0] + (src)[s4];                                                            \
        const int a1 = (src)[s0] - (src)[s4];                                                            \
        const int a2 = (src)[s2] + (src)[s6];                                                            \
        const int a3 = MUL(A1, (src)[s2] - (src)[s6]);                                                   \
        const int a4 = (src)[s5] + (src)[s3];                                                            \
        const int a5 = (src)[s5] - (src)[s3];                                                            \
        const int a6 = (src)[s1] + (src)[s7];                                                            \
        const int a7 = (src)[s1] - (src)[s7];                                                            \
        const int b0 = a4 + a6;                                                                          \
        const int b1 = MUL(A3, a5 + a7);                                                                 \
        const int b2 = MUL(A4, a5) - b0 + b1;                                                            \
        const int b3 = MUL(A1, a6 - a4) - b2;                                                            \
        const int b4 = MUL(A2, a7) + b3 - b1;                                                            \
        (dest)[d0] = munge(a0 + a2 + b0);                                                                \
        (dest)[d1] = munge(a1 + a3 - a2 + b2);                                                           \
        (dest)[d2] = munge(a1 - a3 + a2 + b3);                                                           \
        (dest)[d3] = munge(a0 - a2 - b4);                                                                \
        (dest)[d4] = munge(a0 - a2 + b4);                                                                \
        (dest)[d5] = munge(a1 - a3 + a2 - b3);                                                           \
        (dest)[d6] = munge(a1 + a3 - a2 - b2);                                                           \
        (dest)[d7] = munge(a0 + a2 - b0);                                                                \
    }

#define MUNGE_NONE(x) (x)
#define IDCT_COL(dest, src) IDCT_TRANSFORM(dest, 0, 8, 16, 24, 32, 40, 48, 56, 0, 8, 16, 24, 32, 40, 48, 56, MUNGE_NONE, src)

#define MUNGE_ROW(x) (((x) + 0x7F) >> 8)
#define IDCT_ROW(dest, src) IDCT_TRANSFORM(dest, 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7, MUNGE_ROW, src)

void bink_idct_col(int* dest, const std::int32_t* src)
{
    if ((src[8] | src[16] | src[24] | src[32] | src[40] | src[48] | src[56]) == 0)
    {
        dest[0] =
            dest[8] =
                dest[16] =
                    dest[24] =
                        dest[32] =
                            dest[40] =
                                dest[48] =
                                    dest[56] = src[0];
    }
    else
    {
        IDCT_COL(dest, src);
    }
}

void bink_idct_c(std::int32_t* block)
{
    int temp[64];
    for (int i = 0; i < 8; i++)
        bink_idct_col(&temp[i], &block[i]);
    for (int i = 0; i < 8; i++)
        IDCT_ROW((&block[8 * i]), (&temp[8 * i]));
}

void bink_idct_add_c(std::uint8_t* dest, int linesize, std::int32_t* block)
{
    bink_idct_c(block);
    for (int i = 0; i < 8; i++, dest += linesize, block += 8)
        for (int j = 0; j < 8; j++)
            dest[j] += block[j];
}

void bink_idct_put_c(std::uint8_t* dest, int linesize, std::int32_t* block)
{
    int temp[64];
    for (int i = 0; i < 8; i++)
        bink_idct_col(&temp[i], &block[i]);
    for (int i = 0; i < 8; i++)
        IDCT_ROW((&dest[i * linesize]), (&temp[8 * i]));
}

void scale_block_c(const std::uint8_t src[64], std::uint8_t* dst, int linesize)
{
    auto* dst1 = reinterpret_cast<std::uint16_t*>(dst);
    auto* dst2 = reinterpret_cast<std::uint16_t*>(dst + linesize);

    for (int j = 0; j < 8; j++)
    {
        for (int i = 0; i < 8; i++)
            dst1[i] = dst2[i] = static_cast<std::uint16_t>(src[i]) * 0x0101;
        src += 8;
        dst1 += linesize;
        dst2 += linesize;
    }
}

void add_pixels8_c(std::uint8_t* pixels, std::int16_t* block, int line_size)
{
    for (int i = 0; i < 8; i++)
    {
        pixels[0] += block[0];
        pixels[1] += block[1];
        pixels[2] += block[2];
        pixels[3] += block[3];
        pixels[4] += block[4];
        pixels[5] += block[5];
        pixels[6] += block[6];
        pixels[7] += block[7];
        pixels += line_size;
        block += 8;
    }
}

} // namespace

void bink_dsp_init(BinkDSPContext* c)
{
    c->idct_put = &bink_idct_put_c;
    c->idct_add = &bink_idct_add_c;
    c->scale_block = &scale_block_c;
    c->add_pixels8 = &add_pixels8_c;
}

} // namespace Codec
