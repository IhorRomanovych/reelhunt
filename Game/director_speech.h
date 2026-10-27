// Game/director_speech.h - the director's voice in Cash's earpiece (PlayDirectorSpeech).
//
// The level script names a line by its AISPEECH id (AISPEECH_D1V1_TRF_HOODS_LEVEL_START = 39).
// The exe maps the id to a range of SFX bank samples (FUN_00450fa0, table 0x6b2746) and
// picks one line from the range at random; a line can span several consecutive samples
// (FUN_004510f0, table 0x6b2692: BORN1A plays BORN1A, 1B, 2A, 2B). Both tables, with the subtitle
// table (0x7c2d64: id 39 -> "BRN1", ...), are read from the player's exe at start-up
// (Game/exe_tables.h): part n of a line shows the level text "<key>A<n>".
#pragma once
#include "engine_types.h"

#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Game {

class DirectorSpeech {
public:
    bool load(const std::string& path);
    [[nodiscard]] std::size_t lineCount() const noexcept { return lines_.size(); }

    // The samples to play for speech `id`, in order (empty if the id has no line). The bank is
    // reached through `indexOf` (name -> record index, npos if unknown) and `nameAt`.
    [[nodiscard]] std::vector<std::string> samples(int32 id, const std::function<std::size_t(const std::string&)>& indexOf,
                                                   const std::function<std::string(std::size_t)>& nameAt,
                                                   std::mt19937& rng) const;

    // The level text key of part `part` (0-based) of line `id`, e.g. (39, 0) -> "BRN1A1"; empty
    // when the line has no subtitle (the random XLOW / HMED ... reactions).
    [[nodiscard]] std::string subtitleKey(int32 id, std::size_t part) const;

private:
    struct Range {
        std::string first, last;
    };
    std::unordered_map<int32, Range> lines_;
    std::unordered_map<std::string, int32> parts_; // first sample -> consecutive samples in the line
    std::unordered_map<int32, std::string> subtitles_; // id -> subtitle key prefix ("BRN1")
};

} // namespace Game
