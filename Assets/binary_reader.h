// Assets/binary_reader.h - bounds-checked little-endian reading shared by the native parsers.
#pragma once
#include "engine_types.h"

#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace Assets {

// Reads a whole file into memory. NotFound if it cannot be opened, Invalid on a short read.
std::expected<std::vector<uint8_t>, Engine::EngineError> readFileBytes(const std::string& path);

// Sequential little-endian reader over an in-memory buffer. Every read is bounds-checked and
// returns false (consuming nothing) when the buffer is too short.
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) noexcept : data_(data) {}
    ByteReader(const uint8_t* data, std::size_t size) noexcept : data_(data, size) {}

    [[nodiscard]] std::size_t offset() const noexcept { return pos_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }
    [[nodiscard]] bool eof() const noexcept { return pos_ >= data_.size(); }

    bool skip(std::size_t n) noexcept {
        if (n > remaining())
            return false;
        pos_ += n;
        return true;
    }

    template <typename T>
    bool read(T& out) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (sizeof(T) > remaining())
            return false;
        std::memcpy(&out, data_.data() + pos_, sizeof(T)); // all supported targets are little-endian
        pos_ += sizeof(T);
        return true;
    }
    bool readU8(uint8& v) noexcept { return read(v); }
    bool readU16(uint16& v) noexcept { return read(v); }
    bool readI16(int16& v) noexcept { return read(v); }
    bool readU32(uint32& v) noexcept { return read(v); }
    bool readI32(int32& v) noexcept { return read(v); }
    bool readF32(float32& v) noexcept { return read(v); }

    // The game "padded string": NUL-terminated, and the whole block (chars + NUL + padding)
    // occupies len + (4 - len % 4) bytes, i.e. a whole number of 32-bit words.
    bool readPaddedString(std::string& out) {
        std::size_t len = 0;
        while (pos_ + len < data_.size() && data_[pos_ + len] != 0)
            ++len;
        const std::size_t block = len + (4u - (len % 4u));
        if (block > remaining())
            return false;
        out.assign(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += block;
        return true;
    }

private:
    std::span<const uint8_t> data_;
    std::size_t pos_ = 0;
};

// Random-access little-endian load from `data` at `offset`; returns false if out of range.
template <typename T>
bool readAt(std::span<const uint8_t> data, std::size_t offset, T& out) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        return false;
    std::memcpy(&out, data.data() + offset, sizeof(T));
    return true;
}

} // namespace Assets
