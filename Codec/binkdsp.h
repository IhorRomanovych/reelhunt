// SPDX-License-Identifier: LGPL-2.1-or-later
// Codec/binkdsp.h - Bink DSP routines (IDCT add/put, scale_block, add_pixels8), ported from FFmpeg binkdsp.c.
#pragma once

#include <cstdint>

namespace Codec
{

    // Mirrors FFmpeg's BinkDSPContext so the decoder can call c->binkdsp.xxx unchanged.
    struct BinkDSPContext
    {
        void (*idct_put)(std::uint8_t* dest, int line_size, std::int32_t* block);
        void (*idct_add)(std::uint8_t* dest, int line_size, std::int32_t* block);
        void (*scale_block)(const std::uint8_t src[64], std::uint8_t* dst, int linesize);
        void (*add_pixels8)(std::uint8_t* pixels, std::int16_t* block, int line_size);
    };

    // Sets all four function pointers to the scalar C implementations.
    void bink_dsp_init(BinkDSPContext* c);

} // namespace Codec
