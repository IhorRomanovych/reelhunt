// Game/exe_tables.cpp - see exe_tables.h.
#include "Game/exe_tables.h"

#include "Core/platform.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

namespace Game {

namespace {

// The executable's bytes, addressed by virtual address through its PE section table.
class ExeImage {
public:
    explicit ExeImage(std::vector<uint8> bytes) : bytes_(std::move(bytes)) { parse(); }
    [[nodiscard]] bool valid() const noexcept { return !sections_.empty(); }

    // `size` bytes at `va`, or nullptr when they are not all in one section's file data.
    [[nodiscard]] const uint8* at(uint32 va, std::size_t size) const
    {
        for (const Section& s : sections_)
            if (va >= s.va && va - s.va + size <= s.rawSize && s.raw + (va - s.va) + size <= bytes_.size())
                return bytes_.data() + s.raw + (va - s.va);
        return nullptr;
    }
    template <class T>
    [[nodiscard]] std::optional<T> read(uint32 va) const
    {
        const uint8* p = at(va, sizeof(T));
        if (!p)
            return std::nullopt;
        T v;
        std::memcpy(&v, p, sizeof(T));
        return v;
    }

private:
    struct Section {
        uint32 va = 0, raw = 0, rawSize = 0;
    };

    template <class T>
    T word(std::size_t off) const
    {
        T v{};
        if (off + sizeof(T) <= bytes_.size())
            std::memcpy(&v, bytes_.data() + off, sizeof(T));
        return v;
    }

    void parse()
    {
        if (bytes_.size() < 0x40 || bytes_[0] != 'M' || bytes_[1] != 'Z')
            return;
        const uint32 pe = word<uint32>(0x3c);
        if (word<uint32>(pe) != 0x4550) // "PE\0\0"
            return;
        const uint16 count = word<uint16>(pe + 6);
        const uint16 optionalSize = word<uint16>(pe + 20);
        const uint32 imageBase = word<uint32>(pe + 24 + 28);
        for (uint32 i = 0, off = pe + 24 + optionalSize; i < count; ++i, off += 40)
            sections_.push_back({imageBase + word<uint32>(off + 12), word<uint32>(off + 20), word<uint32>(off + 16)});
    }

    std::vector<uint8> bytes_;
    std::vector<Section> sections_;
};

constexpr uint16 kNoLine = 0x3713; // a speech slot with no line

// PlayDirectorSpeech (0x450fa0 / 0x4510f0) and the subtitles.
constexpr uint32 kDirectorLines = 0x6b2746;  // u16 first, u16 last per AISPEECH id 0..0x98
constexpr uint32 kDirectorParts = 0x6b2692;  // u16 sample, u8 parts, u8 pad, x 44
constexpr uint32 kDirectorTitles = 0x7c2d64; // { u32 AISPEECH id, char key[8] }, ended by an empty key
// The hunters' lines (0x44cf00): u16 first, u16 last per voice * 59 + (context - 0x25).
constexpr uint32 kHunterLines = 0x6af966;
constexpr int kVoices = 49, kContexts = 59;
// PlayHunterSpeech's AISPEECH id -> SPECIFIC_SPEECH_n (0x412060): a switch over ids 0..0x75 whose
// cases return 0x20 + n - 1; anything else has no slot. Its jump table (0x6704ac, in .rdata) sends
// each id to one of the eight cases, 8-byte blocks from 0x412070 in slot order.
constexpr uint32 kSpecificSwitch = 0x412060;
constexpr uint32 kSpecificTable = 0x6704ac;
constexpr uint32 kSpecificCases = 0x412070;
// The ped clips (0x30-byte names, ids 0..0x355) and behaviours (0x18 bytes each, 0..0x3d).
constexpr uint32 kClipNames = 0x6851c0, kBehaviours = 0x68f210;
constexpr int kClips = 0x356, kBehaviourCount = 0x3e;
constexpr uint32 kDefaultBindings = 0x710994; // both key tables, 0x260 bytes (0x604c70)
// Names of the game's own files, as the exe spells them.
constexpr uint32 kSaveNames = 0x7d7c10;   // the eight slots' file names, 0x20 apart
constexpr uint32 kUserFiles = 0x739cd5;   // "<sep><folder>": the Documents folder of saves and settings
constexpr uint32 kSaveGames = 0x739ce9;   // "<sep>SaveGames"
constexpr uint32 kTuningPak = 0x739cf5;   // ".<sep>levels<sep>global<sep>data<sep><name>.Pak"
constexpr uint32 kFontAtlas = 0x7cdb4b;   // font 1's texture in frontend_pc.txd

std::string cString(const uint8* p, std::size_t max)
{
    return std::string(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), max));
}

