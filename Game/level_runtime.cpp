// Game/level_runtime.cpp - a running level's gameplay. See level_runtime.h.
#include "Game/level_runtime.h"
#include "Core/platform.h"
#include "Game/material_types.h"

#include "Assets/pak.h"
#include "Game/ai/stealth.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <bit>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>

#include "Assets/grf.h"
#include "Assets/ini_records.h"
#include "Assets/gxt.h"
#include "Assets/txd.h"
#include <map>
#include "Game/pickup_halo.h"
#include "Assets/mls.h"
#include "Game/ai/ai_natives.h"
#include "Game/ai/gun_combat.h"
#include "Game/checkpoint.h"
#include "Game/level_content.h"
#include "Game/player_field.h"
#include "Game/weapon_strip.h"
#include "Math/math.h"

namespace Game {

namespace {

using Script::NativeCall;
using Script::Value;

constexpr int16 kHeadBoneId = 1040;

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

float32 flatDistance(const Math::Vector3& a, const Math::Vector3& b)
{
    const float32 dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// Horizontal distance from p to the segment a-b.
float32 segmentDistance(const Math::Vector3& p, const Math::Vector3& a, const Math::Vector3& b)
{
    const float32 abx = b.x - a.x, abz = b.z - a.z;
    const float32 len2 = abx * abx + abz * abz;
    float32 t = len2 > 1e-6f ? ((p.x - a.x) * abx + (p.z - a.z) * abz) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float32 dx = p.x - (a.x + abx * t), dz = p.z - (a.z + abz * t);
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Loading

void LevelRuntime::load(const Setup& s)
{
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    auto msSince = [](Clock::time_point t) { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count(); };
    setup_ = s;
    if (!s.hunterSpeechPath.empty() && !hunterSpeech_.load(s.hunterSpeechPath))
        std::cerr << "runtime: no hunter speech table (" << s.hunterSpeechPath << ")\n";
    std::vector<Assets::IniRecord> entityTypes;
    if (auto pak = Assets::PakFile::load(s.pakPath))
    {
        if (const auto text = pak->file("levels/" + s.levelName + "/entityTypeData.ini"))
            entityTypes = Assets::parseIniRecords(*text);
        if (const auto text = pak->file("levels/global/tvp/TimedVectorPairs.ini"))
            tvp_ = Assets::parseIniRecords(*text);
        std::cerr << "runtime: " << tuning_.read(*pak, s.levelName) << "\n";
        loadFx(*pak);
        levelSetup_ = LevelSetup::read(*pak, s.levelName);
        std::cerr << "runtime: level setup: " << levelSetup_.summary() << "\n";
        if (const auto ini = pak->file("levels/" + s.levelName + "/spl/splines.ini");
            ini && splines_.load(*ini, s.levelDir + "spl/splines.spl"))
            std::cerr << "runtime: camera splines " << splines_.size() << "\n";
    }
    else
        std::cerr << "runtime: no tuning pak (" << pak.error().message << "), shipped AI defaults\n";
    playerHealth_ = s.playerHealth > 0.0f ? s.playerHealth : tuning_.playerHitPoints;
    hardDifficulty_ = s.hardDifficulty;
    if (text_.load(s.levelDir, s.levelName, s.textSuffix))
        std::cerr << "runtime: level text loaded\n";
    doors_.build(*s.level);
    // Each door's MATERIAL (its record's, for its sounds).
    for (std::size_t i = 0; i < doors_.size(); ++i)
        if (const auto it = tuning_.propPhysics.find(doors_.door(i).record); it != tuning_.propPhysics.end())
            doors_.door(i).material = it->second.material;
    for (const Assets::InstPlacement& p : s.level->placements())
        if (LevelContent::isMover(p.record))
        {
            Mover m;
            m.name = lower(p.internalName);
            m.placement = p.index;
            m.origin = s.level->frame().placementOrigin(p.position);
            m.meshes = s.level->meshesOf(p);
            movers_.push_back(std::move(m));
        }
    buildDamageProps(entityTypes);
    buildPhysicsProps();
    buildUseables(entityTypes);
    for (const Assets::IniRecord& r : entityTypes)
        if (r.line("GAS_CAN"))
            gasCanRecords_.insert(lower(r.name));
    buildLadders(entityTypes);
    if (const Assets::IniRecord* r = Assets::findRecord(entityTypes, "Gen_Save_Point"))
        savePointModel_ = r->text("MODEL").value_or(savePointModel_);
    auto global = Assets::Gxt::load(s.gameRoot + "levels/GLOBAL/PC_TEXT/pc_game" + s.globalTextSuffix + ".gxt");
    if (!global)
        global = Assets::Gxt::load(s.gameRoot + "levels/GLOBAL/PC_TEXT/pc_game.gxt");
    if (global)
    {
        checkpointText_ = global->text("CHEKPNT");
        swapPrompt_ = global->text("SWAPINV");
        // The name of each collectable type (0x4c5870: CT -> its global-text key).
        static constexpr std::pair<int32, const char*> kNames[] = {
            {2, "GASTANK"},  {8, "PAINKIL"},  {9, "PAINKIL"},  {16, "FISTS"},   {18, "KNIFE"},   {19, "G_SHARD"},
            {22, "BOTTLE"},  {24, "MEAT_C"},  {26, "CROWBAR"}, {27, "SICKLE"},  {28, "NITSTIK"}, {30, "ICE_PIK"},
            {31, "ICE_PIK"}, {32, "MACHETE"}, {33, "BLAKJAK"}, {34, "WBASBAT"}, {35, "WBASBAT"}, {38, "BAT_SPI"},
            {39, "REVLVR"},  {40, "GLOCK"},   {43, "SHOTGUN"}, {44, "SHOTGUN"}, {45, "SHO_TOR"}, {46, "D_EAGLE"},
            {47, "COLT_C"},  {48, "SNI_RIF"}, {49, "TRANQ_R"}, {50, "SAWNOFF"}, {56, "BRICK"},   {58, "PLA_BAG"},
            {70, "CHAINSW"}, {81, "CHAINSW"}, {83, "CHAINSW"}, {88, "CHAINSW"}, {89, "NAILGUN"}, {90, "SPIKE"},
            {91, "SPIKE"},   {92, "SPIKE"},   {96, "SPIKE"},   {93, "SSNIRIF"}, {94, "PIG_SHA"}, {95, "PIG_WIR"},
            {97, "HAMMER"},  {98, "DOLL_1"},  {99, "DOLL_2"},  {100, "DOLL_3"}, {101, "NAILS"},  {102, "NAILS"},
            {103, "SHELLS"}, {104, "HB_BUL"}, {105, "A_CLIP"}, {106, "TDARTS"}, {107, "RIFLE_A"}, {108, "CHAINSW"},
            {109, "DVTAPE"}, {110, "HANDCAM"}};
        for (const auto& [ct, key] : kNames)
            if (global->contains(key))
                itemNames_[ct] = global->text(key);
    }
    if (auto grf = Assets::Grf::load(s.levelDir + "mapAI.grf"))
        nav_ = NavGraph(*grf, s.level->frame());
    else
        std::cerr << "runtime: no AI graph (" << grf.error().message << ")\n";

    // Every named thing the scripts may ask for.
    addEntity(s.levelName, Kind::Level, {0.0f, 0.0f, 0.0f});
    playerHandle_ = addEntity("player", Kind::Player, {0.0f, 0.0f, 0.0f});
    for (const Assets::InstPlacement& p : s.level->placements())
    {
        if (p.entityClass == "Player_Inst" || p.entityClass == "Hunter_Inst")
            continue;
        const Math::Vector3 pos = s.level->frame().placementOrigin(p.position);
        const uint32 h = addEntity(p.internalName, p.entityClass == "Trigger_Inst" ? Kind::Trigger : Kind::Placement, pos);
        entity(h)->placement = p.index;
        if (p.entityClass == "Trigger_Inst")
        {
            Trigger t;
            t.name = lower(p.internalName);
            t.placement = p.index;
            t.savePoint = Assets::iequals(p.record, "Gen_Save_Point");
            if (t.savePoint)
                std::cerr << "runtime: save point " << p.internalName << " at (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
            t.a = pos;
            t.origin = pos;
            t.radius = p.params.size() > 1 ? std::clamp(std::bit_cast<float32>(p.params[1]), 0.5f, 50.0f) : 1.0f;
            entity(h)->trigger = static_cast<int32>(triggers_.size());
            triggers_.push_back(t);
        }
    }
    // The baked light at every graph node (search_parameters DARKNODE_%).
    nodeLight_.assign(static_cast<std::size_t>(nav_.size()), 1.0f);
    for (int32 n = 0; n < static_cast<int32>(nav_.size()); ++n)
        if (const auto l = s.field->lightBelow(nav_.node(n).position + Math::Vector3{0.0f, 0.5f, 0.0f}))
            nodeLight_[static_cast<std::size_t>(n)] = (l->x + l->y + l->z) / 3.0f;

    const auto t1 = Clock::now();
    if (!Script::loadConstants(s.constantsPath, constants_))
        std::cerr << "runtime: script constants missing (" << s.constantsPath << ")\n";
    items_.read(entityTypes, constants_); // hunters carry items too
    for (const Assets::IniRecord& r : entityTypes)
        if (const auto block = r.text("ANIMATION_BLOCK"))
            animBlocks_[lower(r.name)] = *block;
    spawnHunters();
    const auto spawnMs = msSince(t1);

    const auto t2 = Clock::now();
    Assets::MlsFile mls;
    if (mls.load(s.levelDir + "scripts/" + s.levelName + ".mls"))
    {
        scripts_.load(mls, constants_);
        // The level program (et_level) and its data, for a save's script data.
        if (const int i = mls.findScriptByEntity(s.levelName); i >= 0)
        {
            levelProgram_ = mls.script(static_cast<std::size_t>(i)).entityName;
            levelSymbols_ = mls.script(static_cast<std::size_t>(i)).symbols;
            levelData_ = mls.script(static_cast<std::size_t>(i)).data;
        }
    }
    buildPickups();
    for (Trigger& t : triggers_)
        if (t.savePoint)
            dressSavePoint(t);
    for (const Assets::InstPlacement& p : s.level->placements())
    {
        const Assets::IniRecord* r = p.entityClass == "Base_Inst" ? Assets::findRecord(entityTypes, p.record) : nullptr;
        if (!r || !r->line("GAS_CAN") || !s.scene)
            continue;
        FixedHalo h;
        h.halo = gasCanHalo();
        h.centre = s.level->frame().placementOrigin(p.position) + Math::Vector3{0.0f, 0.3f, 0.0f}; // 0x435a50: 0.3 up
        h.placement = p.index;
        auto mesh = makePickupHalo(haloTexture_.find("halo"));
        h.mesh = mesh.get();
        s.scene->addEntity(std::move(mesh));
        fixedHalos_.push_back(h);
    }
    const auto scriptMs = msSince(t2);
    for (const Entity& e : entities_)
        scripts_.setEntityHandle(e.name, handleOf(e.name));
    bindAiNatives(scripts_, director_, [this](const Math::Vector3& v) { return toScene(v); });
    bindNatives();
    std::cerr << "runtime: entities " << entities_.size() << ", triggers " << triggers_.size() << ", hunters "
              << hunters_.size() << ", graph nodes " << nav_.size() << " ("
              << std::count_if(nodeLight_.begin(), nodeLight_.end(), [this](float32 l) { return l <= tuning_.alert.darkNode; })
              << " dark) (" << msSince(t0) << " ms: hunters "
              << spawnMs << ", scripts " << scriptMs << ")\n";
}

uint32 LevelRuntime::addEntity(const std::string& name, Kind kind, const Math::Vector3& pos)
{
    Entity e;
    e.name = lower(name);
    e.kind = kind;
    e.position = pos;
    entities_.push_back(std::move(e));
    const uint32 handle = static_cast<uint32>(entities_.size());
    byName_[entities_.back().name] = handle;
    if (started_)
    {
        // Made during play (a spawn, a script's trigger): its program is bound now (0x480ec0).
        scripts_.attach(entities_.back().name);
        scripts_.setEntityHandle(entities_.back().name, handle);
        scripts_.start(entities_.back().name, "OnCreate");
    }
    return handle;
}

LevelRuntime::Entity* LevelRuntime::entity(uint32 handle)
{
    return handle >= 1 && handle <= entities_.size() ? &entities_[handle - 1] : nullptr;
}

uint32 LevelRuntime::handleOf(const std::string& name) const
{
    const auto it = byName_.find(lower(name));
    return it == byName_.end() ? 0u : it->second;
}

namespace {

// Hunter_Inst WEAPON (melee) and WEAPON2 (firearm) ids -> item records (the id names are
// Sor3nt's file-format toolkit's Inst.php tables, "Thanks to ERMACCER": Derelict's hCrowbar has 7 = crowbar,
// hCheck 8 = small bat; 6 / 7 are "not defined"). Which of the three "baseball bat" ids is the
// metal bat is ours.
const char* meleeWeaponRecord(int32 id)
{
    switch (id)
    {
    case 0: return "Pipe_(CT)";
    case 1: return "Cleaver_(CT)";
    case 2: return "W_B_Bat_(CT)";
    case 3: return "Hunt_Knife_(CT)";
    case 4: case 5: case 14: return "M_B_Bat_(CT)";
    case 7: return "Crowbar_(CT)";
    case 8: return "Small_Bat_(CT)";
    case 9: return "NightStick_(CT)";
    case 10: return "Axe_(CT)";
    case 11: return "IcePick_(CT)";
    case 12: return "Machete_(CT)";
    case 13: return "Sickle_(CT)";
    case 15: return "W_B_Bat_Nails_(CT)";
    case 16: return "Chainsaw_(CT)";
    default: return nullptr;
    }
}

const char* firearmRecord(int32 id)
{
    switch (id)
    {
    case 0: return "Glock_(CT)";
    case 1: return "Glock_Torch_(CT)";
    case 2: return "Desert_Eagle_(CT)";
    case 3: return "Shotgun_(CT)";
    case 4: return "Uzi_(CT)";
    case 5: return "C_Commando_(CT)";
    case 6: return "Sniper_Rifle_(CT)";
    case 8: return "Nail_Gun_(CT)";
    case 9: return "6Shooter_(CT)";
    case 10: return "SawnOff_Shotgun_(CT)";
    case 11: return "Tranq_Rifle_(CT)";
    case 12: return "Shotgun_Torch_(CT)";
    default: return nullptr;
    }
}

constexpr int16 kHandBone = 1075; // Bip01 R Hand (the same biped ids as Cash)

} // namespace

void LevelRuntime::spawnHunters()
{
    for (const Assets::InstPlacement& p : setup_.level->placements())
        if (p.entityClass == "Hunter_Inst" && !spawnHunter(p) && !models_)
            return;
}

uint32 LevelRuntime::spawnHunter(const Assets::InstPlacement& p)
{
    const std::string dff = setup_.levelDir + "pak/modelspc.dff";
    std::shared_ptr<const DffLibrary>& models = models_; // parsed on the first hunter, shared by the rest
    {
        if (!models && !(models = DffLibrary::load(dff)))
        {
            std::cerr << "runtime: cannot read " << dff << "\n";
            return 0;
        }
        // The record's MODEL and its HEAD record's MODEL; a record without them is named after its
        // model (Hod_BodB1: the body; its head is the matching Hod_HedB1).
        const HunterTraits traits = tuning_.hunter(p.record);
        const std::string body = traits.model.empty() ? p.record : traits.model;
        ModelDesc md;
        md.dffPath = dff;
        md.library = models.get();
        md.clumpName = body;
        md.textures = &setup_.level->textures();
        auto model = std::make_unique<Character>();
        if (!model->load(md, *setup_.scene))
            return 0;
        // A record with a MODEL and no HEAD has its head in the body (Jou_Bod).
        std::string head = traits.head;
        if (head.empty() && traits.model.empty())
            if (const std::size_t bod = body.find("Bod"); bod != std::string::npos)
                head = std::string(body).replace(bod, 3, "Hed");
        if (!head.empty())
            model->attach(*models, head, kHeadBoneId, *setup_.scene, &setup_.level->textures());
        model->loadAnimations(setup_.animations);
        if (const auto block = animBlocks_.find(lower(p.record)); block != animBlocks_.end())
            model->setAnimationBlock(block->second);
        const Math::Vector3 feet = setup_.level->frame().placementOrigin(p.position);
        const float32 yaw = LevelFrame::characterYaw(p.rotation);
        // The body size is the letter after "Bod" in the model (Hod_BodS1 / M1 / B1).
        const std::size_t bod = body.find("Bod");
        const char size = bod != std::string::npos && bod + 3 < body.size() ? body[bod + 3] : 'M';
        // Hit points: the placement's own (Hunter_Inst param 0), else the model record's.
        const float32 health = !p.params.empty() && p.params[0] > 0 ? static_cast<float32>(p.params[0]) : traits.hitPoints;
        hunters_.push_back(std::make_unique<Hunter>(lower(p.internalName), std::move(model), feet, yaw, traits, health,
                                                     size, setup_.field->bodyHeight(), setup_.field->playerRadius()));
        Hunter& hunter = *hunters_.back();
        hunterRecords_[&hunter] = body;
        hunter.setKind(gun::bodyKind(body));
        hunter.setMeleeTraits(defaultMeleeTraits(body));
        // Skin: one of the model's own (A) and its SKIN_TEX variants, picked at random (the
        // exe's choice is not traced). Voice: VOICE -> SFX_H<n>V<1|2>_, alternating V1 / V2.
        const auto skins = tuning_.skins.find(lower(body));
        const int32 variants = 1 + (skins == tuning_.skins.end() ? 0 : static_cast<int32>(skins->second.size()));
        const int32 skin = std::uniform_int_distribution<int32>(0, variants - 1)(rng_);
        applySkin(hunter, body, skin);
        hunter.setVoice(voiceSamplePrefix(traits.voice, static_cast<int32>(hunters_.size())));
        std::cerr << "runtime: hunter " << hunter.name() << " " << body << " skin " << static_cast<char>('A' + skin)
                  << " voice " << (hunter.voice().empty() ? std::string("none") : hunter.voice()) << "\n";
        // Its weapon: WEAPON (melee) if it has one, else WEAPON2 (a firearm it carries; it fights
        // with its fists until hunters can shoot).
        const int32 meleeId = p.params.size() > 4 ? static_cast<int32>(p.params[4]) : -1;
        const int32 gunId = p.params.size() > 5 ? static_cast<int32>(p.params[5]) : -1;
        const char* record = meleeWeaponRecord(meleeId);
        {
            const char* gunRecord = firearmRecord(gunId);
            hunterArms_[&hunter] = {record ? items_.find(record) : nullptr, gunRecord ? items_.find(gunRecord) : nullptr};
        }
        if (!record || !items_.find(record))
            record = firearmRecord(gunId);
        // REELHUNT_HUNTER_WEAPON=<record> arms every hunter with it (debugging armed fights on
        // levels whose hunters carry nothing).
        if (const char* v = std::getenv("REELHUNT_HUNTER_WEAPON"))
            record = v;
        armHunter(hunter, record ? items_.find(record) : nullptr);
        const uint32 h = handleOf(p.internalName) ? handleOf(p.internalName) : addEntity(p.internalName, Kind::Hunter, feet);
        entity(h)->kind = Kind::Hunter;
        entity(h)->hunter = hunters_.back().get();
        entity(h)->placement = p.index;
        return h;
    }
}

uint32 LevelRuntime::addSpawned(Assets::InstPlacement p)
{
    exploded_.erase(lower(p.internalName)); // (a new one by that name can be shot again)
    p.index = setup_.level->instCount() + static_cast<int32>(spawned_.size());
    p.entityClass = tuning_.hunters.count(lower(p.record))            ? "Hunter_Inst"
                    : Assets::iequals(p.record, "Gen_Save_Point") ? "Trigger_Inst"
                                                                    : "Base_Inst";
    // A hunter's class data has a placed one's 9 words (health from its record, no WEAPON /
    // WEAPON2: ours, the exe's values for a spawn are not traced).
    if (p.entityClass == "Hunter_Inst" && p.params.empty())
        p.params = {0, 0, 0, 0, -1, -1, 0, 0, 0};
    spawned_.push_back(std::move(p));
    const Assets::InstPlacement& s = spawned_.back();
    const Math::Vector3 pos = setup_.level->frame().placementOrigin(s.position);
    uint32 h = 0;
    if (s.entityClass == "Hunter_Inst")
        h = spawnHunter(s);
    else if (const ItemType* item = items_.find(s.record))
    {
        // An item lies where it was put (on the floor below), as a pickup that keeps its place
        // in the registry: taken, its record is "NUL".
        const std::size_t before = pickups_.size();
        spawnPickup(item, pos, 0.0f);
        if (pickups_.size() > before)
            pickups_.back().placement = s.index;
    }
    if (!h)
    {
        const Kind kind = s.entityClass == "Trigger_Inst" ? Kind::Trigger : Kind::Placement;
        h = handleOf(s.internalName) ? handleOf(s.internalName) : addEntity(s.internalName, kind, pos);
        entity(h)->placement = s.index;
        if (kind == Kind::Trigger)
        {
            Trigger t;
            t.name = lower(s.internalName);
            t.placement = s.index;
            t.savePoint = true;
            t.a = pos;
            t.origin = pos;
            t.radius = s.params.size() > 1 ? std::clamp(std::bit_cast<float32>(s.params[1]), 0.5f, 50.0f) : 1.0f;
            entity(h)->trigger = static_cast<int32>(triggers_.size());
            triggers_.push_back(t);
            if (setup_.scene && models_)
                dressSavePoint(triggers_.back());
        }
    }
    scripts_.setEntityHandle(lower(s.internalName), h);
    std::cerr << "runtime: spawned " << s.record << " " << s.internalName << " (" << s.entityClass << ") at (" << pos.x
              << ", " << pos.y << ", " << pos.z << ")\n";
    return h;
}

const Assets::InstPlacement* LevelRuntime::placementAt(int32 index) const
{
    const int32 inst = setup_.level->instCount();
    if (index >= inst)
        return static_cast<std::size_t>(index - inst) < spawned_.size() ? &spawned_[static_cast<std::size_t>(index - inst)] : nullptr;
    for (const Assets::InstPlacement& p : setup_.level->placements())
        if (p.index == index)
            return &p;
    return nullptr;
}

void LevelRuntime::armHunter(Hunter& hunter, const ItemType* item)
{
    const DffLibrary* models = models_.get();
    if (!item || !models)
        return;
    if (const ItemType* held = hunter.weapon())
        hunter.model().detach(held->model, kHandBone);
    if (hunter.model().attach(*models, item->model, kHandBone, *setup_.scene, &setup_.level->textures()))
    {
        const auto melee = tuning_.melee.find(lower(item->weaponType));
        hunter.setWeapon(item, melee != tuning_.melee.end() ? &melee->second : nullptr);
        hunter.setFirearm(tuning_.firearm(item->weaponType)); // a gun it fires at him
        std::cerr << "runtime: hunter " << hunter.name() << " carries " << item->record << "\n";
    }
}

void LevelRuntime::disarmHunter(Hunter& hunter)
{
    if (const ItemType* held = hunter.weapon())
        hunter.model().detach(held->model, kHandBone);
    hunter.setWeapon(nullptr, nullptr);
    hunter.setFirearm(nullptr);
}

int32 LevelRuntime::slotOf(const ItemType* t) const
{
    if (!t || std::find(inventory_.begin(), inventory_.end(), t) == inventory_.end())
        return -1;
    if (const int32 holster = inventorySlot(t->holsterSlot); holster > 0)
        return holster;
    int32 slot = 6;
    for (const ItemType* i : inventory_)
    {
        if (i == t)
            return slot;
        if (inventorySlot(i->holsterSlot) == 0)
            ++slot;
    }
    return -1;
}

const ItemType* LevelRuntime::itemAtSlot(int64 slot) const
{
    for (const ItemType* t : inventory_)
        if (slotOf(t) == slot)
            return t;
    return nullptr;
}

void LevelRuntime::applySkin(Hunter& h, const std::string& record, int32 id)
{
    // Body record Hod_BodB1 wears Hod_BodB1A_128 by default; variant k is the k-th SKIN_TEX
    // (Hod_BodB1B_128, ...), and the matching head record (Hod_HedB1) changes with it.
    std::vector<std::pair<std::string, std::string>> swaps;
    std::string head = record;
    if (const std::size_t bod = head.find("Bod"); bod != std::string::npos)
        head.replace(bod, 3, "Hed");
    for (const std::string& r : {record, head})
    {
        const auto it = tuning_.skins.find(lower(r));
        const std::string own = r + "A_128";
        if (id <= 0)
            swaps.emplace_back(own, own);
        else if (it != tuning_.skins.end() && id <= static_cast<int32>(it->second.size()))
            swaps.emplace_back(own, it->second[static_cast<std::size_t>(id - 1)]);
    }
    // Re-texturing works from the original names, so "back to A" is a plain lookup.
    h.model().retexture(setup_.level->textures(), swaps);
}

bool LevelRuntime::playerSpotted() const noexcept
{
    for (const auto& h : hunters_)
        if (h->alive() && h->action() >= AiAction::Chase)
            return true;
    return false;
}

std::vector<int32> LevelRuntime::takeDirectorSpeech()
{
    std::vector<int32> out;
    out.swap(directorSpeech_);
    return out;
}

std::vector<LevelRuntime::SoundEvent> LevelRuntime::takeSounds()
{
    std::vector<SoundEvent> out;
    out.swap(sounds_);
    return out;
}

void LevelRuntime::emitSound(const std::string& sample, const Math::Vector3& at, float32 priority, bool positional,
                             float32 range)
{
    if (sounds_.size() < 64)
        sounds_.push_back({sample, at, priority, positional, range});
}

void LevelRuntime::weaponSound(int32 ct, uint16 event, float32 value, const Math::Vector3& at, const void* shooter)
{
    SoundEvent ev;
    ev.position = at;
    ev.priority = 0.8f;
    ev.weaponCt = ct;
    ev.weaponEvent = event;
    ev.weaponValue = value;
    if (sounds_.size() < 64)
        sounds_.push_back(ev);
    // The uzi (0x2b): each shot keeps its burst going; a gap of more than its fire clip ends it
    // (0x4b6e50 posts 0xb with value 1 then: the tails).
    if (ct == 0x2b && event == 0xb && value == 0.0f)
    {
        auto it = std::find_if(bursts_.begin(), bursts_.end(), [&](const Burst& b) { return b.shooter == shooter; });
        if (it == bursts_.end())
            bursts_.push_back({shooter, time_, at});
        else
            *it = {shooter, time_, at};
    }
}

void LevelRuntime::start()
{
    // The exe: the level program's OnCreate runs as the level loads (0x480b10: handler 0).
    // An entity's own program - the MLS script named after it - is bound when the entity first
    // enters the world (0x4345c0 -> 0x480ec0, found by name with 0x47f5a0) and its OnCreate
    // queued then (0x480190(script, 1) -> 0x480550). A program whose entity is not in the level
    // does not run: a hunter dead in a checkpoint, one a script has yet to spawn (addEntity
    // starts it when it comes).
    std::unordered_set<std::string> bound;
    for (const Entity& e : entities_)
        if (e.kind == Kind::Level && bound.insert(e.name).second)
            scripts_.start(e.name, "OnCreate");
    for (const Entity& e : entities_)
        if (byName_.contains(e.name) && bound.insert(e.name).second)
            scripts_.start(e.name, "OnCreate");
    started_ = true;
    std::string waiting;
    for (const std::string& name : scripts_.programEntities())
        if (!bound.contains(name))
            waiting += " " + name;
    std::cerr << "runtime: OnCreate for " << bound.size() << " entities; programs waiting for their entity:"
              << (waiting.empty() ? std::string(" none") : waiting) << "\n";
    std::string unbound;
    const std::vector<std::string> names = scripts_.unboundNatives();
    for (const std::string& n : names)
        unbound += " " + n;
    std::cerr << "runtime: " << names.size() << " natives the scripts call are not bound:" << unbound << "\n";
}

LevelRuntime::Mover* LevelRuntime::moverOf(uint32 handle)
{
    const Entity* en = entity(handle);
    if (!en)
        return nullptr;
    for (Mover& m : movers_)
        if (m.name == en->name)
            return &m;
    return nullptr;
}

LevelRuntime::LightState* LevelRuntime::lightOf(uint32 handle)
{
    const Entity* en = entity(handle);
    if (!en || en->placement < 0 || !setup_.level || !setup_.level->lightOf(en->placement))
        return nullptr; // (0x481ac0: not a light entity)
    return &lights_[en->placement];
}

void LevelRuntime::updateLights(float32 dt)
{
    // CEntityLight's update (0x4cd2a0), in milliseconds.
    const float32 ms = dt * 1000.0f;
    for (auto& [placement, l] : lights_)
    {
        if (l.flicker == 2 && l.left != 0.0f)
        {
            l.timer += ms;
            // Lit for up to onMs, dark for up to offMs; each frame before that it turns early when
            // rand() % hold < hold / 35.
            const float32 hold = l.on ? l.onMs : l.offMs;
            const uint32 h = static_cast<uint32>(std::max(1.0f, hold));
            if (hold < l.timer || rng_() % h < h / 35u)
            {
                l.on = !l.on;
                l.timer = 0.0f;
            }
            if (!l.forever)
            {
                l.left -= ms;
                if (l.left < 1.0f)
                {
                    l.timer = 0.0f;
                    l.left = 0.0f;
                    if (l.offAtEnd)
                        l.on = false;
                }
            }
        }
        if (const LevelContent::LevelLight* ll = setup_.level->lightOf(placement))
            ll->light->setIntensity(l.on ? ll->intensity : 0.0f);
    }
}

void LevelRuntime::updateMovers(float32 dt)
{
    for (Mover& m : movers_)
    {
        if (!m.moving)
            continue;
        const Math::Vector3 to = m.target - m.offset;
        const float32 left = to.length(), step = m.speed * dt;
        if (left <= step)
        {
            m.offset = m.target;
            m.moving = false;
        }
        else
            m.offset += to * (step / left);
        const Math::Matrix4x4 t = Math::Matrix4x4::translation(m.offset);
        for (Renderer::Mesh* mesh : m.meshes)
            mesh->setTransform(t);
        if (setup_.field)
            setup_.field->setMoverOffset(m.placement, m.offset);
        if (const uint32 h = handleOf(m.name))
            entity(h)->position = m.origin + m.offset;
    }
}

void LevelRuntime::slotPlaying(int32 slot, float32 seconds)
{
    if (slot >= 0 && slot < static_cast<int32>(slotEnds_.size()))
        slotEnds_[static_cast<std::size_t>(slot)] = time_ + std::max(0.0f, seconds);
}

std::vector<HudBar> LevelRuntime::hudBars()
{
    std::vector<HudBar> out;
    auto label = [](std::string n) {
        if (!n.empty())
            n[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])));
        return n; // (ours: the entity's name as the level spells it is not kept)
    };
    // The buddy: a living hunter pursuing a BeBuddy goal (0x54c9b0 shows its bar, 0x54ca20 hides it).
    for (const auto& h : hunters_)
        if (h->alive() && !h->gone())
            if (const GoalDef* g = director_.goalFor(h->name(), false); g && g->type == GoalType::BeBuddy)
            {
                out.push_back({label(h->name()), h->health() / std::max(1.0f, h->fullHealth())});
                break;
            }
    // The boss: Ramirez (scene 13) or Piggsy (scene 19), once he fights him (0x5ef990: the player's
    // opponent, ped +0x6a4, is that one; ours: the boss is in combat).
    const char* boss = Assets::iequals(setup_.levelName, "ramirez") ? "ramirez"
                     : Assets::iequals(setup_.levelName, "attic")   ? "piggsy"
                                                                    : nullptr;
    if (boss)
        for (const auto& h : hunters_)
            if (h->name() == boss && !h->gone())
            {
                bossShown_ = bossShown_ || h->action() == AiAction::Combat;
                if (bossShown_ && h->alive())
                    out.push_back({label(h->name()), h->health() / std::max(1.0f, h->fullHealth())});
            }
    return out;
}

void LevelRuntime::playerArrived()
{
    if (debugLog_)
        std::cerr << "[script] the player is there" << std::endl;
    playerGoto_.reset();
    playerArrived_ = true;
}

std::string LevelRuntime::hudCounter() const
{
    if (timerShown_)
    {
        const int32 s = static_cast<int32>(std::ceil(timerLeft_));
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
        return buf;
    }
    return counterShown_ ? std::to_string(counter_) : std::string();
}

void LevelRuntime::loadFx(const Assets::PakFile& pak)
{
    const std::string dir = setup_.gameRoot + "levels/GLOBAL/SFX/";
    if (std::ifstream f(Engine::Platform::resolvePath(dir + "mh_fx_pc.fxp"), std::ios::binary); f)
    {
        std::ostringstream text;
        text << f.rdbuf();
        fxLib_.loadProject(text.str());
    }
    if (const auto ini = pak.file("levels/global/data/ParticleEffects.INI"))
        fxLib_.loadTemplates(Assets::parseIniRecords(*ini));
    for (const char* txd : {"mh_fx_pc.txd", "sfx_pc.txd"})
        if (auto t = Assets::Txd::load(dir + txd))
            fxTextures_.add(*t);
    std::cerr << "runtime: particle effects " << fxLib_.size() << "\n";
}

void LevelRuntime::shotStruck(const Firearm& gun, const Math::Vector3& at, const Math::Vector3& dir, const PlayerField::ShotHit& hit,
                              bool smoke)
{
    if (debugLog_)
        std::cerr << "[fx] shot struck material " << int(hit.material) << " -> "
                  << (MaterialTypes::hitEffect(hit.material) ? MaterialTypes::hitEffect(hit.material) : gun.hardHitFx.c_str()) << std::endl;
    if (const char* fx = MaterialTypes::hitEffect(hit.material))
    {
        if (const int32 id = playFx(fx, at, dir * -1.0f))
            fx_.setUp(id, hit.normal);
    }
    else
        playFx(gun.hardHitFx, at, dir * -1.0f);
    if (smoke)
        playFx(gun.smokeHitFx, at, dir * -1.0f);
    if (const int32 hole = MaterialTypes::bulletHole(hit.material); hole >= 0)
        decals_.push_back({hole, at, hit.normal}); // 0x4829b0 with the type's size, turned at random
}

int32 LevelRuntime::playFx(const std::string& name, const Math::Vector3& at, const Math::Vector3& forward)
{
    return fx_.spawn(fxLib_.find(name), at, forward);
}

std::vector<uint32> LevelRuntime::textureHandles() const
{
    std::vector<uint32> out = haloTexture_.handles();
    for (uint32 t : fxTextures_.handles())
        out.push_back(t);
    for (const auto& h : hunters_)
        for (uint32 t : h->model().textureHandles()) // empty while hunters share the level's textures
            out.push_back(t);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Coordinates: scripts use the .bsp world frame (Y up); the scene is that frame recentred.

Math::Vector3 LevelRuntime::toScene(const Math::Vector3& script) const
{
    return setup_.level->frame().worldPoint(script);
}

void LevelRuntime::placeProp(Entity& en, float32 yaw, const Math::Vector3& to)
{
    const Assets::InstPlacement* p = placementAt(en.placement);
    if (!p || !setup_.level)
        return;
    const Math::Vector3 origin = setup_.level->frame().placementOrigin(p->position);
    const Math::Matrix4x4 t = Math::Matrix4x4::translation(to) * Math::Matrix4x4::rotationY(yaw) *
                              Math::Matrix4x4::translation(Math::Vector3{0.0f, 0.0f, 0.0f} - origin);
    for (Renderer::Mesh* mesh : setup_.level->meshesOf(*p))
        mesh->setTransform(t);
    propYaw_[en.placement] = yaw;
    en.position = to;
}

void LevelRuntime::placeProp(Entity& en, const Math::Matrix4x4& transform, const Math::Vector3& to)
{
    const Assets::InstPlacement* p = placementAt(en.placement);
    if (!p || !setup_.level)
        return;
    for (Renderer::Mesh* mesh : setup_.level->meshesOf(*p))
        mesh->setTransform(transform);
    en.position = to;
}

uint32 LevelRuntime::entityOfPlacement(int32 placement)
{
    if (placementEntity_.empty())
        for (std::size_t i = 0; i < entities_.size(); ++i)
            if (entities_[i].placement >= 0 && !entities_[i].hunter)
                placementEntity_.emplace(entities_[i].placement, static_cast<uint32>(i + 1));
    const auto it = placementEntity_.find(placement);
    return it == placementEntity_.end() ? 0u : it->second;
}

void LevelRuntime::buildPhysicsProps()
{
    // Every placed prop whose record names a rigid PHYSICS record (cuboid or sphere) and has its
    // own collision: a body on that collision's bounds, asleep.
    if (!setup_.level || !setup_.field)
        return;
    const LevelFrame& f = setup_.level->frame();
    int32 iron = 0;
    for (const Assets::InstPlacement& p : setup_.level->placements())
    {
        const auto it = tuning_.propPhysics.find(lower(p.record));
        if (it == tuning_.propPhysics.end() || it->second.type.immovable ||
            (it->second.type.cls != PhysicsType::RigidCuboid && it->second.type.cls != PhysicsType::RigidSphere))
            continue;
        const auto tris = setup_.field->propTriangles(p.index);
        if (tris.empty())
            continue;
        const Math::Vector3 origin = f.placementOrigin(p.position);
        const Math::Matrix4x4 r4 = LevelFrame::placementRotation(p.rotation);
        Math::Matrix3x3 rest;
        rest.m00 = r4.m00; rest.m01 = r4.m01; rest.m02 = r4.m02;
        rest.m10 = r4.m10; rest.m11 = r4.m11; rest.m12 = r4.m12;
        rest.m20 = r4.m20; rest.m21 = r4.m21; rest.m22 = r4.m22;
        const Math::Matrix3x3 back = rest.transpose();
        Math::Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (const auto& t : tris)
            for (const Math::Vector3& v : {t.a, t.b, t.c})
            {
                const Math::Vector3 m = back.multiply(v - origin);
                lo = {std::min(lo.x, m.x), std::min(lo.y, m.y), std::min(lo.z, m.z)};
                hi = {std::max(hi.x, m.x), std::max(hi.y, m.y), std::max(hi.z, m.z)};
            }
        physics_.body(physics_.add(p.index, it->second.type, it->second.iron, origin, rest, lo, hi)).material = it->second.material;
        iron += it->second.iron ? 1 : 0;
    }
    if (physics_.size() > 0)
        std::cerr << "runtime: physics props " << physics_.size() << " (iron " << iron << ")\n";
}

void LevelRuntime::updatePhysicsProps(float32 dt, HunterWorld& w)
{
    if (physics_.size() == 0 || !setup_.field)
        return;
    // REELHUNT_DROP=<hunter>@<s> (testing): at <s> seconds the nearest iron prop is woken 3 m
    // above the hunter.
    if (static const char* v = std::getenv("REELHUNT_DROP"); v && !cutscene_)
    {
        static bool dropped = false;
        const std::string spec(v);
        const std::size_t at = spec.find('@');
        if (!dropped && time_ >= (at == std::string::npos ? 0.0f : std::stof(spec.substr(at + 1))))
            for (const auto& h : hunters_)
                if (h->alive() && h->name() == lower(spec.substr(0, at)))
                {
                    int32 best = -1;
                    float32 bestD = 1e9f;
                    for (int32 i = 0; i < static_cast<int32>(physics_.size()); ++i)
                        if (const float32 d = (physics_.origin(i) - h->feet()).length(); physics_.body(i).iron && d < bestD)
                        {
                            best = i;
                            bestD = d;
                        }
                    if (best >= 0)
                    {
                        PhysicsProps::Body& b = physics_.body(best);
                        if (physics_.wake(best))
                            setup_.field->makeDynamic(b.id);
                        b.x += h->feet() + Math::Vector3{0.0f, 3.0f, 0.0f} - physics_.origin(best);
                        std::cerr << "[physics] test: a prop " << bestD << " m away drops on " << h->name() << std::endl;
                    }
                    dropped = true;
                }
    }
    PhysicsProps::World pw;
    const PlayerField* field = setup_.field;
    pw.hit = [field](const Math::Vector3& a, const Math::Vector3& b) -> std::optional<PhysicsProps::Hit> {
        const auto h = field->shotHit(a, b);
        if (!h)
            return std::nullopt;
        return PhysicsProps::Hit{a + (b - a) * h->fraction, h->normal};
    };
    for (std::size_t i = 0; i < hunters_.size(); ++i)
        if (hunters_[i]->alive())
            pw.characters.push_back({static_cast<int32>(i), hunters_[i]->feet(), 1.8f, 0.35f, 80.0f, {}});
    pw.characters.push_back({-1, player_.feet, 1.8f, 0.35f, 65.0f, {}});
    // He bumps into a prop (0x466de0 -> 0x4a31c0: the player, or a ped flagged +0x338 0x10, into
    // a rigid, sphere or hanging body that is no door): graded by his largest contact impulse,
    // under 33.3 LURE_VERY_LOW, under 50 LURE_LOW, over 100 LURE_MEDIUM (between: none). Ours: his
    // body's 0.5 m radius and 5 cm more, at most every 0.25 s.
    if (dt > 0.0f && player_.alive)
    {
        PhysicsProps::Character me{-1, player_.feet, 1.8f, 0.55f, 65.0f, (player_.feet - bumpFeet_) * (1.0f / dt)};
        me.velocity.y = 0.0f;
        const float32 j = physics_.bump(me);
        const char* lure = j <= 0.0f ? nullptr : j < 33.333f ? "LURE_VERY_LOW" : j < 50.0f ? "LURE_LOW" : j > 100.0f ? "LURE_MEDIUM" : nullptr;
        if (lure && time_ - bumpAt_ >= 0.25f)
        {
            bumpAt_ = time_;
            postSound(lure, player_.feet);
            if (debugLog_)
                std::cerr << "[physics] he bumps a prop (" << j << "): " << lure << std::endl;
        }
    }
    bumpFeet_ = player_.feet;
    for (const PhysicsProps::Crush& c : physics_.update(dt, pw))
    {
        if (debugLog_)
            std::cerr << "[physics] a prop crushes " << (c.character < 0 ? std::string("him") : hunters_[static_cast<std::size_t>(c.character)]->name())
                      << " (" << c.damage << ")" << std::endl;
        if (c.character < 0)
            hurtPlayer(c.damage, c.at);
        else if (Hunter* h = hunters_[static_cast<std::size_t>(c.character)].get(); h->alive())
            h->hit(c.damage, c.at, w, HitKind::Hard);
    }
    for (const int32 id : physics_.takeNewlyAwake())
        setup_.field->makeDynamic(id);
    // Their impact sounds (0x456710 at the prop).
    for (const PhysicsProps::Impact& im : physics_.takeImpacts())
        if (const int32 id = PhysicsProps::impactSound(physics_.body(physics_.find(im.body)).material, static_cast<uint32>(rng_())); id >= 0)
        {
            SoundEvent s;
            s.sample = "#" + std::to_string(id);
            s.position = im.at;
            s.volume = static_cast<float32>(im.volume) / 127.0f;
            sounds_.push_back(s);
        }
    // The awake bodies' meshes and collision where they are.
    for (int32 i = 0; i < static_cast<int32>(physics_.size()); ++i)
    {
        const PhysicsProps::Body& b = physics_.body(i);
        if (!b.awake)
            continue;
        const Math::Matrix4x4 t = physics_.transform(i);
        setup_.field->setDynamicTransform(b.id, t);
        if (Entity* en = entity(entityOfPlacement(b.id)))
            placeProp(*en, t, physics_.origin(i));
    }
}

void LevelRuntime::updateCrane(float32 dt, bool left, bool right, bool magnet, float32 mouseDX)
{
    if (craneCam_.on)
        craneCam_.onFor += dt;
    Entity* turned = padTurned_ ? entity(padTurned_) : nullptr;
    // REELHUNT_CRANE=<s> (testing): once the crane is worked, it swings over the iron prop nearest
    // its hanging prop's sweep, holds the magnet on for <s> seconds, swings a quarter turn away
    // and lets go.
    if (static const char* v = std::getenv("REELHUNT_CRANE"); v && turned && turned->placement >= 0)
    {
        static float32 t = 0.0f, aim = 0.0f;
        static int32 phase = 0;
        t += dt;
        const float32 hold = static_cast<float32>(std::atof(v));
        left = right = false;
        mouseDX = 0.0f;
        magnet = false;
        if (phase == 0 && t > 1.0f)
        {
            // The yaw putting the hanging prop's pivot over the nearest iron body.
            const Assets::InstPlacement* pp = placementAt(turned->placement);
            for (const PropLink& l : propLinks_)
                if (l.parent == padTurned_ && l.hanging && pp)
                    if (const Entity* ch = entity(l.child))
                        if (const Assets::InstPlacement* cp = placementAt(ch->placement))
                        {
                            const LevelFrame& f = setup_.level->frame();
                            const Math::Vector3 off = f.placementOrigin(cp->position) - f.placementOrigin(pp->position);
                            float32 best = 1e9f;
                            for (int32 i = 0; i < static_cast<int32>(physics_.size()); ++i)
                                if (physics_.body(i).iron)
                                    for (float32 a = 0.0f; a < 6.2832f; a += 0.005f)
                                    {
                                        const Math::Vector3 tip = turned->position + Math::Matrix4x4::rotationY(a).multiplyPoint(off);
                                        const Math::Vector3 o = physics_.origin(i);
                                        const float32 d = std::hypot(tip.x - o.x, tip.z - o.z);
                                        if (d < best)
                                        {
                                            best = d;
                                            aim = a;
                                        }
                                    }
                            std::cerr << "[crane] test: swings to " << Math::degrees(aim) << " deg (" << best << " m off)" << std::endl;
                        }
            phase = 1;
            t = 0.0f;
        }
        if (phase == 1)
            propYaw_[turned->placement] += std::clamp(aim - propYaw_[turned->placement], -0.6f * dt, 0.6f * dt);
        if (phase == 1 && std::abs(aim - propYaw_[turned->placement]) < 1e-3f && t > 1.0f)
        {
            phase = 2;
            t = 0.0f;
            magnet = true; // on
            std::cerr << "[crane] test: magnet on" << std::endl;
        }
        if (phase == 2 && t > hold)
        {
            // Over the nearest hunter it can reach, else a quarter turn away.
            phase = 3;
            t = 0.0f;
            const float32 from = aim;
            aim += 1.5708f;
            const Assets::InstPlacement* pp = placementAt(turned->placement);
            for (const PropLink& l : propLinks_)
                if (l.parent == padTurned_ && l.hanging && pp)
                    if (const Entity* ch = entity(l.child))
                        if (const Assets::InstPlacement* cp = placementAt(ch->placement))
                        {
                            const LevelFrame& f = setup_.level->frame();
                            const Math::Vector3 off = f.placementOrigin(cp->position) - f.placementOrigin(pp->position);
                            float32 best = 1.0f;
                            for (const auto& h : hunters_)
                                if (h->alive())
                                    for (float32 a = 0.0f; a < 6.2832f; a += 0.005f)
                                    {
                                        const Math::Vector3 tip = turned->position + Math::Matrix4x4::rotationY(a).multiplyPoint(off);
                                        const float32 d = std::hypot(tip.x - h->feet().x, tip.z - h->feet().z);
                                        if (d < best)
                                        {
                                            best = d;
                                            aim = from + std::remainder(a - from, 6.2832f);
                                            std::cerr << "[crane] test: over " << h->name() << " (" << d << " m)" << std::endl;
                                        }
                                    }
                        }
        }
        if (phase == 3)
        {
            propYaw_[turned->placement] += std::clamp(aim - propYaw_[turned->placement], -0.6f * dt, 0.6f * dt);
            if (std::abs(aim - propYaw_[turned->placement]) < 1e-3f)
            {
                phase = 4;
                magnet = true; // off
                std::cerr << "[crane] test: magnet off" << std::endl;
            }
        }
    }
    if (turned && turned->placement >= 0)
        placeProp(*turned,
                  propYaw_[turned->placement] + craneTurn_.update(dt, left, right) + Math::radians(mouseDX * -18.0f * dt),
                  turned->position);
    // The magnet (0x432a10): the Action button toggles it while the crane is worked.
    if (turned && magnet && !magnetButton_)
        magnetOn_ = !magnetOn_;
    magnetButton_ = magnet;
    if (!turned)
        magnetOn_ = false;
    // What rides on a turned prop: attached props keep their place on it; a hanging one's origin
    // swings under its pivot there.
    for (PropLink& l : propLinks_)
    {
        Entity* child = entity(l.child);
        Entity* parent = entity(l.parent);
        if (!child || !parent || child->placement < 0 || parent->placement < 0)
            continue;
        const Assets::InstPlacement* pp = placementAt(parent->placement);
        const Assets::InstPlacement* cp = placementAt(child->placement);
        if (!pp || !cp)
            continue;
        const float32 yaw = propYaw_[parent->placement];
        const LevelFrame& f = setup_.level->frame();
        const Math::Vector3 parentOrigin = f.placementOrigin(pp->position);
        const Math::Matrix4x4 turn = Math::Matrix4x4::rotationY(yaw);
        // Both keep their place on it: a hanging one's pivot is at its offset in the parent's frame,
        // which is where it was placed (Scrap2's magnet: 7.9 m up, 7.6 m out).
        const Math::Vector3 to = parent->position + turn.multiplyPoint(f.placementOrigin(cp->position) - parentOrigin);
        if (!l.hanging)
        {
            if (yaw != 0.0f)
                placeProp(*child, yaw, to);
            continue;
        }
        // The hanging one swings under its pivot (Game/crane.h Hanging): its centre of mass is
        // its PHYSICS record's CENTRE_OF_MASS below it (the magnet's 4 m).
        float32 length = 4.0f, spin = 0.01f, mass = 500.0f;
        if (const auto ph = tuning_.propPhysics.find(lower(cp->record)); ph != tuning_.propPhysics.end())
        {
            length = std::max(0.1f, ph->second.type.centreOfMass.length());
            spin = ph->second.type.spinResistance;
            mass = ph->second.type.mass;
        }
        l.swing.update(dt, to, length, spin);
        const Math::Vector3 down = l.swing.down();
        const Math::Vector3 axis = Math::Vector3{0.0f, -1.0f, 0.0f}.cross(down);
        const float32 s = axis.length();
        Math::Matrix4x4 swing = Math::Matrix4x4::identity();
        if (s > 1e-5f)
            swing = Math::Quaternion::fromAxisAngle(axis * (1.0f / s), std::atan2(s, -down.y)).toMatrix().make4x4();
        const Math::Vector3 origin0 = f.placementOrigin(cp->position);
        placeProp(*child,
                  Math::Matrix4x4::translation(to) * swing * Math::Matrix4x4::rotationY(yaw) *
                      Math::Matrix4x4::translation(Math::Vector3{0.0f, 0.0f, 0.0f} - origin0),
                  to);
        if (!magnetOn_ || l.parent != padTurned_)
            continue;
        // On, it draws what is made of iron: its point 6.8 m down its hang (0x69b4c0, set at
        // 0x437562). Within 1 m it holds it there (half way a frame beyond 0.3 m), against
        // gravity; farther it pulls by 6 x 25 / (d + 1) m/s^2 less 0.7 of the speed already
        // that way, and the magnet is pulled back as much (by their masses).
        const Math::Vector3 point = to + down * 6.8f;
        for (int32 i = 0; i < static_cast<int32>(physics_.size()); ++i)
        {
            PhysicsProps::Body& b = physics_.body(i);
            if (!b.iron)
                continue;
            const Math::Vector3 dir = point - physics_.origin(i);
            const float32 d = dir.length();
            if (d < 1.0f)
            {
                if (physics_.wake(i))
                    setup_.field->makeDynamic(b.id);
                Math::Vector3 v = dir * 30.0f; // (the gap in one frame: ours, a 1/30 s tick)
                if (d > 0.3f)
                    v *= 0.5f;
                v.y += 25.0f / 30.0f;
                b.v = v;
                b.w = {0.0f, 0.0f, 0.0f};
            }
            else if (d < 11.0f)
            {
                // (ours: only within 11 m, where the pull can beat gravity's half)
                const Math::Vector3 n = dir * (1.0f / d);
                const Math::Vector3 dv = n * ((6.0f * 25.0f / (d + 1.0f) - 0.7f * b.v.dot(n)) * dt);
                if (physics_.wake(i))
                    setup_.field->makeDynamic(b.id);
                b.v += dv;
                l.swing.push(dv * (-b.type.mass / std::max(mass, 1.0f)));
            }
        }
    }
}

std::optional<std::pair<Math::Vector3, Math::Vector3>> LevelRuntime::craneView()
{
    if (!craneCam_.on)
        return std::nullopt;
    if (const Entity* en = craneCam_.lookAt ? entity(craneCam_.lookAt) : nullptr)
        craneCam_.target = en->position + craneCam_.offset;
    // After a second the camcorder look and the hand-held shake go (0x58f5e0: 0x7a121c counts
    // down 30 frames).
    if (craneCam_.onFor >= 1.0f)
    {
        look_.camcorder.fill(false);
        look_.handCam = false;
    }
    return std::make_pair(craneCam_.position, craneCam_.target);
}

Math::Vector3 LevelRuntime::toScript(const Math::Vector3& scene) const
{
    const LevelFrame& f = setup_.level->frame();
    return scene * (1.0f / f.scale) + f.center;
}

// ---------------------------------------------------------------------------------------------
// Natives

void LevelRuntime::bindNatives()
{
    Script::Engine& e = scripts_;
    auto none = [] { return Value::nil(); };
    auto entityArg = [this](NativeCall& c, std::size_t i) -> uint32 {
        const Value& v = c.arg(i);
        return v.type == Script::ValueType::String ? handleOf(v.s) : v.asEntity();
    };

    e.bind("getentity", [this](NativeCall& c) { return Value::entity(handleOf(c.string(0))); });
    e.bind("getplayer", [this](NativeCall&) { return Value::entity(playerHandle_); });
    e.bind("isentityalive", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        if (!en)
            return Value::boolean(false);
        if (en->kind == Kind::Player)
            return Value::boolean(playerAlive());
        return Value::boolean(!en->hunter || en->hunter->alive());
    });
    e.bind("getentityposition", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        if (!en)
            return Value::vec3({0.0f, 0.0f, 0.0f});
        const Math::Vector3 p = en->kind == Kind::Player ? player_.feet : (en->hunter ? en->hunter->feet() : en->position);
        return Value::vec3(toScript(p));
    });
    e.bind("getplayerposition", [this](NativeCall&) { return Value::vec3(toScript(player_.feet)); });
    // GetPlayerAreaName: the AI area of the graph node nearest the player (mapAI.grf). Level
    // scripts read it in OnCreate to tell which save point the level restarted at (Mall's
    // levelstatus).
    e.bind("getplayerareaname", [this](NativeCall&) {
        const int32 n = nav_.nearest(player_.feet);
        return Value::string(n >= 0 ? nav_.areaName(nav_.node(n).area) : std::string());
    });
    // PlayDirectorSpeech(position, AISPEECH id, volume, ?): a line in Cash's earpiece.
    e.bind("playdirectorspeech", [this](NativeCall& c) {
        directorSpeech_.push_back(c.integer(1));
        directorSpeaking_ = true; // until the session has it (IsDirectorSpeechPlaying right after)
        return Value::nil();
    });
    e.bind("setpedskintextureid", [this, entityArg](NativeCall& c) {
        Entity* en = entity(entityArg(c, 0));
        if (en && en->hunter)
            applySkin(*en->hunter, hunterRecords_[en->hunter], c.integer(1));
        return Value::nil();
    });
    // AISetSpeechTypes(hunter, types, on): only "all types" is told apart for now.
    e.bind("aisetspeechtypes", [this](NativeCall& c) {
        const uint32 h = handleOf(c.string(0));
        if (Entity* en = entity(h); en && en->hunter)
            en->hunter->setSpeech(c.boolean(2));
        return Value::nil();
    });
    e.bind("moveentity", [this, entityArg](NativeCall& c) {
        const uint32 h = entityArg(c, 0);
        if (h == playerHandle_)
            teleport_ = toScene(c.vec3(1));
        else if (Entity* en = entity(h); en && en->hunter)
        {
            // (Mall's OnCreate gathers a checkpoint's surviving hunters into a hunt pack.)
            Math::Vector3 at = toScene(c.vec3(1)) + Math::Vector3{0.0f, setup_.field->bodyHeight(), 0.0f};
            Math::Vector3 v{0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 40 && !setup_.field->resolveCharacter(at, v); ++i)
                at.y -= 0.25f;
            en->hunter->placeAt(at - Math::Vector3{0.0f, setup_.field->bodyHeight(), 0.0f});
            en->position = en->hunter->feet();
        }
        else if (en && en->kind == Kind::Placement && en->placement >= 0 && !moverOf(h))
        {
            // A placed thing (Mall's bar light swaps CJ_LIGHT_on_(L) and _off_(L), its TV light
            // moves to the screen): its model and its light go there (ours: not its collision).
            if (const Assets::InstPlacement* p = placementAt(en->placement))
            {
                const Math::Vector3 to = toScene(c.vec3(1));
                const Math::Vector3 origin = setup_.level->frame().placementOrigin(p->position);
                const Math::Matrix4x4 t = Math::Matrix4x4::translation(to - origin);
                for (Renderer::Mesh* mesh : setup_.level->meshesOf(*p))
                    mesh->setTransform(t);
                if (const LevelContent::LevelLight* ll = setup_.level->lightOf(en->placement))
                    ll->light->setPosition(ll->origin + (to - origin));
                en->position = to;
            }
        }
        return Value::nil();
    });
    // SetPlayerHeading(degrees): heading 0 faces level -Y like an unrotated Player_Inst
    // (LevelFrame::characterYaw); Jury_Turf's intro sets 0 where its Player_Inst says ~7.
    e.bind("setplayerheading", [this](NativeCall& c) {
        heading_ = Math::radians(c.real(0)) + Math::pi;
        return Value::nil();
    });

