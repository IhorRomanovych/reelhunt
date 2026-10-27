// Assets/binary_reader.cpp - file loading for the native parsers.
#include "Assets/binary_reader.h"
#include "Core/platform.h"

#include <fstream>

namespace Assets {

std::expected<std::vector<uint8_t>, Engine::EngineError> readFileBytes(const std::string& path) {
    std::ifstream f(Engine::Platform::resolvePath(path), std::ios::binary | std::ios::ate);
    if (!f)
        return std::unexpected(Engine::EngineError::failure(Engine::EngineError::Code::NotFound,
                                                            "cannot open " + path));
    const std::streamoff size = f.tellg();
    if (size < 0)
        return std::unexpected(Engine::EngineError::failure(Engine::EngineError::Code::Failed,
                                                            "cannot size " + path));
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    f.seekg(0, std::ios::beg);
    if (size > 0 && !f.read(reinterpret_cast<char*>(bytes.data()), size))
        return std::unexpected(Engine::EngineError::failure(Engine::EngineError::Code::Invalid,
                                                            "short read on " + path));
    return bytes;
}

} // namespace Assets
