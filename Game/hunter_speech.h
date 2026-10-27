// Game/hunter_speech.h - which line a hunter says (its own speech and PlayHunterSpeech).
//
// The exe keeps one {first, last} sample range per voice and speech context (table 0x6af966:
// voice * 59 + context - 0x25; the voices are AISetEntityVoiceID's VOICE_ ids, the contexts the
// speech enum NEGATIVE_SEARCH_RESULT .. SILENCE, then SPECIFIC_SPEECH_1..8 for the scripted lines
// at slots 51..58). PlayHunterSpeech(hunter, voice, AISPEECH id) needs the hunter to use that voice
// (0x51cc40) and turns the id into a SPECIFIC_SPEECH slot (0x412060). The line (0x44cf00): from a
// range of more than 16, any at random; otherwise one not said yet, until all have been (a mask
// per voice and context); and a vocal one of the last ten still within its 60 s is not said
// again by anyone - then nothing is said. Both tables are read from the player's exe at
// start-up (Game/exe_tables.h).
#pragma once
#include "engine_types.h"

#include <array>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Game {

class HunterSpeech {
public:
    bool load(const std::string& path);
    [[nodiscard]] bool empty() const noexcept { return lines_.empty(); }

    // The voice id of a bank prefix ("SFX_P2V1_" -> 15), -1 when unknown.
    [[nodiscard]] int32 voiceOf(const std::string& prefix) const;
    // PlayHunterSpeech: the table slot of AISPEECH `id` (51 + its SPECIFIC_SPEECH number), -1 if none.
    [[nodiscard]] int32 specificSlot(int32 id) const;
    // The table slot of a line of the AI's speech orders (the Communications enum, 0x524480: the
    // number 0x500960 / 0x514590 pass, e.g. 0x29 ShoutForAssistance), -1 if it has none. Its
    // context comes from the switch at 0x527a50 (slot = context - 0x25).
    [[nodiscard]] static int32 lineSlot(int32 line) noexcept;
    // The voice and slot a sample stem names ("SFX_H1V1_TAUNT_CHASE": voice 0, slot 12).
    [[nodiscard]] bool find(const std::string& stem, int32& voice, int32& slot) const;

    // The sample to say for `voice`'s `slot` at level time `now` (seconds), or nothing.
    [[nodiscard]] std::optional<std::string> pick(int32 voice, int32 slot, float32 now, std::mt19937& rng);

    static constexpr int32 kContexts = 59;       // slots per voice
    static constexpr int32 kSpecificSlot = 51;   // SPECIFIC_SPEECH_1
    static constexpr float32 kRepeatGap = 60.0f; // a vocal sample is not said again within it (0x44d1a6)

private:
    struct Lines {
        std::vector<std::string> samples;
        uint16 said = 0; // the lines said since the last reset (ranges of up to 16)
    };
    std::unordered_map<int32, Lines> lines_; // voice * kContexts + slot
    std::unordered_map<std::string, int32> stems_;
    std::unordered_map<std::string, int32> voices_;
    std::unordered_map<int32, int32> specific_;
    struct Recent {
        std::string sample;
        float32 until = -1.0f;
    };
    std::array<Recent, 10> recent_{};
    std::size_t nextRecent_ = 0;
};

} // namespace Game
