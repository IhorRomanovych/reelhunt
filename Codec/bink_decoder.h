// SPDX-License-Identifier: LGPL-2.1-or-later
// Codec/bink_decoder.h - Bink video decoder (version > 'b', e.g. 'i'/'k'), ported from FFmpeg libavcodec/bink.c.
// Decodes one compressed frame into planar YUV420P (or YUVA420P when the stream has an alpha plane).
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "bink_bitreader.h"
#include "binkdsp.h"

namespace Codec
{

    // Bink video block types (version > 'b').
    enum BlockTypes
    {
        SKIP_BLOCK = 0,
        SCALED_BLOCK,
        MOTION_BLOCK,
        RUN_BLOCK,
        RESIDUE_BLOCK,
        INTRA_BLOCK,
        FILL_BLOCK,
        INTER_BLOCK,
        PATTERN_BLOCK,
        RAW_BLOCK,
    };

    // Data types decoded per plane (version > 'b').
    enum Sources
    {
        BINK_SRC_BLOCK_TYPES = 0,
        BINK_SRC_SUB_BLOCK_TYPES,
        BINK_SRC_COLORS,
        BINK_SRC_PATTERN,
        BINK_SRC_X_OFF,
        BINK_SRC_Y_OFF,
        BINK_SRC_INTRA_DC,
        BINK_SRC_INTER_DC,
        BINK_SRC_RUN,

        BINK_NB_SRC, // 9
    };

    constexpr std::uint32_t BINK_FLAG_ALPHA = 0x00100000;
    constexpr std::uint32_t BINK_FLAG_GRAY = 0x00020000;

    // Planar YUV frame. data[0]=Y, [1]=U, [2]=V, [3]=A (only when hasAlpha).
    struct BinkFrame
    {
        std::vector<std::uint8_t> storage; // all planes concatenated, with padding between them
        std::uint8_t* data[4] = {};
        int linesize[4] = {};
        int width = 0;
        int height = 0;
        bool hasAlpha = false;

        // Allocates the three (or four) planes. Chroma is half-resolution, as in YUV420P.
        void alloc(int w, int h, bool alpha);
    };

    // Internal decoder state (mirrors FFmpeg's BinkContext for the version > 'b' path).
    struct BinkCtx
    {
        int width = 0;
        int height = 0;
        char version = 0;      // internal Bink file version ('i', 'k', ...)
        bool hasAlpha = false;
        bool swapPlanes = false;
        unsigned frameNum = 0;

        struct Tree
        {
            int vlc_num = 0;   // index into the shared bink_trees[] table set
            std::uint8_t syms[16] = {}; // leaf value -> symbol mapping
        };

        struct Bundle
        {
            int len = 0;       // number of bits used to store the element count
            Tree tree;         // Huffman tree for this data type
            std::uint8_t* data = nullptr;     // decoded-symbol buffer
            std::uint8_t* data_end = nullptr; // end of buffer
            std::uint8_t* cur_dec = nullptr;  // not-yet-decoded part
            std::uint8_t* cur_ptr = nullptr;  // not-yet-read part
        };

        Bundle bundle[BINK_NB_SRC];
        Tree colHigh[16];       // trees for the high nibble of "colours"
        int colLastVal = 0;     // last decoded high nibble in "colours"

        BinkDSPContext dsp;
        BinkFrame last;         // previous frame, used as motion-compensation reference (empty on first)
    };

    class BinkDecoder
    {
    public:
        // Configure from demuxer-provided parameters. `flags` is the 4-byte extradata word.
        void configure(int width, int height, char version, std::uint32_t flags);

        // Decode one compressed frame into `out`. `data` must be followed by at least 16 zero bytes
        // (the bit reader reads up to 8 bytes past the end). Returns false on a decode error.
        bool decode(const std::uint8_t* data, int size, BinkFrame& out);

    private:
        BinkCtx ctx_;
        std::vector<std::uint8_t> bundleStorage_; // owns the memory pointed to by bundle[i].data
    };

    // Convert a decoded YUV420P frame to 8-bit RGBA (limited-range BT.601, matching Bink 'i'/'k').
    // `outRgba` must hold width*height*4 bytes; alpha is set to opaque.
    void yuv420p_to_rgba(const BinkFrame& f, std::uint8_t* outRgba);

} // namespace Codec
