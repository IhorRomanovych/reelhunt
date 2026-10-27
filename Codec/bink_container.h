// SPDX-License-Identifier: LGPL-2.1-or-later
// Codec/bink_container.h - Bink1 video container (.bik) parser, ported from FFmpeg libavformat/bink.c.
// Owns the file bytes and exposes per-frame VIDEO payloads (audio stripped) for BinkDecoder.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace Codec
{

    class BinkContainer
    {
    public:
        // Parses a .bik from an in-memory buffer. Returns false if it is not a decodable Bink1 video.
        bool open(const std::uint8_t* data, std::size_t size);

        int width() const { return width_; }
        int height() const { return height_; }
        char version() const { return version_; }      // 4th byte of the signature ('i', 'k', ...)
        std::uint32_t flags() const { return flags_; } // 4-byte extradata word (BINK_FLAG_*)
        int numFrames() const { return numFrames_; }
        double fps() const { return fpsDen_ ? static_cast<double>(fpsNum_) / static_cast<double>(fpsDen_) : 0.0; }

        // Video payload for frame `index`: pointer into the owned buffer + byte size (audio removed).
        // The returned data is followed by >=16 bytes of valid memory (trailing zero pad), as BinkDecoder requires.
        bool frame(int index, const std::uint8_t** outData, int* outSize) const;

        // Audio track (present when the file has one) and its per-frame payload.
        bool hasAudio() const { return numAudioTracks_ > 0; }
        int audioSampleRate() const { return audioSampleRate_; } // Hz (0 if no audio)
        std::uint16_t audioFlags() const { return audioFlags_; } // BINK_AUD_* bits (0 if no audio)

        // Track-0 audio payload for frame `index`: pointer into the owned buffer + byte size.
        // Returns false (size 0) for a frame that carries no audio data.
        bool audioFrame(int index, const std::uint8_t** outData, int* outSize) const;

    private:
        struct Frame
        {
            std::size_t videoOffset = 0; // absolute offset of the video payload in buf_
            std::size_t videoSize = 0;   // byte count of the video payload
            std::size_t audioOffset = 0; // absolute offset of track-0 audio payload in buf_ (after its 4-byte size)
            std::size_t audioSize = 0;   // byte count of the track-0 audio payload (0 = none)
            bool keyframe = false;
        };

        static std::uint32_t rl32(const std::uint8_t* p);
        static std::uint16_t rl16(const std::uint8_t* p);

        std::vector<std::uint8_t> buf_; // file bytes + 32 zero pad tail (bit-reader over-read safety)

        std::size_t fileSize_ = 0;      // stored header size (+8); end of the last frame
        int width_ = 0, height_ = 0;
        char version_ = 0;
        std::uint32_t flags_ = 0;
        std::uint32_t fpsNum_ = 0, fpsDen_ = 1;
        int numFrames_ = 0;
        int numAudioTracks_ = 0;
        int audioSampleRate_ = 0;    // track-0 sample rate in Hz
        std::uint16_t audioFlags_ = 0; // track-0 BINK_AUD_* bits

        std::vector<Frame> frames_;
    };

} // namespace Codec