    // GetEntityView(entity): the way it faces, in the script frame (its axes are the scene's).
    e.bind("getentityview", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        float32 yaw = 0.0f;
        if (!en)
            return Value::vec3({0.0f, 0.0f, 0.0f});
        if (en->kind == Kind::Player)
            yaw = player_.yaw;
        else if (en->hunter)
            yaw = en->hunter->yaw();
        else if (const Assets::InstPlacement* p = placementAt(en->placement))
            yaw = LevelFrame::characterYaw(p->rotation);
        return Value::vec3({-std::sin(yaw), 0.0f, -std::cos(yaw)});
    });
    // SpawnEntityWithDirection(record, position, name, direction): a new entity of an
    // entityTypeData record, facing `direction` (Attic's second save point, the bonus levels'
    // and Cellblock's hunters).
    e.bind("spawnentitywithdirection", [this](NativeCall& c) {
        const Math::Vector3 dir = c.vec3(3);
        const float32 yaw = std::atan2(-dir.x, -dir.z); // yaw 0 faces scene -Z
        Assets::InstPlacement p;
        p.record = c.arg(0).s; // as the script spells them (the save keeps them so)
        p.internalName = c.arg(2).s;
        p.position = Checkpoint::instPosition(setup_.level->frame(), toScene(c.vec3(1)));
        p.rotation = Checkpoint::instYawRotation(yaw - Math::pi); // LevelFrame::characterYaw
        return Value::entity(addSpawned(std::move(p)));
    });

    // SpawnMovingEntity(record, position, name): as SpawnEntityWithDirection with no facing given
    // (the levels' hunters, Jury_Turf's TRAINING_GUY, first-aid kits). SpawnEntityWithVelocity
    // (record, position, name, velocity) and SpawnStaticEntity(record, position, name) the same
    // (ours: the thrown item lies where it lands below the point; the velocity is not used).
    const auto spawnAt = [this](NativeCall& c) {
        Assets::InstPlacement p;
        p.record = c.arg(0).s;
        p.internalName = c.arg(2).s;
        p.position = Checkpoint::instPosition(setup_.level->frame(), toScene(c.vec3(1)));
        p.rotation = Math::Vector4{0.0f, 0.0f, 0.0f, 1.0f};
        return Value::entity(addSpawned(std::move(p)));
    };
    e.bind("spawnmovingentity", spawnAt);
    e.bind("spawnentitywithvelocity", spawnAt);
    e.bind("spawnstaticentity", spawnAt);
    // AttachToEntity(trigger, entity): the trigger made at an offset rides along with it.
    e.bind("attachtoentity", [this, entityArg](NativeCall& c) {
        const Entity* trig = entity(entityArg(c, 0));
        const uint32 to = entityArg(c, 1);
        if (trig && trig->trigger >= 0 && entity(to))
        {
            Trigger& t = triggers_[static_cast<std::size_t>(trig->trigger)];
            t.attachedTo = to;
            t.attachOffset = t.a - toScene(Math::Vector3{0.0f, 0.0f, 0.0f}); // made at an offset
        }
        else if (trig && !trig->hunter && trig->placement >= 0 && entity(to))
            propLinks_.push_back({entityArg(c, 0), to, {0.0f, 0.0f, 0.0f}, false, {}}); // a prop rides along
        return Value::nil();
    });
    // CreateAndFireWeapon(record, position): a WC_EXPLODER goes off there (the levels' gas
    // drums): everyone within its sphere is hurt, the edge's DAMAGE / PLAYER_DAMAGE at its rim
    // and twice that at its centre (ours: the exe's falloff is not traced; no effect yet).
    e.bind("createandfireweapon", [this](NativeCall& c) {
        const auto x = tuning_.exploders.find(lower(c.string(0)));
        if (x == tuning_.exploders.end())
            return Value::nil();
        const Math::Vector3 at = toScene(c.vec3(1));
        if (!x->second.flashFx.empty())
            playFx(x->second.flashFx, at); // FLASH_FX (Drum_Explode)
        // Its scorch (0x4facc0 for a WC_EXPLODER): explos, 4..5 m across at a random angle.
        decals_.push_back({6, at, {0.0f, 1.0f, 0.0f}, drawBetween(4.0f, 5.0f)});
        const float32 r = std::max(0.5f, x->second.radius);
        auto scale = [&](const Math::Vector3& p) { return 2.0f - std::clamp((p - at).length() / r, 0.0f, 1.0f); };
        HunterWorld w = world(player_);
        for (const auto& h : hunters_)
            if (h->alive() && (h->feet() - at).length() <= r)
                h->hit(x->second.damage * scale(h->feet()), at, w, HitKind::Hard);
        if ((player_.feet - at).length() <= r)
            hurtPlayer(x->second.playerDamage * scale(player_.feet), at);
        // The blast (an SC_AREA shot) reaches the scripted things in it: OnStartExploding (event
        // 16) - an exploder drum goes off in turn, Asylum's exploding_wall comes down. Ours: by
        // the entity's origin within the radius and a metre more (its bounds), each once.
        for (std::size_t i = 0; i < entities_.size(); ++i)
        {
            const Entity& en = entities_[i];
            // (Not the thing going off itself: its origin is the blast's, 0.2 m under it.)
            const float32 gap = (en.position - at).length();
            if (en.hunter || en.kind != Kind::Placement || exploded_.contains(en.name) || gap > r + 1.0f || gap < 0.5f ||
                !scripts_.has(en.name, "OnStartExploding"))
                continue;
            exploded_.insert(en.name);
            scripts_.start(en.name, "OnStartExploding");
            std::cerr << "[runtime] the blast reaches " << en.name << "\n";
        }
        std::cerr << "[runtime] " << c.string(0) << " goes off\n";
        return Value::nil();
    });
    // IsPlayerCarryingGasCan (Scrap2): the gasoline can (CT_GASOLINE) in his hand.
    e.bind("isplayercarryinggascan", [this](NativeCall&) { return Value::boolean((equipped_ && equipped_->ct == 2) || gasCarried_ != 0); });
    // EntityIgnoreCollisions(entity, TRUE): nothing collides with it any more (Asylum's
    // exploding_wall once blown in; ours: its triangles and spheres leave the field); FALSE puts
    // them back (Attic's desk and chain door, Journo_Streets' window, Scrap's crusher). TurnOffPhysics
    // is accepted without effect.
    e.bind("entityignorecollisions", [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && !en->hunter && en->placement >= 0 && c.count() > 1)
        {
            const bool ignore = c.boolean(1);
            if (ignore)
                setup_.field->removeProp(en->placement);
            else
                setup_.field->restoreProp(en->placement);
            std::cerr << "[script] " << en->name << (ignore ? " ignores collisions" : " collides again") << std::endl;
        }
        return Value::nil();
    });
    e.bind("turnoffphysics", [](NativeCall&) { return Value::nil(); });

    // Switches and useables (CSwitch / CUseable).
    auto useableArg = [this, entityArg](NativeCall& c) -> Useable* {
        const Entity* en = entity(entityArg(c, 0));
        return en ? useableNamed(en->name) : nullptr;
    };
    // SetSwitchState(switch, 1 on / 0 off): its state, no events (0x4d0560).
    e.bind("setswitchstate", [useableArg](NativeCall& c) {
        if (Useable* u = useableArg(c))
            u->state = c.integer(1) == 1 ? 1 : 2;
        return Value::nil();
    });
    e.bind("useablesetstate", [useableArg](NativeCall& c) {
        if (Useable* u = useableArg(c))
            u->state = c.integer(1) == 1 ? 1 : 2;
        return Value::nil();
    });
    // GetSwitchState(switch): 1 when on (0x4d05f0), else 0.
    e.bind("getswitchstate", [useableArg](NativeCall& c) {
        const Useable* u = useableArg(c);
        return Value::integer(u && u->state == 1 ? 1 : 0);
    });
    // EnableUseable(useable, on): whether he can use it.
    e.bind("enableuseable", [useableArg](NativeCall& c) {
        if (Useable* u = useableArg(c))
            u->enabled = c.boolean(1);
        return Value::nil();
    });

    // Triggers.
    e.bind("settriggerradius", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->trigger >= 0)
            triggers_[static_cast<std::size_t>(en->trigger)].radius = std::max(0.2f, c.real(1));
        return Value::nil();
    });
    // SetTriggerType(trigger, TRIGGER_SPHERE): the only type the levels set.
    e.bind("settriggertype", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->trigger >= 0)
            triggers_[static_cast<std::size_t>(en->trigger)].sphere = true;
        return Value::nil();
    });
    e.bind("createspheretrigger", [this](NativeCall& c) {
        Trigger t;
        t.name = c.string(2);
        t.sphere = true;
        t.a = toScene(c.vec3(0));
        t.radius = std::max(0.2f, c.real(1));
        const uint32 h = handleOf(t.name) ? handleOf(t.name) : addEntity(t.name, Kind::Trigger, t.a);
        entity(h)->trigger = static_cast<int32>(triggers_.size());
        triggers_.push_back(t);
        scripts_.setEntityHandle(t.name, h);
        return Value::entity(h);
    });
    e.bind("createlinetrigger", [this](NativeCall& c) {
        Trigger t;
        t.name = c.string(2);
        t.sphere = false;
        t.a = toScene(c.vec3(0));
        t.b = toScene(c.vec3(1));
        t.radius = 0.75f;
        const uint32 h = handleOf(t.name) ? handleOf(t.name) : addEntity(t.name, Kind::Trigger, t.a);
        entity(h)->trigger = static_cast<int32>(triggers_.size());
        triggers_.push_back(t);
        scripts_.setEntityHandle(t.name, h);
        return Value::entity(h);
    });
    // ForceWeatherType(WEATHER_*, on): the level's weather record of that index (fog, sky).
    e.bind("forceweathertype", [this](NativeCall& c) {
        if (auto pak = Assets::PakFile::load(setup_.pakPath))
        {
            forcedWeather_ = LevelSetup::read(*pak, setup_.levelName, static_cast<int32>(c.integer(0)));
            levelSetup_ = *forcedWeather_;
            std::cerr << "[script] weather " << c.integer(0) << ": " << levelSetup_.summary() << "\n";
        }
        return Value::nil();
    });
    // SetHunterSkin / SetHunterHeadSkin(hunter, id): its skin variant (the body record's
    // SKIN_TEX; the head follows the body here).
    auto skin = [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            applySkin(*en->hunter, hunterRecords_[en->hunter], static_cast<int32>(c.integer(1)));
        return Value::nil();
    };
    e.bind("sethunterskin", skin);
    e.bind("sethunterheadskin", skin);
    e.bind("isscriptaudioslotloaded", [this](NativeCall& c) {
        const int64 slot = c.integer(0);
        return Value::boolean(slot >= 0 && slot < static_cast<int64>(scriptAudio_.size()) &&
                              scriptAudio_[static_cast<std::size_t>(slot)] >= 0);
    });
    // CreateBoxTrigger(corner, corner, name): an axis-aligned box (script frame corners).
    e.bind("createboxtrigger", [this](NativeCall& c) {
        Trigger t;
        t.name = c.string(2);
        t.sphere = false;
        t.box = true;
        t.a = toScene(c.vec3(0));
        t.b = toScene(c.vec3(1));
        const uint32 h = handleOf(t.name) ? handleOf(t.name) : addEntity(t.name, Kind::Trigger, (t.a + t.b) * 0.5f);
        entity(h)->trigger = static_cast<int32>(triggers_.size());
        triggers_.push_back(t);
        scripts_.setEntityHandle(t.name, h);
        return Value::entity(h);
    });
    // SetEntityScriptsFromEntity(template, entity): the entity runs its own copy of the
    // template's scripts (trigger templates, exploding drums).
    e.bind("setentityscriptsfromentity", [this, entityArg](NativeCall& c) {
        const Entity* to = entity(entityArg(c, 1));
        const std::string target = to ? to->name : c.string(1);
        const bool ok = scripts_.copyScripts(c.string(0), target);
        if (ok && to)
            scripts_.setEntityHandle(target, handleOf(target));
        return Value::boolean(ok);
    });
    auto triggerQuery = [this, entityArg](NativeCall& c, int what) {
        const Entity* trig = entity(entityArg(c, 0));
        if (!trig || trig->trigger < 0 || entityArg(c, 1) != playerHandle_)
            return Value::boolean(false);
        const Trigger& t = triggers_[static_cast<std::size_t>(trig->trigger)];
        return Value::boolean(what == 0 ? t.inside : (what == 1 ? t.entered : t.left));
    };
    e.bind("insidetrigger", [triggerQuery](NativeCall& c) { return triggerQuery(c, 0); });
    // TriggerAddEntityClass(trigger, COL_ mask) (native 0x20d, handler 0x57c0ae: CTrigger +0x180
    // |= mask) and the queries over the entities of those classes in it, by entity class
    // (0x431820): InsideTriggerType (0x1d5 -> 0x4d28d0: already in or came in), EnteredTriggerType
    // (0x1d1 -> 0x4d2860: came in this frame), GetNumberOfTypesInsideTrigger (0x1da -> 0x4d2a30)
    // and GetNameOfTypeInTriggerFromIndex (0x1db -> 0x4d2ae0: the n-th, from 1, those already in
    // first).
    auto triggerOf = [this, entityArg](NativeCall& c) -> Trigger* {
        const Entity* en = entity(entityArg(c, 0));
        return en && en->trigger >= 0 ? &triggers_[static_cast<std::size_t>(en->trigger)] : nullptr;
    };
    e.bind("triggeraddentityclass", [triggerOf](NativeCall& c) {
        if (Trigger* t = triggerOf(c))
            t->classes |= static_cast<uint32>(c.integer(1));
        return Value::nil();
    });
    auto countOf = [](const Trigger& t, int32 ec, bool arrivalsOnly) {
        int32 n = 0;
        for (const auto& o : t.arrivals)
            n += o.second == ec;
        if (!arrivalsOnly)
            for (const auto& o : t.occupants)
                n += o.second == ec;
        return n;
    };
    e.bind("insidetriggertype", [triggerOf, countOf](NativeCall& c) {
        const Trigger* t = triggerOf(c);
        return Value::boolean(t && countOf(*t, static_cast<int32>(c.integer(1)), false) > 0);
    });
    e.bind("enteredtriggertype", [triggerOf, countOf](NativeCall& c) {
        const Trigger* t = triggerOf(c);
        return Value::boolean(t && countOf(*t, static_cast<int32>(c.integer(1)), true) > 0);
    });
    e.bind("getnumberoftypesinsidetrigger", [triggerOf, countOf](NativeCall& c) {
        const Trigger* t = triggerOf(c);
        return Value::integer(t ? countOf(*t, static_cast<int32>(c.integer(1)), false) : 0);
    });
    e.bind("getnameoftypeintriggerfromindex", [triggerOf](NativeCall& c) {
        const Trigger* t = triggerOf(c);
        const int32 ec = static_cast<int32>(c.integer(1));
        int64 left = c.integer(2);
        if (t)
            for (const auto* list : {&t->occupants, &t->arrivals})
                for (const auto& [name, cls] : *list)
                    if (cls == ec && --left == 0)
                        return Value::string(name);
        return Value::string("");
    });
    e.bind("enteredtrigger", [triggerQuery](NativeCall& c) { return triggerQuery(c, 1); });
    e.bind("lefttrigger", [triggerQuery](NativeCall& c) { return triggerQuery(c, 2); });

    // Game state queries: a first play-through on normal difficulty, no cut-scene running.
    // The DIFFICULTY setting: 0 FETISH (DIFFICULTY_EASY), 1 HARDCORE (ours: the exe's handler not traced).
    e.bind("getdifficultylevel", [this](NativeCall&) { return Value::integer(hardDifficulty_ ? 1 : 0); });
    e.bind("getplayerlevelrestarts", [](NativeCall&) { return Value::integer(0); });
    e.bind("thislevelbeencompletedalready", [](NativeCall&) { return Value::boolean(false); });
    // Cut-scenes: the scripts take the camera (CutSceneStart .. CutSceneEnd), place it, aim it
    // at an entity or run it along a FOLLOW / LOOKAT spline pair.
    e.bind("cutscenestart", [this, none](NativeCall&) {
        cutscene_ = true;
        return none();
    });
    e.bind("cutsceneend", [this, none](NativeCall&) {
        endCutscene();
        return none();
    });
    e.bind("iscutsceneinprogress", [this](NativeCall&) { return Value::boolean(cutscene_); });
    // AICutSceneEntityEnable(name, on): this hunter keeps acting during cut-scenes.
    e.bind("aicutsceneallentitiesenable", [this, none](NativeCall& c) {
        cutsceneAllActors_ = c.boolean(0);
        return none();
    });
    e.bind("aicutsceneentityenable", [this, none](NativeCall& c) {
        const std::string name = c.string(0);
        std::erase(cutsceneActors_, name);
        if (c.boolean(1))
            cutsceneActors_.push_back(name);
        return none();
    });
    e.bind("hascutscenebeenplayed", [this](NativeCall& c) {
        return Value::boolean(std::find(cutscenesPlayed_.begin(), cutscenesPlayed_.end(), c.integer(0)) != cutscenesPlayed_.end());
    });
    e.bind("markcutsceneasplayed", [this, none](NativeCall& c) {
        cutscenesPlayed_.push_back(static_cast<int32>(c.integer(0)));
        return none();
    });
    e.bind("setcameraposition", [this, none](NativeCall& c) {
        camPosition_ = toScene(c.vec3(0));
        return none();
    });
    e.bind("setcameraview", [this, none](NativeCall& c) {
        camView_ = c.vec3(0); // a direction: the frame only moves the origin
        return none();
    });
    e.bind("cameraforcelookatentity", [this, entityArg, none](NativeCall& c) {
        camLookAt_ = entityArg(c, 0);
        return none();
    });
    e.bind("camerastoplookatentity", [this, none](NativeCall&) {
        camLookAt_ = 0;
        return none();
    });
    // PlaySplineFileDefault(name): runs the spline for its DURATION; false if the level has none.
    e.bind("playsplinefiledefault", [this](NativeCall& c) {
        const CameraSpline* s = splines_.find(c.rawString(0));
        if (!s)
            return Value::boolean(false);
        (s->follow ? followSpline_ : lookSpline_) = s;
        (s->follow ? followT_ : lookT_) = 0.0f;
        (s->follow ? followRate_ : lookRate_) = 1.0f;
        return Value::boolean(true);
    });
    // PlaySplineFile(name, seconds, ?, ?, ?): as PlaySplineFileDefault, run over `seconds` (the
    // three flags are not read: ours).
    e.bind("playsplinefile", [this](NativeCall& c) {
        const CameraSpline* s = splines_.find(c.rawString(0));
        if (!s)
            return Value::boolean(false);
        (s->follow ? followSpline_ : lookSpline_) = s;
        (s->follow ? followT_ : lookT_) = 0.0f;
        const float32 seconds = c.count() > 1 ? c.real(1) : 0.0f;
        (s->follow ? followRate_ : lookRate_) = seconds > 0.05f ? s->duration / seconds : 1.0f;
        return Value::boolean(true);
    });
    // GetCameraPosition: where the camera is (script frame).
    e.bind("getcameraposition", [this](NativeCall&) { return Value::vec3(toScript(viewEye_)); });
    e.bind("issplineplaying", [this](NativeCall&) { return Value::boolean(followSpline_ || lookSpline_); });
    // SetZoomLerp(from, to, seconds): the camera zooms (field of view / zoom) over the time.
    e.bind("setzoomlerp", [this, none](NativeCall& c) {
        zoomFrom_ = std::max(0.1f, c.real(0));
        zoomTo_ = std::max(0.1f, c.real(1));
        zoomTime_ = std::max(0.0f, c.real(2));
        zoomT_ = 0.0f;
        return none();
    });

    // Inventory: IsNamedItemInInventory(entity, CT_x) gives the item's slot, -1 when he has none;
    // GetLastItemPickedUp(entity) the eCollectableType of the latest pickup (OnPickUpInventoryItem).
    e.bind("isnameditemininventory", [this](NativeCall& c) {
        const int64 ct = c.integer(1);
        for (const ItemType* t : inventory_)
            if (t->ct == ct)
                return Value::integer(slotOf(t));
        return Value::integer(-1);
    });
    e.bind("getlastitempickedup", [this](NativeCall&) { return Value::integer(lastPickedUp_); });
    // RemoveItemFromInventory(holder, item): the item's type leaves his inventory (and his hand).
    // Mall takes the tape and the handycam as they are picked up (GotVideo / GotCamera).
    e.bind("removeitemfrominventory", [this, entityArg, none](NativeCall& c) {
        const ItemType* type = nullptr;
        if (const Entity* en = entity(entityArg(c, 1)))
        {
            for (const Assets::InstPlacement& p : setup_.level->placements())
                if (p.index == en->placement)
                    type = items_.find(p.record);
            if (!type)
                type = items_.find(en->name);
        }
        if (!type)
            type = pickedUp_; // (ours: a nil item is the one just picked up)
        if (const auto at = std::find(inventory_.begin(), inventory_.end(), type); type && at != inventory_.end())
        {
            inventory_.erase(at);
            inventoryNames_.erase(type);
            if (equipped_ == type)
                equipped_ = nullptr;
            std::cerr << "[runtime] " << type->record << " taken from the inventory\n";
        }
        return none();
    });
    // --- G3: the inventory as the scripts address it. The handlers (traced by their native ids):
    // the inventory is ped +0x150 (an array the scripts index by slot) with the current slot at
    // +0x154 (-1 none); removing calls the ped's +0x88 (item, 1). Ours keeps the items and
    // gives them the exe's slot numbers (slotOf). ---
    const auto takeOut = [this](const ItemType* t) {
        if (const auto at = std::find(inventory_.begin(), inventory_.end(), t); t && at != inventory_.end())
        {
            inventory_.erase(at);
            inventoryNames_.erase(t);
            if (equipped_ == t)
                equipped_ = nullptr;
            std::cerr << "[runtime] " << t->record << " taken from the inventory\n";
        }
    };
    const auto isGun = [this](const ItemType* t) { return t && t->weapon && tuning_.firearm(t->weaponType); };
    // HasPlayerWeaponTypeAtSlot(slot, kind) (0x2e7, handler 0x57ef4b): a weapon (0x431ae0) in that
    // slot whose class (0x4f8150 on its weapon data) is 0 / 1 for kind 0, 3 for kind 1 (ours:
    // kind 1 a gun, kind 0 any other weapon).
    e.bind("hasplayerweapontypeatslot", [this, isGun](NativeCall& c) {
        const ItemType* t = itemAtSlot(c.integer(0));
        if (!t || !t->weapon)
            return Value::boolean(false);
        return Value::boolean(c.integer(1) == 1 ? isGun(t) : !isGun(t));
    });
    // RemoveItemFromInventoryAtSlot(entity, slot) (0xbc, handler 0x47b9a4).
    e.bind("removeitemfrominventoryatslot", [this, entityArg, takeOut](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->kind == Kind::Player)
            takeOut(itemAtSlot(c.integer(1)));
        return Value::nil();
    });
    // RemoveCurrentInventoryItem(entity) (0xbd, handler 0x47b9ec): what is in its hand goes (the
    // Warden's and Piggsy's weapons).
    e.bind("removecurrentinventoryitem", [this, entityArg, takeOut](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)))
        {
            if (en->kind == Kind::Player)
                takeOut(equipped_);
            else if (en->hunter)
                disarmHunter(*en->hunter);
        }
        return Value::nil();
    });
    // SetInventoryItemAsCurrent(entity, slot) (0xbf -> 0x4abe70): to its hand. A hunter's slots
    // are its two weapons (0 melee, 1 firearm; ours).
    e.bind("setinventoryitemascurrent", [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)))
        {
            const int64 slot = c.integer(1);
            if (en->kind == Kind::Player && !weaponSelectionDisabled_)
            {
                if (const ItemType* t = itemAtSlot(slot); t && t->weapon)
                    equipped_ = t;
            }
            else if (en->hunter)
                if (const auto arms = hunterArms_.find(en->hunter); arms != hunterArms_.end() && slot >= 0 && slot < 2)
                    if (const ItemType* t = arms->second[static_cast<std::size_t>(slot)]; t && t != en->hunter->weapon())
                        armHunter(*en->hunter, t);
        }
        return Value::nil();
    });
    // GetCurrentInventoryItemType(entity) (0x129 -> 0x47e070): the CT of what is in its hand (0
    // empty-handed).
    e.bind("getcurrentinventoryitemtype", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        const ItemType* t = !en ? nullptr : en->kind == Kind::Player ? equipped_ : (en->hunter ? en->hunter->weapon() : nullptr);
        return Value::integer(t ? t->ct : 0);
    });
    // GetIndexFromInventoryItemType(entity, CT) (0xc5 -> 0x4c67f0): its slot, -1 none.
    e.bind("getindexfrominventoryitemtype", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        const int64 ct = c.integer(1);
        if (en && en->kind == Kind::Player)
        {
            for (const ItemType* t : inventory_)
                if (t->ct == ct)
                    return Value::integer(slotOf(t));
        }
        else if (en && en->hunter)
            if (const auto arms = hunterArms_.find(en->hunter); arms != hunterArms_.end())
                for (int32 i = 0; i < 2; ++i)
                    if (arms->second[static_cast<std::size_t>(i)] && arms->second[static_cast<std::size_t>(i)]->ct == ct)
                        return Value::integer(i);
        return Value::integer(-1);
    });
    // GetIndexFromInventoryItemName(entity, name) (0xc4): the slot of the item that was that
    // entity (or is that record), -1 none.
    e.bind("getindexfrominventoryitemname", [this](NativeCall& c) {
        const std::string name = lower(c.string(1));
        for (const ItemType* t : inventory_)
            if (const auto n = inventoryNames_.find(t); (n != inventoryNames_.end() && lower(n->second) == name) || lower(t->record) == name)
                return Value::integer(slotOf(t));
        return Value::integer(-1);
    });
    // IsItemInInventory(entity, item) (0xc0 -> 0x492830): he carries that entity.
    e.bind("isitemininventory", [this, entityArg](NativeCall& c) {
        const Entity* item = entity(entityArg(c, 1));
        if (!item)
            return Value::boolean(false);
        for (const auto& [t, name] : inventoryNames_)
            if (lower(name) == item->name)
                return Value::boolean(true);
        return Value::boolean(false);
    });
    // GetInventoryItemFromName(entity, record): non-NIL when it carries one (ours: its own
    // handle; the journalist's guns, Piggsy's chainsaw).
    e.bind("getinventoryitemfromname", [this, entityArg](NativeCall& c) {
        const uint32 h = entityArg(c, 0);
        const Entity* en = entity(h);
        const std::string record = lower(c.string(1));
        bool has = false;
        if (en && en->kind == Kind::Player)
            for (const ItemType* t : inventory_)
                has = has || lower(t->record) == record;
        else if (en && en->hunter)
            has = en->hunter->weapon() && lower(en->hunter->weapon()->record) == record;
        return has ? Value::entity(h) : Value::nil();
    });
    // IsEntityHoldingAmmoWeapon(entity) (0x24d, handler 0x57cdb3): a gun in its hand.
    e.bind("isentityholdingammoweapon", [this, entityArg, isGun](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        if (!en)
            return Value::boolean(false);
        return Value::boolean(en->kind == Kind::Player ? isGun(equipped_) : (en->hunter && isGun(en->hunter->weapon())));
    });
    // ReturnAmmoOfInventoryWeapon(entity, CT) (0x2dd -> 0x4929e0), InventoryWeaponGetAmmoAmount
    // (0x1b7, in the gun) and InventoryWeaponGetReserveAmmoAmount (0x1b8, spare): the player's
    // rounds for that gun (0 when he has none; a hunter's are not counted: 0).
    const auto rounds = [this, entityArg, isGun](NativeCall& c, int what) {
        const Entity* en = entity(entityArg(c, 0));
        const ItemType* t = items_.byCollectable(static_cast<int32>(c.integer(1)));
        if (!en || en->kind != Kind::Player || !isGun(t) || std::find(inventory_.begin(), inventory_.end(), t) == inventory_.end())
            return Value::integer(0);
        const std::string w = lower(t->weaponType);
        return Value::integer(what == 1 ? ammo_.loaded(w) : what == 2 ? ammo_.spare(w) : ammo_.loaded(w) + ammo_.spare(w));
    };
    e.bind("returnammoofinventoryweapon", [rounds](NativeCall& c) { return rounds(c, 0); });
    e.bind("inventoryweapongetammoamount", [rounds](NativeCall& c) { return rounds(c, 1); });
    e.bind("inventoryweapongetreserveammoamount", [rounds](NativeCall& c) { return rounds(c, 2); });
    // SetWeaponAmmo(entity, rounds) (0x269 -> 0x4f8b40): a gun lying in the level holds that many.
    e.bind("setweaponammo", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->placement >= 0)
            for (Pickup& k : pickups_)
                if (k.placement == en->placement)
                    k.rounds = static_cast<int32>(c.integer(1));
        return Value::nil();
    });
    // TemporarySetPlayerToFists (0x2bb -> 0x46e5b0) / RestorePlayerWeapon (0x2bc -> 0x46e8b0):
    // his hands are emptied for a cut-scene, then what he held goes back.
    e.bind("temporarysetplayertofists", [this](NativeCall&) {
        if (!fistsTemporary_)
        {
            fistsSaved_ = equipped_;
            fistsTemporary_ = true;
        }
        equipped_ = nullptr;
        return Value::nil();
    });
    e.bind("restoreplayerweapon", [this](NativeCall&) {
        if (fistsTemporary_ && std::find(inventory_.begin(), inventory_.end(), fistsSaved_) != inventory_.end())
            equipped_ = fistsSaved_;
        fistsTemporary_ = false;
        fistsSaved_ = nullptr;
        return Value::nil();
    });
    // DisableWeaponSelection(on) (0x2f6): he cannot change weapons.
    e.bind("disableweaponselection", [this](NativeCall& c) {
        weaponSelectionDisabled_ = c.boolean(0);
        return Value::nil();
    });

    // On-screen text (GameText): the hint box, its settings, subtitles and the level goals.
    e.bind("displaygametext", [this, none](NativeCall& c) {
        std::cerr << "[script] game text '" << c.rawString(0) << "'\n";
        text_.display(c.rawString(0));
        return none();
    });
    e.bind("isgametextdisplaying", [this](NativeCall&) { return Value::boolean(text_.displaying()); });
    e.bind("setgametextdisplaytime", [this, none](NativeCall& c) {
        text_.setDisplayTime(static_cast<float32>(c.integer(0)) * 0.001f);
        return none();
    });
    e.bind("setgametextboxposition", [this, none](NativeCall& c) {
        text_.setBoxPosition(c.real(0), c.real(1));
        return none();
    });
    e.bind("setgametextboxsize", [this, none](NativeCall& c) {
        text_.setBoxSize(c.real(0), c.real(1));
        return none();
    });
    e.bind("setgametextteletype", [this, none](NativeCall& c) {
        text_.setTeletype(c.boolean(0));
        return none();
    });
    e.bind("killsubtitletext", [this, none](NativeCall&) {
        text_.killSubtitle();
        return none();
    });
    e.bind("setlevelgoal", [this, none](NativeCall& c) {
        text_.setGoal(c.rawString(0));
        return none();
    });
    e.bind("clearlevelgoal", [this, none](NativeCall& c) {
        text_.clearGoal(c.rawString(0));
        return none();
    });
    e.bind("clearalllevelgoals", [this, none](NativeCall&) {
        for (const std::string& g : std::vector<std::string>(text_.goals()))
            text_.clearGoal(g);
        return none();
    });
    // What the player is doing (the session's snapshot of him this frame).
    e.bind("isplayercarryingbody", [this](NativeCall&) { return Value::boolean(player_.carryingBody); });
    e.bind("isplayerwallsquashed", [this](NativeCall&) { return Value::boolean(player_.wallSquashed); });
    e.bind("isplayersneaking", [this](NativeCall&) { return Value::boolean(player_.sneaking); });
    e.bind("isplayerwalking", [this](NativeCall&) { return Value::boolean(!player_.sneaking && player_.gait == 1); });
    e.bind("isplayerrunning", [this](NativeCall&) { return Value::boolean(!player_.sneaking && player_.gait == 2); });
    e.bind("isplayersprinting", [this](NativeCall&) { return Value::boolean(!player_.sneaking && player_.gait == 3); });
    e.bind("iswhitenoisedisplaying", [this](NativeCall&) { return Value::boolean(look_.noise > 0.01f); });
    e.bind("setnumberofkillablehuntersinlevel", [this, none](NativeCall& c) {
        killable_ = {c.integer(0), c.integer(1)}; // the score target (0x5d2430)
        return none();
    });
    e.bind("setlevelcompleted", [this, none](NativeCall&) {
        std::cerr << "[script] level completed\n";
        levelCompleted_ = true;
        return none();
    });

    // Doors (Doors): frozen until unfrozen, then pushed open unless locked; the override angle
    // swings one by itself.
    auto doorArg = [this, entityArg](NativeCall& c) -> Doors::Door* {
        const Entity* en = entity(entityArg(c, 0));
        return doors_.find(en ? en->name : c.string(0));
    };
    // FreezeEntity(door): it stays shut until unfrozen; IsEntityLocked(door).
    e.bind("freezeentity", [doorArg, none](NativeCall& c) {
        if (Doors::Door* d = doorArg(c))
            d->frozen = true;
        return none();
    });
    // SetSlideDoorAjarDistance(door, metres) / SetSlideDoorSpeed(door, metres a second).
    e.bind("setslidedoorajardistance", [doorArg](NativeCall& c) {
        if (Doors::Door* d = doorArg(c); d && d->slide)
            d->ajar = std::max(0.0f, c.real(1));
        return Value::nil();
    });
    e.bind("setslidedoorspeed", [doorArg](NativeCall& c) {
        if (Doors::Door* d = doorArg(c); d && d->slide)
            d->slideSpeed = std::max(0.01f, c.real(1));
        return Value::nil();
    });
    // Movers: SetMoverSpeed(mover, metres a second), SetMoverIdlePosition(mover, position),
    // MoveMoverToIdlePosition(mover); SetMoverAccel / SetMoverIdlePosMargin accepted.
    e.bind("setmoverspeed", [this, entityArg](NativeCall& c) {
        if (Mover* m = moverOf(entityArg(c, 0)))
            m->speed = std::max(0.01f, c.real(1));
        return Value::nil();
    });
    e.bind("setmoveridleposition", [this, entityArg](NativeCall& c) {
        if (Mover* m = moverOf(entityArg(c, 0)))
            m->target = toScene(c.vec3(1)) - m->origin;
        return Value::nil();
    });
    e.bind("movemovertoidleposition", [this, entityArg](NativeCall& c) {
        if (Mover* m = moverOf(entityArg(c, 0)))
        {
            m->moving = true;
            std::cerr << "[mover] " << m->name << " goes to (" << m->target.x << ", " << m->target.y << ", " << m->target.z
                      << ") from its place at " << m->speed << " m/s" << std::endl;
        }
        return Value::nil();
    });
    for (const char* name : {"setmoveraccel", "setmoveridleposmargin"})
        e.bind(name, [](NativeCall&) { return Value::nil(); });
    e.bind("isentitylocked", [doorArg](NativeCall& c) {
        const Doors::Door* d = doorArg(c);
        return Value::boolean(d && d->locked);
    });
    e.bind("unfreezeentity", [doorArg, none](NativeCall& c) {
        if (Doors::Door* d = doorArg(c))
            d->frozen = false;
        return none();
    });
    e.bind("lockentity", [doorArg, none](NativeCall& c) {
        if (Doors::Door* d = doorArg(c))
            d->locked = true;
        return none();
    });
    e.bind("unlockentity", [doorArg, none](NativeCall& c) {
        if (Doors::Door* d = doorArg(c))
        {
            d->locked = false;
            d->frozen = false;
            std::cerr << "[script] door " << d->name << " unlocked\n";
        }
        return none();
    });
    e.bind("setdooropenanglein", [doorArg, none](NativeCall& c) {
        if (Doors::Door* d = doorArg(c))
            d->openIn = Math::radians(c.real(1));
        return none();
    });
    e.bind("setdooropenangleout", [doorArg, none](NativeCall& c) {
        if (Doors::Door* d = doorArg(c))
            d->openOut = Math::radians(c.real(1));
        return none();
    });
    // GraphModifyConnections(entity, AISCRIPT_GRAPHLINK_ALLOW_NOTHING / _EVERYTHING): closes or
    // opens the AI graph links across a door's leaf, or near another entity (a trigger).
    e.bind("graphmodifyconnections", [this, entityArg, doorArg](NativeCall& c) {
        const bool open = c.integer(1) != 0;
        Math::Vector3 centre{0.0f, 0.0f, 0.0f};
        float32 reach = 2.0f;
        if (const Doors::Door* d = doorArg(c))
        {
            centre = Doors::closedCentre(*d);
            reach = Doors::halfWidth(*d) + 0.3f;
        }
        else if (const Entity* en = entity(entityArg(c, 0)))
            centre = en->position;
        else
            return Value::boolean(false);
        const std::size_t n = nav_.setLinksNear(centre, reach, open);
        std::cerr << "[script] graph links " << (open ? "opened" : "closed") << ": " << n << " near " << (entity(entityArg(c, 0)) ? entity(entityArg(c, 0))->name : c.rawString(0))
                  << " (" << nav_.closedLinks() << " closed)\n";
        return Value::boolean(n > 0);
    });
    e.bind("setdooroverrideangle", [doorArg, none](NativeCall& c) {
        if (Doors::Door* d = doorArg(c))
        {
            d->overrideAngle = Math::radians(c.real(1));
            d->driving = true;
        }
        return none();
    });

    // The cut-scene look: hand-held sway, camcorder effects, white-noise bursts at cuts.
    e.bind("handcamsetactive", [this, none](NativeCall& c) {
        look_.handCam = c.boolean(0);
        return none();
    });
    // HandCamSetAll(period, braking, kick, spring, unused) (0x1e4): the shake's yaw and pitch, copied
    // to its roll (0x57f5b0).
    e.bind("handcamsetall", [this, none](NativeCall& c) {
        HandCam::Set s;
        s.period = static_cast<int32>(c.integer(0));
        s.braking = c.real(1);
        s.kick = c.real(2);
        s.spring = c.real(3);
        look_.handCamLook = look_.handCamRoll = s;
        return none();
    });
    e.bind("handcamsetalleffects", [this, none](NativeCall& c) {
        look_.allEffects = c.boolean(0);
        look_.camcorder.fill(c.boolean(0)); // 0x277 -> 0x58db90
        return none();
    });
    e.bind("handcamsetothereffects", [this, none](NativeCall& c) {
        look_.otherEffects = c.boolean(0);
        look_.camcorder[4] = c.boolean(0); // 0x276 -> 0x58db80
        return none();
    });
    // The camcorder overlay's parts one by one (natives 0x25f..0x262 -> 0x58db40..0x58db70).
    e.bind("handcamsetvideoeffecttimecode", [this, none](NativeCall& c) {
        look_.camcorder[0] = c.boolean(0);
        return none();
    });
    e.bind("handcamsetvideoeffectrecorddot", [this, none](NativeCall& c) {
        look_.camcorder[1] = c.boolean(0);
        return none();
    });
    e.bind("handcamsetvideoeffectfuzz", [this, none](NativeCall& c) {
        look_.camcorder[2] = c.boolean(0);
        return none();
    });
    e.bind("handcamsetvideoeffectscrollbar", [this, none](NativeCall& c) {
        look_.camcorder[3] = c.boolean(0);
        return none();
    });
    // WhiteNoiseSetVal(n): a burst of video static (the scripts use 5 and 10 at camera cuts).
    e.bind("whitenoisesetval", [this, none](NativeCall& c) {
        look_.noise = std::clamp(static_cast<float32>(c.integer(0)) / 10.0f, look_.noise, 1.0f);
        return none();
    });
    // The HUD: which parts show and which flash (the tutorial points them out).
    e.bind("drawhud", [this, none](NativeCall& c) {
        look_.drawHud = c.boolean(0);
        return none();
    });
    e.bind("sethudflag", [this, none](NativeCall& c) {
        look_.hudFlags = static_cast<uint32>(c.integer(0)) & 0xffu;
        return none();
    });
    e.bind("togglehudflag", [this, none](NativeCall& c) {
        const uint32 bit = static_cast<uint32>(c.integer(0)) & 0xffu;
        look_.hudFlags = c.boolean(1) ? (look_.hudFlags | bit) : (look_.hudFlags & ~bit);
        return none();
    });
    e.bind("hudtoggleflashflags", [this, none](NativeCall& c) {
        const uint32 bit = static_cast<uint32>(c.integer(0)) & 0xffu;
        look_.hudFlash = c.boolean(1) ? (look_.hudFlash | bit) : (look_.hudFlash & ~bit);
        return none();
    });

    // The radar: AISetHunterOnRadar(hunter, on) keeps a hunter on it or off it;
    // RadarPositionSetEntity(entity, MAP_COLOR) / RadarPositionClearEntity(entity) mark an entity
    // (10 slots, the exe 0x7ca4f0); AITriggerSound(sound, entity) and
    // AITriggerSoundKnownLocation ring out on it (the hunters' side of them is not done yet), the
    // ...NoRadar forms do not.
    e.bind("aisethunteronradar", [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            radarTracks_[en->hunter].onRadar = c.boolean(1);
        return Value::nil();
    });
    e.bind("radarpositionsetentity", [this, entityArg](NativeCall& c) {
        const uint32 h = entityArg(c, 0);
        auto slot = std::find_if(radarScript_.begin(), radarScript_.end(), [h](const auto& s) { return s.first == h; });
        if (slot == radarScript_.end())
            slot = std::find_if(radarScript_.begin(), radarScript_.end(), [](const auto& s) { return s.first == 0u; });
        if (slot != radarScript_.end() && h)
            *slot = {h, c.integer(1)};
        return Value::nil();
    });
    e.bind("radarpositionclearentity", [this, entityArg](NativeCall& c) {
        const uint32 h = entityArg(c, 0);
        for (auto& s : radarScript_)
            if (s.first == h)
                s = {};
        return Value::nil();
    });
    // The hunters hear it where the entity is, as they hear a thrown bottle land (AISounds.ini:
    // its volume and interest), and go to look there. Ours: the ...KnownLocation forms the same
    // (a hunter hearing one goes to that spot either way); their native handlers are not traced.
    auto triggerSound = [this, entityArg](NativeCall& c, bool radar) {
        const Entity* en = entity(entityArg(c, 1));
        if (en)
        {
            const Math::Vector3 at = en->kind == Kind::Player ? player_.feet : (en->hunter ? en->hunter->feet() : en->position);
            const std::string name = c.string(0);
            worldSounds_.push_back({name, at});
            if (radar)
                radarNoise_.emplace_back(at, soundRange(name.c_str()));
        }
        return Value::nil();
    };
    for (const char* name : {"aitriggersound", "aitriggersoundknownlocation", "aitriggersoundlocationknown"})
        e.bind(name, [triggerSound](NativeCall& c) { return triggerSound(c, true); });
    for (const char* name : {"aitriggersoundnoradar", "aitriggersoundknownlocationnoradar"})
        e.bind(name, [triggerSound](NativeCall& c) { return triggerSound(c, false); });

    // GetEntityName(entity): its name ('' for none) - the scripts pass it to every AI call.
    e.bind("getentityname", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        return Value::string(en ? en->name : std::string());
    });
    // IntegerToString(var, n): n's digits into `var` (Mall builds 'HUNTER' + n with it).
    e.bind("integertostring", [](NativeCall& c) {
        c.set(0, Value::string(std::to_string(c.arg(1).asInt())));
        return Value::nil();
    });
    // StringCat(var, a, b, ...): the pieces joined into `var` (numbers as digits).
    e.bind("stringcat", [](NativeCall& c) {
        std::string out;
        for (std::size_t i = 1; i < c.count(); ++i)
        {
            const Value& v = c.arg(i);
            if (v.type == Script::ValueType::String)
                out += v.s;
            else if (v.type == Script::ValueType::Int || v.type == Script::ValueType::Bool)
                out += std::to_string(v.asInt());
            else if (v.type == Script::ValueType::Real)
                out += std::to_string(v.asFloat());
        }
        c.set(0, Value::string(out));
        return Value::string(out);
    });
    e.bind("killscript", [this](NativeCall& c) {
        scripts_.killScript(c.string(0), c.string(1));
        return Value::nil();
    });
    // AINumberInSubPack(leader, subpack): how many of its hunters are alive.
    e.bind("ainumberinsubpack", [this](NativeCall& c) {
        int64 n = 0;
        for (const auto& h : hunters_)
            if (h->alive())
                if (const Subpack* sp = director_.subpackOf(h->name()); sp && Assets::iequals(sp->name, c.string(1)))
                    ++n;
        return Value::integer(n);
    });
    e.bind("setplayercontrollable", [this](NativeCall& c) {
        playerControllable_ = c.boolean(0);
        return Value::nil();
    });
    // --- Lights (H). ---
    // SwitchLightOn / SwitchLightOff(entity) (0xda -> 0x47c583 / 0xd9 -> 0x47c5a8): a light entity
    // (0x481ac0) lit (0x4cd8a0) or darkened (0x4cd8e0). SetLightFlicker(entity, duration ms (-1
    // for ever), lit ms, dark ms, off at the end) (0xde -> 0x47c6d5): +0x14c = 2, +0x150 = +0x154
    // = duration, +0x158 lit, +0x160 dark, +0x168 = 0, +0x16c the last.
    e.bind("switchlighton", [this, entityArg](NativeCall& c) {
        if (LightState* l = lightOf(entityArg(c, 0)))
        {
            l->on = true;
            if (debugLog_)
                std::cerr << "[light] " << entity(entityArg(c, 0))->name << " on" << std::endl;
        }
        return Value::nil();
    });
    e.bind("switchlightoff", [this, entityArg](NativeCall& c) {
        if (LightState* l = lightOf(entityArg(c, 0)))
        {
            l->on = false;
            if (debugLog_)
                std::cerr << "[light] " << entity(entityArg(c, 0))->name << " off" << std::endl;
        }
        return Value::nil();
    });
    e.bind("setlightflicker", [this, entityArg](NativeCall& c) {
        if (LightState* l = lightOf(entityArg(c, 0)))
        {
            const int32 duration = static_cast<int32>(c.integer(1));
            l->flicker = 2;
            l->forever = duration == -1;
            l->left = static_cast<float32>(duration);
            l->onMs = static_cast<float32>(c.integer(2));
            l->offMs = static_cast<float32>(c.integer(3));
            l->timer = 0.0f;
            l->offAtEnd = c.integer(4) != 0;
            if (debugLog_)
                std::cerr << "[light] " << entity(entityArg(c, 0))->name << " flickers " << duration << " ms" << std::endl;
        }
        return Value::nil();
    });
    // --- Particle effects (H; ours: the handlers are not traced). ---
    // CreateMhFx(name, position, flag) / CreateKillableMHFX(name, position, flag): an FX project
    // system there (Drum_Explode, FLM001 fire, FXSMOK3 smoke, the sirens); the killable one's id
    // ends it with KillMHFX(id).
    const auto mhfx = [this](NativeCall& c) {
        const Math::Vector3 at = toScene(c.vec3(1));
        const int32 id = playFx(c.string(0), at);
        if (Assets::iequals(c.string(0), "FLM001") || Assets::iequals(c.string(0), "fxwater"))
            addMusicSource(at, 0); // the music's fire (0x57dc13; not CreateKillableMHFX's)
        if (debugLog_)
            std::cerr << "[fx] " << c.string(0) << (id ? " at (" : " (no such effect) at (") << at.x << ", " << at.z << ") y " << at.y
                      << std::endl;
        return Value::integer(id);
    };
    e.bind("setmaxnumberofrats", [this](NativeCall& c) {
        ratLimit_ = std::clamp(static_cast<int32>(c.integer(0)), 0, 16);
        return Value::nil();
    });
    e.bind("createmhfx", mhfx);
    e.bind("createkillablemhfx", [this](NativeCall& c) { return Value::integer(playFx(c.string(0), toScene(c.vec3(1)))); });
    e.bind("killmhfx", [this](NativeCall& c) {
        fx_.kill(static_cast<int32>(c.integer(0)));
        return Value::nil();
    });
    // NewParticleEffect(template): a copy of a ParticleEffects.INI template, not started;
    // CreateEffect(effect) starts it, KillEffect(effect) ends it. SetEffectPosition (script frame,
    // or the offset from the matrix it was attached to), SetEffectDirection, AttachEffectToMatrix
    // (GetEntityMatrix(entity) gives the entity), SetEffectRGBAStart / End (RGBAInt).
    e.bind("newparticleeffect", [this](NativeCall& c) {
        return Value::integer(fx_.spawn(fxLib_.find(c.string(0)), player_.feet, {0.0f, 1.0f, 0.0f}, false));
    });
    e.bind("createeffect", [this](NativeCall& c) {
        const int32 id = static_cast<int32>(c.integer(0));
        fx_.start(id);
        // A slowburn copy is one of the music's fires (0x47b440), where it is now: its matrix's
        // entity and offset, or its own place.
        if (const FxSystemDef* def = fx_.defOf(id); def && Assets::iequals(def->name, "slowburn"))
        {
            Math::Vector3 at = fx_.positionOf(id).value_or(player_.feet);
            if (const auto it = fxAttached_.find(id); it != fxAttached_.end())
                if (const Entity* en = entity(it->second.entity))
                    at = (en->hunter ? en->hunter->feet() : en->position) + it->second.offset;
            addMusicSource(at, id);
        }
        return Value::nil();
    });
    e.bind("killeffect", [this](NativeCall& c) {
        fx_.kill(static_cast<int32>(c.integer(0)));
        return Value::nil();
    });
    e.bind("getentitymatrix", [entityArg](NativeCall& c) { return Value::entity(entityArg(c, 0)); });
    e.bind("attacheffecttomatrix", [this](NativeCall& c) {
        fxAttached_[static_cast<int32>(c.integer(0))].entity = c.arg(1).asEntity();
        return Value::nil();
    });
    e.bind("seteffectposition", [this](NativeCall& c) {
        const int32 id = static_cast<int32>(c.integer(0));
        if (const auto it = fxAttached_.find(id); it != fxAttached_.end())
            it->second.offset = toScene(c.vec3(1)) - toScene(Math::Vector3{0.0f, 0.0f, 0.0f});
        else
            fx_.setPosition(id, toScene(c.vec3(1)));
        return Value::nil();
    });
    e.bind("seteffectdirection", [this](NativeCall& c) {
        const Math::Vector3 d = toScene(c.vec3(1)) - toScene(Math::Vector3{0.0f, 0.0f, 0.0f});
        fx_.setForward(static_cast<int32>(c.integer(0)), d);
        return Value::nil();
    });
    const auto rgba = [](const Value& v) {
        return Math::Vector4{v.v.x, v.v.y, v.v.z, static_cast<float32>(v.i)};
    };
    e.bind("seteffectrgbastart", [this, rgba](NativeCall& c) {
        fx_.setColours(static_cast<int32>(c.integer(0)), rgba(c.arg(1)), std::nullopt);
        return Value::nil();
    });
    e.bind("seteffectrgbaend", [this, rgba](NativeCall& c) {
        fx_.setColours(static_cast<int32>(c.integer(0)), std::nullopt, rgba(c.arg(1)));
        return Value::nil();
    });
    // --- G8 / G9 (ours: the handlers are not traced). ---
    // Round(x); SubtractVectors(VAR out, a, b): out = a - b.
    e.bind("round", [](NativeCall& c) { return Value::integer(static_cast<int64>(std::lround(c.real(0)))); });
    e.bind("subtractvectors", [](NativeCall& c) {
        c.set(0, Value::vec3(c.vec3(1) - c.vec3(2)));
        return Value::nil();
    });
    // KillGameText: the hint box goes; SetGameTextTTypeDisplayTime(ms): how long the box stays.
    e.bind("killgametext", [this](NativeCall&) {
        text_.killBox();
        return Value::nil();
    });
    e.bind("setgametextttypedisplaytime", [this](NativeCall& c) {
        text_.setDisplayTime(static_cast<float32>(c.integer(0)) / 1000.0f);
        return Value::nil();
    });
    // SetCurrentLOD(entity, n): its model shows LOD n (the "_L<n>" frames; Subway's power display
    // counts with them); GetNumLODLevels(entity).
    e.bind("setcurrentlod", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        const Assets::InstPlacement* p = en ? placementAt(en->placement) : nullptr;
        if (!p)
            return Value::nil();
        const int32 lod = static_cast<int32>(c.integer(1));
        for (Renderer::Mesh* m : setup_.level->meshesOf(*p))
            if (const int32 l = setup_.level->lodOf(m); l >= 0)
                m->setVisible(l == lod);
        return Value::nil();
    });
    e.bind("getnumlodlevels", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        const Assets::InstPlacement* p = en ? placementAt(en->placement) : nullptr;
        int32 n = 1;
        if (p)
            for (Renderer::Mesh* m : setup_.level->meshesOf(*p))
                n = std::max(n, setup_.level->lodOf(m) + 1);
        return Value::integer(n);
    });
    // EntityPlayAnim(entity, clip, loop): a hunter plays the clip (Guard13's Idle_Cerb_Stand); a
    // prop (the body bag, Piggsy's grill, doors, the asylum victim) plays it on its model.
    e.bind("entityplayanim", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        if (en && en->hunter && en->hunter->alive())
            en->hunter->playAnim(c.rawString(1), c.count() > 2 && c.boolean(2));
        else if (en && !en->hunter && en->kind == Kind::Placement && en->placement >= 0)
            if (const Assets::InstPlacement* p = placementAt(en->placement))
            {
                animatedProps_.insert(en->placement);
                propAnims_.push_back({p, en->position, c.rawString(1), c.count() > 2 && c.boolean(2)});
            }
        return Value::nil();
    });
    // AIEntityPlayAnim(hunter, clip) / AIEntityPlayAnimLooped / AIEntityCancelAnim.
    e.bind("aientityplayanim", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->hunter && en->hunter->alive())
            en->hunter->playAnim(c.rawString(1), false);
        return Value::nil();
    });
    e.bind("aientityplayanimlooped", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->hunter && en->hunter->alive())
            en->hunter->playAnim(c.rawString(1), true);
        return Value::nil();
    });
    e.bind("aientitycancelanim", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->cancelAnim();
        return Value::nil();
    });
    // EntityPlayCutsceneAnimation(player, position, degrees, clip): he is put there, facing that
    // way, and plays the clip (Attic's pick-ups).
    e.bind("entityplaycutsceneanimation", [this, entityArg](NativeCall& c) {
        if (entityArg(c, 0) == playerHandle_)
        {
            teleport_ = toScene(c.vec3(1));
            heading_ = Math::radians(c.real(2)) + Math::pi;
            playerAnim_ = c.rawString(3);
        }
        return Value::nil();
    });
    // InflictDamage(entity, amount, ARM_ class): it is hurt by that much.
    e.bind("inflictdamage", [this, entityArg](NativeCall& c) {
        const uint32 h = entityArg(c, 0);
        if (h == playerHandle_)
            hurtPlayer(c.real(1), player_.feet);
        else if (const Entity* en = entity(h); en && en->hunter && en->hunter->alive())
        {
            HunterWorld w = world(player_);
            en->hunter->hit(c.real(1), en->hunter->feet(), w, HitKind::Hard);
        }
        return Value::nil();
    });
    // Accepted without effect (no system for them yet): the particle effects' settings
    // (SetEffectRGBAStart / End, Radius, PauseLength, PauseCycle), killable effects
    // (CreateKillableMHFX / KillMHFX),
    // ActivateShadows (0x57ef12 only pops its argument), ScriptHogProcessorStart / End (a hint to
    // run the script at once) and Bonus3's SetBasketBrawlSpecialFlag.
    e.bind("switchlitteron", [this](NativeCall& c) {
        litterOn_ = c.boolean(0);
        return Value::nil();
    });
    for (const char* name : {"seteffectradius", "seteffectpauselength", "seteffectpausecycle",
                             "activateshadows", "scripthogprocessorstart", "scripthogprocessorend", "setbasketbrawlspecialflag"})
        e.bind(name, [](NativeCall&) { return Value::nil(); });
    // Scrap2's crane (see updateCrane / craneView).
    e.bind("setcameramodecrane", [this](NativeCall& c) {
        craneCam_.on = c.boolean(0);
        craneCam_.onFor = 0.0f;
        return Value::nil();
    });
    e.bind("setcameraposcrane", [this](NativeCall& c) {
        craneCam_.position = toScene(c.vec3(0));
        return Value::nil();
    });
    e.bind("setcameralookatentitycrane", [this, entityArg](NativeCall& c) {
        craneCam_.lookAt = entityArg(c, 0);
        craneCam_.offset = c.vec3(1) * setup_.level->frame().scale;
        return Value::nil();
    });
    // SetPadControlEntityRotation(entity, on) (0x2b6 -> +0xc8 & 0x400000; the player's & 0x800).
    e.bind("setpadcontrolentityrotation", [this, entityArg](NativeCall& c) {
        const uint32 h = entityArg(c, 0);
        if (c.boolean(1))
            padTurned_ = h;
        else if (padTurned_ == h)
            padTurned_ = 0;
        craneTurn_.reset();
        return Value::nil();
    });
    // LinkHangingEntToEnt(hanging, parent, offset) (0x2b7): the hanging entity's physics object
    // (type 5) hangs from the parent at `offset` in its frame (+0xa8 / +0xac). Ours: its pivot
    // rides where it was placed on the parent (that offset) and it swings (Game/crane.h Hanging).
    e.bind("linkhangingenttoent", [this, entityArg](NativeCall& c) {
        const uint32 child = entityArg(c, 0), parent = entityArg(c, 1);
        if (entity(child) && entity(parent))
            propLinks_.push_back({child, parent, c.vec3(2), true, {}});
        return Value::nil();
    });
    // --- G4: the scripts driving the player (ours: the handlers are not traced). ---
    // DisableUserInput / EnableUserInput, LockPlayerMovement(on), ClearInputStates.
    // --- G5: hunters (ours unless noted: the handlers are not traced). ---
    // GetHunterAreaName(hunter): the AI area of the graph node it stands at ('' none).
    e.bind("gethunterareaname", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        if (!en || !en->hunter || nav_.empty())
            return Value::string("");
        const int32 n = nav_.nearest(en->hunter->feet());
        return Value::string(n >= 0 ? nav_.areaName(nav_.node(n).area) : std::string());
    });
    // SetHunterDropAmmo(hunter, rounds): its gun falls with that many.
    e.bind("sethunterdropammo", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            dropAmmo_[en->hunter] = static_cast<int32>(c.integer(1));
        return Value::nil();
    });
    // SetPedDecayInstantly(ped): its body goes soon after it dies (ours: 2 s); SetPedDoNotDecay:
    // ours keeps every body, so nothing to do.
    e.bind("setpeddecayinstantly", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->hunter)
        {
            decayInstantly_.insert(en->hunter);
            if (!en->hunter->alive())
                decayAt_[en->hunter] = time_;
        }
        return Value::nil();
    });
    // ClearHunterAimTarget(hunter): SetHunterAimTarget undone.
    e.bind("clearhunteraimtarget", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setAimTarget(std::nullopt);
        return Value::nil();
    });
    // SetPedOrientation(ped, degrees): it faces that way (as SetPlayerHeading for him).
    e.bind("setpedorientation", [this, entityArg](NativeCall& c) {
        const uint32 h = entityArg(c, 0);
        const float32 yaw = Math::radians(c.real(1)) + Math::pi;
        if (h == playerHandle_)
            heading_ = yaw;
        else if (const Entity* en = entity(h); en && en->hunter)
            en->hunter->setYaw(yaw);
        return Value::nil();
    });
    // LockPed(name, on).
    e.bind("lockped", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setLocked(c.boolean(1));
        return Value::nil();
    });
    // HunterUseSwitch(hunter, switch): the hunter works the switch (ours: at once, wherever it
    // stands) - it turns on / off and runs OnUseableUsed (not OnUseByPlayer).
    e.bind("hunteruseswitch", [this, entityArg](NativeCall& c) {
        const Entity* sw = entity(entityArg(c, 1));
        if (Useable* u = sw ? useableNamed(sw->name) : nullptr)
        {
            u->used = true;
            u->state = u->twoState && u->state == 1 ? 2 : 1;
            scripts_.start(u->name, "OnUseableUsed");
            std::cerr << "[runtime] a hunter uses " << u->name << "\n";
        }
        return Value::nil();
    });
    // GetExecutionType: the execution under way, 0 hasty .. 2 gruesome (Cellblock's tutorial).
    e.bind("getexecutiontype", [this](NativeCall&) { return Value::integer(executionType_); });
    // AIIsGuardIdle(name): as AIIsIdle (ours).
    e.bind("aiisguardidle", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        return Value::boolean(en && en->hunter && en->hunter->alive() && en->hunter->action() == AiAction::NotOnAlert);
    });
    e.bind("aidoesleaderhavesubpack", [this](NativeCall& c) { return Value::boolean(director_.leaderHasSubpack(c.string(0), c.string(1))); });
    e.bind("aicopysubpacksareastoanothersubpack", [this](NativeCall& c) {
        director_.copySubpackAreas(c.string(0), c.string(1));
        return Value::nil();
    });
    // IsHunterSpeechPlaying (0x2db): the subtitle box is up (0x722034), as after a speech
    // placeholder.
    e.bind("ishunterspeechplaying", [this](NativeCall&) { return Value::boolean(text_.subtitling()); });
    // SetDirectorSpeechTime(ms) (0x2dc): the subtitle box's page time (0x722050).
    e.bind("setdirectorspeechtime", [this](NativeCall& c) {
        speechSeconds_ = static_cast<float32>(c.integer(0)) / 1000.0f;
        return Value::nil();
    });
    // JoinMySquad(leader, member) / AbandonMySquad(leader, member): the member mimics the leader or
    // stops (Hunter::setSquadLeader).
    e.bind("joinmysquad", [this, entityArg](NativeCall& c) {
        const Entity* leader = entity(entityArg(c, 0));
        const Entity* member = entity(entityArg(c, 1));
        if (leader && member && leader->hunter && member->hunter && leader->hunter != member->hunter)
            member->hunter->setSquadLeader(leader->hunter);
        return Value::nil();
    });
    e.bind("abandonmysquad", [this, entityArg](NativeCall& c) {
        if (const Entity* member = entity(entityArg(c, 1)); member && member->hunter)
            member->hunter->setSquadLeader(nullptr);
        return Value::nil();
    });
    // Accepted without effect: AISetIdlePatrolStopDirection, SetPedDoNotDecay,
    // RegisterNonExecutableHunterInLevel.
    for (const char* name : {"aisetidlepatrolstopdirection", "setpeddonotdecay", "registernonexecutablehunterinlevel"})
        e.bind(name, [](NativeCall&) { return Value::nil(); });
    // SetHunterHideHealth(hunter, percent): Hunter::setHideHealth.
    e.bind("sethunterhidehealth", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setHideHealth(static_cast<int32>(c.integer(1)));
        return Value::nil();
    });
    // AISetEntityAim(hunter, on) / AIModifyGoalAim(goal, on): it idles aiming (Hunter::setIdleAim).
    e.bind("aisetentityaim", [this, entityArg](NativeCall& c) {
        if (const Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setIdleAim(c.boolean(1));
        return Value::nil();
    });
    e.bind("aimodifygoalaim", [this](NativeCall& c) {
        director_.setGoalAim(c.string(0), c.boolean(1));
        return Value::nil();
    });
    e.bind("disableuserinput", [this](NativeCall&) {
        userInputDisabled_ = true;
        return Value::nil();
    });
    e.bind("enableuserinput", [this](NativeCall&) {
        userInputDisabled_ = false;
        return Value::nil();
    });
    e.bind("lockplayermovement", [this](NativeCall& c) {
        movementLocked_ = c.boolean(0);
        return Value::nil();
    });
    e.bind("clearinputstates", [this](NativeCall&) {
        padPressed_ = 0;
        clearInput_ = true;
        return Value::nil();
    });
    // IsPadButtonPressed(PAD_x): held now; HasPadButtonBeenPressed(PAD_x): pressed since it was
    // last asked (or ClearInputStates). The levels read them in their debug pad loops and on
    // Scrap2's crane (TRIANGLE drops its load).
    e.bind("ispadbuttonpressed", [this](NativeCall& c) { return Value::boolean((padHeld_ & static_cast<uint32>(c.integer(0))) != 0); });
    e.bind("haspadbuttonbeenpressed", [this](NativeCall& c) {
        const uint32 bit = static_cast<uint32>(c.integer(0));
        const bool was = (padPressed_ & bit) != 0;
        padPressed_ &= ~bit;
        return Value::boolean(was);
    });
    // HasControlAxisBeenSwapped (Scrap's crane hint): ours, never.
    e.bind("hascontrolaxisbeenswapped", [](NativeCall&) { return Value::boolean(false); });
    // SetPlayerGotoNode(node, position, flag): he walks there by himself (the session steers him;
    // the flag is not read); HasPlayerArrivedAtNode: he is there.
    e.bind("setplayergotonode", [this](NativeCall& c) {
        playerGoto_ = toScene(c.vec3(1));
        playerArrived_ = false;
        if (debugLog_)
            std::cerr << "[script] the player walks to " << c.string(0) << " (" << playerGoto_->x << ", " << playerGoto_->z << ")" << std::endl;
        return Value::nil();
    });
    e.bind("hasplayerarrivedatnode", [this](NativeCall&) { return Value::boolean(playerArrived_); });
    // PlayerPlayFullBodyAnim(clip) / PlayerFullBodyAnimDone; PlayerDropBody.
    e.bind("playerplayfullbodyanim", [this](NativeCall& c) {
        playerAnim_ = c.rawString(0);
        return Value::nil();
    });
    e.bind("playerfullbodyanimdone", [this](NativeCall&) { return Value::boolean(playerAnim_.empty() && !player_.actionBusy); });
    e.bind("playerdropbody", [this](NativeCall&) {
        dropBody_ = true;
        return Value::nil();
    });
    e.bind("setentityinvulnerable", [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)))
        {
            if (en->kind == Kind::Player)
                playerInvulnerable_ = c.boolean(1);
            else if (en->hunter)
                en->hunter->setInvulnerable(c.boolean(1));
        }
        return Value::nil();
    });
    // SetDamage(entity, health): sets its health (doors' strength is not modelled).
    e.bind("setdamage", [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)))
        {
            if (en->kind == Kind::Player)
                playerHealth_ = std::clamp(c.real(1), 0.0f, tuning_.playerHitPoints);
            else if (en->hunter)
                en->hunter->setHealth(c.real(1));
        }
        return Value::nil();
    });
    // MutilateHunter(entity) (native 0x2bd, handler 0x57e8d8): a hunter dies at once (0x4ece00:
    // death mode 0x1b, its navigation stopped) and its head bursts (0x49ccb0 with (0, 1, 0), the
    // Explode_Head entity) - Estate_Ext's corpse at the start, Cerb_Start02.
    e.bind("mutilatehunter", [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && en->hunter && en->hunter->alive())
        {
            HunterWorld w = world(player_);
            en->hunter->killNow(w);
            headBursts_.push_back(en->hunter);
            if (debugLog_)
                std::cerr << "[script] MutilateHunter " << en->hunter->name() << std::endl;
        }
        return Value::nil();
    });
    // KillEntity(entity): a hunter dies, a pickup (Name_(CT)) or door goes, the player dies.
    e.bind("killentity", [this, entityArg, doorArg](NativeCall& c) {
        Entity* en = entity(entityArg(c, 0));
        if (!en)
            return Value::nil();
        if (en->kind == Kind::Player)
            playerHealth_ = 0.0f;
        else if (en->hunter)
        {
            HunterWorld w = world(player_);
            en->hunter->killNow(w);
        }
        else if (Doors::Door* d = doorArg(c))
        {
            d->destroyed = true;
            for (Renderer::Mesh* m : d->meshes)
                m->setVisible(false);
        }
        else
            for (Pickup& k : pickups_)
                if (!k.taken && (k.position - en->position).length() < 0.6f)
                {
                    k.taken = true; // (as if taken: it shows no more)
                    for (Renderer::Mesh* m : k.meshes)
                        m->setVisible(false);
                    if (k.model)
                        k.model->setVisible(false);
                    if (k.halo)
                        k.halo->setVisible(false);
                }
        return Value::nil();
    });
    // HideEntity / ShowEntity(entity): a hunter, door or prop shows or not.
    auto showEntity = [this, entityArg, doorArg](NativeCall& c, bool show) {
        if (Doors::Door* d = doorArg(c))
            for (Renderer::Mesh* m : d->meshes)
                m->setVisible(show && !d->destroyed);
        else if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->model().setVisible(show);
        else if (en && en->kind == Kind::Placement && en->placement >= 0)
        {
            if (show)
                hiddenProps_.erase(en->placement);
            else
                hiddenProps_.insert(en->placement);
            // (an animated one's model is the session's; its static meshes stay hidden)
            if (const Assets::InstPlacement* p = placementAt(en->placement); p && !animatedProps_.contains(en->placement))
                for (Renderer::Mesh* m : setup_.level->meshesOf(*p))
                    m->setVisible(show);
        }
        return Value::nil();
    };
    e.bind("hideentity", [showEntity](NativeCall& c) { return showEntity(c, false); });
    e.bind("showentity", [showEntity](NativeCall& c) { return showEntity(c, true); });
    // AddAmmoToInventoryWeapon(entity, CT_*, rounds): rounds for the player's gun of that type.
    e.bind("addammotoinventoryweapon", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        const ItemType* t = items_.byCollectable(static_cast<int32>(c.integer(1)));
        if (en && en->kind == Kind::Player && t)
            if (const Firearm* gun = tuning_.firearm(t->weaponType))
                (void)ammo_.takeRounds(*gun, lower(t->weaponType), static_cast<int32>(c.integer(2)));
        return Value::nil();
    });
    // AIEntityGoHomeIfIdle, AIGuardModifyShootOutsideRadius, SetHunterHitAccuracy: accepted, no
    // effect yet.
    // Also accepted: SetHunterTauntProbability (how often it taunts; our taunt pacing stays).
    for (const char* name : {"aientitygohomeifidle", "aiguardmodifyshootoutsideradius", "sethuntertauntprobability"})
        e.bind(name, [none](NativeCall&) { return none(); });
    // LoadFrontEndAudioStream(SCRIPTED_TRACK_*, 0x49..0x56): opens that stream paused (the file
    // table at 0x7a7ef0); the one already open stays.
    e.bind("loadfrontendaudiostream", [this](NativeCall& c) {
        static constexpr const char* kFiles[] = {"TRAIN/TRAIN1", "TRAIN/TRAIN2", "TRAIN/TRAIN3", "PIGGRILL/PIGGRIL1",
                                                 "PIGGRILL/PIGGRIL2", "CRAPPER/CRAPPER", "GASPOUR/GASPOUR", "JOURNO/JOURNO",
                                                 "PORN/PORN", "DIRECT/DIRECT", "DDEATH/DDEATH", "DDOOR/DDOOR",
                                                 "PDOOR/PDOOR", "MALLTV/MALLTV"};
        const int32 id = static_cast<int32>(c.integer(0));
        if (id < 0x49 || id > 0x56)
            return Value::integer(0);
        if (id != frontEndId_)
        {
            frontEndId_ = id;
            frontEnd_.load = std::string("audio/PC/SCRIPTED/") + kFiles[id - 0x49] + ".RIB";
            frontEnd_.play = -1;
        }
        return Value::integer(1);
    });
    // SetHunterHitAccuracy / SetHunterBlockAccuracy(hunter, per cent).
    e.bind("sethunterhitaccuracy", [this, entityArg, none](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setHitAccuracy(c.real(1));
        return none();
    });
    e.bind("sethunterblockaccuracy", [this, entityArg, none](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setBlockAccuracy(c.real(1));
        return none();
    });
    // SetHunterAimTarget(hunter, position).
    e.bind("sethunteraimtarget", [this, entityArg, none](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setAimTarget(toScene(c.vec3(1)));
        return none();
    });

    // --- Scene 2 on: natives read from how the level scripts call them (Sor3nt's file-format toolkit names;
    // The exe calls natives by id, and its handlers are not traced yet). ---
    auto positionOf = [this](const Entity* en) {
        return en->kind == Kind::Player ? player_.feet : (en->hunter ? en->hunter->feet() : en->position);
    };
    // CreateInventoryItem(CT_*, entity, select): the item of that collectable type, given to the
    // player (in hand if `select`) or put in a hunter's hand.
    e.bind("createinventoryitem", [this, entityArg](NativeCall& c) {
        const ItemType* t = items_.byCollectable(static_cast<int32>(c.integer(0)));
        Entity* en = entity(entityArg(c, 1));
        if (!t || !en)
            return Value::nil();
        if (en->kind == Kind::Player)
        {
            const ItemType* was = equipped_;
            give(t->record);
            if (c.count() > 2 && !c.boolean(2))
                equipped_ = was;
        }
        else if (en->hunter)
            armHunter(*en->hunter, t);
        return Value::nil();
    });
    // DestroyEntity(entity): a door goes (no mesh, no collision); others are not removed yet.
    // DestroyEntity(entity) and RemoveEntity(entity) (ours: the same; the exe's RemoveEntity
    // handler is not traced).
    const auto destroy = [this, entityArg, doorArg](NativeCall& c) {
        if (Doors::Door* d = doorArg(c))
        {
            d->destroyed = true;
            for (Renderer::Mesh* m : d->meshes)
                m->setVisible(false);
            nav_.setLinksNear(Doors::closedCentre(*d), Doors::halfWidth(*d) + 0.3f, true);
        }
        else if (Entity* en = entity(entityArg(c, 0)); en && en->kind != Kind::Player && en->kind != Kind::Level)
        {
            // Gone from the level: GetEntity finds it no more and a checkpoint keeps it "NUL".
            if (en->hunter)
                en->hunter->vanish();
            for (Pickup& k : pickups_)
                if (!k.taken && en->placement >= 0 && k.placement == en->placement)
                {
                    k.taken = true;
                    for (Renderer::Mesh* m : k.meshes)
                        m->setVisible(false);
                    if (k.model)
                        k.model->setVisible(false);
                    if (k.halo)
                        k.halo->setVisible(false);
                }
            if (const Assets::InstPlacement* p = en->hunter ? nullptr : placementAt(en->placement))
                for (Renderer::Mesh* m : setup_.level->meshesOf(*p))
                    m->setVisible(false);
            if (en->placement >= 0)
                destroyed_.insert(en->placement);
            if (en->trigger >= 0)
                triggers_[static_cast<std::size_t>(en->trigger)].gone = true;
            byName_.erase(en->name);
            scripts_.setEntityHandle(en->name, 0);
            scripts_.detach(en->name); // its script instance goes with it
        }
        return Value::nil();
    };
    e.bind("destroyentity", destroy);
    e.bind("removeentity", destroy);
    // GetDoorState(door): DOOR_CLOSED 2 shut, DOOR_OPEN 0 swung out to its limit (or the angle
    // it was set to), DOOR_OPENING 1 between (ours: by its angle).
    e.bind("getdoorstate", [doorArg](NativeCall& c) {
        const Doors::Door* d = doorArg(c);
        if (d && d->slide)
            return Value::integer(d->offset < 0.02f ? 2 : std::fabs(d->offset - d->target) < 0.02f ? 0 : 1);
        if (!d || std::fabs(d->angle) < 0.02f)
            return Value::integer(2);
        const float32 target = d->overrideAngle ? std::fabs(*d->overrideAngle) : std::min(d->openIn, d->openOut);
        return Value::integer(std::fabs(d->angle) >= target - 0.05f ? 0 : 1);
    });
    // SetDoorState(door, DOOR_OPEN 0 / DOOR_OPENING 1 / DOOR_CLOSED 2 / DOOR_CLOSING 3): swings it open (its
    // open-in angle) or shut.
    e.bind("setdoorstate", [this, doorArg](NativeCall& c) {
        if (Doors::Door* d = doorArg(c); d && d->slide)
        {
            const int64 state = c.integer(1);
            d->target = state == 0 || state == 1 ? d->ajar : 0.0f; // open / opening, closed / closing
            std::cerr << "[door] " << d->name << " slides to " << d->target << " m" << std::endl;
            if (state == 0)
            {
                d->offset = d->ajar; // already open (closed to open: no sound, 0x4cfcb0)
                doors_.place(*d);
            }
        }
        else if (Doors::Door* d = doorArg(c))
        {
            const int64 state = c.integer(1);
            d->frozen = false;
            if (state == 2 || state == 3)
            {
                // DOOR_CLOSED / CLOSING: its spring shuts it, and it latches (ours).
                d->overrideAngle.reset();
                d->driving = false;
                d->awake = true;
            }
            else
            {
                d->overrideAngle = d->openIn;
                d->driving = true;
                if (state == 0)
                    d->angle = d->lastAngle = d->openIn; // already open
            }
        }
        return Value::nil();
    });
    // KillThisScript: the running handler ends (as RemoveThisScript ends it).
    e.bind("killthisscript", [](NativeCall& c) {
        c.endTask();
        return Value::nil();
    });
    // SetLevelFailed: the scene is failed (Scene Failed, as when he dies).
    e.bind("setlevelfailed", [this](NativeCall&) {
        levelFailed_ = true;
        return Value::nil();
    });
    e.bind("isexecutioninprogress", [this](NativeCall&) { return Value::boolean(player_.executing); });
    // IsEntityDying(name): a hunter that has died (ours: dead at all; the exe's dying window is
    // not traced).
    e.bind("isentitydying", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        return Value::boolean(en && en->hunter && !en->hunter->alive() && !en->hunter->gone());
    });
    // The timer: SetTimer(minutes, seconds), StartTimer / StopTimer, ShowTimer / HideTimer,
    // HasTimerEnded; at zero every script with OnTimerEnded runs it (ours: the exe's timer object
    // is not traced).
    e.bind("settimer", [this](NativeCall& c) {
        timerLeft_ = static_cast<float32>(c.integer(0) * 60 + c.integer(1));
        timerEnded_ = false;
        return Value::nil();
    });
    e.bind("starttimer", [this](NativeCall&) {
        timerRunning_ = timerLeft_ > 0.0f;
        return Value::nil();
    });
    e.bind("stoptimer", [this](NativeCall&) {
        timerRunning_ = false;
        return Value::nil();
    });
    e.bind("showtimer", [this](NativeCall&) {
        timerShown_ = true;
        return Value::nil();
    });
    e.bind("hidetimer", [this](NativeCall&) {
        timerShown_ = false;
        return Value::nil();
    });
    e.bind("hastimerended", [this](NativeCall&) { return Value::boolean(timerEnded_); });
    // The counter (the bonus levels' kills): SetCounter(n), IncrementCounter, ShowCounter.
    e.bind("setcounter", [this](NativeCall& c) {
        counter_ = c.integer(0);
        return Value::nil();
    });
    e.bind("incrementcounter", [this](NativeCall&) {
        ++counter_;
        return Value::nil();
    });
    e.bind("showcounter", [this](NativeCall&) {
        counterShown_ = true;
        return Value::nil();
    });
    e.bind("randnum", [this](NativeCall& c) {
        const int64 n = std::max<int64>(1, c.integer(0));
        return Value::integer(std::uniform_int_distribution<int64>(0, n - 1)(rng_));
    });
    e.bind("removescript", [this](NativeCall& c) {
        scripts_.removeScript(c.string(0), c.string(1));
        return Value::nil();
    });
    // GetDamage(entity): its health (the scripts test the player's against 60).
    e.bind("getdamage", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        if (!en)
            return Value::integer(0);
        const float32 hp = en->kind == Kind::Player ? playerHealth_ : (en->hunter ? en->hunter->health() : 0.0f);
        return Value::integer(static_cast<int64>(hp));
    });
    // AIIsEnemyInSight(hunter, 'player', priority): it sees him now.
    e.bind("aiisenemyinsight", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        const Entity* target = entity(entityArg(c, 1));
        const bool sees = en && en->hunter && target && target->kind == Kind::Player && en->hunter->alive() &&
                          en->hunter->sight() != kNoLevel;
        return Value::boolean(sees);
    });
    // AIIsEntityGuard(hunter): its goal now is a guard goal (ours: what the name says).
    e.bind("aiisentityguard", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        if (!en || !en->hunter)
            return Value::boolean(false);
        const GoalDef* g = director_.goalFor(en->hunter->name(), en->hunter->action() >= AiAction::Hunt);
        return Value::boolean(g && (g->type == GoalType::GuardDirection || g->type == GoalType::GuardLookAtEntity));
    });
    // AIGetHunterLastNodeName(hunter): the graph node it stands by (its label, else its number).
    e.bind("aigethunterlastnodename", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        const int32 n = en && en->hunter ? nav_.nearest(en->hunter->feet()) : -1;
        if (n < 0)
            return Value::string({});
        const std::string& label = nav_.node(n).label;
        return Value::string(label.empty() ? std::to_string(n) : label);
    });
    // AIIsEnemyInRadius(hunter, entity, radius): the entity within that many metres of it.
    e.bind("aiisenemyinradius", [this, entityArg](NativeCall& c) {
        const Entity* a = entity(entityArg(c, 0));
        const Entity* b = entity(entityArg(c, 1));
        auto where = [&](const Entity* en) {
            return en->kind == Kind::Player ? player_.feet : (en->hunter ? en->hunter->feet() : en->position);
        };
        return Value::boolean(a && b && (where(a) - where(b)).length() <= c.real(2));
    });
    // AIPlayCommunication(hunter, action, type): the Communications.ini action's speech
    // ('DefiniteSighting' -> DEFINITE_SIGHTING, 'PainLong' -> PAIN_LONG).
    e.bind("aiplaycommunication", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        if (!en || !en->hunter || en->hunter->voice().empty() || !en->hunter->speechOn())
            return Value::nil();
        std::string context;
        for (const char ch : c.rawString(1))
        {
            if (std::isupper(static_cast<unsigned char>(ch)) && !context.empty())
                context += '_';
            context += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        int32 voice = -1, slot = -1;
        const std::string stem = en->hunter->voice() + context;
        if (hunterSpeech_.find(stem, voice, slot))
            if (const auto line = hunterSpeech_.pick(voice, slot, time_, rng_))
                emitSound(*line, en->hunter->feet() + Math::Vector3{0.0f, 1.65f, 0.0f}, 0.8f);
        return Value::nil();
    });
    e.bind("aiisidle", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        return Value::boolean(en && en->hunter && en->hunter->alive() && en->hunter->action() == AiAction::NotOnAlert);
    });
    // AISetEntityVoiceID(hunter, VOICE_<GANG><n>VOICE<m>): it speaks with SFX_<letter><n>V<m>_
    // (the SFX bank's prefixes: Bunny B, Cerberus C, Hooded H, Innocent I, Journalist J, Skinz K,
    // Monkey M, Police P, Ramirez R, Smiley S, Piggsy X).
    e.bind("aisetentityvoiceid", [this, entityArg](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        if (!en || !en->hunter)
            return Value::nil();
        const int64 id = c.integer(1);
        for (const auto& [name, value] : constants_)
        {
            if (value != id || name.rfind("voice_", 0) != 0)
                continue;
            static const std::pair<const char*, char> kGangs[] = {
                {"bunny", 'B'},  {"cerberus", 'C'}, {"hooded", 'H'}, {"innocent", 'I'}, {"journalist", 'J'}, {"skinz", 'K'},
                {"monkey", 'M'}, {"police", 'P'},   {"ramirez", 'R'}, {"smiley", 'S'},  {"piggsy", 'X'}};
            for (const auto& [gang, letter] : kGangs)
                if (name.rfind(std::string("voice_") + gang, 0) == 0)
                {
                    const std::string rest = name.substr(6 + std::char_traits<char>::length(gang)); // <n>voice<m>
                    const std::size_t v = rest.find("voice");
                    if (v != std::string::npos)
                        en->hunter->setVoice("SFX_" + std::string(1, letter) + rest.substr(0, v) + "V" + rest.substr(v + 5) + "_");
                }
        }
        return Value::nil();
    });
    e.bind("setpedlockonable", [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setLockonable(c.boolean(1));
        return Value::nil();
    });
    // SetHunterMeleeTraits(hunter, MTT_*): its melee-traits record (Game/ai/melee_traits.h).
    e.bind("sethuntermeleetraits", [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setMeleeTraits(static_cast<int32>(c.integer(1)));
        return Value::nil();
    });
    e.bind("sethunterexecutable", [this, entityArg](NativeCall& c) {
        if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->setExecutable(c.boolean(1));
        return Value::nil();
    });
    // RotateEntityLeft(entity, degrees): a door swings, a hunter turns (searchlights and cameras
    // are props we do not move yet).
    e.bind("rotateentityleft", [this, entityArg, doorArg](NativeCall& c) {
        const float32 a = Math::radians(c.real(1));
        if (Doors::Door* d = doorArg(c))
            d->lastAngle = d->angle += a;
        else if (Entity* en = entity(entityArg(c, 0)); en && en->hunter)
            en->hunter->turn(a);
        return Value::nil();
    });
    // Script audio slots: LoadScriptAudioSlot(slot, SFX_SCRIPT_SLOT_* = an SFX bank sample id),
    // PlayScriptAudioSlotOneShotFromEntity / LoopedFromEntity(entity, slot, volume 0..127, range
    // 0..255 m; -1 the sample's own: 0x5b39e0, 0x447320), EndScriptAudioSlotLooped(slot). The
    // machines' samples also start or stop their music ambience (Game/music_ambience.h).
    e.bind("loadscriptaudioslot", [this](NativeCall& c) {
        const int64 slot = c.integer(0);
        if (slot >= 0 && slot < static_cast<int64>(scriptAudio_.size()))
            scriptAudio_[static_cast<std::size_t>(slot)] = c.integer(1);
        return Value::nil();
    });
    auto slotLevel = [](SoundEvent& s, int64 sample, NativeCall& c) {
        std::array<int32, 2> own = MusicAmbience::slotDefaults(static_cast<int32>(sample));
        if (own[0] < 0)
            own = {127, 40}; // 0x447080
        const int64 vol = c.count() > 2 ? c.integer(2) : -1, range = c.count() > 3 ? c.integer(3) : -1;
        s.volume = std::clamp(static_cast<float32>(vol < 0 ? own[0] : vol) / 127.0f, 0.0f, 1.0f);
        s.range = static_cast<float32>(range < 0 ? own[1] : range);
    };
    auto playSlot = [this, entityArg, positionOf, slotLevel](NativeCall& c, bool loop) {
        const uint32 handle = entityArg(c, 0);
        const Entity* en = entity(handle);
        const int64 slot = c.integer(1);
        if (!en || slot < 0 || slot >= static_cast<int64>(scriptAudio_.size()) || scriptAudio_[static_cast<std::size_t>(slot)] < 0)
            return Value::nil();
        const int64 sample = scriptAudio_[static_cast<std::size_t>(slot)];
        SoundEvent s;
        s.sample = "#" + std::to_string(sample);
        s.position = positionOf(en);
        s.priority = 0.9f;
        slotLevel(s, sample, c);
        musicAmbienceState().slotPlayed(static_cast<int32>(sample), c.count() > 2 ? static_cast<int32>(c.integer(2)) : -1,
                                        c.count() > 3 ? static_cast<int32>(c.integer(3)) : -1, handle, nullptr, time_);
        s.loopSlot = loop ? static_cast<int32>(slot) : -1;
        s.slot = loop ? -1 : static_cast<int32>(slot);
        if (!loop)
            slotEnds_[static_cast<std::size_t>(slot)] = time_ + 0.1f; // until the session reports its length
        sounds_.push_back(s);
        return Value::nil();
    };
    // PlayScriptAudioSlotOneShotFromPos(position, slot, ?, volume): as from an entity, at a point.
    e.bind("playscriptaudioslotoneshotfrompos", [this, slotLevel](NativeCall& c) {
        const int64 slot = c.integer(1);
        if (slot < 0 || slot >= static_cast<int64>(scriptAudio_.size()) || scriptAudio_[static_cast<std::size_t>(slot)] < 0)
            return Value::nil();
        const int64 sample = scriptAudio_[static_cast<std::size_t>(slot)];
        SoundEvent s;
        s.sample = "#" + std::to_string(sample);
        s.position = toScene(c.vec3(0));
        s.priority = 0.9f;
        slotLevel(s, sample, c);
        musicAmbienceState().slotPlayed(static_cast<int32>(sample), c.count() > 2 ? static_cast<int32>(c.integer(2)) : -1,
                                        c.count() > 3 ? static_cast<int32>(c.integer(3)) : -1, 0, &s.position, time_);
        s.slot = static_cast<int32>(slot);
        slotEnds_[static_cast<std::size_t>(slot)] = time_ + 0.1f;
        sounds_.push_back(s);
        return Value::nil();
    });
    // IsScriptAudioSlotCompleted(slot): its last one-shot has played out.
    e.bind("isscriptaudioslotcompleted", [this](NativeCall& c) {
        const int64 slot = c.integer(0);
        return Value::boolean(slot < 0 || slot >= static_cast<int64>(slotEnds_.size()) || time_ >= slotEnds_[static_cast<std::size_t>(slot)]);
    });
    // PlayAudioOneShotFromEntity(entity, SFX_ id, volume 0..127, range) / FromPos(position, ...):
    // a sample of the SFX bank once (the doors' slams, the levers; ours: the 4th argument read
    // as its range in metres / 10).
    const auto oneShot = [this](const Math::Vector3& at, NativeCall& c) {
        SoundEvent s;
        s.sample = "#" + std::to_string(c.integer(1));
        s.position = at;
        s.priority = 0.7f;
        s.volume = c.count() > 2 ? std::clamp(static_cast<float32>(c.integer(2)) / 127.0f, 0.0f, 1.0f) : 1.0f;
        if (c.count() > 3 && c.integer(3) > 0)
            s.range = static_cast<float32>(c.integer(3)) / 10.0f;
        sounds_.push_back(s);
        return Value::nil();
    };
    e.bind("playaudiooneshotfromentity", [this, entityArg, positionOf, oneShot](NativeCall& c) {
        const Entity* en = entity(entityArg(c, 0));
        return en ? oneShot(positionOf(en), c) : Value::nil();
    });
    e.bind("playaudiooneshotfrompos", [this, oneShot](NativeCall& c) { return oneShot(toScene(c.vec3(0)), c); });
    // IsDirectorSpeechPlaying: a director line is being spoken (the session reports it).
    e.bind("isdirectorspeechplaying", [this](NativeCall&) { return Value::boolean(directorSpeaking_); });
    // The placeholders for lines never recorded (PlayDirectorSpeechPlaceholder 0x28c /
    // PlayHunterSpeechPlaceholder 0x29a -> 0x5c45c0): with SUBTITLES on, the level text `key` in
    // the subtitle box for its page time (ours: one page); 1 if shown.
    auto placeholder = [this](NativeCall& c) {
        return Value::integer(subtitles_ && text_.subtitle(c.rawString(0), speechSeconds_) ? 1 : 0);
    };
    e.bind("playdirectorspeechplaceholder", placeholder);
    e.bind("playhunterspeechplaceholder", placeholder);
    // PlayFrontEndAudioStreamOneShot(volume 0..127): the loaded stream, once.
    e.bind("playfrontendaudiostreamoneshot", [this](NativeCall& c) {
        const int32 v = static_cast<int32>(c.integer(0));
        if (frontEndId_ < 0 || v < 0 || v >= 0x80)
            return Value::integer(0);
        frontEnd_.play = v;
        return Value::integer(1);
    });
    // Accepted without effect: WhiteNoiseSet.
    e.bind("whitenoiseset", [](NativeCall&) { return Value::nil(); });
    e.bind("playscriptaudioslotoneshotfromentity", [playSlot](NativeCall& c) { return playSlot(c, false); });
    e.bind("playscriptaudioslotloopedfromentity", [playSlot](NativeCall& c) { return playSlot(c, true); });
    e.bind("endscriptaudioslotlooped", [this](NativeCall& c) {
        endedLoops_.push_back(static_cast<int32>(c.integer(0)));
        return Value::nil();
    });
    // Accepted without effect for now: particle effects (CreateEffect, NewParticleEffect,
    // AttachEffectToMatrix, SetEffectPosition / Direction, KillEffect; GetEntityMatrix gives
    // nothing), the AI's active-area links and follow-through, hunter
    // speech the scripts play (PlayHunterSpeech, AIEntitySpecificSpeechAnim: the hunter AISPEECH
    // table is not traced), the director's speech gap and the trigger debug display.
    for (const char* name : {"aiassociatetwoactiveareaswithplayerarea", "aiassociatethreeactiveareaswithplayerarea", "aiassociatefouractiveareaswithplayerarea",
                             "aisetsubpackfollowthrough", "aientityspecificspeechanim",
                             "showtriggers"})
        e.bind(name, [none](NativeCall&) { return none(); });

    // PlayHunterSpeech(hunter, VOICE_ id, AISPEECH id) (native 0x299 -> 0x53db60 -> 0x51cc40): only
    // when the hunter uses that voice ("Entity is not using the specified voice" otherwise); the
    // id picks its SPECIFIC_SPEECH line (0x412060), said where it stands. -1 when nothing is said.
    e.bind("playhunterspeech", [this](NativeCall& c) {
        const std::string who = lower(c.string(0));
        const int32 voice = static_cast<int32>(c.integer(1));
        const int32 slot = hunterSpeech_.specificSlot(static_cast<int32>(c.integer(2)));
        for (const auto& h : hunters_)
            if (h->name() == who && h->alive())
            {
                if (hunterSpeech_.voiceOf(h->voice()) != voice || slot < 0 || !h->speechOn())
                    break;
                if (const auto line = hunterSpeech_.pick(voice, slot, time_, rng_); line)
                {
                    emitSound(*line, h->feet() + Math::Vector3{0.0f, 1.65f, 0.0f}, 0.8f);
                    return Value::integer(1);
                }
                break;
            }
        return Value::integer(-1);
    });

    // SetPlayerStatusFlash(on): the status icon flashes (the tutorial points it out).
    e.bind("setplayerstatusflash", [this, none](NativeCall& c) {
        look_.hudFlash = c.boolean(0) ? (look_.hudFlash | 2u) : (look_.hudFlash & ~2u);
        return none();
    });

    // Accepted without effect for now: audio/speech (G8) and the rest below.
    for (const char* name : {"aiaddplayer", "aientityalwaysenabled", "aisetentityidleoverride",
                             "aisetentityallowsurprise",
                             "createmhfx_unused"})
        e.bind(name, [none](NativeCall&) { return none(); });
}

