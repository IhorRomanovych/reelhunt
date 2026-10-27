// Assets/rib.h - the game .RIB audio (audio/PC/EXECUTE/<WEAPON>/*.RIB, the level music).
//
// IMA ADPCM in interleave blocks of 0x10000 bytes per channel, each cut into chunks of `chunk`
// bytes: i16 predictor, u8 step index, u8 pad, then two 4-bit codes per byte (low nibble first),
// so a 0x400 chunk gives 1 + 2 * 1020 = 2041 samples (layout after Sor3nt's file-format toolkit
// Archive/Rib.php and winterheart's RIB unpacker).
//
// The execution sounds use 0x400 chunks: each chunk's stored predictor then continues the last
// sample of the one before (median jump 0..600 over all 72 files), while 0x200 restarts the
// decoder in the middle of the data (jumps of ~10000, 2-8 % of the samples clipped: the noise
// the executions played before). They are mono at 44100 Hz: EXECUTE/BAG/BAG<n>A.RIB and
// BAG<n>B.RIB for execution level n + 1 (BAG1 sounds for 7.7 s of Bag_Sneak_Attack2's 8.17 s). A
// and B are two takes the exe streams one at a time, by level (Game/execution.h), not stereo
// sides: their samples correlate 0.0..0.73. The files are padded to whole interleave blocks with
// silence.
#pragma once
#include "engine_types.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace Assets {

struct RibAudio {
    std::vector<int16_t> samples; // interleaved when channels > 1
    uint32 channels = 1;
    uint32 sampleRate = 44100;
};

// Decodes `bytes` holding `channels` interleaved channels.
RibAudio decodeRib(const std::vector<uint8_t>& bytes, uint32 channels = 1, uint32 chunk = 0x400, uint32 sampleRate = 44100);

// An execution sound (one mono .RIB), without the silent padding at its end.
std::expected<RibAudio, Engine::EngineError> loadExecutionSound(const std::string& path);

} // namespace Assets
