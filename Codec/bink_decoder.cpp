// SPDX-License-Identifier: LGPL-2.1-or-later
// Codec/bink_decoder.cpp - Bink video decode logic (version > 'b'), ported from FFmpeg libavcodec/bink.c.
#include "bink_decoder.h"
#include "binkdata.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Codec
{
namespace
{

    // ---- Shared VLC tables (mirrors FFmpeg's static bink_trees[16] + table[976]) ----
    VLCElem g_treeTable[976];
    BinkVLC g_trees[16];
    bool g_treesInit = false;

    void ensureTrees()
    {
        if (g_treesInit)
            return;
        int offset = 0;
        for (int i = 0; i < 16; ++i)
        {
            const int maxbits = bink_tree_lens[i][15];
            g_trees[i].table = g_treeTable + offset;
            g_trees[i].table_allocated = 1 << maxbits;
            g_trees[i].table_size = 1 << maxbits;
            g_trees[i].bits = maxbits;
            bink_build_vlc(g_trees[i].table, bink_tree_lens[i], bink_tree_bits[i]);
            offset += g_trees[i].table_allocated;
        }
        g_treesInit = true;
    }

    // GET_HUFF: decode one 4-bit Huffman value and map it through the tree's symbol table.
    #define GET_HUFF(gb, tree) \
        (tree).syms[bs_read_vlc((gb), g_trees[(tree).vlc_num].table, g_trees[(tree).vlc_num].bits, 1)]

    static int binkLog2(int x)
    {
        return 31 - __builtin_clz(static_cast<unsigned>(x)); // x is always > 0 here
    }

    #define BINK_LOG(...) std::fprintf(stderr, __VA_ARGS__)

    // ---- Simple pixel helpers (replace FFmpeg's BlockDSPContext / put_pixels_tab) ----
    static void bink_clear_block(std::int16_t* block)
    {
        std::memset(block, 0, sizeof(std::int16_t) * 64);
    }

    static void bink_fill(std::uint8_t* dst, int v, int stride, int w, int h)
    {
        for (int i = 0; i < h; ++i)
            std::memset(dst + i * stride, static_cast<std::uint8_t>(v), w);
    }

    // Plain 8x8 copy (FFmpeg's put_pixels_tab[1][0]); Bink only calls it for non-overlapping blocks.
    static void bink_copy8x8(std::uint8_t* dst, const std::uint8_t* src, int stride)
    {
        for (int i = 0; i < 8; ++i)
            std::memcpy(dst + i * stride, src + i * stride, 8);
    }

    // ---- init_lengths: set the element-count bit length in every bundle ----
    static void init_lengths(BinkCtx* c, int width, int bw)
    {
        width = (width + 7) & ~7; // FFALIGN(width, 8)

        c->bundle[BINK_SRC_BLOCK_TYPES].len = binkLog2((width >> 3) + 511) + 1;
        c->bundle[BINK_SRC_SUB_BLOCK_TYPES].len = binkLog2((width >> 4) + 511) + 1;
        c->bundle[BINK_SRC_COLORS].len = binkLog2(bw * 64 + 511) + 1;
        c->bundle[BINK_SRC_INTRA_DC].len =
            c->bundle[BINK_SRC_INTER_DC].len =
                c->bundle[BINK_SRC_X_OFF].len =
                    c->bundle[BINK_SRC_Y_OFF].len = binkLog2((width >> 3) + 511) + 1;
        c->bundle[BINK_SRC_PATTERN].len = binkLog2((bw << 3) + 511) + 1;
        c->bundle[BINK_SRC_RUN].len = binkLog2(bw * 48 + 511) + 1;
    }

    // ---- merge: interleave two equal-size lists according to bits read ----
    static void merge(BinkBits* gb, std::uint8_t* dst, std::uint8_t* src, int size)
    {
        std::uint8_t* src2 = src + size;
        int size2 = size;

        do
        {
            if (!bs_read_bit(gb))
            {
                *dst++ = *src++;
                size--;
            }
            else
            {
                *dst++ = *src2++;
                size2--;
            }
        } while (size && size2);

        while (size--)
            *dst++ = *src++;
        while (size2--)
            *dst++ = *src2++;
    }

    // ---- read_tree: decode which base Huffman tree + symbol remap to use ----
    static int read_tree(BinkBits* gb, BinkCtx::Tree* tree)
    {
        std::uint8_t tmp1[16] = {0}, tmp2[16];
        std::uint8_t* in = tmp1;
        std::uint8_t* out = tmp2;
        int i, t, len;

        if (bs_left(gb) < 4)
            return -1;

        tree->vlc_num = bs_read(gb, 4);
        if (!tree->vlc_num)
        {
            for (i = 0; i < 16; i++)
                tree->syms[i] = static_cast<std::uint8_t>(i);
            return 0;
        }
        if (bs_read_bit(gb))
        {
            len = bs_read(gb, 3);
            for (i = 0; i <= len; i++)
            {
                tree->syms[i] = static_cast<std::uint8_t>(bs_read(gb, 4));
                tmp1[tree->syms[i]] = 1;
            }
            for (i = 0; i < 16 && len < 16 - 1; i++)
                if (!tmp1[i])
                    tree->syms[++len] = static_cast<std::uint8_t>(i);
        }
        else
        {
            len = bs_read(gb, 2);
            for (i = 0; i < 16; i++)
                in[i] = static_cast<std::uint8_t>(i);
            for (i = 0; i <= len; i++)
            {
                const int size = 1 << i;
                for (t = 0; t < 16; t += size << 1)
                    merge(gb, out + t, in + t, size);
                std::swap(in, out); // FFSWAP(uint8_t*, in, out)
            }
            std::memcpy(tree->syms, in, 16);
        }
        return 0;
    }

    // ---- read_bundle: prepare a bundle for decoding (read its tree(s)) ----
    static int read_bundle(BinkBits* gb, BinkCtx* c, int bundle_num)
    {
        if (bundle_num == BINK_SRC_COLORS)
        {
            for (int i = 0; i < 16; i++)
            {
                const int ret = read_tree(gb, &c->colHigh[i]);
                if (ret < 0)
                    return ret;
            }
            c->colLastVal = 0;
        }
        if (bundle_num != BINK_SRC_INTRA_DC && bundle_num != BINK_SRC_INTER_DC)
        {
            const int ret = read_tree(gb, &c->bundle[bundle_num].tree);
            if (ret < 0)
                return ret;
        }
        c->bundle[bundle_num].cur_dec =
            c->bundle[bundle_num].cur_ptr = c->bundle[bundle_num].data;

        return 0;
    }

    // Common prologue before decoding a bundle's values.
    #define CHECK_READ_VAL(gb, b, t) \
        if (!(b)->cur_dec || (b)->cur_dec > (b)->cur_ptr) \
            return 0; \
        (t) = bs_read((gb), (b)->len); \
        if (!(t)) { \
            (b)->cur_dec = nullptr; \
            return 0; \
        }

    static int read_runs(BinkBits* gb, BinkCtx::Bundle* b)
    {
        int t, v;
        const std::uint8_t* dec_end;

        CHECK_READ_VAL(gb, b, t);
        dec_end = b->cur_dec + t;
        if (dec_end > b->data_end)
            return -1;
        if (bs_left(gb) < 1)
            return -1;
        if (bs_read_bit(gb))
        {
            v = bs_read(gb, 4);
            std::memset(b->cur_dec, static_cast<std::uint8_t>(v), t);
            b->cur_dec += t;
        }
        else
        {
            while (b->cur_dec < dec_end)
                *b->cur_dec++ = static_cast<std::uint8_t>(GET_HUFF(gb, b->tree));
        }
        return 0;
    }

    static int read_motion_values(BinkBits* gb, BinkCtx::Bundle* b)
    {
        int t, sign, v;
        const std::uint8_t* dec_end;

        CHECK_READ_VAL(gb, b, t);
        dec_end = b->cur_dec + t;
        if (dec_end > b->data_end)
            return -1;
        if (bs_left(gb) < 1)
            return -1;
        if (bs_read_bit(gb))
        {
            v = bs_read(gb, 4);
            if (v)
            {
                sign = -bs_read_bit(gb);
                v = (v ^ sign) - sign;
            }
            std::memset(b->cur_dec, static_cast<std::uint8_t>(v), t);
            b->cur_dec += t;
        }
        else
        {
            while (b->cur_dec < dec_end)
            {
                v = GET_HUFF(gb, b->tree);
                if (v)
                {
                    sign = -bs_read_bit(gb);
                    v = (v ^ sign) - sign;
                }
                *b->cur_dec++ = static_cast<std::uint8_t>(v);
            }
        }
        return 0;
    }

    const std::uint8_t bink_rlelens[4] = {4, 8, 12, 32};

    static int read_block_types(BinkCtx* c, BinkBits* gb, BinkCtx::Bundle* b)
    {
        int t, v;
        int last = 0;
        const std::uint8_t* dec_end;

        CHECK_READ_VAL(gb, b, t);
        if (c->version == 'k')
        {
            t ^= 0xBBu;
            if (t == 0)
            {
                b->cur_dec = nullptr;
                return 0;
            }
        }
        dec_end = b->cur_dec + t;
        if (dec_end > b->data_end)
            return -1;
        if (bs_left(gb) < 1)
            return -1;
        if (bs_read_bit(gb))
        {
            v = bs_read(gb, 4);
            std::memset(b->cur_dec, static_cast<std::uint8_t>(v), t);
            b->cur_dec += t;
        }
        else
        {
            while (b->cur_dec < dec_end)
            {
                v = GET_HUFF(gb, b->tree);
                if (v < 12)
                {
                    last = v;
                    *b->cur_dec++ = static_cast<std::uint8_t>(v);
                }
                else
                {
                    const int run = bink_rlelens[v - 12];
                    if (dec_end - b->cur_dec < run)
                        return -1;
                    std::memset(b->cur_dec, static_cast<std::uint8_t>(last), run);
                    b->cur_dec += run;
                }
            }
        }
        return 0;
    }

    static int read_patterns(BinkBits* gb, BinkCtx::Bundle* b)
    {
        int t, v;
        const std::uint8_t* dec_end;

        CHECK_READ_VAL(gb, b, t);
        dec_end = b->cur_dec + t;
        if (dec_end > b->data_end)
            return -1;
        while (b->cur_dec < dec_end)
        {
            if (bs_left(gb) < 2)
                return -1;
            v = GET_HUFF(gb, b->tree);
            v |= GET_HUFF(gb, b->tree) << 4;
            *b->cur_dec++ = static_cast<std::uint8_t>(v);
        }
        return 0;
    }

    static int read_colors(BinkBits* gb, BinkCtx::Bundle* b, BinkCtx* c)
    {
        int t, sign, v;
        const std::uint8_t* dec_end;

        CHECK_READ_VAL(gb, b, t);
        dec_end = b->cur_dec + t;
        if (dec_end > b->data_end)
            return -1;
        if (bs_left(gb) < 1)
            return -1;
        if (bs_read_bit(gb))
        {
            c->colLastVal = GET_HUFF(gb, c->colHigh[c->colLastVal]);
            v = GET_HUFF(gb, b->tree);
            v = (c->colLastVal << 4) | v;
            if (c->version < 'i')
            {
                sign = static_cast<std::int8_t>(v) >> 7;
                v = ((v & 0x7F) ^ sign) - sign;
                v += 0x80;
            }
            std::memset(b->cur_dec, static_cast<std::uint8_t>(v), t);
            b->cur_dec += t;
        }
        else
        {
            while (b->cur_dec < dec_end)
            {
                if (bs_left(gb) < 2)
                    return -1;
                c->colLastVal = GET_HUFF(gb, c->colHigh[c->colLastVal]);
                v = GET_HUFF(gb, b->tree);
                v = (c->colLastVal << 4) | v;
                if (c->version < 'i')
                {
                    sign = static_cast<std::int8_t>(v) >> 7;
                    v = ((v & 0x7F) ^ sign) - sign;
                    v += 0x80;
                }
                *b->cur_dec++ = static_cast<std::uint8_t>(v);
            }
        }
        return 0;
    }

    // Number of bits used to store the first DC value in a bundle.
    constexpr int DC_START_BITS = 11;

    static int read_dcs(BinkBits* gb, BinkCtx::Bundle* b, int start_bits, int has_sign)
    {
        int i, j, len, len2, bsize, sign, v, v2;
        std::int16_t* dst = reinterpret_cast<std::int16_t*>(b->cur_dec);
        const std::int16_t* dst_end = reinterpret_cast<const std::int16_t*>(b->data_end);

        CHECK_READ_VAL(gb, b, len);
        if (bs_left(gb) < start_bits - has_sign)
            return -1;
        v = bs_read(gb, start_bits - has_sign);
        if (v && has_sign)
        {
            sign = -bs_read_bit(gb);
            v = (v ^ sign) - sign;
        }
        if (dst_end - dst < 1)
            return -1;
        *dst++ = static_cast<std::int16_t>(v);
        len--;
        for (i = 0; i < len; i += 8)
        {
            len2 = std::min(len - i, 8);
            if (dst_end - dst < len2)
                return -1;
            bsize = bs_read(gb, 4);
            if (bsize)
            {
                for (j = 0; j < len2; j++)
                {
                    v2 = bs_read(gb, bsize);
                    if (v2)
                    {
                        sign = -bs_read_bit(gb);
                        v2 = (v2 ^ sign) - sign;
                    }
                    v += v2;
                    *dst++ = static_cast<std::int16_t>(v);
                    if (v < -32768 || v > 32767)
                        return -1;
                }
            }
            else
            {
                for (j = 0; j < len2; j++)
                    *dst++ = static_cast<std::int16_t>(v);
            }
        }

        b->cur_dec = reinterpret_cast<std::uint8_t*>(dst);
        return 0;
    }

    // Retrieve the next value from a bundle (width depends on the data type).
    static inline int get_value(BinkCtx* c, int bundle)
    {
        if (bundle < BINK_SRC_X_OFF || bundle == BINK_SRC_RUN)
            return *c->bundle[bundle].cur_ptr++;
        if (bundle == BINK_SRC_X_OFF || bundle == BINK_SRC_Y_OFF)
            return static_cast<std::int8_t>(*c->bundle[bundle].cur_ptr++);
        const std::int16_t ret = *reinterpret_cast<const std::int16_t*>(c->bundle[bundle].cur_ptr);
        c->bundle[bundle].cur_ptr += 2;
        return ret;
    }

    // Read an 8x8 block of DCT coefficients (adaptive bit-length coding).
    static int read_dct_coeffs(BinkCtx* /*c*/, BinkBits* gb, std::int32_t block[64],
                                const std::uint8_t* scan, int* coef_count_,
                                int coef_idx[64], int q)
    {
        int coef_list[128];
        int mode_list[128];
        int i, t, bits, ccoef, mode, sign;
        int list_start = 64, list_end = 64, list_pos;
        int coef_count = 0;
        int quant_idx;

        if (bs_left(gb) < 4)
            return -1;

        coef_list[list_end] = 4; mode_list[list_end++] = 0;
        coef_list[list_end] = 24; mode_list[list_end++] = 0;
        coef_list[list_end] = 44; mode_list[list_end++] = 0;
        coef_list[list_end] = 1; mode_list[list_end++] = 3;
        coef_list[list_end] = 2; mode_list[list_end++] = 3;
        coef_list[list_end] = 3; mode_list[list_end++] = 3;

        for (bits = bs_read(gb, 4) - 1; bits >= 0; bits--)
        {
            list_pos = list_start;
            while (list_pos < list_end)
            {
                if (!(mode_list[list_pos] | coef_list[list_pos]) || !bs_read_bit(gb))
                {
                    list_pos++;
                    continue;
                }
                ccoef = coef_list[list_pos];
                mode = mode_list[list_pos];
                switch (mode)
                {
                case 0:
                    coef_list[list_pos] = ccoef + 4;
                    mode_list[list_pos] = 1;
                    [[fallthrough]];
                case 2:
                    if (mode == 2)
                    {
                        coef_list[list_pos] = 0;
                        mode_list[list_pos++] = 0;
                    }
                    for (i = 0; i < 4; i++, ccoef++)
                    {
                        if (bs_read_bit(gb))
                        {
                            coef_list[--list_start] = ccoef;
                            mode_list[list_start] = 3;
                        }
                        else
                        {
                            if (!bits)
                                t = 1 - (bs_read_bit(gb) << 1);
                            else
                            {
                                t = bs_read(gb, bits) | 1 << bits;
                                sign = -bs_read_bit(gb);
                                t = (t ^ sign) - sign;
                            }
                            block[scan[ccoef]] = t;
                            coef_idx[coef_count++] = ccoef;
                        }
                    }
                    break;
                case 1:
                    mode_list[list_pos] = 2;
                    for (i = 0; i < 3; i++)
                    {
                        ccoef += 4;
                        coef_list[list_end] = ccoef;
                        mode_list[list_end++] = 2;
                    }
                    break;
                case 3:
                    if (!bits)
                        t = 1 - (bs_read_bit(gb) << 1);
                    else
                    {
                        t = bs_read(gb, bits) | 1 << bits;
                        sign = -bs_read_bit(gb);
                        t = (t ^ sign) - sign;
                    }
                    block[scan[ccoef]] = t;
                    coef_idx[coef_count++] = ccoef;
                    coef_list[list_pos] = 0;
                    mode_list[list_pos++] = 0;
                    break;
                }
            }
        }

        if (q == -1)
            quant_idx = bs_read(gb, 4);
        else
            quant_idx = q;

        *coef_count_ = coef_count;
        return quant_idx;
    }

    static void unquantize_dct_coeffs(std::int32_t block[64], const std::int32_t quant[64],
                                      int coef_count, int coef_idx[64], const std::uint8_t* scan)
    {
        block[0] = (static_cast<int>(block[0]) * quant[0]) >> 11;
        for (int i = 0; i < coef_count; i++)
        {
            const int idx = coef_idx[i];
            block[scan[idx]] = (static_cast<int>(block[scan[idx]]) * quant[idx]) >> 11;
        }
    }

    // Read an 8x8 residue block after motion compensation.
    static int read_residue(BinkBits* gb, std::int16_t block[64], int masks_count)
    {
        int coef_list[128];
        int mode_list[128];
        int i, sign, mask, ccoef, mode;
        int list_start = 64, list_end = 64, list_pos;
        int nz_coeff[64];
        int nz_coeff_count = 0;

        coef_list[list_end] = 4; mode_list[list_end++] = 0;
        coef_list[list_end] = 24; mode_list[list_end++] = 0;
        coef_list[list_end] = 44; mode_list[list_end++] = 0;
        coef_list[list_end] = 0; mode_list[list_end++] = 2;

        for (mask = 1 << bs_read(gb, 3); mask; mask >>= 1)
        {
            for (i = 0; i < nz_coeff_count; i++)
            {
                if (!bs_read_bit(gb))
                    continue;
                if (block[nz_coeff[i]] < 0)
                    block[nz_coeff[i]] -= mask;
                else
                    block[nz_coeff[i]] += mask;
                masks_count--;
                if (masks_count < 0)
                    return 0;
            }
            list_pos = list_start;
            while (list_pos < list_end)
            {
                if (!(coef_list[list_pos] | mode_list[list_pos]) || !bs_read_bit(gb))
                {
                    list_pos++;
                    continue;
                }
                ccoef = coef_list[list_pos];
                mode = mode_list[list_pos];
                switch (mode)
                {
                case 0:
                    coef_list[list_pos] = ccoef + 4;
                    mode_list[list_pos] = 1;
                    [[fallthrough]];
                case 2:
                    if (mode == 2)
                    {
                        coef_list[list_pos] = 0;
                        mode_list[list_pos++] = 0;
                    }
                    for (i = 0; i < 4; i++, ccoef++)
                    {
                        if (bs_read_bit(gb))
                        {
                            coef_list[--list_start] = ccoef;
                            mode_list[list_start] = 3;
                        }
                        else
                        {
                            nz_coeff[nz_coeff_count++] = bink_scan[ccoef];
                            sign = -bs_read_bit(gb);
                            block[bink_scan[ccoef]] = static_cast<std::int16_t>((mask ^ sign) - sign);
                            masks_count--;
                            if (masks_count < 0)
                                return 0;
                        }
                    }
                    break;
                case 1:
                    mode_list[list_pos] = 2;
                    for (i = 0; i < 3; i++)
                    {
                        ccoef += 4;
                        coef_list[list_end] = ccoef;
                        mode_list[list_end++] = 2;
                    }
                    break;
                case 3:
                    nz_coeff[nz_coeff_count++] = bink_scan[ccoef];
                    sign = -bs_read_bit(gb);
                    block[bink_scan[ccoef]] = static_cast<std::int16_t>((mask ^ sign) - sign);
                    coef_list[list_pos] = 0;
                    mode_list[list_pos++] = 0;
                    masks_count--;
                    if (masks_count < 0)
                        return 0;
                    break;
                }
            }
        }

        return 0;
    }

    // Motion-compensated copy of an 8x8 block from the previous frame.
    static int bink_put_pixels(BinkCtx* c, std::uint8_t* dst, std::uint8_t* prev, int stride,
                               std::uint8_t* ref_start, std::uint8_t* ref_end)
    {
        const int xoff = get_value(c, BINK_SRC_X_OFF);
        const int yoff = get_value(c, BINK_SRC_Y_OFF);
        std::uint8_t* ref = prev + xoff + yoff * stride;
        if (ref < ref_start || ref > ref_end)
            return -1;

        bink_copy8x8(dst, ref, stride);
        return 0;
    }

    // Decode a single plane (luma or chroma). Ported from FFmpeg's bink_decode_plane.
    static int bink_decode_plane(BinkCtx* c, BinkFrame* frame, BinkBits* gb,
                                 int plane_idx, int is_chroma)
    {
        std::int16_t block[64];
        std::uint8_t ublock[64];
        std::int32_t dctblock[64];
        int coordmap[64], quant_idx, coef_count, coef_idx[64];

        const int stride = frame->linesize[plane_idx];
        const int bw = is_chroma ? (c->width + 15) >> 4 : (c->width + 7) >> 3;
        const int bh = is_chroma ? (c->height + 15) >> 4 : (c->height + 7) >> 3;
        const int width = c->width >> is_chroma;
        const int height = c->height >> is_chroma;

        if (c->version == 'k' && bs_read_bit(gb))
        {
            const int fill = bs_read(gb, 8);
            std::uint8_t* dst = frame->data[plane_idx];
            for (int i = 0; i < height; i++)
                std::memset(dst + i * stride, static_cast<std::uint8_t>(fill), width);
            if (bs_tell(gb) & 0x1F) // next plane data starts at a 32-bit boundary
                bs_skip(gb, 32 - (bs_tell(gb) & 0x1F));
            return 0;
        }

        init_lengths(c, std::max(width, 8), bw);
        for (int i = 0; i < BINK_NB_SRC; i++)
        {
            const int ret = read_bundle(gb, c, i);
            if (ret < 0)
                return ret;
        }

        std::uint8_t* ref_start = c->last.data[plane_idx] ? c->last.data[plane_idx] : frame->data[plane_idx];
        std::uint8_t* ref_end =
            ref_start + (bw - 1 + c->last.linesize[plane_idx] * (bh - 1)) * 8;

        for (int i = 0; i < 64; i++)
            coordmap[i] = (i & 7) + (i >> 3) * stride;

        std::uint8_t* dst;
        std::uint8_t* prev;
        int v, blk;
        int col[2];
        const std::uint8_t* scan;

        for (int by = 0; by < bh; by++)
        {
            if ((blk = read_block_types(c, gb, &c->bundle[BINK_SRC_BLOCK_TYPES])) < 0) return blk;
            if ((blk = read_block_types(c, gb, &c->bundle[BINK_SRC_SUB_BLOCK_TYPES])) < 0) return blk;
            if ((blk = read_colors(gb, &c->bundle[BINK_SRC_COLORS], c)) < 0) return blk;
            if ((blk = read_patterns(gb, &c->bundle[BINK_SRC_PATTERN])) < 0) return blk;
            if ((blk = read_motion_values(gb, &c->bundle[BINK_SRC_X_OFF])) < 0) return blk;
            if ((blk = read_motion_values(gb, &c->bundle[BINK_SRC_Y_OFF])) < 0) return blk;
            if ((blk = read_dcs(gb, &c->bundle[BINK_SRC_INTRA_DC], DC_START_BITS, 0)) < 0) return blk;
            if ((blk = read_dcs(gb, &c->bundle[BINK_SRC_INTER_DC], DC_START_BITS, 1)) < 0) return blk;
            if ((blk = read_runs(gb, &c->bundle[BINK_SRC_RUN])) < 0) return blk;

            dst = frame->data[plane_idx] + 8 * by * stride;
            prev = (c->last.data[plane_idx] ? c->last.data[plane_idx] : frame->data[plane_idx]) + 8 * by * stride;
            for (int bx = 0; bx < bw; bx++, dst += 8, prev += 8)
            {
                blk = get_value(c, BINK_SRC_BLOCK_TYPES);
                // A 16x16 block type on an odd line/col is part of an already-decoded block: skip it.
                if (((by & 1) || (bx & 1)) && blk == SCALED_BLOCK)
                {
                    bx++;
                    dst += 8;
                    prev += 8;
                    continue;
                }
                switch (blk)
                {
                case SKIP_BLOCK:
                    bink_copy8x8(dst, prev, stride);
                    break;
                case SCALED_BLOCK:
                    blk = get_value(c, BINK_SRC_SUB_BLOCK_TYPES);
                    switch (blk)
                    {
                    case RUN_BLOCK:
                        if (bs_left(gb) < 4) return -1;
                        scan = bink_patterns[bs_read(gb, 4)];
                        {
                            int i = 0;
                            do
                            {
                                const int run = get_value(c, BINK_SRC_RUN) + 1;
                                i += run;
                                if (i > 64) return -1;
                                if (bs_read_bit(gb))
                                {
                                    v = get_value(c, BINK_SRC_COLORS);
                                    for (int j = 0; j < run; j++) ublock[*scan++] = static_cast<std::uint8_t>(v);
                                }
                                else
                                {
                                    for (int j = 0; j < run; j++) ublock[*scan++] = static_cast<std::uint8_t>(get_value(c, BINK_SRC_COLORS));
                                }
                            } while (i < 63);
                            if (i == 63) ublock[*scan++] = static_cast<std::uint8_t>(get_value(c, BINK_SRC_COLORS));
                        }
                        break;
                    case INTRA_BLOCK:
                        std::memset(dctblock, 0, sizeof(dctblock));
                        dctblock[0] = get_value(c, BINK_SRC_INTRA_DC);
                        if ((quant_idx = read_dct_coeffs(c, gb, dctblock, bink_scan, &coef_count, coef_idx, -1)) < 0) return quant_idx;
                        unquantize_dct_coeffs(dctblock, bink_intra_quant[quant_idx], coef_count, coef_idx, bink_scan);
                        c->dsp.idct_put(ublock, 8, dctblock);
                        break;
                    case FILL_BLOCK:
                        v = get_value(c, BINK_SRC_COLORS);
                        bink_fill(dst, v, stride, 16, 16);
                        break;
                    case PATTERN_BLOCK:
                        for (int i = 0; i < 2; i++) col[i] = get_value(c, BINK_SRC_COLORS);
                        for (int j = 0; j < 8; j++)
                        {
                            v = get_value(c, BINK_SRC_PATTERN);
                            for (int i = 0; i < 8; i++, v >>= 1) ublock[i + j * 8] = static_cast<std::uint8_t>(col[v & 1]);
                        }
                        break;
                    case RAW_BLOCK:
                        for (int j = 0; j < 8; j++)
                            for (int i = 0; i < 8; i++) ublock[i + j * 8] = static_cast<std::uint8_t>(get_value(c, BINK_SRC_COLORS));
                        break;
                    default:
                        return -1;
                    }
                    if (blk != FILL_BLOCK)
                        c->dsp.scale_block(ublock, dst, stride);
                    bx++;
                    dst += 8;
                    prev += 8;
                    break;
                case MOTION_BLOCK:
                    if ((blk = bink_put_pixels(c, dst, prev, stride, ref_start, ref_end)) < 0) return blk;
                    break;
                case RUN_BLOCK:
                    scan = bink_patterns[bs_read(gb, 4)];
                    {
                        int i = 0;
                        do
                        {
                            const int run = get_value(c, BINK_SRC_RUN) + 1;
                            i += run;
                            if (i > 64) return -1;
                            if (bs_read_bit(gb))
                            {
                                v = get_value(c, BINK_SRC_COLORS);
                                for (int j = 0; j < run; j++) dst[coordmap[*scan++]] = static_cast<std::uint8_t>(v);
                            }
                            else
                            {
                                for (int j = 0; j < run; j++) dst[coordmap[*scan++]] = static_cast<std::uint8_t>(get_value(c, BINK_SRC_COLORS));
                            }
                        } while (i < 63);
                        if (i == 63) dst[coordmap[*scan++]] = static_cast<std::uint8_t>(get_value(c, BINK_SRC_COLORS));
                    }
                    break;
                case RESIDUE_BLOCK:
                    if ((blk = bink_put_pixels(c, dst, prev, stride, ref_start, ref_end)) < 0) return blk;
                    bink_clear_block(block);
                    {
                        const int v2 = bs_read(gb, 7);
                        read_residue(gb, block, v2);
                    }
                    c->dsp.add_pixels8(dst, block, stride);
                    break;
                case INTRA_BLOCK:
                    std::memset(dctblock, 0, sizeof(dctblock));
                    dctblock[0] = get_value(c, BINK_SRC_INTRA_DC);
                    if ((quant_idx = read_dct_coeffs(c, gb, dctblock, bink_scan, &coef_count, coef_idx, -1)) < 0) return quant_idx;
                    unquantize_dct_coeffs(dctblock, bink_intra_quant[quant_idx], coef_count, coef_idx, bink_scan);
                    c->dsp.idct_put(dst, stride, dctblock);
                    break;
                case FILL_BLOCK:
                    v = get_value(c, BINK_SRC_COLORS);
                    bink_fill(dst, v, stride, 8, 8);
                    break;
                case INTER_BLOCK:
                    if ((blk = bink_put_pixels(c, dst, prev, stride, ref_start, ref_end)) < 0) return blk;
                    std::memset(dctblock, 0, sizeof(dctblock));
                    dctblock[0] = get_value(c, BINK_SRC_INTER_DC);
                    if ((quant_idx = read_dct_coeffs(c, gb, dctblock, bink_scan, &coef_count, coef_idx, -1)) < 0) return quant_idx;
                    unquantize_dct_coeffs(dctblock, bink_inter_quant[quant_idx], coef_count, coef_idx, bink_scan);
                    c->dsp.idct_add(dst, stride, dctblock);
                    break;
                case PATTERN_BLOCK:
                    for (int i = 0; i < 2; i++) col[i] = get_value(c, BINK_SRC_COLORS);
                    for (int i = 0; i < 8; i++)
                    {
                        v = get_value(c, BINK_SRC_PATTERN);
                        for (int j = 0; j < 8; j++, v >>= 1) dst[i * stride + j] = static_cast<std::uint8_t>(col[v & 1]);
                    }
                    break;
                case RAW_BLOCK:
                    for (int i = 0; i < 8; i++)
                        std::memcpy(dst + i * stride, c->bundle[BINK_SRC_COLORS].cur_ptr + i * 8, 8);
                    c->bundle[BINK_SRC_COLORS].cur_ptr += 64;
                    break;
                default:
                    return -1;
                }
            }
        }

        if (bs_tell(gb) & 0x1F) // next plane data starts at a 32-bit boundary
            bs_skip(gb, 32 - (bs_tell(gb) & 0x1F));

        return 0;
    }

} // namespace

// ---- BinkFrame::alloc ----
void BinkFrame::alloc(int w, int h, bool alpha)
{
    width = w;
    height = h;
    hasAlpha = alpha;

    const int cw = (w + 1) >> 1; // chroma width
    const int ch = (h + 1) >> 1; // chroma height
    linesize[0] = w;
    linesize[1] = cw;
    linesize[2] = cw;
    linesize[3] = alpha ? w : 0;

    const std::size_t ySize = static_cast<std::size_t>(w) * h;
    const std::size_t cSize = static_cast<std::size_t>(cw) * ch;
    const std::size_t aSize = alpha ? ySize : 0;
    // +16 bytes of padding after the last plane (harmless, keeps pointers well-formed).
    storage.assign(ySize + cSize + cSize + aSize + 16, 0);

    std::uint8_t* p = storage.data();
    data[0] = p; p += ySize;
    data[1] = p; p += cSize;
    data[2] = p; p += cSize;
    data[3] = alpha ? p : nullptr;
}

// ---- BinkDecoder::configure ----
void BinkDecoder::configure(int width, int height, char version, std::uint32_t flags)
{
    ctx_.width = width;
    ctx_.height = height;
    ctx_.version = version;
    ctx_.hasAlpha = (flags & BINK_FLAG_ALPHA) != 0;
    ctx_.swapPlanes = version >= 'h';
    ctx_.frameNum = 0;

    bink_dsp_init(&ctx_.dsp);
    ensureTrees();

    // Allocate the per-bundle decoded-symbol buffers (sized for luma 8x8 block count).
    const int bw = (width + 7) >> 3;
    const int bh = (height + 7) >> 3;
    const std::size_t blocks = static_cast<std::size_t>(bw) * bh;
    bundleStorage_.assign(blocks * 64 * BINK_NB_SRC, 0);

    std::uint8_t* tmp = bundleStorage_.data();
    for (int i = 0; i < BINK_NB_SRC; i++)
    {
        ctx_.bundle[i].data = tmp;
        tmp += blocks * 64;
        ctx_.bundle[i].data_end = tmp;
    }

    // Reference frame starts empty (data==null) so the first frame decodes intra,
    // matching FFmpeg's freshly-allocated c->last. It is filled in after decode().
}

// ---- BinkDecoder::decode ----
bool BinkDecoder::decode(const std::uint8_t* data, int size, BinkFrame& out)
{
    BinkCtx* c = &ctx_;
    BinkBits gb;
    const int bits_count = size << 3;

    out.alloc(c->width, c->height, c->hasAlpha);

    if (bs_init(&gb, data, static_cast<unsigned>(bits_count)) < 0)
        return false;

    if (c->hasAlpha && c->version >= 'i')
        bs_skip(&gb, 32);
    if (c->hasAlpha)
    {
        const int ret = bink_decode_plane(c, &out, &gb, 3, 0);
        if (ret < 0)
            return false;
    }
    if (c->version >= 'i')
        bs_skip(&gb, 32);

    c->frameNum++;

    for (int plane = 0; plane < 3; plane++)
    {
        const int plane_idx = (!plane || !c->swapPlanes) ? plane : (plane ^ 3);
        const int ret = bink_decode_plane(c, &out, &gb, plane_idx, !!plane);
        if (ret < 0)
            return false;
        if (bs_tell(&gb) >= bits_count)
            break;
    }

    // Store this frame as the reference for the next one (in-place memcpy keeps
    // ctx_.last.data[] pointers valid; no struct copy that would dangle them).
    if (!ctx_.last.data[0])
        ctx_.last.alloc(c->width, c->height, c->hasAlpha);
    const int ch = (c->height + 1) >> 1;
    std::memcpy(ctx_.last.data[0], out.data[0], (std::size_t)ctx_.last.linesize[0] * c->height);
    std::memcpy(ctx_.last.data[1], out.data[1], (std::size_t)ctx_.last.linesize[1] * ch);
    std::memcpy(ctx_.last.data[2], out.data[2], (std::size_t)ctx_.last.linesize[2] * ch);
    if (c->hasAlpha)
        std::memcpy(ctx_.last.data[3], out.data[3], (std::size_t)ctx_.last.linesize[3] * c->height);
    return true;
}

// ---- yuv420p_to_rgba ----
void yuv420p_to_rgba(const BinkFrame& f, std::uint8_t* outRgba)
{
    const int w = f.width, h = f.height;
    const std::uint8_t* Y = f.data[0];
    const std::uint8_t* U = f.data[1];
    const std::uint8_t* V = f.data[2];
    const int ys = f.linesize[0], cs = f.linesize[1];

    auto clampByte = [](float v) -> std::uint8_t {
        if (v < 0.0f) return 0;
        if (v > 255.0f) return 255;
        return static_cast<std::uint8_t>(v + 0.5f);
    };

    for (int y = 0; y < h; ++y)
    {
        const int cy = y >> 1;
        for (int x = 0; x < w; ++x)
        {
            const int cx = x >> 1;
            const float Yv = static_cast<float>(Y[y * ys + x]) - 16.0f;
            const float Cb = static_cast<float>(U[cy * cs + cx]) - 128.0f;
            const float Cr = static_cast<float>(V[cy * cs + cx]) - 128.0f;
            const float R = 1.164f * Yv + 1.596f * Cr;
            const float G = 1.164f * Yv - 0.3917f * Cb - 0.8134f * Cr;
            const float B = 1.164f * Yv + 2.017f * Cb;

            std::uint8_t* o = outRgba + (static_cast<std::size_t>(y) * w + x) * 4u;
            o[0] = clampByte(R);
            o[1] = clampByte(G);
            o[2] = clampByte(B);
            o[3] = 255;
        }
    }
}

} // namespace Codec