// ---------------------------------------------------------------------------------------------
// Frame

HunterWorld LevelRuntime::world(const PlayerSnapshot& player)
{
    HunterWorld w{nav_, director_, *setup_.field, tuning_, player, rng_, {}, {}, {}, {}, {}, {}, {}, nullptr, nullptr, 0.0f, false, {}, nullptr, false, nullptr, {}, {}, {}, {}, {}, {}};
    w.hardDifficulty = hardDifficulty_;
    w.sounds = &worldSounds_;
    w.scriptEvent = [this](const std::string& hunter, const char* event) {
        // A death counts for the director's kill commentary unless it was an execution: the
        // exe's counter (0x5b63a0, called from 0x4ecde3) skips a hunter dying in an execution
        // clip (0x4ef3a0) and one killed by another hunter.
        if (std::strcmp(event, "OnDeath") == 0)
        {
            const auto it = std::find_if(hunters_.begin(), hunters_.end(),
                                         [&](const auto& h) { return Assets::iequals(h->name(), hunter); });
            if (it == hunters_.end() || (!(*it)->diedInExecution() && !(*it)->killedByHunter()))
                ++hunterDeaths_;
            if (it != hunters_.end() && decayInstantly_.contains(it->get()))
                decayAt_[it->get()] = time_ + 2.0f; // SetPedDecayInstantly (ours: once it has fallen)
        }
        scripts_.start(hunter, event);
    };
    // Hunters stay quiet while a scripted cut-scene plays (ours: their mumbling and taunts talked
    // over the director).
    w.weaponSound = [this](int32 ct, uint16 event, float32 value, const Math::Vector3& at) {
        if (!cutscene_)
            weaponSound(ct, event, value, at);
    };
    w.sound = [this](const std::string& sample, const Math::Vector3& at, float32 priority) {
        if (cutscene_)
            return;
        // A hunter's speech context ("SFX_H1V1_TAUNT_CHASE"): the line is picked the exe's way,
        // and may be none (Game/hunter_speech.h).
        // A line is said only within its samples' widest range of the camera (0x447430: every
        // voice sample is 40 m, Game/sample_levels.h), and heard as the audio manager plays it.
        auto speak = [&](const std::string& line) {
            SoundEvent ev;
            ev.sample = line;
            ev.position = at;
            ev.priority = priority;
            ev.range = 0.0f; // (the sample's own)
            ev.exeFalloff = true;
            if (sounds_.size() < 64)
                sounds_.push_back(ev);
        };
        const bool heard = (at - viewEye_).length() <= kSpeechRange;
        int32 voice = -1, slot = -1;
        if (hunterSpeech_.find(sample, voice, slot))
        {
            if (heard)
                if (const auto line = hunterSpeech_.pick(voice, slot, time_, rng_))
                    speak(*line);
            return;
        }
        // "<voice prefix>@<line>": a line of the AI's speech orders (HunterSpeech::lineSlot).
        if (const std::size_t atSign = sample.find('@'); atSign != std::string::npos)
        {
            voice = hunterSpeech_.voiceOf(sample.substr(0, atSign));
            slot = HunterSpeech::lineSlot(std::atoi(sample.c_str() + atSign + 1));
            if (voice >= 0 && slot >= 0 && heard)
                if (const auto line = hunterSpeech_.pick(voice, slot, time_, rng_))
                    speak(*line);
            if (debugLog_)
                std::cerr << "[speech] " << sample << " -> slot " << slot << std::endl;
            return;
        }
        emitSound(sample, at, priority);
    };
    w.nodeLight = &nodeLight_;
    w.searched = &searched_;
    w.time = time_;
    w.meleeRepeats = &meleeRepeats_;
    w.debugLog = debugLog_;
    w.otherSwinging = [this](const Hunter& self) {
        for (const auto& h : hunters_)
            if (h.get() != &self && h->swinging())
                return true;
        return false;
    };
    w.crowdedBy = [this](const Hunter& self, float32 radius) -> std::optional<Math::Vector3> {
        std::optional<Math::Vector3> nearest;
        float32 best = radius;
        for (const auto& h : hunters_)
            if (h.get() != &self && h->alive())
                if (const float32 d = (h->feet() - self.feet()).length(); d < best)
                {
                    best = d;
                    nearest = h->feet();
                }
        return nearest;
    };
    w.boundaryNode = [this](const Hunter& self) { return boundaryNode(self); };
    w.pickFoe = [this](const Hunter& self, bool sight) -> Hunter* {
        // The enemies of its leader (or of itself, leading) that are hunters: the nearest. With
        // `sight` one it can see - within 30 m (FRUSTUM_m), a clear line from its eye to their
        // chest (ours: no cone, no light).
        const HunterSetup* s = director_.hunter(self.name());
        if (!s || !s->added || s->hostage || s->blind)
            return nullptr;
        const std::string& leader = s->leaderName.empty() ? s->name : s->leaderName;
        Hunter* best = nullptr;
        float32 bestDist = sight ? tuning_.vision.range : 1e9f;
        const Math::Vector3 eye = self.feet() + Math::Vector3{0.0f, 1.65f, 0.0f};
        for (const auto& h : hunters_)
        {
            if (h.get() == &self || !h->alive() || h->gone() || !director_.isEnemy(leader, h->name()))
                continue;
            if (const HunterSetup* hs = director_.hunter(h->name()); hs && hs->hostage)
                continue; // tied: no one's foe
            const float32 d = (h->feet() - self.feet()).length();
            if (d < bestDist && (!sight || setup_.field->lineOfSight(eye, h->feet() + Math::Vector3{0.0f, 1.2f, 0.0f})))
            {
                best = h.get();
                bestDist = d;
            }
        }
        return best;
    };
    w.standingOver = [this](const Hunter& self, const Math::Vector3& at, float32 radius) {
        auto near = [&](const Math::Vector3& p) {
            const float32 dx = p.x - at.x, dz = p.z - at.z;
            return dx * dx + dz * dz < radius * radius && std::abs(p.y - at.y) < 1.5f;
        };
        if (player_.alive && !player_.down && near(player_.feet))
            return true;
        for (const auto& h : hunters_)
            if (h.get() != &self && h->alive() && h->state() != HunterState::Down && near(h->feet()))
                return true;
        return false;
    };
    w.meleeEngaged = [this](const Hunter& self) {
        int32 n = 0;
        for (const auto& h : hunters_)
            if (h.get() != &self && h->alive() && h->meleeEngaged(time_))
                ++n;
        return n;
    };
    w.entityAt = [this](const std::string& name) -> std::optional<Math::Vector3> {
        const Entity* en = entity(handleOf(name));
        if (!en)
            return std::nullopt;
        if (en->kind == Kind::Player)
            return player_.feet;
        if (en->hunter)
            return en->hunter->feet();
        return en->position;
    };
    w.grabPlayer = [this](Hunter& by) {
        if (grab_ || !player_.alive)
            return false;
        grab_ = &by;
        return true;
    };
    w.reloadGun = [this](Hunter& by) { gunEvents_.push_back({&by, by.weapon(), true, {}}); };
    w.shootPlayer = [this](Hunter& by, float32 damage, const Firearm& gun, bool hit) {
        // The shot: its gun's report at the hunter, the nail / bullet at him.
        const Math::Vector3 from = by.feet();
        if (debugLog_)
            std::cerr << "[gun] " << by.name() << " fires at t " << time_ << (hit ? " (hit)" : " (miss)") << "\n";
        weaponSound(by.weapon() ? by.weapon()->ct : 0, 0xb, 0.0f, from + Math::Vector3{0.0f, 1.4f, 0.0f}, &by);
        if (gun.sound != "TRANQRIFLE")
            musicAmbienceState().gunFired(); // (the weapons' fire sounds, 0x44f773..0x4502d5)
        {
            // Its line: from its gun hand to his chest, or past him when it misses (by up to
            // 0.6 m: ours), stopped by the world.
            Math::Matrix4x4 hand;
            const Math::Vector3 muzzle = by.model().boneTransform(kHandBone, hand) && by.weapon()
                                             ? hand.multiplyPoint(by.weapon()->gun.flashPos)
                                             : from + Math::Vector3{0.0f, 1.4f, 0.0f};
            Math::Vector3 aim = player_.feet + Math::Vector3{0.0f, player_.sneaking ? 0.7f : 1.2f, 0.0f};
            if (!hit)
            {
                std::uniform_real_distribution<float32> off(-0.6f, 0.6f);
                aim += Math::Vector3{off(rng_), off(rng_) * 0.5f, off(rng_)};
            }
            const Math::Vector3 dir = (aim - muzzle).normalized();
            const Math::Vector3 end = muzzle + dir * gun.range;
            const auto struck = setup_.field->shotHit(muzzle, end);
            const std::optional<float32> wall = struck ? std::optional<float32>(struck->fraction) : std::nullopt;
            const Math::Vector3 to = hit ? aim : (wall ? muzzle + (end - muzzle) * *wall : end);
            gunEvents_.push_back({&by, by.weapon(), false, {ShotLine{muzzle, to, hit}}});
            if (hit)
                playFx(gun.bodyHitFx, to, dir * -1.0f); // CHARACTER_HIT_FX on him
            else if (struck)
                shotStruck(gun, to, dir, *struck, true);
        }
        if (playerHealth_ <= 0.0f)
            return;
        const Math::Vector3 head = player_.feet + Math::Vector3{0.0f, 1.6f, 0.0f};
        if (!hit)
        {
            emitSound("SFX_WEAPON_BULLET_SWISH", head, 0.5f);
            return;
        }
        playerHealth_ = std::max(0.0f, playerHealth_ - (playerInvulnerable_ ? 0.0f : damage));
        emitSound("SFX_WEAPON_BULLET_HIT_PED", head, 0.8f);
        playerCries(head);
        shot_ = from;
    };
    w.hitPlayer = [this](float32 damage, const Math::Vector3& from, bool hard, const std::string& stem, int32 step) {
        if (playerHealth_ <= 0.0f)
            return;
        // His guard: a blow from in front does only his weapon's BLOCK_DAMAGE share.
        const Math::Vector3 to{from.x - player_.feet.x, 0.0f, from.z - player_.feet.z};
        const Math::Vector3 ahead{-std::sin(player_.yaw), 0.0f, -std::cos(player_.yaw)};
        if (player_.blocking && ahead.dot(to) > 0.0f)
        {
            damage *= hard ? player_.blockHard : player_.blockQuick;
            blockedHit_ = hard ? 2 : 1;
            emitSound("SFX_WEAPON_MELEE_COLLISION_FIST", player_.feet + Math::Vector3{0.0f, 1.4f, 0.0f}, 0.5f);
            if (damage <= 0.0f)
                return;
            playerHealth_ = std::max(0.0f, playerHealth_ - (playerInvulnerable_ ? 0.0f : damage));
            return;
        }
        playerHealth_ = std::max(0.0f, playerHealth_ - (playerInvulnerable_ ? 0.0f : damage));
        const Math::Vector3 head = player_.feet + Math::Vector3{0.0f, 1.6f, 0.0f};
        playFx("FXP002", player_.feet + Math::Vector3{0.0f, 1.3f, 0.0f}, Math::Vector3{from.x - player_.feet.x, 0.0f, from.z - player_.feet.z});
        emitSound("SFX_WEAPON_MELEE_COLLISION_FIST", head, 0.8f);
        playerCries(head);
        hit_ = from;
        // His reaction: from in front Damage_Melee_<Quick|Hard><n>_<stem> (by the attacker's swing
        // and weapon; it throws him back), from behind Damage_Melee_Bkw_<stem> (forwards).
        if (stem == "Obstruct")
            hitClip_ = "Damage_Obstruct_Hit"; // struck with a gun
        else if (ahead.dot(to) >= 0.0f)
            hitClip_ = std::string("Damage_Melee_") + (hard ? "Hard" : "Quick") + std::to_string(std::clamp(step, 1, 3)) + "_" + stem;
        else
            hitClip_ = "Damage_Melee_Bkw_" + stem;
        if (stem == "Obstruct")
        {
            // The gun's blow shoves him (obstruct*Shot SHOT_FORCE 50; 0.6 m: ours).
            Math::Vector3 away{player_.feet.x - from.x, 0.0f, player_.feet.z - from.z};
            if (const float32 len = away.length(); len > 1e-3f)
                push_ = away * (0.6f / len);
        }
        // A hard blow he does not block knocks him down (ours: the original's rule is not traced).
        if (hard && playerHealth_ > 0.0f && !player_.down)
            knock_ = Knock{from, stem};
    };
    return w;
}

