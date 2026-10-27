#pragma once
#include "engine_types.h"
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Assets {

// One sound effect, copied out of the bank. sfx.RAW holds signed 16-bit little-endian mono PCM
// (verified on the data: the high byte of each word is 0x00/0xFF for quiet samples).
struct SfxSample {
    std::vector<uint8_t> pcm;  // raw bytes: signed 16-bit LE mono (length() is in bytes)
    uint32 sampleRate = 0;
    [[nodiscard]] std::size_t length() const noexcept { return pcm.size(); }
};

// The game PC SFX bank: sfx.lst (names in record order), sfx.SDT (20-byte records:
// u32 offset, u32 length, u32 sampleRate, 2 x u32 unused) and sfx.RAW (the PCM data).
class SfxBank {
public:
    SfxBank() = default;
    SfxBank(const SfxBank&) = delete;
    SfxBank& operator=(const SfxBank&) = delete;

    bool load(const std::string& dir);
    [[nodiscard]] bool isLoaded() const noexcept { return loaded_; }
    [[nodiscard]] size_t sampleCount() const noexcept { return records_.size(); }
    [[nodiscard]] size_t nameCount() const noexcept { return nameToIndex_.size(); }

    // Copy the named sample out of the bank. Thread-safe. False if the name is unknown or the
    // record points outside sfx.RAW.
    bool getSample(const std::string& name, SfxSample& out) const;
    [[nodiscard]] bool has(const std::string& name) const { return nameToIndex_.contains(name); }
    // Record order (sfx.lst): the index of a name (npos if unknown) and the name at an index.
    [[nodiscard]] size_t indexOf(const std::string& name) const
    {
        const auto it = nameToIndex_.find(name);
        return it == nameToIndex_.end() ? static_cast<size_t>(-1) : it->second;
    }
    [[nodiscard]] const std::string& nameAt(size_t i) const { return names_.at(i); }
    [[nodiscard]] size_t nameCountInOrder() const noexcept { return names_.size(); }

private:
    struct Record {
        uint32 offset = 0;
        uint32 length = 0;
        uint32 sampleRate = 0;
    };

    std::vector<Record> records_;
    std::unordered_map<std::string, size_t> nameToIndex_;
    std::vector<std::string> names_; // in record order
    mutable std::ifstream rawFile_;
    mutable std::mutex rawMutex_;   // getSample seeks the shared stream
    uint64 rawSize_ = 0;
    bool loaded_ = false;
};

} // namespace Assets