// A string of the exe (at most `max` bytes), or nullopt outside its data.
std::optional<std::string> exeString(const ExeImage& exe, uint32 va, std::size_t max)
{
    const uint8* p = exe.at(va, max);
    return p ? std::optional<std::string>(cString(p, max)) : std::nullopt;
}

// A Windows path of the exe as a relative path with '/' separators, without a leading ".\" or "\".
std::string relativePath(std::string p)
{
    for (char& c : p)
        if (c == '\\')
            c = '/';
    while (!p.empty() && (p.front() == '.' || p.front() == '/'))
        p.erase(p.begin());
    return p;
}

bool readNames(const ExeImage& exe, ExeTables& t)
{
    for (std::size_t i = 0; i < t.saveNames.size(); ++i)
    {
        const auto name = exeString(exe, kSaveNames + 0x20 * static_cast<uint32>(i), 0x20);
        if (!name || name->size() < 5 || name->compare(name->size() - 4, 4, ".SAV") != 0)
            return false;
        t.saveNames[i] = *name;
    }
    const auto user = exeString(exe, kUserFiles, 0x14), saves = exeString(exe, kSaveGames, 0x0c);
    const auto pak = exeString(exe, kTuningPak, 0x40), font = exeString(exe, kFontAtlas, 0x10);
    if (!user || !saves || !pak || !font || font->empty())
        return false;
    t.userFilesDir = relativePath(*user);
    t.saveGamesDir = relativePath(*saves);
    t.tuningPak = relativePath(*pak);
    t.fontAtlas = *font;
    return !t.userFilesDir.empty() && !t.saveGamesDir.empty() && t.tuningPak.ends_with(".Pak");
}

std::vector<std::string> sfxNames(const std::string& path)
{
    std::vector<std::string> names;
    std::ifstream in(Engine::Platform::resolvePath(path), std::ios::binary);
    std::string line;
    while (std::getline(in, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.rfind("-- ", 0) == 0)
            names.push_back(line.substr(3));
    }
    return names;
}

std::optional<std::string> directorSpeech(const ExeImage& exe, const std::vector<std::string>& names)
{
    std::string out = "# Director speech (PlayDirectorSpeech), read from the exe by the engine (Game/exe_tables.cpp).\n"
                      "# line <AISPEECH id> <first sample> <last sample>   (a range: one line picked at random)\n"
                      "# parts <sample> <count>                            (a line made of consecutive samples)\n"
                      "# subtitle <AISPEECH id> <key>                      (part n shows level text <key>A<n>)\n";
    for (uint32 i = 0; i < 0x99; ++i)
    {
        const auto a = exe.read<uint16>(kDirectorLines + 4 * i), b = exe.read<uint16>(kDirectorLines + 4 * i + 2);
        if (!a || !b)
            return std::nullopt;
        if (*a == kNoLine || *a >= names.size() || *b >= names.size())
            continue;
        out += "line " + std::to_string(i) + ' ' + names[*a] + ' ' + names[*b] + '\n';
    }
    for (uint32 i = 0; i < 44; ++i)
    {
        const auto s = exe.read<uint16>(kDirectorParts + 4 * i);
        const auto n = exe.read<uint8>(kDirectorParts + 4 * i + 2);
        if (!s || !n)
            return std::nullopt;
        if (*s < names.size())
            out += "parts " + names[*s] + ' ' + std::to_string(*n) + '\n';
    }
    for (uint32 at = kDirectorTitles;; at += 12)
    {
        const auto id = exe.read<uint32>(at);
        const uint8* key = exe.at(at + 4, 8);
        if (!id || !key)
            return std::nullopt;
        const std::string k = cString(key, 8);
        if (k.empty())
            break;
        out += "subtitle " + std::to_string(*id) + ' ' + k + '\n';
    }
    return out;
}