void LevelRuntime::update(float32 dt, const PlayerSnapshot& player)
{
    time_ += dt;
    text_.update(dt);
    for (auto it = bursts_.begin(); it != bursts_.end();)
    {
        if (time_ - it->last <= 0.2f) // (ours: the gap that ends a burst)
        {
            ++it;
            continue;
        }
        const Math::Vector3 at = it->at;
        it = bursts_.erase(it);
        weaponSound(0x2b, 0xb, 1.0f, at);
    }
    padPressed_ |= player.pad & ~padHeld_;
    padHeld_ = player.pad;
    updateMovers(dt);
    updateLights(dt);
    // Effects riding an entity's matrix, then the particles.
    for (auto it = fxAttached_.begin(); it != fxAttached_.end();)
    {
        const Entity* en = entity(it->second.entity);
        if (!fx_.exists(it->first) || !en)
        {
            it = fxAttached_.erase(it);
            continue;
        }
        const Math::Vector3 at = en->kind == Kind::Player ? player.feet : (en->hunter ? en->hunter->feet() : en->position);
        fx_.setPosition(it->first, at + it->second.offset);
        ++it;
    }
    // REELHUNT_FX=<effect>: it plays 3 m in front of him every second (testing the effects).
    if (static const char* v = std::getenv("REELHUNT_FX"); v && !cutscene_)
    {
        static float32 next = 0.0f;
        if (time_ >= next)
        {
            next = time_ + 1.0f;
            const Math::Vector3 ahead{-std::sin(player.yaw), 0.0f, -std::cos(player.yaw)};
            playFx(v, player.feet + ahead * 3.0f + Math::Vector3{0.0f, 1.0f, 0.0f}, ahead);
        }
    }
    // REELHUNT_DECAL=<type>: that decal on the floor under him every second, 3 m across (testing them).
    if (static const char* v = std::getenv("REELHUNT_DECAL"); v && !cutscene_)
    {
        static float32 next = 0.0f;
        if (time_ >= next)
        {
            next = time_ + 1.0f;
            decals_.push_back({std::atoi(v), player.feet + Math::Vector3{0.0f, 0.1f, 0.0f}, {0.0f, 1.0f, 0.0f}, 3.0f});
        }
    }
    fx_.update(dt, rng_);
    if (setup_.scene)
        fx_.draw(*setup_.scene, fxTextures_, viewEye_, viewRight_, viewUp_);
    for (auto it = decayAt_.begin(); it != decayAt_.end();)
    {
        if (time_ < it->second)
        {
            ++it;
            continue;
        }
        for (const auto& h : hunters_)
            if (h.get() == it->first)
                h->vanish();
        it = decayAt_.erase(it);
    }
    if (timerRunning_ && !cutscene_ && (timerLeft_ -= dt) <= 0.0f)
    {
        timerLeft_ = 0.0f;
        timerRunning_ = false;
        timerEnded_ = true;
        for (const std::string& name : scripts_.programEntities())
            if (scripts_.has(name, "OnTimerEnded"))
                scripts_.start(name, "OnTimerEnded");
    }
    look_.noise = std::max(0.0f, look_.noise - dt * 2.0f); // a burst dies away in about half a second
    const bool wasHidden = player_.hidden;
    player_ = player;
    player_.alive = playerAlive();
    // The light hunters see him in, and whether that hides him (Game/ai/stealth.h).
    const float32 safe = tuning_.vision.safeZone;
    player_.light = effectivePlayerLight(player.light, nearSafeZoneNode(nav_, nodeLight_, player.feet, safe), safe);
    player_.hidden = player_.alive && player_.light < safe;
    player_.justHidden = player_.hidden && !wasHidden;
    if (Entity* p = entity(playerHandle_))
        p->position = player.feet;
    if (!cutscene_)
        updateTriggers(player_); // the player sets nothing off while a cut-scene holds him
    scripts_.update(dt);
    updateCamera(dt);
    updatePickups(dt);
    updateSavePoints(dt);
    updateHalos();
    updateUseables(dt);

    HunterWorld w = world(player_);
    updateThrown(dt, w);
    // REELHUNT_GOD: he takes no damage (testing fights).
    if (static const bool god = std::getenv("REELHUNT_GOD") != nullptr; god)
        playerInvulnerable_ = true;
    // REELHUNT_ENEMY=<leader>:<hunter>: 3 s in, that hunter becomes the leader's enemy (testing hunters
    // fighting hunters).
    if (static const char* v = std::getenv("REELHUNT_ENEMY"); v && !cutscene_)
    {
        static bool given = false;
        const std::string spec(v);
        if (const std::size_t c = spec.find(':'); !given && time_ >= 3.0f && c != std::string::npos)
        {
            director_.addLeaderEnemy(spec.substr(0, c), spec.substr(c + 1));
            given = true;
        }
    }
    updatePhysicsProps(dt, w);
    // REELHUNT_KILL=<hunter>[:<hp>[!]][@<s>]: it dies once (or is left with <hp> hit points; with !
    // by a blow from him), when the level has run <s> seconds (testing how bodies are found, and
    // hide health).
    if (static const char* v = std::getenv("REELHUNT_KILL"); v && !cutscene_)
    {
        static bool killed = false;
        const std::string spec(v);
        const std::size_t at = spec.find('@');
        const std::string who = spec.substr(0, at);
        const std::size_t colon = who.find(':');
        if (!killed && time_ >= (at == std::string::npos ? 0.0f : std::stof(spec.substr(at + 1))))
            for (const auto& h : hunters_)
                if (h->alive() && h->name() == lower(who.substr(0, colon)))
                {
                    if (colon == std::string::npos)
                        h->killNow(w);
                    else if (const std::string hp = who.substr(colon + 1); !hp.empty() && hp.back() == '!')
                        h->hit(std::max(0.0f, h->health() - std::stof(hp)), player_.feet, w);
                    else
                        h->setHealth(std::stof(hp));
                    killed = true;
                }
    }
    // REELHUNT_HFALL=<hunter>:<m>@<s> (testing): at <s> seconds it is lifted <m> metres and falls.
    if (static const char* v = std::getenv("REELHUNT_HFALL"); v && !cutscene_)
    {
        static bool lifted = false;
        const std::string spec(v);
        const std::size_t colon = spec.find(':'), at = spec.find('@');
        if (!lifted && colon != std::string::npos && at != std::string::npos && time_ >= std::stof(spec.substr(at + 1)))
            for (const auto& h : hunters_)
                if (h->alive() && h->name() == lower(spec.substr(0, colon)))
                {
                    h->raise(std::stof(spec.substr(colon + 1, at - colon - 1)));
                    lifted = true;
                }
    }
    // REELHUNT_PROVOKE=<hunter> (or *): it always knows where he is and fights him (testing fights).
    if (!cutscene_)
        if (static const char* v = std::getenv("REELHUNT_PROVOKE"); v)
            for (const auto& h : hunters_)
                if (h->alive() && (std::string_view(v) == "*" || h->name() == lower(v)))
                    h->provoke(player_.feet, tuning_.alert);
    for (const auto& h : hunters_)
    {
        const HunterSetup* setup = director_.hunter(h->name());
        if (setup && setup->invisible)
        {
            h->model().setVisible(false); // the pack leader exists only to own the subpacks
            continue;
        }
        h->setAmbient(hunterAmbient_[h.get()].update(setup_.field->lightBelow(h->feet() + Math::Vector3{0.0f, 0.5f, 0.0f}), dt));
        // A cut-scene freezes every hunter it has not enabled (AICutSceneEntityEnable); one in a
        // kill or a grapple keeps playing it (its clip follows the player's), and so does the
        // victim of a kill, which dies (its Die_ clip) before his half of the kill ends.
        const bool driven = h->beingExecuted() || h->state() == HunterState::Held || h->diedInExecution();
        if (cutscene_ && !driven && !cutsceneAllActors_ && std::none_of(cutsceneActors_.begin(), cutsceneActors_.end(),
                                      [&](const std::string& n) { return Assets::iequals(n, h->name()); }))
            continue;
        h->update(dt, w);
        hunterSteps(*h);
    }
    if (!cutscene_)
    {
        scanBodies(w);
        updateBackupWaits(w);
    }
    worldSounds_.clear(); // heard (or not) this frame
    separate(player_);
    updateRadar(dt);
    // A hunter that died lets go of its weapon.
    for (const auto& h : hunters_)
        if (!h->alive() && h->weapon())
            dropHunterWeapon(*h);

    // Bodies walking into doors push them open.
    std::vector<Doors::Pusher> pushers;
    const float32 body = setup_.field->bodyHeight(), radius = setup_.field->playerRadius();
    const float32 inv = dt > 0.0f ? 1.0f / dt : 0.0f;
    if (player_.alive)
        pushers.push_back({player_.feet + Math::Vector3{0.0f, body, 0.0f}, (player_.feet - lastPlayerFeet_) * inv, radius, 65.0f});
    lastPlayerFeet_ = player_.feet;
    for (const auto& h : hunters_)
    {
        const Math::Vector3 feet = h->feet();
        const auto last = lastHunterFeet_.find(h.get());
        if (h->alive() && last != lastHunterFeet_.end())
            pushers.push_back({feet + Math::Vector3{0.0f, body, 0.0f}, (feet - last->second) * inv, radius});
        lastHunterFeet_[h.get()] = feet;
    }
    // REELHUNT_DOOR=<door>@<s> (testing): at <s> seconds the door is unlocked and swung open at
    // 3 rad/s as if pushed; its angle is logged every half second while it moves.
    if (static const char* v = std::getenv("REELHUNT_DOOR"); v && !cutscene_)
    {
        static bool kicked = false;
        static float32 logAt = 0.0f;
        const std::string spec(v);
        const std::size_t at = spec.find('@');
        if (Doors::Door* d = doors_.find(spec.substr(0, at)))
        {
            if (!kicked && time_ >= (at == std::string::npos ? 0.0f : std::stof(spec.substr(at + 1))))
            {
                d->frozen = d->locked = false;
                d->speed = 3.0f;
                d->awake = kicked = true;
                std::cerr << "[door] test: " << d->name << " swung" << std::endl;
            }
            if (kicked && time_ >= logAt && (d->awake || d->speed != 0.0f))
            {
                logAt = time_ + 0.5f;
                std::cerr << "[door] test: " << d->name << " at " << Math::degrees(d->angle) << " deg, " << d->speed << " rad/s"
                          << std::endl;
            }
        }
    }
    doors_.update(dt, pushers);
    // Their sounds: through the audio manager's entity events (0x456150), played by class.
    for (const Doors::Sound& s : doors_.takeSounds())
    {
        entityAudio_.post(static_cast<int32>(s.door), s.event, s.value);
        // A limit hit harder than 1000 is heard by the hunters too (AI sound 0x1c, 0x520640).
        if (s.event == EntityAudio::DoorLimit && s.value > 1000.0f)
            postSound("DOOR_BANG", doors_.doors()[s.door].hinge);
    }
    for (const EntityAudio::Posted& p : entityAudio_.drain())
    {
        const Doors::Door& d = doors_.doors()[static_cast<std::size_t>(p.entity)];
        const EntityAudio::Sample sample = d.slide ? EntityAudio::slideDoor(p.event)
                                                   : EntityAudio::swingDoor(p.event, d.material, p.value, static_cast<uint32>(rng_()));
        if (sample.id < 0 || sample.volume <= 0.0f)
            continue;
        SoundEvent ev;
        ev.sample = "#" + std::to_string(sample.id);
        ev.position = d.hinge + (d.slide ? d.slideAxis * d.offset : Math::Vector3{0.0f, 0.0f, 0.0f}) + Math::Vector3{0.0f, 1.0f, 0.0f};
        ev.volume = sample.volume;
        ev.range = sample.range;
        ev.exeFalloff = true;
        sounds_.push_back(ev);
        if (debugLog_)
            std::cerr << "[door] " << d.name << " sound event 0x" << std::hex << p.event << std::dec << " value " << p.value
                      << " -> sample " << sample.id << " volume " << sample.volume << std::endl;
    }

    if (debugLog_ && (logTimer_ += dt) >= 2.0f)
    {
        logTimer_ = 0.0f;
        static const char* kStates[] = {"", " swinging", " hurt", " stunned", " down", " held", " executed", " dead"};
        std::cerr << "[ai] player hp " << playerHealth_ << " at (" << player_.feet.x << ", " << player_.feet.z << ") light "
                  << player_.light << (player_.hidden ? " HIDDEN" : "")
                  << (player_.sound ? std::string(" ") + player_.sound : std::string());
        // The nearest safe-zone node (somewhere to hide).
        int32 safest = -1;
        float32 best = 1e9f;
        for (int32 n = 0; n < static_cast<int32>(nav_.size()); ++n)
            if (nodeLight_[static_cast<std::size_t>(n)] < tuning_.vision.safeZone)
                if (const float32 d = (nav_.node(n).position - player_.feet).length(); d < best)
                {
                    best = d;
                    safest = n;
                }
        if (safest >= 0)
            std::cerr << "; feet y " << player_.feet.y << " node y " << nav_.node(safest).position.y
                      << "; shadows " << best << " m away at (" << nav_.node(safest).position.x << ", "
                      << nav_.node(safest).position.z << ")";
        for (const auto& h : hunters_)
        {
            const HunterSetup* s = director_.hunter(h->name());
            if (s && s->invisible)
                continue;
            const GoalDef* g = director_.goalFor(h->name(), h->action() != AiAction::NotOnAlert);
            std::cerr << "\n  " << h->name() << " " << aiActionName(h->action()) << kStates[static_cast<int>(h->state())]
                      << " alert " << senseLevelName(h->alertLevel()) << " sight " << senseLevelName(h->sight())
                      << " heard " << senseLevelName(h->hearing()) << " hp " << h->health() << " at (" << h->feet().x
                      << ", " << h->feet().z << ") y " << h->feet().y << " yaw " << static_cast<int>(Math::degrees(h->yaw()))
                      << (g ? " goal " + g->name : std::string(s && !s->idle.empty() ? " idle-actions" : ""));
            if (h->routeLeft() > 0)
                std::cerr << " route " << h->routeLeft() << " -> (" << h->routePoint().x << ", " << h->routePoint().z << ") y "
                          << h->routePoint().y;
        }
        std::cerr << std::endl;
    }
}

