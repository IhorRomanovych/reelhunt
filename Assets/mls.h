#pragma once
#include "engine_types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Assets {

struct MlsEvent {
    char name[64]{};
    uint32 onTrigger = 0;
    uint32 scriptStart = 0;
};

struct MlsSymbol {
    char name[32]{};
    uint32 offset = 0xFFFFFFFF;
    uint32 size = 0xFFFFFFFF;
    uint32 valueType = 0;
    uint32 occCount = 0;
    std::vector<uint32> occurrences;
};

struct MlsScript {
    std::string name;
    uint32 entityType = 0;
    std::string entityName;
    std::vector<MlsEvent> events;
    std::vector<uint32> code;
    std::vector<uint8_t> data;
    uint32 smemSize = 0;
    uint32 dmemSize = 0;
    std::string source;
    std::vector<MlsSymbol> symbols;
};

class MlsFile {
public:
    MlsFile() = default;

    bool load(const std::string& path);
    [[nodiscard]] size_t scriptCount() const noexcept { return scripts_.size(); }
    [[nodiscard]] const MlsScript& script(size_t i) const noexcept { return scripts_[i]; }

    int findScriptByEntity(const std::string& entityName) const;
    int findEvent(const MlsScript& s, const std::string& eventName) const;

private:
    static uint32 readU32LE(const uint8_t* p);
    static void parseSections(const uint8_t* data, size_t size, MlsScript& out);
    static void parseSCPT(const uint8_t* d, size_t n, MlsScript& s);
    static void parseCODE(const uint8_t* d, size_t n, MlsScript& s);
    static void parseSTAB(const uint8_t* d, size_t n, MlsScript& s);
    static void parseDBUG(const uint8_t* d, size_t n, MlsScript& s);

    std::vector<MlsScript> scripts_;
};

} // namespace Assets