// The bank prefix of a voice's sample ("SFX_H1V1_TAUNT" -> "SFX_H1V1_"), or "" when it has none.
std::string voicePrefix(const std::string& sample)
{
    if (sample.size() < 9 || sample.compare(0, 4, "SFX_") != 0)
        return {};
    const char* p = sample.c_str() + 4;
    const bool ok = p[0] >= 'A' && p[0] <= 'Z' && std::isdigit(static_cast<unsigned char>(p[1])) && p[2] == 'V' &&
                    std::isdigit(static_cast<unsigned char>(p[3])) && p[4] == '_';
    return ok ? sample.substr(0, 9) : std::string();
}

std::optional<std::string> hunterSpeech(const ExeImage& exe, const std::vector<std::string>& names)
{
    std::string out = "# Hunter speech, read from the exe by the engine (Game/exe_tables.cpp).\n"
                      "# voice <VOICE id> <bank prefix>                (AISetEntityVoiceID ids, table order)\n"
                      "# line <voice> <slot 0..58> <sample> ...         (the lines of one context; slot = context - 0x25)\n"
                      "# specific <AISPEECH id> <0..7>                  (PlayHunterSpeech: SPECIFIC_SPEECH_n, slot 51 + n)\n";
    for (int v = 0; v < kVoices; ++v)
    {
        std::string prefix, rows;
        for (int c = 0; c < kContexts; ++c)
        {
            const uint32 at = kHunterLines + 4 * static_cast<uint32>(v * kContexts + c);
            const auto a = exe.read<uint16>(at), b = exe.read<uint16>(at + 2);
            if (!a || !b)
                return std::nullopt;
            if (*a == kNoLine || *a >= names.size() || *b >= names.size() || *b < *a)
                continue;
            rows += "line " + std::to_string(v) + ' ' + std::to_string(c);
            for (uint16 s = *a; s <= *b; ++s)
                rows += ' ' + names[s];
            rows += '\n';
            if (prefix.empty())
                prefix = voicePrefix(names[*a]);
        }
        if (!prefix.empty())
            out += "voice " + std::to_string(v) + ' ' + prefix + '\n';
        out += rows;
    }

    // The slot comes from the jump table alone: the case an id jumps to. Copies whose code is
    // encrypted on disk (the Steam release's DRM wrapper) keep the table readable. Where the code
    // can be read, it must be the switch: mov ecx, [esp+4]; cmp ecx, 0x75; ja default;
    // jmp [ecx*4 + table], each case mov eax, 0x20 + slot; ret 4.
    const uint8* code = exe.at(kSpecificSwitch, 16);
    const uint8* cases = exe.at(kSpecificCases, 8 * 8);
    static constexpr uint8 kHead[] = {0x8b, 0x4c, 0x24, 0x04, 0x83, 0xf9, 0x75, 0x77};
    if (code && cases && std::memcmp(code, kHead, sizeof kHead) == 0)
    {
        uint32 table = 0;
        std::memcpy(&table, code + 12, 4);
        if (code[9] != 0xff || code[10] != 0x24 || code[11] != 0x8d || table != kSpecificTable)
            return std::nullopt;
        for (uint32 slot = 0; slot < 8; ++slot)
            if (cases[8 * slot] != 0xb8 || cases[8 * slot + 1] != 0x20 + slot)
                return std::nullopt;
    }
    std::vector<std::string> bySlot(8);
    for (uint32 id = 0; id <= 0x75; ++id)
    {
        const auto target = exe.read<uint32>(kSpecificTable + 4 * id);
        if (!target)
            return std::nullopt;
        if (*target < kSpecificCases || *target >= kSpecificCases + 8 * 8)
            continue; // the default: no slot
        if ((*target - kSpecificCases) % 8 != 0)
            return std::nullopt; // not this build's layout
        const uint32 slot = (*target - kSpecificCases) / 8;
        bySlot[slot] += "specific " + std::to_string(id) + ' ' + std::to_string(slot) + '\n';
    }
    for (const std::string& s : bySlot)
        out += s;
    return out;
}

