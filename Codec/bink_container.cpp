// SPDX-License-Identifier: LGPL-2.1-or-later
// Codec/bink_container.cpp - Bink1 video container parser, ported from FFmpeg libavformat/bink.c (video path).
#include "bink_container.h"

namespace Codec
{

    namespace
    {
        constexpr std::uint32_t BIK_TAG = 'B' | ('I' << 8) | ('K' << 16); // low 3 bytes of the signature
        constexpr int BINK_MAX_AUDIO_TRACKS = 256;
    }

    std::uint32_t BinkContainer::rl32(const std::uint8_t* p)
    {
        return static_cast<std::uint32_t>(p[0]) |
               (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) |
               (static_cast<std::uint32_t>(p[3]) << 24);
    }

    std::uint16_t BinkContainer::rl16(const std::uint8_t* p)
    {
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }

    bool BinkContainer::open(const std::uint8_t* data, std::size_t size)
    {
        if (!data || size < 44)
            return false;

        buf_.assign(data, data + size);
        buf_.resize(buf_.size() + 32, 0); // trailing zero pad so the last frame's bit-reader over-read is safe

        const std::uint8_t* p = buf_.data();

        // SMUSH-wrapped files are not supported (asylum.bik and the rest of the game's .bik set are plain Bink1).
        if (p[0] == 'S' && p[1] == 'M' && p[2] == 'U' && p[3] == 'S')
            return false;

        // offset 0: signature "BIK" + version char.
        const std::uint32_t codecTag = rl32(p + 0);
        if ((codecTag & 0xFFFFFF) != BIK_TAG)
            return false;
        version_ = static_cast<char>((codecTag >> 24) & 0xFF);

        // offset 4: file_size - 8.
        fileSize_ = rl32(p + 4) + 8;
        // offset 8: num_frames (duration).
        numFrames_ = static_cast<int>(rl32(p + 8));
        if (numFrames_ <= 0 || numFrames_ > 1000000)
            return false;
        // offset 12: largest frame size (validated, then skipped along with the next 4 bytes).
        if (rl32(p + 12) > fileSize_)
            return false;

        // offset 20/24: width / height.
        width_ = static_cast<int>(rl32(p + 20));
        height_ = static_cast<int>(rl32(p + 24));
        if (width_ <= 0 || height_ <= 0)
            return false;

        // offset 28/32: fps numerator / denominator.
        fpsNum_ = rl32(p + 28);
        fpsDen_ = rl32(p + 32);
        if (fpsNum_ == 0 || fpsDen_ == 0)
            return false;

        // offset 36: extradata (4 bytes) -> decoder flags.
        flags_ = rl32(p + 36);

        // offset 40: number of audio tracks.
        numAudioTracks_ = static_cast<int>(rl32(p + 40));
        if (numAudioTracks_ > BINK_MAX_AUDIO_TRACKS)
            return false;

        std::size_t pos = 44;

        // Version 'k' has one extra unknown field after the audio-track count.
        if (version_ == 'k')
            pos += 4;

        if (numAudioTracks_ < 0 || pos + 12u * static_cast<std::size_t>(numAudioTracks_) > buf_.size())
            return false;
        if (numAudioTracks_ > 0)
        {
            pos += 4 * numAudioTracks_;              // max decoded size per track
            for (int i = 0; i < numAudioTracks_; ++i)
            {
                if (i == 0)
                {
                    audioSampleRate_ = static_cast<int>(rl16(p + pos));
                    audioFlags_ = rl16(p + pos + 2);
                }
                pos += 4;                            // sample_rate (2) + flags (2) per track
            }
            pos += 4 * numAudioTracks_;              // stream id (4) per track
        }

        if (pos + static_cast<std::size_t>(numFrames_) * 4 > buf_.size())
            return false;

        // Frame index table: numFrames_ uint32 values. Value i holds frame i's even byte offset,
        // with bit 0 as that frame's keyframe flag (frame 0 is always a keyframe).
        std::vector<std::uint32_t> idx(numFrames_);
        for (int i = 0; i < numFrames_; ++i)
        {
            idx[i] = rl32(p + pos);
            pos += 4;
        }

        const std::size_t fileEnd = fileSize_ < buf_.size() ? fileSize_ : buf_.size();
        frames_.resize(numFrames_);
        for (int i = 0; i < numFrames_; ++i)
        {
            const std::size_t start = idx[i] & ~1u;
            const std::size_t end = (i + 1 < numFrames_) ? (idx[i + 1] & ~1u) : fileEnd;
            // Both ends must lie inside the file: an index entry past it would hand the decoder a
            // payload that reads beyond the buffer.
            if (end <= start || end > fileEnd)
                return false;

            // Walk this frame's audio packets to reach the video payload, remembering track 0's payload.
            std::size_t foff = start;
            for (int t = 0; t < numAudioTracks_; ++t)
            {
                if (foff + 4 > end)
                    return false;
                const std::uint32_t audioSize = rl32(p + foff);
                if (t == 0)
                {
                    frames_[i].audioOffset = foff + 4;
                    frames_[i].audioSize = audioSize;
                }
                foff += 4 + audioSize;
            }
            if (foff > end)
                return false;

            frames_[i].videoOffset = foff;
            frames_[i].videoSize = static_cast<std::size_t>(end - foff);
            frames_[i].keyframe = (i == 0) ? true : ((idx[i] & 1u) != 0);
        }

        return true;
    }

    bool BinkContainer::frame(int index, const std::uint8_t** outData, int* outSize) const
    {
        if (index < 0 || index >= numFrames_)
            return false;
        *outData = buf_.data() + frames_[index].videoOffset;
        *outSize = static_cast<int>(frames_[index].videoSize);
        return true;
    }

    bool BinkContainer::audioFrame(int index, const std::uint8_t** outData, int* outSize) const
    {
        if (index < 0 || index >= numFrames_ || numAudioTracks_ == 0)
            return false;
        *outData = buf_.data() + frames_[index].audioOffset;
        *outSize = static_cast<int>(frames_[index].audioSize);
        return frames_[index].audioSize > 0;
    }

} // namespace Codec
