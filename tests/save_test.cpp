// tests/save_test.cpp - G00.5 vertical slice: parse a real original save slot 1 with
// Game/save_system, print the entity block, verify structure, and round-trip (write ->
// re-read -> data-equal) to prove our loader+writer understand the original format.
#include "Assets/inst.h"
#include "Game/checkpoint.h"
#include "Game/level_frame.h"
#include "Game/save_system.h"
#include "Game/save_restore.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "Game/exe_tables.h"
#include "test_paths.h"

namespace
{
    bool near(float a, float b, float eps)
    {
        return std::fabs(a - b) <= eps;
    }

    bool recordsEqual(const Game::SaveFile& a, const Game::SaveFile& b)
    {
        if (a.entities.size() != b.entities.size())
            return false;
        for (std::size_t i = 0; i < a.entities.size(); ++i)
        {
            const auto& x = a.entities[i];
            const auto& y = b.entities[i];
            if (x.name != y.name || x.instance != y.instance)
                return false;
            for (int k = 0; k < 3; ++k)
                if (x.pos[k] != y.pos[k])
                    return false;
            if (x.plane != y.plane || x.classData != y.classData)
                return false;
        }
        return true;
    }
}

int main()
{
    if (!TestPaths::haveGameData())
        return TestPaths::skipNoGameData();
    const std::string saveDir = TestPaths::saveDir(); // reads the game's executable: the slot names
    const std::string srcS = saveDir + Game::gameExe().saveNames[1];
    const std::string rtS = TestPaths::scratch() + "save_rt.SAV";
    const char* src = srcS.c_str();
    const char* rt = rtS.c_str();
    if (!std::filesystem::exists(srcS))
    {
        printf("SKIP: no original save at %s (set REELHUNT_SAVE_DIR)\n", src);
        return TestPaths::kSkip;
    }

    Game::SaveFile sf;
    std::vector<std::uint8_t> raw;
    if (!Game::loadSaveFile(src, sf, &raw))
    {
        printf("FAIL: cannot load %s\n", src);
        return 1;
    }
    printf("loaded : %s (%zu bytes)\n", src, raw.size());
    printf("layout : buf1=%zu buf2=%zu buf3=%zu buf4=%zu buf5=%zu buf6=%zu buf7=%zu (total %zu)\n",
           sf.header.size(), Game::SaveLayout::buffer2, sf.buffer3.size(), sf.buffer4.size(),
           sf.buffer5.size(), sf.buffer6.size(), sf.buffer7.size(), Game::SaveLayout::total);
    printf("entities: %zu\n", sf.entities.size());

    int failures = 0;
    auto check = [&](bool cond, const char* what)
    {
        if (!cond)
        {
            ++failures;
            printf("  CHECK FAIL: %s\n", what);
        }
    };

    // Structure assertions.
    check(sf.entities.size() == 353, "entity count == 353");
    check(sf.header.size() == 0x117c, "buffer1 size");
    check(sf.buffer7.size() == 0x130, "buffer7 size");

    // Decode the buffer-1 UTF-16LE slot title at offset 0x10 (0x30 bytes).
    if (sf.header.size() >= 0x40)
    {
        std::string title;
        for (std::size_t i = 0x10; i + 1 < 0x40; i += 2)
        {
            const char c = static_cast<char>(sf.header[i]); // low byte of UTF-16LE
            if (c == 0)
                break;
            title.push_back(c);
        }
        printf("title : '%s'\n", title.c_str());
        check(!title.empty(), "buffer1 has a non-empty UTF-16 slot title");
    }

    // Header metadata: slot words + resolution (this save recorded 0x0, i.e. no active screen).
    printf("res   : %u x %u (slot words %u / %u)\n",
           sf.screenWidth(), sf.screenHeight(), sf.headerWord(4), sf.headerWord(8));
    check(sf.headerWord(4) == 130 && sf.headerWord(8) == 65, "slot words 130 / 65");
    check(sf.screenWidth() == 0 && sf.screenHeight() == 0, "this save records no resolution (0x0)");
    check(sf.header.size() == Game::SaveFile::kHeaderSize + Game::SaveFile::kStateBlockSize,
          "buffer1 == header + state block");

    // Buffer-1 state block (straight copy of the live 0x7d6ae4 integer game-var array): the
    // leading dwords are small ints that are identical across saves of the same level.
    const std::uint32_t expectState[24] = {0, 1, 14, 14, 12, 1, 3, 3, 3, 1, 0, 0,
                                           0, 0, 3, 3, 1, 0, 0, 1, 10, 10, 6, 0};
    bool stateOk = true;
    for (int i = 0; i < 24; ++i)
        stateOk = stateOk && sf.stateDword(i) == expectState[i];
    printf("state : d0..d23 = %u,%u,%u,%u,...,%u,%u,%u,%u\n",
           sf.stateDword(0), sf.stateDword(1), sf.stateDword(2), sf.stateDword(3),
           sf.stateDword(20), sf.stateDword(21), sf.stateDword(22), sf.stateDword(23));
    check(stateOk, "state-block game vars match the oracle (d0..d23)");
    check(sf.stateDword(31) == 0, "state block is zeroed past the live vars (d31==0)");

    // Buffer-2 tail: a second list of static level props (CJ_*) then zero padding. Each prop ends
    // with a "wibble" marker, so the marker count == the number of static props.
    printf("tail  : %zu bytes, %zu static-prop blobs (then zero pad)\n",
           sf.entityTail.size(), sf.staticPropsCount());
    check(sf.entityTail.size() == 0xd800u - (4u + 353u * 4u) - 25436u, "tail length == buffer2 - header - blobs");
    check(sf.staticPropsCount() == 8, "buffer-2 tail holds 8 static-prop entities");

    // The live player entity: world position + orientation quaternion in the 4 plane floats.
    const Game::SaveEntityRecord* p = sf.player();
    check(p != nullptr, "a live 'player' entity exists in buffer 2");
    if (p)
    {
        printf("player: pos=(%.4f,%.4f,%.4f) quat=(%.4f,%.4f,%.4f,%.4f) class=%zu\n",
               p->pos[0], p->pos[1], p->pos[2],
               p->plane.size() >= 4 ? p->plane[0] : 0.f,
               p->plane.size() >= 4 ? p->plane[1] : 0.f,
               p->plane.size() >= 4 ? p->plane[2] : 0.f,
               p->plane.size() >= 4 ? p->plane[3] : 0.f,
               p->classData.size());
        check(near(p->pos[0], 381.94046f, 0.01f) && near(p->pos[1], 68.75463f, 0.01f) &&
                  near(p->pos[2], -7.93881f, 0.01f),
              "player position matches the original save");
        check(p->plane.size() == 4, "player orientation is a 4-float quaternion");
        check(near(p->plane[1], -0.70253f, 0.001f) && near(p->plane[3], 0.71165f, 0.001f),
              "player quaternion y/w match the original save");
        // The player's classData is the breakable 4-byte "current integrity" field. In this save
        // the player has taken damage: 85 (untouched breakables read 100).
        printf("player health: %u (classData[0]; untouched breakables = 100)\n", sf.playerHealth());
        check(sf.playerHealth() == 85, "player health == 85 (took 15 damage)");
        // Every simple breakable prop has the 4-byte health classData == 100.
        const auto* bag = sf.findEntity("Bag_(CT)", "Bag_(CT)02");
        check(bag && bag->classData.size() == 4 && bag->classData[0] == 100,
              "breakable prop classData == 100 (full integrity)");
    }

    if (sf.entities.size() > 1)
    {
        const auto& e0 = sf.entities[0];
        const auto& e1 = sf.entities[1];
        printf("entity[0]: name='%s' inst='%s' pos=(%.3f,%.3f,%.3f) plane=%zu classData=%zu\n",
               e0.name.c_str(), e0.instance.c_str(), e0.pos[0], e0.pos[1], e0.pos[2],
               e0.plane.size(), e0.classData.size());
        check(e0.name == "NUL", "entity[0] is a NUL null-marker");
        check(e0.plane.empty() && e0.classData.empty(), "NUL has no subfields");
        check(e1.name == "Bag_(CT)", "entity[1].name == Bag_(CT)");
        check(e1.instance == "Bag_(CT)02", "entity[1].instance == Bag_(CT)02");
        check(e1.plane.size() == 4, "Bag has 4 plane floats");
        check(e1.classData.size() == 4, "Bag has 4 classData bytes");
        check(near(e1.pos[0], 348.105f, 0.01f) && near(e1.pos[1], 44.744f, 0.01f),
              "Bag pos[0..1] match");
    }

    // Print a spread of entity types.
    printf("\nfirst 14 entities:\n");
    for (std::size_t i = 0; i < sf.entities.size() && i < 14; ++i)
    {
        const auto& e = sf.entities[i];
        printf("  [%2zu] name='%s' inst='%s' pos=(%.3f,%.3f,%.3f) plane=%zu class=%zu\n",
               i, e.name.c_str(), e.instance.c_str(), e.pos[0], e.pos[1], e.pos[2],
               e.plane.size(), e.classData.size());
    }

    // Find a CameraData and a NUL entity.
    bool haveCamera = false, haveNul = false;
    for (const auto& e : sf.entities)
    {
        if (e.name == "CameraData" && e.classData.size() == 20 && e.plane.size() == 4)
            haveCamera = true;
        if (e.name == "NUL")
            haveNul = true;
    }
    check(haveCamera, "a CameraData entity with 20 classData bytes + 4 plane floats");
    check(haveNul, "a NUL (null-marker) entity exists");

    // --- Full entity restore: reconstruct the typed world from the parsed save ---
    {
        const auto ik = [](Game::EntityKind k) { return static_cast<int>(k); };
        const auto world = Game::restoreEntities(sf);
        check(world.size() == sf.entities.size(), "restoreEntities returns one entity per record");

        std::size_t cnt[9] = {0};
        for (const auto& re : world)
            ++cnt[ik(re.state.kind)];
        printf("restore: unknown=%zu player=%zu breakable=%zu savepoint=%zu trigger=%zu "
               "dynamic=%zu camera=%zu corpse=%zu light=%zu\n",
               cnt[ik(Game::EntityKind::Unknown)], cnt[ik(Game::EntityKind::Player)],
               cnt[ik(Game::EntityKind::Breakable)], cnt[ik(Game::EntityKind::SavePoint)],
               cnt[ik(Game::EntityKind::Trigger)], cnt[ik(Game::EntityKind::Dynamic)],
               cnt[ik(Game::EntityKind::CameraData)], cnt[ik(Game::EntityKind::Corpse)],
               cnt[ik(Game::EntityKind::Light)]);
        check(cnt[ik(Game::EntityKind::Player)] == 1, "exactly one player");
        check(cnt[ik(Game::EntityKind::CameraData)] >= 1, "at least one CameraData");
        check(cnt[ik(Game::EntityKind::Light)] >= 1, "at least one (L) light");
        check(cnt[ik(Game::EntityKind::Breakable)] >= 1, "at least one breakable");

        // Restored player: transform + health.
        const Game::RestoredEntity* rp = nullptr;
        for (const auto& re : world)
            if (re.state.kind == Game::EntityKind::Player)
                rp = &re;
        check(rp && Game::isPlayer(rp->name), "restored player found");
        if (rp)
        {
            check(rp->state.integrity == 85, "restored player integrity == 85");
            check(near(rp->position.x, 381.94046f, 0.01f) && near(rp->position.y, 68.75463f, 0.01f) &&
                      near(rp->position.z, -7.93881f, 0.01f),
                  "restored player position matches the original save");
            check(near(rp->rotation.x, 0.0f, 0.001f) && near(rp->rotation.y, -0.70253f, 0.001f) &&
                      near(rp->rotation.w, 0.71165f, 0.001f),
                  "restored player quaternion matches (x/y/w)");
        }

        // A breakable prop: full integrity.
        const Game::RestoredEntity* rb = nullptr;
        for (const auto& re : world)
            if (re.state.kind == Game::EntityKind::Breakable)
                rb = &re;
        check(rb && rb->state.integrity == 100, "restored breakable integrity == 100");

        // CameraData: parsed waypoint fields.
        const Game::RestoredEntity* rc = nullptr;
        for (const auto& re : world)
            if (re.state.kind == Game::EntityKind::CameraData)
                rc = &re;
        if (rc)
        {
            printf("camera: enabled=%u fov=%.3f weight=%.3f interval=%u\n",
                   rc->state.camEnabled, rc->state.camFov, rc->state.camWeight, rc->state.camInterval);
            check(rc->state.camEnabled == 1 &&
                      (rc->state.camInterval == 30 || rc->state.camInterval == 40),
                  "CameraData enabled + interval 30/40");
        }

        // The stable per-archetype type tags.
        check(Game::typeForKind(Game::EntityKind::Player) == 1234u, "player type tag == 1234");
        check(Game::typeForKind(Game::EntityKind::Breakable) == 2001u, "breakable type tag == 2001");
    }

    // --- Write side: writeBack is the inverse of restore. For an untouched world it must be
    // byte-identical to the original (round-trip); for a mutated entity it must persist. ---
    {
        Game::SaveFile wt = sf;                       // a copy to write back into
        const auto wtWorld = Game::restoreEntities(wt);
        for (std::size_t i = 0; i < wt.entities.size(); ++i)
            Game::writeBack(wt.entities[i], wtWorld[i]);
        check(recordsEqual(wt, sf), "writeBack of an untouched world is byte-identical (round-trip)");

        // Mutate the player (move + damage) and one breakable, then confirm the change persists
        // while an unrelated entity is untouched.
        Game::SaveFile mv = sf;
        auto mvWorld = Game::restoreEntities(mv);
        int pIdx = -1, bIdx = -1;
        for (std::size_t i = 0; i < mvWorld.size(); ++i)
        {
            if (mvWorld[i].state.kind == Game::EntityKind::Player)
                pIdx = static_cast<int>(i);
            else if (mvWorld[i].state.kind == Game::EntityKind::Breakable && bIdx < 0)
                bIdx = static_cast<int>(i);
        }
        check(pIdx >= 0 && bIdx >= 0, "found a player and a breakable to mutate");
        if (pIdx >= 0)
        {
            mvWorld[pIdx].position = Math::Vector3{42.5f, 100.0f, -3.25f};
            mvWorld[pIdx].rotation = Math::Quaternion::identity(); // face forward (w=1)
            mvWorld[pIdx].state.integrity = 50;
            Game::writeBack(mv.entities[pIdx], mvWorld[pIdx]);
            check(mv.entities[pIdx].pos[0] == 42.5f && mv.entities[pIdx].pos[1] == 100.0f &&
                      mv.entities[pIdx].pos[2] == -3.25f,
                  "player transform edit persisted via writeBack");
            check(mv.entities[pIdx].plane.size() == 4 && mv.entities[pIdx].plane[3] == 1.0f &&
                      mv.entities[pIdx].plane[0] == 0.0f,
                  "player quaternion edit persisted via writeBack");
            check(mv.entities[pIdx].classData[0] == 50 && mv.entities[pIdx].classData[1] == 0,
                  "player health edit (50) persisted into classData[0]");
        }
        if (bIdx >= 0)
        {
            mvWorld[bIdx].state.integrity = 73;
            Game::writeBack(mv.entities[bIdx], mvWorld[bIdx]);
            check(mv.entities[bIdx].classData[0] == 73 && mv.entities[bIdx].classData.size() == 4,
                  "breakable health edit (73) persisted into classData[0]");
        }
        // A CameraData entity is non-integrity: its classData must be preserved verbatim.
        int cIdx = -1;
        for (std::size_t i = 0; i < mvWorld.size(); ++i)
            if (mvWorld[i].state.kind == Game::EntityKind::CameraData)
                cIdx = static_cast<int>(i);
        if (cIdx >= 0)
        {
            const auto before = mv.entities[cIdx].classData;
            mvWorld[cIdx].position = Math::Vector3{9.0f, 9.0f, 9.0f};
            Game::writeBack(mv.entities[cIdx], mvWorld[cIdx]);
            check(mv.entities[cIdx].classData == before,
                  "CameraData classData preserved verbatim on writeBack (non-integrity kind)");
            check(mv.entities[cIdx].pos[0] == 9.0f, "CameraData transform still updated on writeBack");
        }
    }

    // --- Buffer 3: the player's inventory (exe 0x5d24c0 / 0x5d2650). The slot 1 save was made
    // carrying a bag (HOLSTER_SLOT BELT_LEFT = slot 3) and nothing else. ---
    {
        const auto inv = sf.inventory();
        check(inv[0].record.empty() && inv[1].record.empty() && inv[2].record == "Bag_(CT)" && inv[3].record.empty() &&
                  inv[0].rounds == -1 && inv[2].rounds == -1 && inv[3].rounds == -1,
              "buffer 3: slot 3 (BELT_LEFT) holds Bag_(CT), rounds -1; slots 1, 2, 4 empty");
        Game::SaveFile same = sf;
        same.setInventory(inv);
        check(same.buffer3 == sf.buffer3, "writing the same inventory back leaves buffer 3 byte-identical");
        Game::SaveFile armed = sf;
        auto slots = inv;
        slots[1] = {"Nail_Gun_(CT)", 25}; // BELT_RIGHT
        slots[2] = {};                    // the bag used up
        armed.setInventory(slots);
        const std::string armedS = TestPaths::scratch() + "save_armed.SAV";
        Game::SaveFile back;
        const bool written = Game::writeSaveFile(armedS, armed) && Game::loadSaveFile(armedS, back);
        const auto got = back.inventory();
        check(written && got[1].record == "Nail_Gun_(CT)" && got[1].rounds == 25 && got[2].record.empty() &&
                  got[2].rounds == -1 && back.buffer3.size() == 0xa8 && recordsEqual(sf, back),
              "an inventory written and read back: the nail gun's 25 rounds in slot 2, slot 3 emptied, entities untouched");
    }

    // Round-trip: write -> re-read -> data-equal.
    if (!Game::writeSaveFile(rt, sf))
    {
        printf("FAIL: cannot write %s\n", rt);
        return 1;
    }
    Game::SaveFile rt2;
    if (!Game::loadSaveFile(rt, rt2))
    {
        printf("FAIL: cannot re-read round-trip %s\n", rt);
        return 1;
    }
    check(recordsEqual(sf, rt2), "round-trip records identical");
    check(sf.entityTail == rt2.entityTail, "round-trip entityTail byte-exact");
    printf("entityTail: %zu bytes preserved\n", sf.entityTail.size());

    // Informational: how many raw bytes differ vs the original (expect padding-only).
    std::ifstream f2(rt, std::ios::binary);
    std::vector<std::uint8_t> rtRaw((std::istreambuf_iterator<char>(f2)),
                                    std::istreambuf_iterator<char>());
    std::size_t diff = 0;
    std::size_t minLen = raw.size() < rtRaw.size() ? raw.size() : rtRaw.size();
    for (std::size_t i = 0; i < minLen; ++i)
        if (raw[i] != rtRaw[i])
            ++diff;
    printf("\nround-trip raw byte diff vs original: %zu / %zu (padding-only expected)\n",
           diff, minLen);

    // --- Semantic round-trip: mutate the live player, write, reload, verify the change
    // persisted while every other entity / header field / buffer is preserved. ---
    const std::string mvS = TestPaths::scratch() + "save_moved.SAV";
    const char* mv = mvS.c_str();
    {
        Game::SaveFile m = sf;
        if (Game::SaveEntityRecord* pm = m.player())
        {
            pm->pos[0] = 42.5f;                 // move the player
            pm->pos[1] = 100.0f;
            pm->pos[2] = -3.25f;
            if (pm->plane.size() == 4)
                pm->plane = {0.0f, 0.0f, 0.0f, 1.0f}; // face-forward identity quaternion
        }
        m.setTitle("9)TEST SLOT-EDITED");
        m.setResolution(800, 600);
        if (!Game::writeSaveFile(mv, m))
        {
            printf("FAIL: cannot write moved save %s\n", mv);
            return 1;
        }
        Game::SaveFile back;
        if (!Game::loadSaveFile(mv, back))
        {
            printf("FAIL: cannot reload moved save %s\n", mv);
            return 1;
        }
        const Game::SaveEntityRecord* bp = back.player();
        check(bp != nullptr, "moved save still has the player entity");
        if (bp)
        {
            printf("moved : pos=(%.3f,%.3f,%.3f) quat=(%.3f,%.3f,%.3f,%.3f)\n",
                   bp->pos[0], bp->pos[1], bp->pos[2],
                   bp->plane.size() >= 4 ? bp->plane[0] : 0.f,
                   bp->plane.size() >= 4 ? bp->plane[1] : 0.f,
                   bp->plane.size() >= 4 ? bp->plane[2] : 0.f,
                   bp->plane.size() >= 4 ? bp->plane[3] : 0.f);
            check(bp->pos[0] == 42.5f && bp->pos[1] == 100.0f && bp->pos[2] == -3.25f,
                  "player position edit persisted through save/load");
            check(bp->plane.size() == 4 && bp->plane[3] == 1.0f && bp->plane[0] == 0.0f,
                  "player quaternion edit persisted");
        }
        check(back.title() == "9)TEST SLOT-EDITED", "title edit persisted");
        check(back.screenWidth() == 800 && back.screenHeight() == 600, "resolution edit persisted");
        check(back.entities.size() == sf.entities.size(), "entity count preserved after edit");
        // A non-player entity (e.g. the first Bag) must be untouched by the player edit.
        const auto* b1 = back.findEntity("Bag_(CT)", "Bag_(CT)02");
        const auto* o1 = sf.findEntity("Bag_(CT)", "Bag_(CT)02");
        check(b1 && o1 && b1->pos[0] == o1->pos[0] && b1->pos[1] == o1->pos[1] &&
                  b1->pos[2] == o1->pos[2] && b1->classData == o1->classData,
              "a non-player entity is byte-identical after the player edit");
    }

    // Backlog D: the level built from this checkpoint (Jury_Turf) writes the same block back.
    {
        std::vector<Assets::InstPlacement> placements;
        int32 count = 0;
        for (const char* name : {"entity.inst", "entity2.inst"})
            if (auto inst = Assets::Inst::load(TestPaths::level("jury_turf") + name))
                for (Assets::InstPlacement& p : *inst)
                {
                    p.index = count++;
                    placements.push_back(p);
                }
        check(count == static_cast<int32>(sf.entities.size()), "one record per .inst placement, in their order (0x43a2c0)");
        Game::Checkpoint::apply(placements, sf.entities);
        std::size_t nul = 0;
        for (const auto& e : sf.entities)
            nul += e.name == "NUL" ? 1 : 0;
        check(placements.size() + nul == sf.entities.size(), "the NUL records' placements are gone (0x437650(0))");
        int mismatches = 0;
        for (const Assets::InstPlacement& p : placements)
        {
            Game::SaveEntityRecord r = Game::Checkpoint::record(p);
            const Game::SaveEntityRecord& s = sf.entities[static_cast<std::size_t>(p.index)];
            if (p.entityClass == "Door_Inst")
                Game::Checkpoint::setWord(r, 2, Game::Checkpoint::word(s, 2)); // the live lock
            bool same = r.name == s.name && r.instance == s.instance && r.classData == s.classData &&
                        r.pos[0] == s.pos[0] && r.pos[1] == s.pos[1] && r.pos[2] == s.pos[2] && r.plane.size() == s.plane.size();
            for (std::size_t k = 0; same && k < r.plane.size(); ++k)
                same = std::fabs(r.plane[k] - s.plane[k]) < 1e-5f;
            if (!same && ++mismatches <= 3)
                printf("  differs: %s %s\n", s.name.c_str(), s.instance.c_str());
        }
        check(mismatches == 0, "every record written back as the checkpoint has it (class data, position, rotation)");
        const std::vector<std::uint8_t> global = Game::Checkpoint::globalScriptData(TestPaths::gameRoot() + "levels/GLOBAL/game.scc");
        check(global.size() == sf.buffer4.size() && std::equal(global.begin(), global.begin() + 40, sf.buffer4.begin()),
              "buffer 4 starts as game.scc's data (the global script's strings)");
    }

    // A script's spawn (SpawnEntityWithDirection): its scene point and facing as a placement,
    // read back the way placed characters are.
    {
        Game::LevelFrame frame;
        frame.center = {12.0f, -3.0f, 40.0f};
        frame.scale = 1.0f;
        const Math::Vector3 scene{2.4375f, 5.686f, -45.476f};
        const Math::Vector3 inst = Game::Checkpoint::instPosition(frame, scene);
        const Math::Vector3 back = frame.placementOrigin(inst);
        check(std::fabs(back.x - scene.x) < 1e-4f && std::fabs(back.y - scene.y) < 1e-4f && std::fabs(back.z - scene.z) < 1e-4f,
              "a spawn's position: scene -> .inst -> scene");
        bool yaws = true;
        for (float yaw : {0.0f, 1.0f, -2.5f, 3.0f})
        {
            const Math::Vector4 q = Game::Checkpoint::instYawRotation(yaw - 3.14159265358979f);
            const float d = std::remainder(Game::LevelFrame::characterYaw(q) - yaw, 6.28318530718f);
            yaws = yaws && std::fabs(d) < 1e-4f;
        }
        check(yaws, "a spawn's facing: characterYaw reads back the yaw it was given");
    }

    printf("\n%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