void LevelRuntime::updateCamera(float32 dt)
{
    auto advance = [dt](const CameraSpline*& s, float32& t, float32 rate) {
        if (s && (t += dt * rate) >= s->duration)
            s = nullptr; // done: the camera stays where the spline left it
    };
    ScriptCamera& cam = camera_;
    if (followSpline_)
        cam.position = toScene(followSpline_->at(followT_ / followSpline_->duration));
    else if (camPosition_)
        cam.position = *camPosition_;
    if (lookSpline_)
        cam.target = toScene(lookSpline_->at(lookT_ / lookSpline_->duration));
    else if (const Entity* en = camLookAt_ ? entity(camLookAt_) : nullptr)
    {
        const Math::Vector3 feet = en->kind == Kind::Player ? player_.feet : (en->hunter ? en->hunter->feet() : en->position);
        cam.target = feet + Math::Vector3{0.0f, 1.5f, 0.0f};
    }
    else if (camView_)
        cam.target = cam.position + *camView_;
    cam.active = cutscene_ && (followSpline_ || lookSpline_ || camPosition_ || camLookAt_);
    advance(followSpline_, followT_, followRate_);
    advance(lookSpline_, lookT_, lookRate_);
    zoomT_ += dt;
    const float32 z = zoomTime_ > 0.0f ? std::clamp(zoomT_ / zoomTime_, 0.0f, 1.0f) : 1.0f;
    cam.zoom = zoomFrom_ + (zoomTo_ - zoomFrom_) * z;
}

