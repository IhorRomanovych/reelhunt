// Game/save_system.h - the game save file (*.SAV) read/write.
//
// A save is 7 concatenated raw buffers (sum 0x10ff4 = 69620 bytes):
//   buffer 1 (0x117c) = a 0x9c-byte header (slot u32s, UTF-16LE slot title, date, and the
//                        640x480 resolution) followed by a 0x10e0-byte "state block" that is a
//                        straight copy of the live game-state array 0x7d6ae4 (integer game vars
//                        and flags -- no floats; the player transform is NOT here).
//   buffer 2 (0xd800) = the "entity block": a u32 count, a u32 per-entity byte-size array, then
//                        the concatenated entity blobs, then a trailing raw region. Each blob is:
//                        name (C-str, 4-aligned) + instance (C-str, 4-aligned) + 3 position floats
//                        + a run of "plane" floats + "wibble" (C-str, 4-aligned) + trailing
//                        class-specific bytes. The plane run is delimited by the "wibble" marker
//                        because its length varies per class (4 floats = a quaternion for most,
//                        incl. the live "player" entity; up to 5 for the Bag).
//   buffers 3..7      = fixed-size subsystem snapshots (0xa8, 0x400, 0x2000, 0xa0, 0x130).
//
// The live player is serialized as the entity with name=="player" && instance=="player": its
// world position is `pos` and its orientation is the 4 plane floats (a quaternion).
//
// We parse the meaningful fields and preserve the class-specific tails opaquely, so a loaded
// save re-serializes byte-for-byte (modulo padding, which the original's own parser ignores)
// and reconstructs the same state. The SaveFile is therefore the canonical in-memory game
// state: read `player()` / `entities` / `title()` on load, mutate them, and write back.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Game
{
    // One inventory slot as buffer 3 keeps it (exe 0x5d24c0 writes it, 0x5d2650 reads it
    // back): the player's inventory slots 1..4 (the HOLSTER_SLOTs BACK, BELT_RIGHT, BELT_LEFT,
    // BELT_REAR; slot 0, fists, and 5, SPECIAL, are not saved) as 0x1c-byte records at 0x1c * slot:
    // the item's entityTypeData record name (C string, up to 0x18 bytes) and at +0x18 a gun's rounds
    // (clip + spare), -1 for anything else and for an empty slot (name "").
    struct SaveInventorySlot
    {
        std::string record;   // "" = empty
        std::int32_t rounds = -1;
    };

    // One entity record as serialized in buffer 2 of a *.SAV file.
    struct SaveEntityRecord
    {
        std::string name;                   // entity class / node name, e.g. "Bag_(CT)"
        std::string instance;               // per-instance id, e.g. "Bag_(CT)02"
        float pos[3] = {0.0f, 0.0f, 0.0f};  // world position (reordered from live transform)
        std::vector<float> plane;           // 3-5 floats derived from the transform (per class)
        std::vector<std::uint8_t> classData; // trailing class-specific bytes (opaque)
    };

    // The 7 concatenated raw buffers of a *.SAV file. This is the canonical in-memory
    // representation of a game save: header + entities (+ raw tail) + the 5 subsystem buffers.
    struct SaveFile
    {
        std::vector<std::uint8_t> header;            // buffer 1 (0x117c): 0x9c header + 0x10e0 state block
        std::vector<SaveEntityRecord> entities;      // buffer 2 (0xd800): parsed entity block
        std::vector<std::uint8_t> entityTail;        // buffer 2 remainder after the blobs (raw)
        std::vector<std::uint8_t> buffer3;           // 0xa8
        std::vector<std::uint8_t> buffer4;           // 0x400
        std::vector<std::uint8_t> buffer5;           // 0x2000
        std::vector<std::uint8_t> buffer6;           // 0xa0
        std::vector<std::uint8_t> buffer7;           // 0x130
        // Not in the file: the director's counts (X, H, S, KT) at the save point. The exe keeps
        // this copy in memory (0x5b5e70, from the save point 0x4d2639) and puts it back when the
        // checkpoint is resumed in the same run (0x5b5a60); a save loaded from disk starts at 0.
        std::optional<std::array<std::int32_t, 4>> directorCounts;

        // --- Buffer-1 header layout ---
        static constexpr std::size_t kHeaderSize       = 0x9c;   // metadata header (before the state block)
        static constexpr std::size_t kStateBlockOffset = 0x9c;   // start of the 0x10e0 game-state array
        static constexpr std::size_t kStateBlockSize   = 0x10e0; // = (buffer1 - kHeaderSize)
        static constexpr std::size_t kTitleOffset      = 0x10;   // UTF-16LE slot title (0x30 bytes / 24 chars)

        // Re-serialize the entity records into the raw buffer-2 byte stream. The result is
        // [count][sizes][blobs][entityTail]; writeSaveFile fits it to the fixed buffer-2 size.
        std::vector<std::uint8_t> encodeEntityBlock() const;

        // --- Header accessors (buffer 1, [0, kHeaderSize)) ---
        std::uint32_t headerWord(std::size_t off) const;       // little-endian u32 at header[off]
        void setHeaderWord(std::size_t off, std::uint32_t v);  // write a little-endian u32 at header[off]
        std::string title() const;                             // UTF-16LE slot title -> UTF-8 (ASCII)
        void setTitle(const std::string& t);                   // write the title (zero-padded to 24 chars)
        // Screen resolution, stored as a (width,height) u32 pair at 0x78/0x7c (the original also
        // duplicates it at 0x88/0x8c). It is 0 in saves made without an active screen, so callers
        // should treat (0,0) as "not recorded" rather than a literal resolution.
        std::uint32_t screenWidth()  const { return headerWord(0x78); }
        std::uint32_t screenHeight() const { return headerWord(0x7c); }
        void setResolution(std::uint32_t w, std::uint32_t h)
        {
            setHeaderWord(0x78, w);
            setHeaderWord(0x7c, h);
            setHeaderWord(0x88, w); // mirror copy the original keeps in sync
            setHeaderWord(0x8c, h);
        }
        // The 0x10e0-byte live game-state array (integer vars/flags), at header[kStateBlockOffset].
        // nullptr when the header is not a full buffer 1.
        const std::uint8_t* stateBlock() const { return header.size() >= kStateBlockOffset + kStateBlockSize ? header.data() + kStateBlockOffset : nullptr; }
        std::uint8_t* stateBlock()             { return header.size() >= kStateBlockOffset + kStateBlockSize ? header.data() + kStateBlockOffset : nullptr; }
        // The integer game-var / flag at state-block dword index `i` (0..1079). The block is the
        // straight-copied live array 0x7d6ae4; its leading dwords are small ints that are identical
        // across saves made in the same level (e.g. d0..d3 = 0,1,14,14; d20,d21 = 10,10).
        std::uint32_t stateDword(std::size_t i) const { return headerWord(kStateBlockOffset + i * 4u); }
        void setStateDword(std::size_t i, std::uint32_t v) { setHeaderWord(kStateBlockOffset + i * 4u, v); }
        // Number of static-prop entity blobs in the buffer-2 tail (each terminates with a "wibble"
        // marker). These are level set-dressing objects (e.g. CJ_Bin_Liner, CJ_BOX, CJ_TV_TRUCK)
        // serialized after the main entity list, followed by zero padding to fill buffer 2.
        std::size_t staticPropsCount() const;

        // --- Buffer 3: the player's inventory (slots 1..4, see SaveInventorySlot) ---
        static constexpr std::size_t kInventorySlots = 4;
        static constexpr std::size_t kInventoryRecord = 0x1c;
        std::array<SaveInventorySlot, kInventorySlots> inventory() const; // [0] = slot 1
        // Writes the slots as the original does: the name with its terminator over what was there
        // (bytes past it are left as they were), the rounds at +0x18.
        void setInventory(const std::array<SaveInventorySlot, kInventorySlots>& slots);

        // --- Entity access ---
        // Finds the first entity whose `name` matches and whose `instance` matches (or, when
        // `instance` is empty, any). Returns null when absent.
        const SaveEntityRecord* findEntity(const std::string& name,
                                           const std::string& instance = "") const;
        SaveEntityRecord* findEntity(const std::string& name, const std::string& instance = "");
        // The live player entity (name=="player" && instance=="player"); null when not present.
        // Its `pos` is the world position and its 4 `plane` floats are the orientation quaternion.
        const SaveEntityRecord* player() const { return findEntity("player", "player"); }
        SaveEntityRecord* player() { return findEntity("player", "player"); }
        // The player's current health, stored as the single u32 in the player entity's classData
        // (the "breakable" 4-byte classData = current integrity; full breakables read 100, so the
        // player's 85 means it has taken 15 damage). Returns 0 when the player is absent or has no
        // classData.
        std::uint32_t playerHealth() const
        {
            const SaveEntityRecord* p = player();
            if (!p || p->classData.size() < 4u)
                return 0;
            return static_cast<std::uint32_t>(p->classData[0]) |
                   (static_cast<std::uint32_t>(p->classData[1]) << 8) |
                   (static_cast<std::uint32_t>(p->classData[2]) << 16) |
                   (static_cast<std::uint32_t>(p->classData[3]) << 24);
        }
    };

    // Parses a *.SAV file. Returns false (and leaves `out` unchanged) on any
    // structural error. `rawOut` (if non-null) receives the full file bytes.
    bool loadSaveFile(const std::string& path, SaveFile& out, std::vector<std::uint8_t>* rawOut = nullptr);

    // Writes `in` as a *.SAV file (7-buffer layout). The file is written to a temporary
    // name and then renamed over `path`, so a failed write never truncates an existing save.
    // Returns false if the buffers do not fit the fixed layout or the write fails.
    bool writeSaveFile(const std::string& path, const SaveFile& in);
    // Before the engine first writes over a file in the game's save folder: a copy of it as
    // <path>.bak, kept once (ours: the original game's saves and settings stay recoverable).
    void keepBackup(const std::string& path);

    // Inventory slots 1..4 as 0x1c-byte records at 0x1c * slot from `base` (`size` bytes): a
    // save's buffer 3, or a scene record's carried inventory (+8, 0x5d24c0 / 0x5d2650 read and
    // write both the same way).
    std::array<SaveInventorySlot, 4> readInventory(const std::uint8_t* base, std::size_t size);
    void writeInventory(std::uint8_t* base, std::size_t size, const std::array<SaveInventorySlot, 4>& slots);

    // Fixed buffer sizes (bytes) of the 7 concatenated save buffers.
    struct SaveLayout
    {
        static constexpr std::size_t buffer1 = 0x117c;
        static constexpr std::size_t buffer2 = 0xd800;
        static constexpr std::size_t buffer3 = 0xa8;
        static constexpr std::size_t buffer4 = 0x400;
        static constexpr std::size_t buffer5 = 0x2000;
        static constexpr std::size_t buffer6 = 0xa0;
        static constexpr std::size_t buffer7 = 0x130;
        static constexpr std::size_t total = buffer1 + buffer2 + buffer3 + buffer4 +
                                             buffer5 + buffer6 + buffer7;
    };
}