std::optional<std::string> pedAnims(const ExeImage& exe)
{
    std::string out = "# Ped clips and behaviours, read from the exe by the engine (Game/exe_tables.cpp).\n"
                      "# clip <id> <name>\n"
                      "# behaviour <BT id> <first clip id> <blend s> <once> <+0xc> <twin> <mask>\n";
    for (int i = 0; i < kClips; ++i)
    {
        const uint8* p = exe.at(kClipNames + 0x30 * static_cast<uint32>(i), 0x30);
        if (!p)
            return std::nullopt;
        if (const std::string name = cString(p, 0x30); !name.empty())
            out += "clip " + std::to_string(i) + ' ' + name + '\n';
    }
    for (int i = 0; i < kBehaviourCount; ++i)
    {
        const uint8* p = exe.at(kBehaviours + 0x18 * static_cast<uint32>(i), 0x18);
        if (!p)
            return std::nullopt;
        uint32 first, once, c, twin, mask;
        float32 blend;
        std::memcpy(&first, p, 4);
        std::memcpy(&blend, p + 4, 4);
        std::memcpy(&once, p + 8, 4);
        std::memcpy(&c, p + 12, 4);
        std::memcpy(&twin, p + 16, 4);
        std::memcpy(&mask, p + 20, 4);
        char line[160];
        std::snprintf(line, sizeof line, "behaviour %d %u %g %u %u %u %u\n", i, first,
                      std::round(static_cast<double>(blend) * 1e4) / 1e4, once, c, twin, mask);
        out += line;
    }
    return out;
}

ExeTables& gameExeSlot()
{
    static ExeTables tables;
    return tables;
}

} // namespace

std::expected<ExeTables, std::string> readExeTables(const std::string& exePath, const std::string& sfxLstPath)
{
    std::ifstream in(Engine::Platform::resolvePath(exePath), std::ios::binary);
    if (!in)
        return std::unexpected("cannot open " + exePath);
    const ExeImage exe(std::vector<uint8>{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});
    if (!exe.valid())
        return std::unexpected(exePath + " is not a Windows executable");
    const std::vector<std::string> names = sfxNames(sfxLstPath);
    if (names.empty())
        return std::unexpected("cannot read the SFX list " + sfxLstPath);

    const std::string other = exePath + " is not the PC 1.0 build of the game this engine reads its tables from";
    ExeTables t;
    t.exePath = exePath;
    auto director = directorSpeech(exe, names);
    auto hunters = hunterSpeech(exe, names);
    auto peds = pedAnims(exe);
    const uint8* keys = exe.at(kDefaultBindings, t.defaultBindings.size());
    if (!director || !hunters || !peds || !keys || !readNames(exe, t))
        return std::unexpected(other);
    t.directorSpeech = std::move(*director);
    t.hunterSpeech = std::move(*hunters);
    t.pedAnims = std::move(*peds);
    std::memcpy(t.defaultBindings.data(), keys, t.defaultBindings.size());
    // The first clip is the sneaking stance in the 1.0 build; another build would place its tables elsewhere.
    if (t.pedAnims.find("\nclip 0 Sneak_Stand\n") == std::string::npos)
        return std::unexpected(other);
    return t;
}

std::expected<ExeTables, std::string> findGameExe(const std::string& gameRoot)
{
    // The game's executable is the one in its folder that holds the tables; its name is not assumed.
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> candidates;
    for (fs::directory_iterator d(gameRoot, ec), end; !ec && d != end; d.increment(ec))
    {
        std::string ext = d->path().extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".exe" && d->is_regular_file(ec))
            candidates.push_back(d->path());
    }
    std::string why = "no game executable in " + gameRoot;
    for (const fs::path& exe : candidates)
    {
        auto t = readExeTables(exe.string(), gameRoot + "audio/PC/SFX/sfx.lst");
        if (t)
            return t;
        why = t.error();
    }
    return std::unexpected(why);
}

const ExeTables& gameExe()
{
    return gameExeSlot();
}

void setGameExe(ExeTables tables)
{
    gameExeSlot() = std::move(tables);
}

bool writeExeTables(const ExeTables& tables, const std::string& dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path d(dir);
    for (const auto& [name, text] : {std::pair{"director_speech.txt", &tables.directorSpeech},
                                     std::pair{"hunter_speech.txt", &tables.hunterSpeech},
                                     std::pair{"ped_anims.txt", &tables.pedAnims}})
    {
        std::ofstream out(d / name, std::ios::binary | std::ios::trunc);
        out << *text;
        if (!out)
            return false;
    }
    return true;
}

} // namespace Game