std::vector<Renderer::Mesh*> LevelRuntime::addModelMeshes(const std::string& model, const Assets::InstPlacement& p)
{
    std::vector<Renderer::Mesh*> out;
    if (!models_ && !(models_ = DffLibrary::load(setup_.levelDir + "pak/modelspc.dff")))
        return out;
    const Assets::DffClump* clump = models_->find(model);
    if (!clump || !setup_.scene)
        return out;
    const LevelFrame& frame = setup_.level->frame();
    const Math::Matrix4x4 rotation = LevelFrame::placementRotation(p.rotation);
    const TextureSet& textures = setup_.level->textures();
    for (const Assets::DffAtomic& atomic : clump->atomics)
    {
        std::map<uint32, std::vector<const Assets::DffTriangle*>> byMaterial;
        for (const auto& t : atomic.triangles)
            byMaterial[t.materialIndex].push_back(&t);
        for (const auto& [mi, tris] : byMaterial)
        {
            const Assets::DffMaterial* dm = mi < atomic.materials.size() ? &atomic.materials[mi] : nullptr;
            if (dm && TextureSet::normalizeName(dm->textureName).starts_with("inv_"))
                continue;
            auto mesh = std::make_unique<Renderer::Mesh>();
            std::unordered_map<uint32, uint32> remap;
            auto vertex = [&](uint32 vi) {
                const auto [it, added] = remap.try_emplace(vi, mesh->vertexCount());
                if (added)
                {
                    const Assets::DffVertex& v = atomic.vertices[vi];
                    mesh->addVertex(frame.placedPoint(rotation, Math::Vector3{0.0f, 0.0f, 0.0f}, v.position),
                                    LevelFrame::placedNormal(rotation, v.normal), v.uv, v.color);
                }
                return it->second;
            };
            for (const Assets::DffTriangle* t : tris)
                mesh->addTriangle(vertex(t->a), vertex(t->b), vertex(t->c));
            mesh->computeBoundingBox();
            Renderer::Material m = Renderer::Material::defaultMaterial();
            m.setFlag(Engine::RenderMode::BackCull, false);
            if (dm)
                m.setDiffuse(Math::Vector4{dm->color.x, dm->color.y, dm->color.z, 1.0f});
            if (const Renderer::Texture* tex = dm ? textures.find(dm->textureName) : nullptr)
                textures.apply(m, tex);
            mesh->setMaterial(m);
            mesh->setLighting(atomic.prelit ? Renderer::MeshLighting::Baked : Renderer::MeshLighting::Dynamic);
            mesh->setCastsShadow(false);
            out.push_back(mesh.get());
            setup_.scene->addEntity(std::move(mesh));
        }
    }
    return out;
}

void LevelRuntime::dressSavePoint(Trigger& t)
{
    if (!setup_.scene || t.halo)
        return;
    if (const Assets::InstPlacement* p = placementAt(t.placement))
        t.model = addModelMeshes(savePointModel_, *p);
    auto halo = makePickupHalo(haloTexture_.find("halo"));
    t.halo = halo.get();
    setup_.scene->addEntity(std::move(halo));
}

void LevelRuntime::updateSavePoints(float32 dt)
{
    (void)dt;
    // 0x4d1ac0: spinning about its up axis at 2 rad/s (frame rotate, dt * 2 * 180 / pi degrees).
    const float32 spin = static_cast<float32>(time_) * 2.0f;
    for (Trigger& t : triggers_)
    {
        if (!t.savePoint)
            continue;
        if (t.gone)
        {
            for (Renderer::Mesh* m : t.model)
                m->setVisible(false);
            if (t.halo)
                t.halo->setVisible(false);
            continue;
        }
        if (t.hitAt >= 0.0f && time_ - t.hitAt > 0.1)
        {
            // Faded (0.1 s): it goes, "Save Point Hit" (CHEKPNT, 0x489fc0) and the checkpoint (0x4d2400).
            t.hitAt = -1.0f;
            t.gone = true;
            for (Renderer::Mesh* m : t.model)
                m->setVisible(false);
            if (t.halo)
                t.halo->setVisible(false);
            savePointHit_ = t.placement;
            if (!checkpointText_.empty())
                text_.displayText("CHEKPNT", checkpointText_);
            continue;
        }
        const Math::Matrix4x4 m = Math::Matrix4x4::translation(t.origin) * Math::Matrix4x4::rotationY(spin);
        for (Renderer::Mesh* mesh : t.model)
            mesh->setTransform(m);
        if (t.halo) // 0x4d27a0: at its origin
            placePickupHalo(*t.halo, savePointHalo(), t.origin, viewEye_, viewRight_, viewUp_, static_cast<uint32>(time_ * 1000.0), rng_);
    }
}

void LevelRuntime::updateHalos()
{
    for (FixedHalo& h : fixedHalos_)
    {
        if (destroyed_.count(h.placement))
        {
            h.mesh->setVisible(false);
            continue;
        }
        placePickupHalo(*h.mesh, h.halo, h.centre, viewEye_, viewRight_, viewUp_, static_cast<uint32>(time_ * 1000.0), rng_);
    }
}

void LevelRuntime::buildUseables(const std::vector<Assets::IniRecord>& entityTypes)
{
    // USEABLE_CLASS names in the order of the exe's parser (0x6aa74c ...), which the player's
    // clips follow (clip ids 0x2ca + class "on", 0x2d5 + class "off": 0x41e440 / 0x41e460).
    static constexpr const char* kClasses[] = {"SWITCH",  "BUTTON", "CONSOLE_BUTTON", "LEVER",   "POWER_LEVER", "WHEEL",
                                               "PADLOCK", "ROPE",   "GENERATOR",      "VANDOORS", "LOCK",        "SEARCHABLE"};
    for (const Assets::InstPlacement& p : setup_.level->placements())
    {
        const Assets::IniRecord* r = Assets::findRecord(entityTypes, p.record);
        const auto cls = r ? r->text("CLASS") : std::nullopt;
        if (!cls || !(Assets::iequals(*cls, "EC_SWITCH") || Assets::iequals(*cls, "EC_USEABLE")))
            continue;
        Useable u;
        u.placement = p.index;
        u.name = lower(p.internalName);
        u.origin = setup_.level->frame().placementOrigin(p.position);
        if (const auto c = r->text("USEABLE_CLASS"))
            for (int32 i = 0; i < static_cast<int32>(std::size(kClasses)); ++i)
                if (Assets::iequals(*c, kClasses[i]))
                    u.cls = i;
        u.reach = r->number("HOTSPOT_RADIUS").value_or(0.7f) + r->number("HOTSPOT_HORIZ_DIST").value_or(0.0f);
        u.focus = r->number("FOCUS_RADIUS").value_or(70.0f);
        u.useDelay = r->number("USE_DELAY").value_or(0.5f);
        u.multiple = r->line("MULTIPLE") != nullptr;
        u.twoState = r->line("2_STATE") != nullptr;
        u.anim = r->text("USEABLE_ANIM").value_or("");
        u.offAnim = r->text("USEABLE_OFF_ANIM").value_or("");
        u.collectableAnim = r->text("COLLECTABLE_ANIM").value_or("");
        u.mustAlign = r->line("MUST_ALIGN") != nullptr;
        for (const auto* l : r->all("USE_ITEM"))
            if (l->size() > 1)
                u.useItems.push_back((*l)[1]);
        useables_.push_back(std::move(u));
    }
    if (!useables_.empty())
        std::cerr << "runtime: useables " << useables_.size() << "\n";
}

LevelRuntime::Useable* LevelRuntime::useableAt(const Math::Vector3& feet, float32 yaw)
{
    const Math::Vector3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    Useable* best = nullptr;
    float32 bestDist = 1e9f;
    for (Useable& u : useables_)
    {
        if (!u.enabled || (u.used && !u.multiple) || u.effectIn >= 0.0f || destroyed_.count(u.placement))
            continue;
        const Math::Vector3 to{u.origin.x - feet.x, 0.0f, u.origin.z - feet.z};
        const float32 d = to.length();
        if (d > u.reach + 0.5f || std::fabs(u.origin.y - feet.y) > 2.0f) // (his reach: ours)
            continue;
        if (d > 0.3f && fwd.dot(to * (1.0f / d)) < std::cos(Math::radians(std::min(u.focus, 179.0f) * 0.5f)))
            continue;
        if (!u.useItems.empty())
        {
            // 0x4d40b0: the item it needs in his hand (a generator's Gas_Can excepted).
            const bool holding = equipped_ && std::any_of(u.useItems.begin(), u.useItems.end(),
                                                          [&](const std::string& i) { return Assets::iequals(i, equipped_->record); });
            if (!holding && !Assets::iequals(u.useItems.front(), "Gas_Can"))
                continue;
        }
        if (d < bestDist)
        {
            bestDist = d;
            best = &u;
        }
    }
    return best;
}

bool LevelRuntime::useableInReach(const Math::Vector3& feet, float32 yaw, bool gasCan) const
{
    const Useable* u = const_cast<LevelRuntime*>(this)->useableAt(feet, yaw);
    return u && (!gasCan || (!u->useItems.empty() && Assets::iequals(u->useItems.front(), "Gas_Can")));
}

std::optional<LevelRuntime::UseStart> LevelRuntime::playerUses(const Math::Vector3& feet, float32 yaw, bool gasCan)
{
    Useable* u = useableAt(feet, yaw);
    const bool takesCan = u && !u->useItems.empty() && Assets::iequals(u->useItems.front(), "Gas_Can");
    if (!u || (gasCan && !takesCan))
        return std::nullopt;
    // His clip: by class, "off" for a two-state one that is on (0x41e440 / 0x41e460).
    static constexpr const char* kOn[] = {"Gen_Switch_On",       "Gen_Wall_Button_On", "Gen_Console_Button_On", "Gen_Lever_On",
                                          "Gen_Power_Lever_On",  "Sewer_Wheel_Raise",  "Crowbar_Chain_Break",   "Rope_Cut",
                                          "Gas_Can_Pour",        "Crowbar_VanDoor",    "Lock_Break",            ""};
    static constexpr const char* kOff[] = {"Gen_Switch_Off",      "Gen_Wall_Button_On", "Gen_Console_Button_On", "Gen_Lever_Off",
                                           "Gen_Power_Lever_Off", "Sewer_Wheel_Lower",  "", "", "", "", "", ""};
    UseStart s;
    // A Gas_Can useable's COLLECTABLE_ANIM (Gas_Can_Pour_W) is played on the can in his hand
    // (0x4d40b0: 0x496720 finds the clip, 0x494380 binds it to the held item's clump).
    if (takesCan && gasCan)
        s.itemClip = u->collectableAnim;
    const bool off = u->twoState && u->state == 1;
    s.clip = off && kOff[u->cls][0] ? kOff[u->cls] : kOn[u->cls];
    const Math::Vector3 to = u->origin - feet;
    s.yaw = u->mustAlign && (to.x * to.x + to.z * to.z) > 1e-4f ? std::atan2(-to.x, -to.z) : yaw;
    // A searchable is searched at once (class 11: vtable +0x88 straight away); the rest take
    // effect USE_DELAY into his clip.
    u->effectIn = u->cls == 11 ? 0.0f : u->useDelay;
    // The entity plays its own clip with his (USEABLE_ANIM, or USEABLE_OFF_ANIM switching off:
    // the lever going over, the chain breaking, the rope cut).
    const std::string& own = off && !u->offAnim.empty() ? u->offAnim : u->anim;
    if (!own.empty())
        if (const Assets::InstPlacement* p = placementAt(u->placement))
        {
            animatedProps_.insert(u->placement);
            propAnims_.push_back({p, u->origin, own, false});
        }
    std::cerr << "[runtime] using " << u->name << (s.clip.empty() ? std::string() : " (" + s.clip + ")") << "\n";
    return s;
}

void LevelRuntime::updateUseables(float32 dt)
{
    for (Useable& u : useables_)
    {
        if (u.effectIn < 0.0f || (u.effectIn -= dt) > 0.0f)
            continue;
        u.effectIn = -1.0f;
        u.used = true;
        // 0x4d4390: on, or off when a two-state one was on; then OnUseableUsed (event 70) and, the
        // player having used it, OnUseByPlayer (3: CSwitch 0x4d0530).
        u.state = u.twoState && u.state == 1 ? 2 : 1;
        scripts_.start(u.name, "OnUseableUsed");
        scripts_.start(u.name, "OnUseByPlayer");
        std::cerr << "[runtime] " << u.name << " used: " << (u.state == 1 ? "on" : "off") << "\n";
    }
}

LevelRuntime::Useable* LevelRuntime::useableNamed(const std::string& name)
{
    for (Useable& u : useables_)
        if (u.name == lower(name))
            return &u;
    return nullptr;
}

void LevelRuntime::buildLadders(const std::vector<Assets::IniRecord>& entityTypes)
{
    // The pieces of one ladder stand on one spot: a BASE (0), MIDs (1) and a TOP (2) (CLIMABLE_TYPE,
    // 0x441870; the climbable's handler 0x4c4c30 mounts and dismounts by the piece's type).
    struct Piece {
        Math::Vector3 at;
        int32 type = 1;
        float32 reach = 0.5f, focus = 45.0f, yaw = 0.0f;
    };
    std::vector<Piece> pieces;
    for (const Assets::InstPlacement& p : setup_.level->placements())
    {
        const Assets::IniRecord* r = Assets::findRecord(entityTypes, p.record);
        const auto cls = r ? r->text("CLASS") : std::nullopt;
        if (!cls || !Assets::iequals(*cls, "EC_CLIMABLE"))
            continue;
        Piece piece;
        piece.at = setup_.level->frame().placementOrigin(p.position);
        if (const auto t = r->text("CLIMABLE_TYPE"))
            piece.type = Assets::iequals(*t, "BASE") ? 0 : Assets::iequals(*t, "TOP") ? 2 : 1;
        piece.reach = r->number("ACTIVATION_DIST").value_or(r->number("HOTSPOT_RADIUS").value_or(0.5f));
        piece.focus = r->number("FOCUS_RADIUS").value_or(piece.type == 2 ? 180.0f : 45.0f);
        piece.yaw = LevelFrame::placementYaw(p.rotation);
        pieces.push_back(piece);
    }
    for (const Piece& b : pieces)
    {
        if (b.type != 0)
            continue;
        Ladder l;
        l.base = b.at;
        l.top = b.at.y;
        l.reach = b.reach;
        l.baseFocus = b.focus;
        for (const Piece& t : pieces)
            if (t.type == 2 && flatDistance(t.at, b.at) < 0.5f && t.at.y > l.top)
            {
                l.top = t.at.y;
                l.topFocus = t.focus;
            }
        if (l.top <= l.base.y + 0.5f)
            continue;
        // A climber faces the way the floor at the top lies (ours: of the four ways along and
        // across the piece, the one with floor at the top's height 1 m out).
        float32 best = 1e9f;
        for (int32 k = 0; k < 4; ++k)
        {
            const float32 a = b.yaw + static_cast<float32>(k) * Math::pi * 0.5f;
            const Math::Vector3 d{-std::sin(a), 0.0f, -std::cos(a)};
            const Math::Vector3 over = Math::Vector3{l.base.x, l.top + 1.0f, l.base.z} + d * 1.0f;
            if (const auto hit = setup_.field->raycast(over, over - Math::Vector3{0.0f, 2.0f, 0.0f}))
                if (const float32 off = std::fabs(2.0f * *hit - 1.0f); off < best)
                {
                    best = off;
                    l.face = d;
                }
        }
        // The floor he climbs onto lies a metre over the TOP piece (Trainyard: -3.25 over -4.21:
        // Ladder_OffTop starts with his root 1.003 m under it), found a metre in along `face`.
        const Math::Vector3 over = Math::Vector3{l.base.x, l.top + 2.0f, l.base.z} + l.face * 1.0f;
        if (const auto roof = setup_.field->raycast(over, over - Math::Vector3{0.0f, 3.0f, 0.0f}))
            l.top = over.y - 3.0f * *roof;
        // Its floor (ours: straight down from the BASE piece) and its face (ours: the first surface
        // met walking along `face` from 1.5 m out, a metre up).
        l.foot = l.base.y;
        if (const auto down = setup_.field->raycast(l.base + Math::Vector3{0.0f, 0.5f, 0.0f}, l.base - Math::Vector3{0.0f, 5.5f, 0.0f}))
            l.foot = l.base.y + 0.5f - 6.0f * *down;
        const Math::Vector3 from = Math::Vector3{l.base.x, l.foot + 1.0f, l.base.z} - l.face * 1.5f;
        const auto hit = setup_.field->raycast(from, from + l.face * 3.0f);
        l.wall = hit ? from + l.face * (3.0f * *hit) : Math::Vector3{l.base.x, l.foot, l.base.z};
        l.wall.y = l.foot;
        std::cerr << "runtime: ladder at (" << l.base.x << ", " << l.base.z << ") from " << l.foot << " to " << l.top << ", facing ("
                  << l.face.x << ", " << l.face.z << "), its face at (" << l.wall.x << ", " << l.wall.z << ")\n";
        ladders_.push_back(l);
    }
}

void LevelRuntime::postSound(const std::string& name, const Math::Vector3& at)
{
    worldSounds_.push_back({name, at});
    if (debugLog_)
        std::cerr << "[ai] sound " << name << " at (" << at.x << ", " << at.y << ", " << at.z << ")\n";
}

const ThrowWeapon* LevelRuntime::playerThrowWeapon() const
{
    return equipped_ && equipped_->weapon ? tuning_.throwWeapon(equipped_->weaponType) : nullptr;
}

void LevelRuntime::playerThrows(const Math::Vector3& from, const Math::Vector3& velocity)
{
    const ItemType* type = equipped_;
    if (!type || !playerThrowWeapon())
        return;
    Thrown t;
    t.type = type;
    t.pos = from;
    t.vel = velocity;
    if (models_ && setup_.scene)
    {
        ModelDesc md;
        md.dffPath = setup_.levelDir + "pak/modelspc.dff";
        md.library = models_.get();
        md.clumpName = type->model;
        md.textures = &setup_.level->textures();
        md.position = from;
        auto c = std::make_shared<Character>();
        if (c->load(md, *setup_.scene))
            t.model = std::move(c);
    }
    thrown_.push_back(std::move(t));
    useUp(type); // out of his hand
    std::cerr << "[runtime] threw " << type->record << " at (" << velocity.x << ", " << velocity.y << ", " << velocity.z << ") m/s\n";
}

void LevelRuntime::updateThrown(float32 dt, HunterWorld& w)
{
    for (Thrown& t : thrown_)
    {
        if (t.done)
            continue;
        t.vel = t.vel + Math::Vector3{0.0f, -25.0f, 0.0f} * dt;
        t.vel.y = std::max(t.vel.y, -30.0f);
        const Math::Vector3 next = t.pos + t.vel * dt;
        // A hunter in its way takes the shot's DAMAGE (ours: within half a metre of his middle).
        Hunter* struck = nullptr;
        Math::Vector3 strikeAt = next;
        for (const auto& h : hunters_)
        {
            if (!h->alive() || h->down() || !h->model().isLoaded())
                continue;
            const Math::Vector3 mid = h->feet() + Math::Vector3{0.0f, 1.0f, 0.0f};
            const Math::Vector3 seg = next - t.pos;
            const float32 len2 = seg.dot(seg);
            const float32 k = len2 > 1e-8f ? std::clamp((mid - t.pos).dot(seg) / len2, 0.0f, 1.0f) : 0.0f;
            const Math::Vector3 closest = t.pos + seg * k;
            if ((closest - mid).length() < 0.5f)
            {
                struck = h.get();
                strikeAt = closest;
                break;
            }
        }
        if (struck)
        {
            if (const ThrowWeapon* tw = tuning_.throwWeapon(t.type->weaponType))
                struck->hit(tw->damage, t.pos, w);
            // The shot's CHARACTER_HIT_FX (every *ThrowShot: FXP002, the blood puff).
            playFx("FXP002", strikeAt, t.pos - strikeAt);
            landThrown(t, strikeAt);
            continue;
        }
        if (const auto hit = setup_.field->shotHit(t.pos, next))
        {
            // The severed head (CT_HEAD, 0x65) splats blood2 where it strikes, the faster the
            // bigger: 0.3 m at 1 m/s up to 0.55 m at 12 (0x4cf1b0).
            const float32 speed = t.vel.length();
            if (t.type->ct == 101 && speed > 1.0f)
            {
                const float32 v = std::min(speed, 12.0f);
                decals_.push_back({2, t.pos + (next - t.pos) * hit->fraction, hit->normal, (v - 1.0f) * 0.25f / 11.0f + 0.3f});
            }
            landThrown(t, t.pos + (next - t.pos) * hit->fraction);
            continue;
        }
        t.pos = next;
        t.spin += dt * 12.0f; // tumbling (ours)
        if (t.model)
        {
            t.model->setPlacement(t.pos - Math::Vector3{0.0f, t.model->halfHeight(), 0.0f}, t.spin);
            t.model->update(dt);
        }
        if (t.pos.y < -500.0f)
            t.done = true; // out of the world
    }
    std::erase_if(thrown_, [](const Thrown& t) { return t.done; });
}

void LevelRuntime::landThrown(Thrown& t, const Math::Vector3& at)
{
    // The exe 0x446df0: a hit harder than 20 (ours: mass 5 x speed) by MATERIAL_BRICK, _CAN or
    // _BOTTLE (30..32) makes LURE_THROWN_MEDIUM where it lands, by MATERIAL_GORE (53, the head)
    // LURE_THROWN_LOW.
    const float32 impulse = 5.0f * t.vel.length();
    const std::string m = lower(t.type->material);
    const char* lure = (m == "material_brick" || m == "material_can" || m == "material_bottle") ? "LURE_THROWN_MEDIUM"
                       : m == "material_gore"                                                     ? "LURE_THROWN_LOW"
                                                                                                  : nullptr;
    if (lure && impulse > 20.0f)
        postSound(lure, at);
    // Its collision sound (the material pair's, 0x444df0 -> 0x444f90: the bank's
    // SFX_WEAPON_THROWER_COLLISION_BRICK / _CAN / _BOTTLE; ours, by its MATERIAL), louder the
    // harder (ours: impulse / 100).
    const char* sample = m == "material_brick" ? "SFX_WEAPON_THROWER_COLLISION_BRICK"
                         : m == "material_can"  ? "SFX_WEAPON_THROWER_COLLISION_CAN"
                         : m == "material_bottle" ? "SFX_WEAPON_THROWER_COLLISION_BOTTLE"
                                                  : nullptr;
    if (sample)
    {
        SoundEvent s;
        s.sample = sample;
        s.position = at;
        s.volume = std::clamp(impulse / 100.0f, 0.2f, 1.0f);
        sounds_.push_back(s);
    }
    std::cerr << "[runtime] " << t.type->record << " landed at (" << at.x << ", " << at.y << ", " << at.z << ")"
              << (lure && impulse > 20.0f ? std::string(", ") + lure : std::string()) << "\n";
    if (t.model)
        t.model->setVisible(false);
    t.done = true;
    // A bottle (SMASHABLE2) breaks; the rest lie where they fell, to be picked up again (ours).
    if (!t.type->smashable)
        spawnPickup(t.type, at, t.spin);
}

int32 LevelRuntime::DamageProp::percent() const noexcept
{
    // 0x4336b0: health * 100 / HIT_POINTS, truncated.
    return hitPoints > 0.0f ? static_cast<int32>(std::max(0.0f, health) * 100.0f / hitPoints) : 0;
}

void LevelRuntime::buildDamageProps(const std::vector<Assets::IniRecord>& entityTypes)
{
    for (const Assets::InstPlacement& p : setup_.level->placements())
    {
        if (p.entityClass != "Base_Inst")
            continue;
        const Assets::IniRecord* r = Assets::findRecord(entityTypes, p.record);
        const auto hp = r ? r->number("HIT_POINTS") : std::nullopt;
        if (!r || !hp || *hp <= 0.0f)
            continue;
        DamageProp d;
        for (const auto* l : r->all("LOD_DATA"))
            if (l->size() > 3)
                d.lods.emplace_back(std::atoi((*l)[1].c_str()), std::atoi((*l)[3].c_str()));
        if (d.lods.size() < 2)
            continue; // one model at any health: nothing shows the damage
        d.placement = p.index;
        d.p = &p;
        d.origin = setup_.level->frame().placementOrigin(p.position);
        d.hitPoints = *hp;
        // The class data's first word is the health as a percentage (a save's 100 = whole).
        d.health = (p.params.empty() ? 100.0f : static_cast<float32>(p.params[0])) * d.hitPoints / 100.0f;
        d.lod = d.lods.front().first;
        d.invulnerable = r->text("ARMOUR_CLASS") && Assets::iequals(*r->text("ARMOUR_CLASS"), "ARM_INVULNERABLE");
        d.noColOnDamage = r->line("NO_COL_ON_DAMAGE") != nullptr;
        d.damageFx = r->text("DAMAGE_FX").value_or("");
        d.spawn = r->text("DAMAGE_SPAWN").value_or("");
        for (std::size_t i = 0; i < 3; ++i)
            if (const auto v = r->number("SPAWN_POSITION", i + 1))
                (i == 0 ? d.spawnPosition.x : i == 1 ? d.spawnPosition.y : d.spawnPosition.z) = *v;
        damageProps_.push_back(std::move(d));
    }
    for (DamageProp& d : damageProps_)
        damageProp(d, 0.0f, true); // a checkpoint's damaged props as it left them
    std::cerr << "runtime: damageable props " << damageProps_.size() << "\n";
}

void LevelRuntime::damageProp(DamageProp& d, float32 damage, bool restoring)
{
    d.health = std::max(0.0f, d.health - damage);
    const int32 pct = d.percent();
    // 0x4336b0: the first LOD_DATA entry whose health floor it is at or above.
    const auto entry = std::find_if(d.lods.begin(), d.lods.end(), [&](const auto& e) { return e.second <= pct; });
    if (entry == d.lods.end() || entry->first == d.lod)
        return;
    // 0x4334e0: below the floor of the LOD it showed, the prop is damaged, once.
    const auto shown = std::find_if(d.lods.begin(), d.lods.end(), [&](const auto& e) { return e.first == d.lod; });
    if (!d.damaged && shown != d.lods.end() && pct < shown->second)
    {
        d.damaged = true;
        if (d.noColOnDamage && setup_.field)
            setup_.field->removeProp(d.placement);
        if (!restoring)
        {
            // DAMAGE_FX at its origin (no particle effects yet), and one time in three its
            // DAMAGE_SPAWN thrown out, unless a script owns the prop (0x480e50).
            std::cerr << "[runtime] " << d.p->internalName << " damaged (" << pct << "%)"
                      << (d.damageFx.empty() ? "" : ", fx " + d.damageFx) << "\n";
            const ItemType* spawn = d.spawn.empty() ? nullptr : items_.find(d.spawn);
            if (spawn && !scripts_.hasProgram(d.p->internalName) && std::uniform_int_distribution<int32>(0, 2)(rng_) == 0)
            {
                const Math::Matrix4x4 rotation = LevelFrame::placementRotation(d.p->rotation);
                spawnPickup(spawn, setup_.level->frame().placedPoint(rotation, d.origin, d.spawnPosition), 0.0f);
            }
        }
    }
    d.lod = entry->first;
    setup_.level->setPropLod(*d.p, d.lod);
}

void LevelRuntime::buildPickups()
{
    // The halo texture (pictures/frontend_pc.txd, loaded by the exe at 0x5ea823 into 0x7d37cc).
    if (auto txd = Assets::Txd::load(setup_.gameRoot + "pictures/frontend_pc.txd"))
    {
        std::vector<Assets::TxdTexture> halo;
        for (Assets::TxdTexture& t : *txd)
            if (Assets::iequals(t.name, "halo"))
                halo.push_back(std::move(t));
        haloTexture_.add(halo);
    }
    if (!haloTexture_.find("halo"))
        std::cerr << "runtime: no halo texture in pictures/frontend_pc.txd\n";
    for (const Assets::InstPlacement& p : setup_.level->placements())
    {
        const ItemType* type = items_.find(p.record);
        if (!type)
            continue;
        Pickup k;
        k.type = type;
        k.placement = p.index;
        k.position = setup_.level->frame().placementOrigin(p.position);
        k.meshes = setup_.level->meshesOf(p);
        if (k.meshes.empty())
            continue;
        Math::Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (Renderer::Mesh* m : k.meshes)
        {
            m->setCastsShadow(false); // a small spinning thing
            const auto& bb = m->boundingBox();
            lo = {std::min(lo.x, bb.min.x), std::min(lo.y, bb.min.y), std::min(lo.z, bb.min.z)};
            hi = {std::max(hi.x, bb.max.x), std::max(hi.y, bb.max.y), std::max(hi.z, bb.max.z)};
        }
        k.centre = (lo + hi) * 0.5f;
        if (setup_.scene && type->halo.shown)
        {
            auto halo = makePickupHalo(haloTexture_.find("halo"));
            k.halo = halo.get();
            setup_.scene->addEntity(std::move(halo));
        }
        pickups_.push_back(std::move(k));
    }
    std::cerr << "runtime: items " << items_.size() << ", pickups " << pickups_.size() << "\n";}

