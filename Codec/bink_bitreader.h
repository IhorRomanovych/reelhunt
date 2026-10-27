// SPDX-License-Identifier: LGPL-2.1-or-later
// Codec/bink_bitreader.h - Self-contained little-endian bit reader + Bink Huffman (VLC) table builder.
// Ported from FFmpeg's libavcodec/bitstream_template.h (LE path), get_bits.h and vlc.c so the Bink
// decoder can run without linking against the FFmpeg framework. Bit-exact with the reference decoder:
//   * bit order matches BITSTREAM_READER_LE (first bit read is the LSB of the first byte);
//   * table layout matches ff_vlc_init_sparse + build_table with VLC_INIT_USE_STATIC | VLC_INIT_LE.
#pragma once

#include <cstdint>
#include <cstddef>

namespace Codec
{

    // ---- Little-endian loads (AV_RL32 / AV_RL64 equivalents) ----
    inline std::uint32_t rl32(const std::uint8_t* p)
    {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    inline std::uint64_t rl64(const std::uint8_t* p)
    {
        return static_cast<std::uint64_t>(rl32(p)) | (static_cast<std::uint64_t>(rl32(p + 4)) << 32);
    }

    // ---- Bit reader context (BitstreamContextLE port) ----
    struct BinkBits
    {
        std::uint64_t bits;             // bits read from the buffer
        const std::uint8_t* buffer;     // start of the bitstream buffer
        const std::uint8_t* buffer_end; // end of the bitstream buffer
        const std::uint8_t* ptr;        // current position inside the buffer
        unsigned bits_valid;            // number of valid bits left in `bits`
        unsigned size_in_bits;          // total size of the stream in bits
    };

    // Refill 64 bits (little-endian). Returns -1 when the end of the stream is hit.
    inline int bs_refill_64(BinkBits* bc)
    {
        if (bc->ptr >= bc->buffer_end)
            return -1;
        bc->bits = rl64(bc->ptr);
        bc->ptr += 8;
        bc->bits_valid = 64;
        return 0;
    }

    // Refill another 32 bits into the high part of `bits`. Returns -1 at end of stream.
    inline int bs_refill_32(BinkBits* bc)
    {
        if (bc->ptr >= bc->buffer_end)
            return -1;
        bc->bits |= static_cast<std::uint64_t>(rl32(bc->ptr)) << bc->bits_valid;
        bc->ptr += 4;
        bc->bits_valid += 32;
        return 0;
    }

    // init_get_bits equivalent: takes the stream size in BITS (bink.c passes pkt->size << 3).
    inline int bs_init(BinkBits* bc, const std::uint8_t* buffer, unsigned bit_size)
    {
        if (!buffer || bit_size > 0x7FFFFFFFu)
            return -1; // would overflow the bit counter
        const unsigned buffer_size = (bit_size + 7u) >> 3;
        bc->buffer = buffer;
        bc->buffer_end = buffer + buffer_size;
        bc->ptr = buffer;
        bc->size_in_bits = bit_size;
        bc->bits_valid = 0;
        bc->bits = 0;
        bs_refill_64(bc);
        return 0;
    }

    // get_bits_count: number of bits already read.
    inline int bs_tell(const BinkBits* bc)
    {
        return static_cast<int>((bc->ptr - bc->buffer) * 8 - bc->bits_valid);
    }

    // get_bits_left: number of bits remaining in the stream.
    inline int bs_left(const BinkBits* bc)
    {
        return static_cast<int>((bc->buffer - bc->ptr) * 8 + bc->size_in_bits + bc->bits_valid);
    }

    // show n bits without consuming them (LE: low n bits of `bits`).
    inline std::uint64_t bs_show(BinkBits* bc, unsigned n)
    {
        return bc->bits & ((n >= 64u) ? ~0ULL : (~0ULL >> (64 - n)));
    }

    inline void bs_skip_remaining(BinkBits* bc, unsigned n)
    {
        bc->bits >>= n;
        bc->bits_valid -= n;
    }

    inline std::uint64_t bs_get(BinkBits* bc, unsigned n)
    {
        const std::uint64_t ret = bs_show(bc, n);
        bs_skip_remaining(bc, n);
        return ret;
    }

    // get_bits1: read a single bit.
    inline int bs_read_bit(BinkBits* bc)
    {
        if (!bc->bits_valid && bs_refill_64(bc) < 0)
            return 0;
        return static_cast<int>(bs_get(bc, 1));
    }

    // get_bits: read n bits (n in 1..32).
    inline std::uint32_t bs_read(BinkBits* bc, unsigned n)
    {
        if (!n)
            return 0;
        if (n > bc->bits_valid)
        {
            if (bs_refill_32(bc) < 0)
                bc->bits_valid = n;
        }
        return static_cast<std::uint32_t>(bs_get(bc, n));
    }

    // show_bits: peek n bits without consuming them (n in 1..32).
    inline std::uint32_t bs_peek(BinkBits* bc, unsigned n)
    {
        if (!n)
            return 0;
        if (n > bc->bits_valid)
            bs_refill_32(bc);
        return static_cast<std::uint32_t>(bs_show(bc, n));
    }

    // skip_bits_long: discard n bits.
    inline void bs_skip(BinkBits* bc, unsigned n)
    {
        if (n < bc->bits_valid)
        {
            bs_skip_remaining(bc, n);
        }
        else
        {
            n -= bc->bits_valid;
            bc->bits = 0;
            bc->bits_valid = 0;

            if (n >= 64u)
            {
                const unsigned skip = n / 8u;
                n -= skip * 8u;
                bc->ptr += skip;
            }
            bs_refill_64(bc);
            if (n)
                bs_skip_remaining(bc, n);
        }
    }

    // ---- Big-endian (MSB-first) bit reader ----
    // FFmpeg's default GetBitContext (init_get_bits8) reads the first bit as the MSB of the first
    // byte. The Bink AUDIO decoder (binkaudio.c) uses this order, unlike the LE Bink video reader above.
    struct MsbBits
    {
        const std::uint8_t* data;
        int size_in_bits;
        int pos; // current bit position (0-based)
    };

    inline void msb_init(MsbBits* b, const std::uint8_t* data, int size_in_bits)
    {
        b->data = data;
        b->size_in_bits = size_in_bits;
        b->pos = 0;
    }

    inline int msb_left(const MsbBits* b)
    {
        return b->size_in_bits - b->pos;
    }

    // Read a single bit (MSB of the current byte first); 0 past end of stream.
    inline int msb_bit(MsbBits* b)
    {
        if (b->pos >= b->size_in_bits)
            return 0;
        const int v = (b->data[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u;
        ++b->pos;
        return v;
    }

    // Read n bits as an n-bit integer (first bit read is the MSB). n in 0..32.
    inline std::uint32_t msb_read(MsbBits* b, int n)
    {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i)
            v = (v << 1) | static_cast<std::uint32_t>(msb_bit(b));
        return v;
    }

    // Discard n bits.
    inline void msb_skip(MsbBits* b, int n)
    {
        b->pos += n;
    }

    // ---- VLC table element + tree ----
    struct VLCElem
    {
        std::int16_t sym;
        std::int16_t len;
    };

    struct BinkVLC
    {
        int bits;          // number of bits peeked per lookup (== max code length)
        VLCElem* table;    // decode table, 2^bits entries
        int table_size;
        int table_allocated;
    };

    // get_vlc2 / bits_read_vlc_le: parse one VLC symbol. Bink always uses max_depth == 1, but the
    // general form is kept for fidelity with the reference implementation.
    inline int bs_read_vlc(BinkBits* bc, const VLCElem* table, int bits, int max_depth)
    {
        int nb_bits;
        unsigned idx = bs_peek(bc, static_cast<unsigned>(bits));
        int code = table[idx].sym;
        int n = table[idx].len;

        if (max_depth > 1 && n < 0)
        {
            bs_skip_remaining(bc, static_cast<unsigned>(bits));
            nb_bits = -n;
            idx = bs_peek(bc, static_cast<unsigned>(nb_bits)) + code;
            code = table[idx].sym;
            n = table[idx].len;
            if (max_depth > 2 && n < 0)
            {
                bs_skip_remaining(bc, static_cast<unsigned>(nb_bits));
                nb_bits = -n;
                idx = bs_peek(bc, static_cast<unsigned>(nb_bits)) + code;
                code = table[idx].sym;
                n = table[idx].len;
            }
        }
        bs_skip_remaining(bc, static_cast<unsigned>(n));
        return code;
    }

    // ---- Bink Huffman table builder (ff_vlc_init_sparse + build_table, LE static path) ----
    //
    // Bink supplies explicit per-symbol code lengths (`lens`) and explicit bit patterns (`codes`).
    // With VLC_INIT_LE the reference does bitswap_32 on input AND output, which cancels out, so the
    // table index for a symbol is simply its raw code value. Because maxbits == lens[15] is always the
    // longest code in a Bink tree (verified: total across all 16 trees == 976), no sub-tables are ever
    // required and this flat builder is bit-identical to FFmpeg's recursive build_table.
    inline int bink_build_vlc(VLCElem* table, const std::uint8_t lens[16], const std::uint8_t codes[16])
    {
        const int maxbits = lens[15];
        const int table_size = 1 << maxbits;

        for (int i = 0; i < table_size; ++i)
        {
            table[i].sym = -1;
            table[i].len = 0;
        }

        for (int s = 0; s < 16; ++s)
        {
            const int n = lens[s];
            if (n <= 0 || n > maxbits)
                continue; // n > maxbits would need a sub-table (never happens in Bink data)

            const std::uint32_t v = codes[s]; // code value, LSB-first (LE) form
            int j = static_cast<int>(v);      // == bitswap_32(bitswap_32(v)) net effect of the LE path
            const int nb = 1 << (maxbits - n);
            const int inc = 1 << n;

            for (int k = 0; k < nb; ++k)
            {
                table[j].len = static_cast<std::int16_t>(n);
                table[j].sym = static_cast<std::int16_t>(s);
                j += inc;
            }
        }

        return table_size;
    }

} // namespace Codec
