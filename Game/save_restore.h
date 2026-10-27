// Game/save_restore.h - reconstruct live game state from a parsed *.SAV.
//
// save_system.h parses a save into opaque records (name / instance / pos / plane / classData).
// This layer turns those records into a typed, engine-consumable form:
//   * classifyEntity  - the save's closed set of archetypes, decoded from the node name
//   * parseClassData  - the per-class classData bytes -> structured state
//   * restoreEntities - every saved entity -> a RestoredEntity (transform + state)
//   * applyToEntity   - push a RestoredEntity onto a live GameEntity (pos / rotation / health)
//
// The live wiring (applyToEntity) is kept in a separate TU (save_restore_live.cpp) so the
// pure-data restore stays linkable into save_test without pulling in the Game subsystem.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Game/save_system.h"
#include "Math/vector.h"
#include "Math/quaternion.h"

namespace Game { class GameEntity; } // forward decl (live apply lives in save_restore_live.cpp)

namespace Game
{
    // The save stores a fixed, closed set of entity archetypes. The node name (and its suffix)
    // decodes which one, which in turn fixes the classData layout (see PLAN.md, G00.5).
    enum class EntityKind : std::uint8_t
    {
        Unknown = 0,
        Player,       // 4 B: u32 currentIntegrity (health); pos + 4-float orientation quaternion
        Breakable,    // 4 B: u32 currentIntegrity (full = 100)
        SavePoint,    // 8 B: u32 state + f32
        Trigger,      // 8 B: u32 state + f32
        Dynamic,      // 12 B: u32 integrity + 2 u32 state (open / closed / activated)
        CameraData,   // 20 B: camera waypoint settings
        Corpse,       // 36 B: u32 integrity + rigid-body / physics state
        Light,        // 60 B: 15 dwords (flags + color RGB + range + type)
    };

    // Per-entity serializable state, parsed from classData by kind. These are the fields the
    // original's restoreClassData (vtable+8) consumes. `raw` keeps the opaque bytes so a
    // round-trip stays byte-exact and the not-yet-pinned layouts (corpse rigid-body, light
    // params) stay available.
    struct EntityState
    {
        EntityKind kind = EntityKind::Unknown;
        std::uint32_t integrity = 0;     // breakable / player / corpse: current integrity (full = 100)
        std::uint32_t state = 0;         // save point / trigger: activation state
        float         stateFloat = 0.0f; // save point (4.0) / trigger (1.0) paired float
        std::uint32_t dynState0 = 0;     // dynamic: first state word (open / closed)
        std::uint32_t dynState1 = 0;     // dynamic: second state word (activated)
        // Camera waypoint (20 B): [enabled, pad, fov ~3.2f, weight ~1.0f, interval 30/40].
        std::uint32_t camEnabled = 0;
        float         camFov = 0.0f;
        float         camWeight = 0.0f;
        std::uint32_t camInterval = 0;
        std::vector<std::uint8_t> raw;   // the full classData, preserved verbatim
    };

    // A fully-restored entity: identity + world transform + parsed state.
    struct RestoredEntity
    {
        std::string name;
        std::string instance;
        Math::Vector3    position = Math::Vector3::zero();
        Math::Quaternion rotation = Math::Quaternion::identity();
        EntityState      state;
    };

    // True when the node name is the live player entity (name == "player").
    bool isPlayer(const std::string& name);

    // True for the kinds whose classData starts with a u32 current integrity (health).
    bool hasIntegrity(EntityKind kind);

    // Classify a saved entity into an EntityKind from its name + classData size.
    EntityKind classifyEntity(const std::string& name, std::size_t classDataSize);

    // Parse the per-class classData bytes into structured state (dispatches on classifyEntity).
    EntityState parseClassData(const std::string& name, const std::vector<std::uint8_t>& classData);

    // Reconstruct every entity in a SaveFile into a structured, engine-consumable list. The
    // transform comes from pos + the 4 plane floats (the orientation quaternion); the state is
    // parsed from classData. Order matches the save's entity block.
    std::vector<RestoredEntity> restoreEntities(const SaveFile& save);

    // The inverse of restore: write a RestoredEntity's transform + integrity back into its
    // SaveEntityRecord (pos, the 4 plane floats, and classData[0] for the integrity-bearing
    // kinds; the rest of classData is preserved verbatim). Pure-data; testable without the Game
    // subsystem. For an entity we restored and then left untouched this is byte-identical to the
    // original, so it doubles as a round-trip check.
    void writeBack(SaveEntityRecord& rec, const RestoredEntity& re);

    // The type tag a live entity should report for a given kind (a stable identity per archetype).
    std::uint32_t typeForKind(EntityKind kind);

    // Push a RestoredEntity's transform + state onto a live GameEntity (position, rotation and,
    // for the integrity-bearing kinds, health). Lives in save_restore_live.cpp.
    void applyToEntity(Game::GameEntity& ent, const RestoredEntity& re);

    // The live inverse of applyToEntity: capture a GameEntity's transform + health into its
    // SaveEntityRecord (the F5 save path). Lives in save_restore_live.cpp.
    void writeBack(SaveEntityRecord& rec, const Game::GameEntity& ent);
}