void LevelRuntime::updatePickups(float32 dt)
{
    const ItemType* dropped = nullptr; // swapped out this frame (spawned after the loop)
    swapOffer_ = {0, 0};
    for (Pickup& k : pickups_)
    {
        if (k.taken)
            continue;
        // Still falling to the floor (it cannot be picked up yet).
        if (k.fall > 0.0f)
        {
            k.fallSpeed = std::min(k.fallSpeed + 25.0f * dt, 30.0f);
            k.fall = std::max(0.0f, k.fall - k.fallSpeed * dt);
        }
        const Math::Vector3 drop{0.0f, k.fall, 0.0f};
        // Lying there: shown DISPLAY_SCALE times bigger, turning once per MODEL_DISPLAY_ANIM.
        float32 period = 2.667f;
        if (setup_.animations)
            if (const auto* a = setup_.animations->findAnimation(k.type->displayAnim); a && a->duration > 0.1f)
                period = a->duration;
        const float32 angle = time_ / period * 2.0f * Math::pi;
        const Math::Matrix4x4 m = Math::Matrix4x4::translation(k.centre + drop) * Math::Matrix4x4::rotationY(angle) *
                                  Math::Matrix4x4::scale(k.type->displayScale) * Math::Matrix4x4::translation(k.centre * -1.0f);
        for (Renderer::Mesh* mesh : k.meshes)
            mesh->setTransform(m);
        if (k.model)
        {
            k.model->setPlacement(k.centre + drop - Math::Vector3{0.0f, k.model->halfHeight(), 0.0f}, k.yaw + angle);
            k.model->update(dt);
        }
        if (k.halo)
            placePickupHalo(*k.halo, k.type->halo, k.centre + drop, viewEye_, viewRight_, viewUp_,
                            static_cast<uint32>(time_ * 1000.0), rng_);
        if (k.fall > 0.0f)
            continue;

        // Walking over it picks it up (HOTSPOT_RADIUS about the feet, on his floor).
        // (Nothing is picked up while a cut-scene or a kill holds him.)
        if (!player_.alive || cutscene_ ||
            std::any_of(hunters_.begin(), hunters_.end(), [](const auto& h) { return h->beingExecuted(); }))
            continue;
        const Math::Vector3 d = k.position - player_.feet;
        if (d.x * d.x + d.z * d.z > k.type->hotspot * k.type->hotspot || std::fabs(d.y) > 1.5f)
        {
            k.waiting = false;
            continue;
        }
        if (k.waiting)
            continue;
        const bool health = !k.type->weapon && Assets::iequals(k.type->collectable, "CT_G_FIRST_AID");
        if (health && playerHealth_ >= tuning_.playerHitPoints)
            continue; // a painkiller is left for later while he is unhurt
        // Guns and ammo boxes (exe 0x4caa..., 0x463ed0): a second gun of a kind he carries
        // gives its rounds to the spare ones, a box its fixed count to the first matching gun he
        // carries. While those are full, or with no gun for a box, they stay where they lie.
        const Firearm* gun = k.type->weapon ? tuning_.firearm(k.type->weaponType) : nullptr;
        const AmmoBox* box = k.type->weapon ? nullptr : ammoBox(k.type->collectable);
        const bool had = std::find(inventory_.begin(), inventory_.end(), k.type) != inventory_.end();
        // One item in each holster slot (0x4c7140), and never two of a kind (guns give their
        // rounds instead, below). An item for a filled slot waits: SWAPINV and its name (0x4c5870)
        // show, unless INVENTORY SWAP is held; then what he had there drops where he stands (and
        // the new one goes to his hand if that one was in it).
        const int32 slot = inventorySlot(k.type->holsterSlot);
        if (had && !gun && !box)
            continue;
        const ItemType* occupant = nullptr;
        if (slot > 0 && !box && !health)
            for (const ItemType* t : inventory_)
                if (t != k.type && inventorySlot(t->holsterSlot) == slot)
                    occupant = t;
        if (occupant && !swapHeld_)
        {
            swapOffer_ = {slot, k.type->ct}; // (0x5df980, every frame he stands on it)
            if (prompted_ != &k || !text_.displaying())
            {
                const auto name = itemNames_.find(k.type->ct);
                const std::string prompt = swapPrompt_ + " " + (name != itemNames_.end() ? name->second : k.type->record);
                if (prompted_ != &k)
                    std::cerr << "[runtime] " << prompt << " (" << occupant->record << " in its slot)\n";
                text_.displayText("SWAPINV", prompt);
                prompted_ = &k;
            }
            continue;
        }
        bool toHand = k.type->weapon && slot != 5; // (a SPECIAL item never goes to his hand)
        if (occupant)
        {
            inventory_.erase(std::find(inventory_.begin(), inventory_.end(), occupant));
            if (equipped_ == occupant)
                equipped_ = nullptr;
            dropped = occupant;
            std::cerr << "[runtime] swapped " << occupant->record << " for " << k.type->record << "\n";
        }
        const int32 rounds = gun ? (k.rounds < 0 ? gun->droppedAmmo : k.rounds) : box ? box->rounds : 0;
        if (gun && !takeGun(k.type, rounds))
            continue;
        if (box && !loadBox(*box))
            continue;
        k.taken = true;
        for (Renderer::Mesh* mesh : k.meshes)
            mesh->setVisible(false);
        if (k.model)
            k.model->setVisible(false);
        if (k.halo)
            k.halo->setVisible(false);
        lastPickedUp_ = k.type->ct;
        pickedUp_ = k.type;
        if (health)
            playerHealth_ = std::min(tuning_.playerHitPoints, playerHealth_ + tuning_.playerHitPoints * 0.5f);
        else if (box || (gun && had))
            ; // only rounds
        else
        {
            inventory_.push_back(k.type);
            std::string name = k.name;
            for (const Entity& e : entities_)
                if (name.empty() && k.placement >= 0 && e.placement == k.placement)
                    name = e.name;
            inventoryNames_[k.type] = name;
            if (toHand)
                equipped_ = k.type; // the new weapon goes to his hand
        }
        std::cerr << "[runtime] picked up " << k.type->record << " (" << k.type->collectable << " " << k.type->ct << ")"
                  << (gun || box ? " +" + std::to_string(rounds) + " rounds" : std::string()) << "\n";
        scripts_.start("player", "OnPickUpInventoryItem");
    }
    // What he swapped out lies at his feet, not taken again until he steps off it.
    if (dropped)
    {
        spawnPickup(dropped, player_.feet, player_.yaw + 1.2f);
        pickups_.back().waiting = true;
    }
}

void LevelRuntime::dropHunterWeapon(Hunter& h)
{
    const ItemType* item = h.weapon();
    if (!item)
        return;
    h.model().detach(item->model, kHandBone);
    h.setWeapon(nullptr, nullptr);
    const Math::Vector3 ahead{-std::sin(h.yaw()), 0.0f, -std::cos(h.yaw())};
    // A gun falls with DROPPED_AMMO rounds in it.
    const Firearm* gun = tuning_.firearm(item->weaponType);
    const auto set = dropAmmo_.find(&h); // SetHunterDropAmmo
    spawnPickup(item, h.feet() + ahead * 0.6f, h.yaw() + 1.2f, {}, gun ? (set != dropAmmo_.end() ? set->second : gun->droppedAmmo) : -1);
    std::cerr << "[runtime] " << h.name() << " dropped " << item->record << "\n";
}

void LevelRuntime::severHead(Hunter& h)
{
    const std::string body = hunterRecords_[&h];
    std::string head = body;
    if (const std::size_t bod = head.find("Bod"); bod != std::string::npos)
        head.replace(bod, 3, "Hed");
    if (!h.model().detach(head))
        return;
    // It lies by the body; picked up it is the Severed_Head_(CT) (CT_HEAD), shown with the
    // hunter's own head model.
    const Math::Vector3 ahead{-std::sin(h.yaw()), 0.0f, -std::cos(h.yaw())};
    if (const ItemType* t = items_.find("Severed_Head_(CT)"))
    {
        const std::size_t before = pickups_.size();
        spawnPickup(t, h.feet() + ahead * 1.2f, h.yaw(), head);
        if (pickups_.size() > before)
        {
            // The head is an entity of its own, <hunter>_h (Asylum asks for 'Hunter1_Tower_h').
            pickups_.back().name = h.name() + "_h";
            if (!handleOf(pickups_.back().name))
                addEntity(pickups_.back().name, Kind::Placement, pickups_.back().position);
        }
    }
    std::cerr << "[runtime] " << h.name() << "'s head comes off\n";
}

void LevelRuntime::spawnPickup(const ItemType* type, const Math::Vector3& ground, float32 yaw, const std::string& model,
                               int32 rounds)
{
    if (!type || !models_ || !setup_.scene)
        return;
    Pickup k;
    k.type = type;
    k.yaw = yaw;
    k.rounds = rounds;
    ModelDesc md;
    md.dffPath = setup_.levelDir + "pak/modelspc.dff";
    md.library = models_.get();
    md.clumpName = model.empty() ? type->model : model;
    md.textures = &setup_.level->textures();
    md.scale = type->displayScale;
    md.position = ground;
    auto c = std::make_shared<Character>();
    if (!c->load(md, *setup_.scene))
        return;
    // It rests on the floor below where it fell.
    Math::Vector3 at = ground + Math::Vector3{0.0f, setup_.field->bodyHeight(), 0.0f};
    Math::Vector3 v{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 40 && !setup_.field->resolveCharacter(at, v); ++i)
        at.y -= 0.25f;
    k.position = at - Math::Vector3{0.0f, setup_.field->bodyHeight(), 0.0f};
    k.fall = std::max(0.0f, ground.y - k.position.y);
    k.centre = k.position + Math::Vector3{0.0f, c->halfHeight() + 0.1f, 0.0f};
    c->setPlacement(k.centre - Math::Vector3{0.0f, c->halfHeight(), 0.0f}, yaw);
    c->update(0.0f);
    k.model = std::move(c);
    if (type->halo.shown)
    {
        auto halo = makePickupHalo(haloTexture_.find("halo"));
        k.halo = halo.get();
        setup_.scene->addEntity(std::move(halo));
    }
    std::cerr << "[runtime] " << type->record << " lies at (" << k.position.x << ", " << k.position.y << ", " << k.position.z << ")\n";
    pickups_.push_back(std::move(k));
}

const ItemType* LevelRuntime::takePickedUp()
{
    return std::exchange(pickedUp_, nullptr);
}

void LevelRuntime::cycleWeapon(bool back)
{
    // By holster slot, as the exe's next weapon (0x5ddce0): 3, 2, 1, 4, fists for an empty 3.
    // INVENTORY DOWN goes the other way (ours).
    if (weaponSelectionDisabled_)
        return; // DisableWeaponSelection(TRUE)
    const std::vector<const ItemType*> choices = weaponCycle(inventory_);
    const auto at = std::find(choices.begin(), choices.end(), equipped_);
    if (back)
        equipped_ = at == choices.end() || at == choices.begin() ? choices.back() : *(at - 1);
    else
        equipped_ = at == choices.end() || at + 1 == choices.end() ? choices.front() : *(at + 1);
    std::cerr << "[runtime] weapon " << (equipped_ ? equipped_->record : std::string("fists")) << "\n";
}

void LevelRuntime::selectSlot(int32 slot)
{
    if (weaponSelectionDisabled_)
        return;
    for (const ItemType* t : weaponCycle(inventory_))
        if (t && inventorySlot(t->holsterSlot) == slot)
        {
            equipped_ = t;
            std::cerr << "[runtime] weapon " << t->record << " (slot " << slot << ")\n";
            return;
        }
}

bool LevelRuntime::takeGun(const ItemType* type, int32 rounds)
{
    const Firearm* gun = type ? tuning_.firearm(type->weaponType) : nullptr;
    if (!gun)
        return false;
    const std::string weapon = lower(type->weaponType);
    if (std::find(inventory_.begin(), inventory_.end(), type) == inventory_.end())
    {
        ammo_.set(*gun, weapon, rounds); // a new gun keeps the rounds it had
        return true;
    }
    return ammo_.takeRounds(*gun, weapon, rounds);
}

bool LevelRuntime::loadBox(const AmmoBox& box)
{
    for (const char* ct : box.guns)
        for (const ItemType* t : inventory_)
            if (ct && t->weapon && Assets::iequals(t->collectable, ct))
                if (const Firearm* gun = tuning_.firearm(t->weaponType))
                    return ammo_.addBox(*gun, lower(t->weaponType), box.rounds);
    return false;
}

std::array<SaveInventorySlot, SaveFile::kInventorySlots> LevelRuntime::inventorySlots() const
{
    std::array<SaveInventorySlot, SaveFile::kInventorySlots> out{};
    for (const ItemType* t : inventory_)
    {
        const int32 slot = inventorySlot(t->holsterSlot);
        if (slot < 1 || slot > static_cast<int32>(out.size()))
            continue;
        SaveInventorySlot& s = out[static_cast<std::size_t>(slot - 1)];
        s.record = t->record;
        s.rounds = -1;
        if (t->weapon && tuning_.firearm(t->weaponType))
        {
            const std::string weapon = lower(t->weaponType);
            s.rounds = ammo_.loaded(weapon) + ammo_.spare(weapon);
        }
    }
    return out;
}

void LevelRuntime::restoreInventory(const std::array<SaveInventorySlot, SaveFile::kInventorySlots>& slots)
{
    inventory_.clear();
    ammo_.clear();
    equipped_ = nullptr;
    for (const SaveInventorySlot& s : slots)
    {
        if (s.record.empty())
            continue;
        const ItemType* t = items_.find(s.record);
        if (!t)
        {
            std::cerr << "[runtime] save: no item " << s.record << " in this level\n";
            continue;
        }
        inventory_.push_back(t);
        if (const Firearm* gun = t->weapon ? tuning_.firearm(t->weaponType) : nullptr)
            ammo_.set(*gun, lower(t->weaponType), s.rounds);
        std::cerr << "[runtime] save: restored " << t->record << (s.rounds >= 0 ? " with " + std::to_string(s.rounds) + " rounds" : "")
                  << "\n";
    }
}

bool LevelRuntime::give(const std::string& record)
{
    const ItemType* t = items_.find(record);
    if (!t)
        return false;
    // As if picked up: an ammo box loads a gun he carries; a gun holds DROPPED_AMMO.
    if (const AmmoBox* box = t->weapon ? nullptr : ammoBox(t->collectable))
        return loadBox(*box);
    if (const Firearm* gun = tuning_.firearm(t->weaponType); gun && t->weapon)
    {
        const bool had = std::find(inventory_.begin(), inventory_.end(), t) != inventory_.end();
        takeGun(t, gun->droppedAmmo);
        if (had)
            return true;
    }
    inventory_.push_back(t);
    if (t->weapon)
        equipped_ = t;
    return true;
}

void LevelRuntime::useUp(const ItemType* item)
{
    if (const auto it = std::find(inventory_.begin(), inventory_.end(), item); it != inventory_.end())
        inventory_.erase(it);
    if (equipped_ == item && std::find(inventory_.begin(), inventory_.end(), item) == inventory_.end())
    {
        // The next weapon he carries, else fists.
        equipped_ = nullptr;
        for (auto it = inventory_.rbegin(); it != inventory_.rend() && !equipped_; ++it)
            if ((*it)->weapon)
                equipped_ = *it;
    }
}

void LevelRuntime::skipCutscene()
{
    constexpr float32 kStep = 0.1f;
    for (int i = 0; i < 3000 && cutscene_; ++i) // at most five minutes of script time
    {
        time_ += kStep;
        text_.update(kStep);
        scripts_.update(kStep);
        updateCamera(kStep);
    }
    text_.killSubtitle();
    directorSpeech_.clear(); // its lines are skipped with it
    std::cerr << "runtime: cut-scene skipped\n";
}

void LevelRuntime::endCutscene()
{
    cutscene_ = false;
    camPosition_.reset();
    camView_.reset();
    camLookAt_ = 0;
    followSpline_ = lookSpline_ = nullptr;
    zoomFrom_ = zoomTo_ = 1.0f;
    zoomTime_ = 0.0f;
    camera_ = ScriptCamera{};
}

void LevelRuntime::updateTriggers(const PlayerSnapshot& player)
{
    // Whether a body standing at `feet` is in the trigger.
    auto contains = [](const Trigger& t, const Math::Vector3& feet) {
        const Math::Vector3 body = feet + Math::Vector3{0.0f, 1.0f, 0.0f};
        if (t.box)
            return body.x >= std::min(t.a.x, t.b.x) && body.x <= std::max(t.a.x, t.b.x) && body.y >= std::min(t.a.y, t.b.y) - 1.0f &&
                   body.y <= std::max(t.a.y, t.b.y) + 1.0f && body.z >= std::min(t.a.z, t.b.z) && body.z <= std::max(t.a.z, t.b.z);
        return t.sphere ? (body - t.a).length() <= t.radius + 0.5f
                        : segmentDistance(feet, t.a, t.b) <= t.radius && std::fabs(feet.y - 0.5f * (t.a.y + t.b.y)) < 3.0f;
    };
    for (Trigger& t : triggers_)
    {
        if (t.gone)
            continue;
        if (t.attachedTo)
        {
            if (const Entity* en = entity(t.attachedTo))
            {
                const Math::Vector3 at = en->kind == Kind::Player ? player.feet : (en->hunter ? en->hunter->feet() : en->position);
                t.a = at + t.attachOffset;
            }
        }
        const bool inside = contains(t, player.feet);
        // The classes TriggerAddEntityClass added: living hunters (COL_HUNTER 0x10), dead ones
        // (COL_DEADPEDJOEY 0x20000000), collectables (COL_COLLECTABLE 0x1000) and weapons
        // (COL_WEAPON 0x2000) lying in it. Ours: by position (the exe's are collision contacts).
        bool othersCame = false, othersLeft = false;
        if (t.classes != 0)
        {
            std::vector<std::pair<std::string, int32>> now;
            for (const auto& h : hunters_)
                if (!h->gone() && (t.classes & (h->alive() ? 0x10u : 0x20000000u)) && contains(t, h->feet()))
                    now.emplace_back(h->name(), 0x1f);
            if (t.classes & 0x3000u)
                for (const Pickup& p : pickups_)
                {
                    if (p.taken || !p.type || p.placement < 0 || !(t.classes & (p.type->weapon ? 0x2000u : 0x1000u)) ||
                        !contains(t, p.position))
                        continue;
                    for (const Entity& e : entities_)
                        if (e.placement == p.placement)
                        {
                            now.emplace_back(e.name, p.type->weapon ? 0x3001 : 0x1001);
                            break;
                        }
                }
            std::vector<std::pair<std::string, int32>> before = std::move(t.occupants);
            before.insert(before.end(), t.arrivals.begin(), t.arrivals.end());
            t.occupants.clear();
            t.arrivals.clear();
            for (const auto& o : now)
                (std::find(before.begin(), before.end(), o) != before.end() ? t.occupants : t.arrivals).push_back(o);
            for (const auto& o : before)
                othersLeft = othersLeft || std::find(now.begin(), now.end(), o) == now.end();
            othersCame = !t.arrivals.empty();
            if (debugLog_)
                for (const auto& [name, cls] : t.arrivals)
                    std::cerr << "[trigger] " << name << " (class " << cls << ") enters " << t.name << std::endl;
        }
        t.entered = inside && !t.inside;
        t.left = !inside && t.inside;
        t.inside = inside;
        if (t.entered && t.savePoint && !t.used)
        {
            // A save point walked into (0x4d1ac0: state 2): it fades for 0.1 s, then the
            // checkpoint (updateSavePoints).
            t.used = true;
            t.hitAt = time_;
        }
        if (t.entered || othersCame)
            scripts_.start(t.name, "OnEnterTrigger");
        if (t.left || othersLeft)
            scripts_.start(t.name, "OnLeaveTrigger");
    }
}

void LevelRuntime::separate(const PlayerSnapshot& player)
{
    // Living bodies do not overlap: hunters step away from the player and from each other. A
    // hunter in a kill or a grapple stands where its paired clip puts it (on the player's own
    // origin), and one on the floor lies where it fell: none of those is pushed.
    auto placed = [](const Hunter& h) {
        return h.beingExecuted() || h.state() == HunterState::Held || h.down();
    };
    for (std::size_t i = 0; i < hunters_.size(); ++i)
    {
        Hunter& a = *hunters_[i];
        if (!a.alive() || placed(a))
            continue;
        const Math::Vector3 pa = a.feet();
        const float32 dp = flatDistance(pa, player.feet);
        const float32 minPlayer = a.radius() * 1.4f;
        if (player.alive && dp < minPlayer && dp > 1e-4f && std::fabs(pa.y - player.feet.y) < 1.5f)
            a.nudge(Math::Vector3{pa.x - player.feet.x, 0.0f, pa.z - player.feet.z} * ((minPlayer - dp) / dp));
        for (std::size_t j = i + 1; j < hunters_.size(); ++j)
        {
            Hunter& b = *hunters_[j];
            if (!b.alive() || placed(b))
                continue;
            const Math::Vector3 d{b.feet().x - a.feet().x, 0.0f, b.feet().z - a.feet().z};
            const float32 dist = d.length();
            const float32 minDist = a.radius() + b.radius();
            if (dist < minDist && dist > 1e-4f && std::fabs(a.feet().y - b.feet().y) < 1.5f)
            {
                const Math::Vector3 push = d * (0.5f * (minDist - dist) / dist);
                a.nudge(push * -1.0f);
                b.nudge(push);
            }
        }
    }
}

void LevelRuntime::playerStrikes(const Math::Vector3& feet, float32 yaw, const MeleeAttack& attack, HitKind kind,
                                 const std::string& stem)
{
    const Math::Vector3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    HunterWorld w = world(player_);
    bool struckHunter = false, struckProp = false;
    for (const auto& h : hunters_)
    {
        if (!h->alive() || h->down() || h->model().isLoaded() == false)
            continue;
        const HunterSetup* setup = director_.hunter(h->name());
        if (setup && setup->invisible)
            continue;
        const Math::Vector3 to{h->feet().x - feet.x, 0.0f, h->feet().z - feet.z};
        const float32 dist = to.length();
        // The shot sphere (radius SIZE, ShotTypeData.ini) reaching the hunter's body, in front.
        if (dist > attack.size + h->radius() || std::fabs(h->feet().y - feet.y) > 1.2f)
            continue;
        if (dist > 0.3f && fwd.dot(to * (1.0f / dist)) < 0.4f) // in front, within ~66 degrees
            continue;
        h->hit(attack.damage, feet, w, kind, stem);
        playFx("FXP002", h->feet() + Math::Vector3{0.0f, 1.3f, 0.0f}, to * -1.0f); // the melee shots' CHARACTER_HIT_FX
        // Killed by a baseball bat (CT 34 / 35 / 38): blood, 1 m, on the ground below his pelvis
        // (0x49d560; the exe also wants the blow on bone 1001 or 1096, not told apart here).
        if (!h->alive() && equipped_ && (equipped_->ct == 34 || equipped_->ct == 35 || equipped_->ct == 38))
            decals_.push_back({1, h->feet() + Math::Vector3{0.0f, 0.1f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f});
        struckHunter = true;
    }
    // A prop in reach takes the blow too (ours: the hunters' reach and arc; the exe's test for
    // props is not traced).
    for (DamageProp& d : damageProps_)
    {
        const Math::Vector3 to{d.origin.x - feet.x, 0.0f, d.origin.z - feet.z};
        const float32 dist = to.length();
        if (d.invulnerable || dist > attack.size + 0.5f || std::fabs(d.origin.y - feet.y) > 1.2f)
            continue;
        if (dist > 0.3f && fwd.dot(to * (1.0f / dist)) < 0.4f)
            continue;
        damageProp(d, attack.damage);
        struckProp = true;
    }
    // A blow on a surface lures (exe 0x4f7120, posted on the striker: "You can lure a
    // hunter by hitting any wall"): a punch or swing (SC_MELEE / _OBSTRUCT) on the world
    // LURE_HIGH, on a prop LURE_MEDIUM; a kick (SC_MELEE_KICK) LURE_MEDIUM (LURE_VERY_LOW on paper
    // or a plastic bag: not told apart here). Whether it reaches the wall is ours: the shot's
    // SIZE ahead of his chest.
    if (struckHunter)
        return;
    const bool kick = kind == HitKind::Kick || kind == HitKind::Stomp;
    const Math::Vector3 chest = feet + Math::Vector3{0.0f, 1.1f, 0.0f};
    if (struckProp || kick)
    {
        if (struckProp || setup_.field->raycast(chest - Math::Vector3{0.0f, 0.6f, 0.0f}, chest - Math::Vector3{0.0f, 0.6f, 0.0f} + fwd * attack.size))
            postSound("LURE_MEDIUM", feet);
    }
    else if (setup_.field->raycast(chest, chest + fwd * attack.size))
        postSound("LURE_HIGH", feet);
}

const Firearm* LevelRuntime::playerGun() const
{
    return equipped_ && equipped_->weapon ? tuning_.firearm(equipped_->weaponType) : nullptr;
}

std::string LevelRuntime::playerGunRecord() const
{
    return equipped_ ? lower(equipped_->weaponType) : std::string();
}

bool LevelRuntime::keepsLock(const Hunter& h, const Math::Vector3& eye)
{
    if (!h.alive() || h.beingExecuted() || !h.model().isLoaded())
        return false;
    const HunterSetup* setup = director_.hunter(h.name());
    if (setup && (setup->invisible || !setup->added))
        return false;
    const Math::Vector3 chest = aimPoint(h.feet(), false);
    return (chest - eye).length() <= tuning_.aim.lockonDist && setup_.field->lineOfSight(eye, chest);
}

Hunter* LevelRuntime::lockTarget(const Math::Vector3& eye, float32 yaw, float32 pitch)
{
    Hunter* best = nullptr;
    float32 bestDist = 1e30f;
    for (const auto& h : hunters_)
    {
        if (!h->lockonable())
            continue; // SetPedLockonable(.., FALSE)
        const Math::Vector3 chest = aimPoint(h->feet(), false);
        const float32 d = (chest - eye).length();
        if (d >= bestDist || !inLockonCone(tuning_.aim, eye, yaw, pitch, chest) || !keepsLock(*h, eye))
            continue;
        best = h.get();
        bestDist = d;
    }
    return best;
}

bool LevelRuntime::playerFires(const Math::Vector3& origin, const Math::Vector3& dir, const Math::Vector3& from)
{
    const Firearm* gun = playerGun();
    if (!gun)
        return false;
    const Math::Vector3 muzzle = from + Math::Vector3{0.0f, 1.4f, 0.0f};
    if (!ammo_.fire(playerGunRecord()))
    {
        weaponSound(equipped_ ? equipped_->ct : 0, 0xc, 0.0f, muzzle);
        return false;
    }
    weaponSound(equipped_ ? equipped_->ct : 0, 0xb, 0.0f, muzzle, &player_);
    if (gun->sound != "TRANQRIFLE")
        musicAmbienceState().gunFired(); // Estate_Ext's bed falls silent (0x5a6e70)
    HunterWorld w = world(player_);
    GunEvent event{nullptr, equipped_, false, {}};
    for (int32 pellet = 0; pellet < gun->pellets; ++pellet)
    {
        const Math::Vector3 d = spreadDirection(dir, gun->spread, rng_);
        // The world stops it; before that it goes through up to BODIES_HIT hunters, nearest first.
        const Math::Vector3 end = origin + d * gun->range;
        const auto struck = setup_.field->shotHit(origin, end);
        const std::optional<float32> wall = struck ? std::optional<float32>(struck->fraction) : std::nullopt;
        const float32 reach = wall ? *wall * gun->range : gun->range;
        std::vector<std::pair<Hunter*, BodyHit>> hits;
        for (const auto& h : hunters_)
        {
            if (!h->alive() || h->beingExecuted() || !h->model().isLoaded())
                continue;
            const HunterSetup* setup = director_.hunter(h->name());
            if (setup && setup->invisible)
                continue;
            // (Out of the line's way altogether: skipped.)
            const Math::Vector3 mid = h->feet() + Math::Vector3{0.0f, 1.0f, 0.0f};
            const float32 along = std::clamp((mid - origin).dot(d), 0.0f, reach);
            if ((origin + d * along - mid).length() > 2.0f)
                continue;
            // Its posed bones (the parts follow its animation: lying, reeling ...); a model without
            // the biped's bones is a pair of cylinders.
            std::unordered_map<int16, Math::Vector3> bones;
            for (const Character::BonePoint& p : h->model().bonePoints())
                bones[p.id] = p.position;
            const std::vector<BodyPart> parts = bodyParts(bones);
            const BodyHit b = parts.empty() ? shotHitsBody(h->feet(), h->yaw(), origin, d, reach)
                                            : shotHitsParts(parts, h->feet(), h->yaw(), origin, d, reach);
            if (b.zone != HitZone::None)
                hits.emplace_back(h.get(), b);
        }
        std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.second.distance < b.second.distance; });
        if (hits.size() > static_cast<std::size_t>(gun->bodies))
            hits.resize(static_cast<std::size_t>(gun->bodies));
        for (const auto& [h, b] : hits)
        {
            const float32 damage = zoneDamage(*gun, b.zone, h->fullHealth(), h->hardHead());
            h->shot(damage, from, b, w);
            playFx(gun->bodyHitFx, origin + d * b.distance, d * -1.0f); // CHARACTER_HIT_FX
            if (debugLog_)
                std::cerr << "[gun] " << h->name() << " shot in the " << hitZoneName(b.zone) << " at " << b.distance << " m for "
                          << damage << ", health " << h->health() << "\n";
        }
        // A line stopped by a prop's own surface damages it (ours: the prop nearest the end of the
        // line, within half a metre of its middle).
        if (wall && (hits.size() < static_cast<std::size_t>(gun->bodies)))
        {
            DamageProp* hitProp = nullptr;
            float32 best = 0.5f;
            const Math::Vector3 stop = origin + d * reach;
            for (DamageProp& prop : damageProps_)
            {
                const float32 gap = (prop.origin + Math::Vector3{0.0f, 0.4f, 0.0f} - stop).length();
                if (!prop.invulnerable && gap < best + 0.4f)
                {
                    best = gap - 0.4f;
                    hitProp = &prop;
                }
            }
            if (hitProp)
                damageProp(*hitProp, gun->damage);
        }
        // A scripted thing the line ends on is shot (OnBeingShot, event 19: the exploder drums,
        // the gas can). Ours: the entity whose middle (0.4 m up) lies within 0.7 m of the line's
        // stop.
        if (hits.size() < static_cast<std::size_t>(gun->bodies))
        {
            const Entity* shotAt = nullptr;
            float32 nearest = reach + 0.5f;
            for (std::size_t i = 0; i < entities_.size(); ++i)
            {
                const Entity& en = entities_[i];
                if (en.hunter || en.kind != Kind::Placement || exploded_.contains(en.name) || static_cast<uint32>(i + 1) == gasCarried_ ||
                    hiddenProps_.contains(en.placement))
                    continue;
                // The line's nearest point to its middle (0.4 m up), up to the stop and half a
                // metre on (its surface stops the line before its middle).
                const Math::Vector3 mid = en.position + Math::Vector3{0.0f, 0.4f, 0.0f};
                const float32 along = std::clamp((mid - origin).dot(d), 0.0f, reach + 0.5f);
                if ((origin + d * along - mid).length() < 0.6f && along < nearest && scripts_.has(en.name, "OnBeingShot"))
                {
                    nearest = along;
                    shotAt = &en;
                }
            }
            if (shotAt)
            {
                const std::string name = shotAt->name;
                exploded_.insert(name);
                scripts_.start(name, "OnBeingShot");
                std::cerr << "[gun] " << name << " is shot\n";
            }
        }
        // Where the line ends: on the last body it stopped at, else the world or its length.
        const bool stopped = hits.size() >= static_cast<std::size_t>(gun->bodies) && !hits.empty();
        event.lines.push_back({origin, origin + d * (stopped ? hits.back().second.distance : reach), stopped});
        // A line that ends on the world rings off it (one sound for the volley).
        if (wall && hits.size() < static_cast<std::size_t>(gun->bodies))
        {
            // The surface's hit effect where it strikes, and the smoke.
            shotStruck(*gun, origin + d * reach, d, *struck, pellet == 0);
        }
        if (wall && hits.size() < static_cast<std::size_t>(gun->bodies) && pellet == 0)
        {
            // By the struck surface's type (0x446750).
            const MaterialTypes::Ricochet r = MaterialTypes::ricochet(struck->material);
            SoundEvent s;
            s.sample = gun->clipStem == "NailGun" ? "SFX_WEAPON_NAIL_TING" : r.sample;
            s.position = origin + d * reach;
            s.priority = 0.5f;
            s.volume = gun->clipStem == "NailGun" ? 1.0f : r.volume;
            if (sounds_.size() < 64)
                sounds_.push_back(s);
        }
    }
    gunEvents_.push_back(std::move(event));
    return true;
}

bool LevelRuntime::playerReloadsOne()
{
    const Firearm* gun = playerGun();
    if (!gun || !ammo_.reloadOne(*gun, playerGunRecord()))
        return false;
    emitSound("SFX_WEAPON_RELOAD_" + gun->reloadSound, player_.feet + Math::Vector3{0.0f, 1.2f, 0.0f}, 0.7f);
    return true;
}

bool LevelRuntime::playerReloads()
{
    const Firearm* gun = playerGun();
    if (!gun || (gun->reloadsRoundByRound() ? !ammo_.canReload(*gun, playerGunRecord()) : ammo_.reload(*gun, playerGunRecord()) == 0))
        return false;
    emitSound("SFX_WEAPON_RELOAD_" + gun->reloadSound, player_.feet + Math::Vector3{0.0f, 1.2f, 0.0f}, 0.7f);
    gunEvents_.push_back({nullptr, equipped_, true, {}});
    return true;
}

Hunter* LevelRuntime::executionTarget(const Math::Vector3& feet, float32 yaw)
{
    constexpr float32 kReach = 1.6f;
    const float32 cosBehind = std::cos(Math::radians(70.0f));
    const Math::Vector3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    Hunter* best = nullptr;
    float32 bestDist = kReach;
    for (const auto& h : hunters_)
    {
        if (h->state() != HunterState::Normal || !h->model().isLoaded() || !h->lockonable() || !h->executable())
            continue; // (SetPedLockonable / SetHunterExecutable FALSE)
        const HunterSetup* setup = director_.hunter(h->name());
        if (setup && setup->invisible)
            continue;
        if (h->sight() >= kMedium || h->action() == AiAction::Chase || h->action() == AiAction::Combat)
            continue;
        const Math::Vector3 to{h->feet().x - feet.x, 0.0f, h->feet().z - feet.z};
        const float32 dist = to.length();
        if (dist > bestDist || dist < 0.05f || std::fabs(h->feet().y - feet.y) > 1.0f)
            continue;
        const Math::Vector3 dir = to * (1.0f / dist);
        const Math::Vector3 itsFwd{-std::sin(h->yaw()), 0.0f, -std::cos(h->yaw())};
        if (itsFwd.dot(dir) < cosBehind || fwd.dot(dir) < cosBehind)
            continue; // not behind it, or not facing it
        best = h.get();
        bestDist = dist;
    }
    return best;
}

void LevelRuntime::beginExecution(Hunter& h, const Math::Vector3& feet, float32 yaw, const std::string& clip)
{
    HunterWorld w = world(player_);
    h.beginExecution(feet, yaw, clip, w);
}

void LevelRuntime::updateBackupWaits(HunterWorld& w)
{
    for (std::size_t i = 0; i < backupWaits_.size();)
    {
        BackupWait& b = backupWaits_[i];
        bool arrived = true;
        for (const Hunter* h : b.backup)
            arrived = arrived && (!h->alive() || !h->answeringBackup() || (h->feet() - b.finderAt).length() < 3.0f);
        if (!arrived && time_ < b.until)
        {
            ++i;
            continue;
        }
        // Check Body (mode 5, 0x50d930) for the squad still standing by.
        if (debugLog_)
            std::cerr << "[body] backup " << (arrived ? "is there" : "timed out") << ": the squad checks the body" << std::endl;
        for (Hunter* h : b.squad)
            if (h->alive() && h->waitingAtBody())
                h->checkBody(b.at, false, w);
        // Each slot's hunter goes to the body too (state 4).
        for (Hunter* h : b.backup)
            if (h->alive() && h->answeringBackup())
                h->checkBody(b.at, false, w);
        backupWaits_.erase(backupWaits_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

int32 LevelRuntime::boundaryNode(const Hunter& h)
{
    // 0x5017a0 / 0x4fd6b0: a squad kept to its territory meets him outside it. Its territory is the
    // nav nodes of its areas (AIAddAreaForSubpack); none in the graph: no rule (ours).
    const Subpack* sp = director_.subpackOf(h.name());
    if (!sp || !sp->stayInTerritory || sp->areas.empty() || nav_.empty())
        return -1;
    Territory& t = territories_[sp->name];
    if (t.areas != sp->areas.size() || t.in.size() != nav_.size())
    {
        t = Territory{};
        t.areas = sp->areas.size();
        t.in.assign(nav_.size(), 0);
        std::vector<int32> areas;
        for (const std::string& a : sp->areas)
            if (const int32 i = nav_.areaIndex(a); i >= 0)
                areas.push_back(i);
        for (std::size_t n = 0; n < nav_.size(); ++n)
            if (std::find(areas.begin(), areas.end(), nav_.node(static_cast<int32>(n)).area) != areas.end())
            {
                t.in[n] = 1;
                t.any = true;
            }
    }
    if (!t.any)
        return -1;
    const int32 playerNode = nav_.nearest(player_.feet);
    if (playerNode >= 0 && t.in[static_cast<std::size_t>(playerNode)])
        return -1; // he is inside: the fight as usual
    BoundaryAnswer& a = boundary_[&h];
    if (a.playerNode == playerNode && time_ - a.at < 1.0f)
        return a.node;
    a.at = time_;
    a.playerNode = playerNode;
    // The territory's nodes nearest him, the first it has a route to (ours: the eight nearest tried).
    std::vector<std::pair<float32, int32>> near;
    for (std::size_t n = 0; n < nav_.size(); ++n)
        if (t.in[n])
            near.emplace_back((nav_.node(static_cast<int32>(n)).position - player_.feet).length(), static_cast<int32>(n));
    std::sort(near.begin(), near.end());
    const int32 from = nav_.nearest(h.feet());
    a.node = -2;
    for (std::size_t i = 0; i < near.size() && i < 8 && from >= 0; ++i)
        if (near[i].second == from || !nav_.route(from, near[i].second).empty())
        {
            a.node = near[i].second;
            break;
        }
    return a.node;
}

void LevelRuntime::scanBodies(HunterWorld& w)
{
    // The squad coordinator (0x50cb80, each tick of a hunter side's coordinator, 0x50ab80):
    //   the bodies are the dead hunters nobody found yet (0x51d450; found: AI entity +0xcc)
    //   it looks through one member in 11 a tick: member (tick % 11), +11, ... (+0x264)
    //   a member looks unless it ignores bodies (AIEntityIgnoreDeadBodies, +0x108) or is dead or
    //   senseless (0x40ddf0); it sees a body with its own sight at 18 m (0x5192c0)
    //   of the bodies it sees it takes the best sighting, then the nearest
    //   then, unless its squad is fighting him (mode KillEnemy, ours: the member chases or fights
    //   him): OnDeadBodyFound on the member (event 46, 0x481070), the body is found, and the squad
    //   is sent: Wait for Backup (mode 7, 0x50daf0; the finder's DEAD_BODY_SEEN), then Check Body
    //   (mode 5, 0x50d930: every member to within 1 m of it) and a search there.
    // Ours: the squad goes at once (the backup wait and its radio exchange, 0x53e770, are not done);
    // a hunter with no subpack goes alone; the coordinator's other find (heads within 10 m, Check
    // Head, mode 6) is not done.
    std::vector<Hunter*> bodies;
    for (const auto& h : hunters_)
        if (!h->alive() && !h->gone() && !h->bodyFound())
            if (const HunterSetup* hs = director_.hunter(h->name()); !hs || !hs->ignoreIfDead) // AIIgnoreEntityIfDead
                bodies.push_back(h.get());
    bool heads = false;
    for (const Pickup& k : pickups_)
        heads = heads || (!k.taken && !k.noticed && k.type && k.type->ct == 101);
    if (bodies.empty() && !heads)
        return;
    const uint32 tick = bodyScan_++;
    for (std::size_t i = tick % 11u; i < hunters_.size(); i += 11u)
    {
        Hunter& finder = *hunters_[i];
        const HunterSetup* setup = director_.hunter(finder.name());
        if (!finder.alive() || !setup || !setup->added || setup->blind || setup->invisible || setup->ignoreBodies ||
            finder.action() >= AiAction::Chase || finder.state() != HunterState::Normal)
            continue;
        Hunter* seen = nullptr;
        SenseLevel best = kNoLevel;
        float32 bestDist = 0.0f;
        for (Hunter* b : bodies)
        {
            // A body on his shoulder is a body all the same (0x51d450 takes every dead one); it
            // is looked for up there (ours: 1.2 m over his feet).
            const Math::Vector3 at = b->feet() + Math::Vector3{0.0f, b->carried() ? 1.2f : 0.0f, 0.0f};
            const auto light = setup_.field->lightBelow(at + Math::Vector3{0.0f, 0.5f, 0.0f});
            const SenseLevel level = finder.seeBody(at, light ? (light->x + light->y + light->z) / 3.0f : 0.1f, w);
            const float32 d = (b->feet() - finder.feet()).length();
            if (level != kNoLevel && (seen == nullptr || level > best || (level == best && d < bestDist)))
            {
                seen = b;
                best = level;
                bestDist = d;
            }
        }
        if (!seen)
        {
            // No body: a severed head (CT_HEAD, the list 0x53deb0) within 10 m, within 75 degrees of
            // where it faces (0x40c2c0), clear from 1.5 m up to 1 m above it (0x4067a0): the nearest
            // sends the squad to it (Check Head, mode 6, 0x50da30: within 1.5 m).
            Pickup* head = nullptr;
            float32 best = 10.0f;
            const Math::Vector3 fwd{-std::sin(finder.yaw()), 0.0f, -std::cos(finder.yaw())};
            for (Pickup& k : pickups_)
            {
                if (k.taken || k.noticed || !k.type || k.type->ct != 101)
                    continue;
                const Math::Vector3 to = k.position - finder.feet();
                const float32 d = to.length();
                if (d >= best || d < 1e-3f)
                    continue;
                if (Math::degrees(std::acos(std::clamp(fwd.dot(to * (1.0f / d)), -1.0f, 1.0f))) > 75.0f)
                    continue;
                if (!setup_.field->lineOfSight(finder.feet() + Math::Vector3{0.0f, 1.5f, 0.0f},
                                               k.position + Math::Vector3{0.0f, 1.0f, 0.0f}))
                    continue;
                head = &k;
                best = d;
            }
            if (!head)
                continue;
            head->noticed = true;
            if (debugLog_)
                std::cerr << "[body] " << finder.name() << " sees a head (" << best << " m)" << std::endl;
            const Subpack* sp = director_.subpackOf(finder.name());
            for (const auto& h : hunters_)
                if (h.get() == &finder || (sp && h->alive() && director_.subpackOf(h->name()) == sp))
                    h->checkBody(head->position, false, w, Hunter::Errand::Head);
            return;
        }
        seen->setBodyFound();
        if (debugLog_)
            std::cerr << "[body] " << finder.name() << " finds " << seen->name() << " (" << bestDist << " m, sight " << best
                      << ")" << std::endl;
        if (w.scriptEvent)
            w.scriptEvent(finder.name(), "OnDeadBodyFound");
        // Wait for Backup (mode 7, 0x50daf0 / 0x50b8c0): the squad stands by while two backup
        // requests (0x53e770) are answered - each slot done when its hunter is there or 6 s after
        // it was posted (0x4bae70(6)) - then Check Body, the backup with them.
        // The backup manager (0x799b50 + 0x850: 8 slots of 0x34 bytes): a request is the finder,
        // its kind (1 unless coordinator +0x280: walk), its node and place, a 4 s timer; two
        // slots each. Every leader offers its members each frame (0x52d7f0): one not answering
        // already (+0x220), free (+0xd4 = 0, 0x52e070) and with no request of its own (+0x21c)
        // volunteers (0x53e980) - for each open slot the nearest within 50 m of the place
        // (0x799958, 2500 squared) is noted, and the one noted last frame is taken (state 1). It
        // walks to within 3 m of the finder (0x52b310, 0x7980ec) and says line 0x38 (slot 47: most voices have
        // none). Ours takes the nearest at once; "free" is: not after him and not
        // on another errand.
        BackupWait b;
        b.at = seen->feet();
        b.finderAt = finder.feet();
        b.until = time_ + 6.0f;
        b.finder = &finder;
        const Subpack* sp = director_.subpackOf(finder.name());
        for (const auto& h : hunters_)
            if (h.get() == &finder || (sp && h->alive() && director_.subpackOf(h->name()) == sp))
            {
                h->checkBody(b.at, h.get() == &finder, w, Hunter::Errand::Wait);
                b.squad.push_back(h.get());
            }
        std::vector<std::pair<float32, Hunter*>> near;
        for (const auto& h : hunters_)
        {
            const HunterSetup* hs = director_.hunter(h->name());
            if (!h->alive() || std::find(b.squad.begin(), b.squad.end(), h.get()) != b.squad.end() || !hs || !hs->added ||
                hs->hostage || h->action() >= AiAction::Investigate || h->answeringBackup() || h->waitingAtBody())
                continue;
            if (const float32 d = (h->feet() - b.finderAt).length(); d < 50.0f)
                near.emplace_back(d, h.get());
        }
        std::sort(near.begin(), near.end());
        for (std::size_t i = 0; i < near.size() && i < 2; ++i)
        {
            near[i].second->checkBody(b.finderAt, false, w, Hunter::Errand::Backup);
            b.backup.push_back(near[i].second);
            if (debugLog_)
                std::cerr << "[body] " << near[i].second->name() << " comes as backup (" << near[i].first << " m)" << std::endl;
        }
        backupWaits_.push_back(std::move(b));
        return; // one find a tick
    }
}

uint32 LevelRuntime::gasCanInReach(const Math::Vector3& feet, float32 reach) const
{
    uint32 best = 0;
    float32 bestDist = reach;
    for (std::size_t i = 0; i < entities_.size(); ++i)
    {
        const Entity& en = entities_[i];
        const Assets::InstPlacement* p = en.placement >= 0 ? placementAt(en.placement) : nullptr;
        if (!p || en.hunter || static_cast<uint32>(i + 1) == gasCarried_ || hiddenProps_.contains(en.placement) ||
            exploded_.contains(en.name) || !gasCanRecords_.contains(lower(p->record)) || std::fabs(en.position.y - feet.y) > 1.0f)
            continue;
        if (const float32 d = flatDistance(en.position, feet); d <= bestDist)
        {
            bestDist = d;
            best = static_cast<uint32>(i + 1);
        }
    }
    return best;
}

void LevelRuntime::takeGasCan(uint32 handle)
{
    Entity* en = entity(handle);
    if (!en || en->placement < 0)
        return;
    gasCarried_ = handle;
    hiddenProps_.insert(en->placement);
    setup_.field->removeProp(en->placement);
    std::cerr << "[runtime] " << en->name << " taken up\n";
}

void LevelRuntime::putGasCan(uint32 handle, const Math::Vector3& at, float32 yaw)
{
    Entity* en = entity(handle);
    if (gasCarried_ == handle)
        gasCarried_ = 0;
    if (!en || en->placement < 0)
        return;
    if (const Assets::InstPlacement* p = placementAt(en->placement))
    {
        const Math::Vector3 origin = setup_.level->frame().placementOrigin(p->position);
        const Math::Matrix4x4 t = Math::Matrix4x4::translation(at) * Math::Matrix4x4::rotationY(yaw - LevelFrame::placementYaw(p->rotation)) *
                                  Math::Matrix4x4::translation(origin * -1.0f);
        for (Renderer::Mesh* mesh : setup_.level->meshesOf(*p))
            mesh->setTransform(t);
    }
    en->position = at;
    hiddenProps_.erase(en->placement);
    std::cerr << "[runtime] " << en->name << " put down at (" << at.x << ", " << at.y << ", " << at.z << ")\n";
}

Hunter* LevelRuntime::buddyToOrder(const Math::Vector3& feet, float32 yaw)
{
    const Math::Vector3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    const Math::Vector3 eye = feet + Math::Vector3{0.0f, 1.7f, 0.0f};
    for (const auto& h : hunters_)
    {
        if (!h->alive() || !h->playerBuddy())
            continue;
        Math::Vector3 to = h->feet() - feet;
        to.y = 0.0f;
        const float32 d = to.length();
        if (d >= 10.0f || (d > 1e-3f && ahead.dot(to * (1.0f / d)) <= 0.55f))
            continue;
        if (setup_.field->lineOfSight(eye, h->feet() + Math::Vector3{0.0f, 1.7f, 0.0f}))
            return h.get();
    }
    return nullptr;
}

void LevelRuntime::orderBuddy(Hunter& buddy)
{
    const bool follow = !buddy.buddyFollowing();
    director_.setBuddyFollow(buddy.name(), follow);
    std::cerr << "[runtime] he orders " << buddy.name() << (follow ? " to follow" : " to wait") << std::endl;
}

Hunter* LevelRuntime::corpseInReach(const Math::Vector3& feet, float32 reach)
{
    Hunter* best = nullptr;
    float32 bestDist = reach;
    for (const auto& h : hunters_)
    {
        if (h->alive() || h->gone() || h->carried() || !h->model().isLoaded() || std::fabs(h->feet().y - feet.y) > 1.0f)
            continue;
        const float32 d = flatDistance(h->feet(), feet);
        if (d <= bestDist)
        {
            bestDist = d;
            best = h.get();
        }
    }
    return best;
}

Hunter* LevelRuntime::nearestHunter(const Math::Vector3& feet)
{
    Hunter* best = nullptr;
    float32 bestDist = 0.0f;
    for (const auto& h : hunters_)
    {
        if (!h->alive() || h->down() || h->beingExecuted() || !h->model().isLoaded())
            continue;
        const HunterSetup* setup = director_.hunter(h->name());
        if (setup && setup->invisible)
            continue;
        const float32 d = (h->feet() - feet).length();
        if (!best || d < bestDist)
        {
            best = h.get();
            bestDist = d;
        }
    }
    return best;
}

float32 LevelRuntime::drawBetween(float32 lo, float32 hi)
{
    return lo + (hi - lo) * static_cast<float32>(std::uniform_int_distribution<int32>(0, 0x7ff)(rng_)) / 2047.0f;
}

void LevelRuntime::addMusicSource(const Math::Vector3& at, int32 fx)
{
    if (musicSources_.size() < 40)
        musicSources_.push_back({at, fx});
}

MusicAmbience& LevelRuntime::musicAmbienceState()
{
    if (ambience_.level() == 0)
        ambience_.reset(LevelMusic::levelNumber(setup_.levelName),
                        toScene({MusicAmbience::kChair[0], MusicAmbience::kChair[1], MusicAmbience::kChair[2]}));
    return ambience_;
}

MusicAmbience::Frame LevelRuntime::musicAmbience(float32 dt)
{
    MusicAmbience::Inputs in;
    ++musicFrame_;
    musicAmbienceState();
    in.frame = musicFrame_;
    in.now = time_;
    in.player = player_.feet;
    // The fires (0x5a3690 asks whether each still burns).
    for (const MusicSource& s : musicSources_)
        in.fires.push_back({s.position, !s.fx || fx_.exists(s.fx)});
    // Heard: the line from it to the camera through the world's surfaces (0x4df3b0; ours: from
    // a quarter metre above it, off the floor it stands on).
    in.heard = [this](const Math::Vector3& at) {
        return setup_.field ? setup_.field->soundThrough(at + Math::Vector3{0.0f, 0.25f, 0.0f}, viewEye_) : 1.0f;
    };
    in.entityAt = [this](uint32 handle, Math::Vector3& at) {
        const Entity* en = entity(handle);
        if (!en)
            return false;
        at = en->kind == Kind::Player ? player_.feet : (en->hunter ? en->hunter->feet() : en->position);
        return true;
    };
    // The rain (0x5afa10): RAINY or THUNDERSTORM, less the shelter - eased up at 0.75 a second
    // and down at 0.5 - when something is above both him (from 2 m up) and the camera.
    const bool raining = Assets::iequals(levelSetup_.weather, "RAINY") || Assets::iequals(levelSetup_.weather, "THUNDERSTORM");
    if (setup_.field)
    {
        const Math::Vector3 up{0.0f, 100.0f, 0.0f};
        if (musicFrame_ % 8 == 0)
        {
            const Math::Vector3 from = player_.feet + Math::Vector3{0.0f, 2.0f, 0.0f};
            shelteredPlayer_ = !setup_.field->lineOfSight(from, from + up);
        }
        if (musicFrame_ % 8 == 3)
            shelteredCamera_ = !setup_.field->lineOfSight(viewEye_, viewEye_ + up);
    }
    const float32 sheltered = shelteredPlayer_ && shelteredCamera_ ? 1.0f : 0.0f;
    shelter_ = sheltered > shelter_ ? std::min(sheltered, shelter_ + 0.75f * dt) : std::max(sheltered, shelter_ - 0.5f * dt);
    in.rain = raining ? 1.0f - shelter_ : 0.0f;
    return ambience_.update(in, dt);
}

float32 LevelRuntime::wind() const
{
    // CLOUDY, WINDY, RAINY, THUNDERSTORM, FOGGY, CLEAR (0x7b3220).
    static constexpr std::array<std::pair<const char*, float32>, 6> kWind{{
        {"CLOUDY", 0.2f}, {"WINDY", 1.0f}, {"RAINY", 0.3f}, {"THUNDERSTORM", 1.0f}, {"FOGGY", 0.0f}, {"CLEAR", 0.1f}}};
    for (const auto& [name, wind] : kWind)
        if (Assets::iequals(levelSetup_.weather, name))
            return wind * (1.0f - shelter_);
    return 0.0f;
}

LevelRuntime::MusicDanger LevelRuntime::musicDanger() const
{
    MusicDanger d;
    for (const auto& h : hunters_)
    {
        if (!h->alive() || h->action() == AiAction::NotOnAlert)
            continue;
        if (h->action() == AiAction::Chase || h->action() == AiAction::Combat)
            d.spotted = true;
        else
            d.hunted = true;
        d.nearest = std::min(d.nearest, (h->feet() - player_.feet).length());
    }
    return d;
}

float32 LevelRuntime::soundRange(const char* sound) const
{
    if (!sound)
        return 0.0f;
    const SoundType t = tuning_.sound(sound);
    if (t.volume < 0 || t.volume >= static_cast<SenseLevel>(tuning_.hearing.volumeDistance.size()))
        return 0.0f;
    return tuning_.hearing.volumeDistance[static_cast<std::size_t>(t.volume)];
}

void LevelRuntime::updateRadar(float32 dt)
{
    // The exe's AI ticks at 30 a second: each tick a blip fades by 2 (0x4ea1b0), and a hunter
    // after him is noticed again (0x4ed310: back to 180 from nothing, else at least 100) - at
    // once while it heard or saw him, otherwise when the player can see it (a line-of-sight check
    // every 7..11 ticks, 0x40f0a0; ours from his eyes).
    radarTick_ += dt;
    const bool tick = radarTick_ >= 1.0f / 30.0f;
    if (tick)
        radarTick_ = 0.0f;
    std::uniform_int_distribution<int32> lookGap(7, 11);
    for (const auto& h : hunters_)
    {
        RadarTrack& t = radarTracks_[h.get()];
        if (!h->alive())
        {
            t.state = 3;
            continue;
        }
        // 0x51a510: after him (a target) by what it knows - lost him, heard him, seen him (ours
        // from its action: hunting, investigating, chasing or fighting).
        switch (h->action())
        {
        case AiAction::NotOnAlert: t.state = 3; break;
        case AiAction::Hunt: t.state = 0; break;
        case AiAction::Investigate: t.state = 1; break;
        default: t.state = 2; break;
        }
        if (!tick)
            continue;
        t.visible = std::max(0.0f, t.visible - 2.0f);
        auto notice = [&t](float32 to) { t.visible = t.visible <= 0.0f ? 180.0f : std::max(t.visible, to); };
        if (t.state == 1 || t.state == 2)
            notice(100.0f);
        else if (t.state == 0 && --t.lookIn <= 0)
        {
            t.lookIn = lookGap(rng_);
            const Math::Vector3 eye = player_.feet + Math::Vector3{0.0f, 1.6f, 0.0f};
            const Math::Vector3 chest = h->feet() + Math::Vector3{0.0f, 1.2f, 0.0f};
            if ((chest - eye).length() < Radar::kRange && !setup_.field->raycast(eye, chest))
                notice(100.0f);
        }
    }
}

std::vector<RadarBlip> LevelRuntime::radarBlips()
{
    std::vector<RadarBlip> out;
    for (const auto& h : hunters_)
    {
        const HunterSetup* setup = director_.hunter(h->name());
        if ((setup && setup->invisible) || !h->model().isLoaded())
            continue;
        const RadarTrack& t = radarTracks_[h.get()];
        if (!t.onRadar)
            continue;
        RadarBlip b;
        b.position = h->feet();
        b.yaw = h->yaw();
        if (!h->alive())
            b.kind = RadarBlip::Kind::DeadHunter; // the dead show black (0x5e1670)
        else if (t.state == 3)
            continue;
        b.state = t.state;
        b.visibility = std::min(100.0f, t.visible) / 100.0f;
        out.push_back(b);
    }
    for (const auto& [handle, colour] : radarScript_)
    {
        const Entity* en = handle ? entity(handle) : nullptr;
        if (!en)
            continue;
        RadarBlip b;
        b.kind = RadarBlip::Kind::Script;
        b.position = en->kind == Kind::Player ? player_.feet : (en->hunter ? en->hunter->feet() : en->position);
        b.colour = colour;
        out.push_back(b);
    }
    return out;
}

void LevelRuntime::finishExecution(Hunter& h, const std::string& dieClip)
{
    HunterWorld w = world(player_);
    h.finishExecution(dieClip, w);
}

bool LevelRuntime::playerKicks(const Math::Vector3& feet, float32 yaw, const KickWeapon& kick)
{
    const Math::Vector3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    HunterWorld w = world(player_);
    bool reached = false;
    for (const auto& h : hunters_)
    {
        if (!h->alive() || h->beingExecuted() || h->down() || h->state() == HunterState::Held)
            continue;
        const Math::Vector3 to{h->feet().x - feet.x, 0.0f, h->feet().z - feet.z};
        const float32 dist = to.length();
        if (dist > kick.shot.size + h->radius() + 0.3f || std::fabs(h->feet().y - feet.y) > 1.2f)
            continue;
        if (dist > 0.3f && fwd.dot(to * (1.0f / dist)) < 0.4f)
            continue;
        h->hit(kick.shot.damage, feet, w, h->state() == HunterState::Stunned ? HitKind::Stomp : HitKind::Kick);
        reached = true;
    }
    return reached;
}

Hunter* LevelRuntime::grabTarget(const Math::Vector3& feet, float32 yaw, float32 reach)
{
    const Math::Vector3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    Hunter* best = nullptr;
    float32 bestDist = reach;
    for (const auto& h : hunters_)
    {
        if (!h->alive() || (h->state() != HunterState::Normal && h->state() != HunterState::Stunned && h->state() != HunterState::Hurt))
            continue;
        const Math::Vector3 to{h->feet().x - feet.x, 0.0f, h->feet().z - feet.z};
        const float32 dist = to.length();
        if (dist > bestDist || std::fabs(h->feet().y - feet.y) > 1.0f || (dist > 0.3f && fwd.dot(to * (1.0f / dist)) < 0.5f))
            continue;
        best = h.get();
        bestDist = dist;
    }
    return best;
}

Hunter* LevelRuntime::downTarget(const Math::Vector3& feet, float32 yaw, float32 reach)
{
    const Math::Vector3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    Hunter* best = nullptr;
    float32 bestDist = reach;
    for (const auto& h : hunters_)
    {
        if (!h->alive() || !h->down())
            continue;
        const Math::Vector3 to{h->feet().x - feet.x, 0.0f, h->feet().z - feet.z};
        const float32 dist = to.length();
        if (dist > bestDist || std::fabs(h->feet().y - feet.y) > 1.0f || (dist > 0.3f && fwd.dot(to * (1.0f / dist)) < 0.3f))
            continue;
        best = h.get();
        bestDist = dist;
    }
    return best;
}

std::optional<LevelRuntime::Knock> LevelRuntime::takeKnockdown()
{
    return std::exchange(knock_, std::nullopt);
}

std::optional<Math::Vector3> LevelRuntime::takeShot()
{
    return std::exchange(shot_, std::nullopt);
}

Hunter* LevelRuntime::takeGrab()
{
    return std::exchange(grab_, nullptr);
}

int32 LevelRuntime::takeBlockedHit()
{
    return std::exchange(blockedHit_, 0);
}

void LevelRuntime::hunterSteps(const Hunter& h)
{
    // As his (GameSession::updateFootsteps / playStep): a foot lands a quarter and three quarters
    // of the way through its gait clip - Sneak_Walk_Fwd / Sneak_Run_Fwd event 0xf (a hunter's slow
    // sneak is heard too), Run_Fwd / Sprint_Fwd 0x11, any other walk 0x10 - and 0x44ea20 plays a
    // sample of its floor's material type at the event's level over its range (12 / 10 m, 25 / 20
    // m, 60 / 20 m). The Journalist's are at half level and 1.3 times the rate, the Tramp's and
    // Piggsy's at half level and 0.8 times (their record names).
    StepTrack& t = stepTracks_[&h];
    const Character& model = h.model();
    const std::string& clip = model.currentClip();
    const bool gait = h.alive() && (clip.find("Walk") != std::string::npos || clip.find("Run") != std::string::npos ||
                                    clip.find("Sprint") != std::string::npos);
    const float32 length = gait ? model.clipDuration(clip) : 0.0f;
    if (length <= 0.0f)
    {
        t.phase = -1.0f;
        return;
    }
    const float32 phase = std::fmod(std::max(0.0f, model.clipTime()) / length, 1.0f);
    const float32 last = clip == t.clip ? t.phase : -1.0f;
    t.phase = phase;
    t.clip = clip;
    if (last < 0.0f)
        return;
    auto crossed = [&](float32 foot) { return last <= phase ? (last < foot && foot <= phase) : (foot > last || foot <= phase); };
    if (!crossed(0.25f) && !crossed(0.75f))
        return;
    const uint16 event = clip.rfind("Sneak_", 0) == 0 ? 0xf : clip == "Run_Fwd" || clip == "Sprint_Fwd" ? 0x11 : 0x10;
    int32 level = event == 0xf ? 12 : event == 0x11 ? 60 : 25;
    const float32 range = event == 0xf ? 10.0f : 20.0f;
    const Math::Vector3 feet = h.feet();
    if (const auto hit = setup_.field->shotHit(feet + Math::Vector3{0.0f, 0.5f, 0.0f}, feet - Math::Vector3{0.0f, 1.0f, 0.0f}))
        t.material = hit->material;
    const MaterialTypes::Steps steps = MaterialTypes::footsteps(t.material);
    if (steps.first < 0 || (feet - viewEye_).length() >= range)
        return;
    float32 rate = 1.0f;
    const std::string record = hunterRecord(h);
    if (Assets::iequals(record, "Journalist"))
        rate = 1.3f;
    else if (Assets::iequals(record, "Tramp") || Assets::iequals(record, "Piggsy"))
        rate = 0.8f;
    if (rate != 1.0f)
        level = (level + 1) / 2;
    SoundEvent ev;
    ev.sample = "#" + std::to_string(steps.first + static_cast<int32>(rng_() % static_cast<uint32>(steps.last - steps.first + 1)));
    ev.position = feet + Math::Vector3{0.0f, 0.25f, 0.0f}; // (off the floor it stands on)
    ev.priority = 0.3f;
    ev.range = range;
    ev.volume = static_cast<float32>(level) / 127.0f;
    ev.exeFalloff = true;
    ev.rate = rate * (1.0f + static_cast<float32>(static_cast<int32>(rng_() % 33u) - 16) / 256.0f); // (0x453080)
    if (sounds_.size() < 64)
        sounds_.push_back(ev);
}

void LevelRuntime::playerCries(const Math::Vector3& head)
{
    // The player's entity audio (0x44e6d0): hurt, event 0x14 plays SFX_GENERIC_PLAYER_PAIN_LOW
    // (its _RANDOM entry, 0x36e2) at most once in 20 frames (+0xd55c); dead, 0x4b one of
    // SFX_GENERIC_PLAYER_DEATH_1..17 (0x3690 + rand % 17).
    if (playerHealth_ <= 0.0f)
        emitSound("SFX_GENERIC_PLAYER_DEATH", head, 0.9f);
    else if (time_ - painAt_ >= 20.0f / 30.0f)
    {
        painAt_ = time_;
        emitSound("SFX_GENERIC_PLAYER_PAIN_LOW", head, 0.9f);
    }
}

void LevelRuntime::hurtPlayer(float32 damage, const Math::Vector3& from)
{
    if (playerHealth_ <= 0.0f)
        return;
    playerHealth_ = std::max(0.0f, playerHealth_ - (playerInvulnerable_ ? 0.0f : damage));
    const Math::Vector3 head = player_.feet + Math::Vector3{0.0f, 1.6f, 0.0f};
    emitSound("SFX_WEAPON_MELEE_COLLISION_FIST", head, 0.8f);
    playerCries(head);
    (void)from;
}

std::optional<Math::Vector3> LevelRuntime::takeHit()
{
    auto h = hit_;
    hit_.reset();
    return h;
}

std::optional<Math::Vector3> LevelRuntime::takeTeleport()
{
    auto t = teleport_;
    teleport_.reset();
    return t;
}

std::optional<float32> LevelRuntime::takeHeading()
{
    auto h = heading_;
    heading_.reset();
    return h;
}

// ---------------------------------------------------------------- checkpoints

std::vector<SaveEntityRecord> LevelRuntime::checkpointRecords(const Math::Vector3& playerFeet, float32 playerYaw,
                                                              float32 playerHealth) const
{
    // 0x43a2c0: every .inst placement in file order; one whose entity is gone is "NUL".
    // The scripts' spawns follow in the order spawned.
    const LevelContent& level = *setup_.level;
    std::vector<SaveEntityRecord> out(static_cast<std::size_t>(level.instCount()) + spawned_.size(), Checkpoint::nulRecord());
    constexpr float32 kHalfTurn = 3.14159265358979f;
    std::vector<const Assets::InstPlacement*> all;
    for (const Assets::InstPlacement& p : level.placements())
        all.push_back(&p);
    for (const Assets::InstPlacement& p : spawned_)
        all.push_back(&p);
    for (const Assets::InstPlacement* pp : all)
    {
        const Assets::InstPlacement& p = *pp;
        if (p.index < 0 || static_cast<std::size_t>(p.index) >= out.size() || p.record == "NUL")
            continue; // (a spawn already gone when its checkpoint was loaded stays "NUL")
        SaveEntityRecord r = Checkpoint::record(p);
        bool gone = false;
        auto place = [&](const Math::Vector3& feet, float32 yaw) {
            const Math::Vector3 at = Checkpoint::instPosition(level.frame(), feet);
            r.pos[0] = at.x;
            r.pos[1] = at.y;
            r.pos[2] = at.z;
            r.plane = Checkpoint::saveRotation(Checkpoint::instYawRotation(yaw - kHalfTurn)); // LevelFrame::characterYaw
        };
        if (p.entityClass == "Player_Inst")
        {
            place(playerFeet, playerYaw);
            Checkpoint::setWord(r, 0, static_cast<std::uint32_t>(std::max(0.0f, playerHealth)));
        }
        else if (p.entityClass == "Hunter_Inst")
        {
            const auto it = std::find_if(hunters_.begin(), hunters_.end(),
                                         [&](const auto& h) { return Assets::iequals(h->name(), p.internalName); });
            if (it == hunters_.end() || !(*it)->alive())
                gone = true;
            else
            {
                place((*it)->feet(), (*it)->yaw());
                Checkpoint::setWord(r, 0, static_cast<std::uint32_t>(std::max(0.0f, (*it)->health())));
            }
        }
        else if (const auto prop = std::find_if(damageProps_.begin(), damageProps_.end(),
                                                [&](const DamageProp& d) { return d.placement == p.index; });
                 prop != damageProps_.end())
            Checkpoint::setWord(r, 0, static_cast<std::uint32_t>(prop->percent())); // its health, 100 = whole
        else if (p.entityClass == "Door_Inst")
        {
            if (const Doors::Door* d = const_cast<Doors&>(doors_).find(lower(p.internalName)))
            {
                gone = d->destroyed;
                Checkpoint::setWord(r, 2, d->locked ? 1u : 0u); // ours: the third word as the lock
            }
        }
        for (const Pickup& k : pickups_)
            if (k.placement == p.index && k.taken)
                gone = true;
        if (destroyed_.count(p.index))
            gone = true;
        for (const Trigger& t : triggers_)
        {
            if (t.placement != p.index)
                continue;
            if (t.savePoint && t.used)
                gone = true;
            if (p.index >= level.instCount())
            {
                // A spawned trigger's class data as a placed one's: [type, radius] (Jury_Turf's
                // Gen_Save_Point02: TRIGGER_SPHERE, 4.0); SetTriggerRadius sets the second.
                Checkpoint::setWord(r, 0, 1u);
                Checkpoint::setWord(r, 1, std::bit_cast<std::uint32_t>(t.radius));
            }
        }
        out[static_cast<std::size_t>(p.index)] = gone ? Checkpoint::nulRecord() : std::move(r);
    }
    return out;
}

std::vector<std::uint8_t> LevelRuntime::scriptData() const
{
    // 0x4812d0: the level program's data memory (its constants, then its VARs at their offsets).
    std::vector<std::uint8_t> data(SaveLayout::buffer5, 0);
    std::copy_n(levelData_.begin(), std::min(levelData_.size(), data.size()), data.begin());
    for (const Assets::MlsSymbol& sym : levelSymbols_)
    {
        if (sym.offset == 0xFFFFFFFF || sym.offset + 4 > data.size())
            continue;
        const Script::Value v = scripts_.global(levelProgram_, sym.name);
        std::uint8_t* at = data.data() + sym.offset;
        switch (v.type)
        {
        case Script::ValueType::Int:
        case Script::ValueType::Bool:
        {
            const auto w = static_cast<std::int32_t>(v.i);
            std::memcpy(at, &w, 4);
            break;
        }
        case Script::ValueType::Real:
        {
            const auto f = static_cast<float32>(v.r);
            std::memcpy(at, &f, 4);
            break;
        }
        case Script::ValueType::Vec3:
            if (sym.offset + 12 <= data.size())
                std::memcpy(at, &v.v, 12);
            break;
        default:
            break; // entities and strings are the level's own again on a load
        }
    }
    return data;
}

void LevelRuntime::restoreScriptData(const std::vector<std::uint8_t>& data)
{
    for (const Assets::MlsSymbol& sym : levelSymbols_)
    {
        if (sym.offset == 0xFFFFFFFF || sym.offset + 4 > data.size())
            continue;
        Script::Value v = scripts_.global(levelProgram_, sym.name);
        const std::uint8_t* at = data.data() + sym.offset;
        switch (v.type)
        {
        case Script::ValueType::Int:
        case Script::ValueType::Bool:
        {
            std::int32_t w = 0;
            std::memcpy(&w, at, 4);
            v.i = w;
            break;
        }
        case Script::ValueType::Real:
        {
            float32 f = 0.0f;
            std::memcpy(&f, at, 4);
            v.r = f;
            break;
        }
        case Script::ValueType::Vec3:
            if (sym.offset + 12 <= data.size())
                std::memcpy(&v.v, at, 12);
            break;
        default:
            continue;
        }
        scripts_.setGlobal(levelProgram_, sym.name, v);
    }
}

void LevelRuntime::restoreSpawned(const std::vector<SaveEntityRecord>& records)
{
    for (std::size_t i = static_cast<std::size_t>(setup_.level->instCount()); i < records.size(); ++i)
    {
        const SaveEntityRecord& r = records[i];
        if (r.name == "NUL")
        {
            // Gone: it keeps its place in the registry (the indices after it stay the same).
            Assets::InstPlacement gone;
            gone.index = static_cast<int32>(i);
            gone.record = "NUL";
            spawned_.push_back(std::move(gone));
            continue;
        }
        Assets::InstPlacement p;
        p.record = r.name;
        p.internalName = r.instance;
        p.position = Math::Vector3{r.pos[0], r.pos[1], r.pos[2]};
        p.rotation = Checkpoint::instRotation(r.plane);
        for (std::size_t w = 0; w < r.classData.size() / 4; ++w)
            p.params.push_back(static_cast<int32>(Checkpoint::word(r, w)));
        addSpawned(std::move(p));
    }
}

void LevelRuntime::restoreDoors(const std::vector<SaveEntityRecord>& records)
{
    for (const Assets::InstPlacement& p : setup_.level->placements())
        if (p.entityClass == "Door_Inst" && p.index >= 0 && static_cast<std::size_t>(p.index) < records.size())
            if (Doors::Door* d = doors_.find(lower(p.internalName)))
                d->locked = Checkpoint::word(records[static_cast<std::size_t>(p.index)], 2) != 0;
}

} // namespace Game
