// Game/ai/ai_tuning.cpp - the tuning pak AI / melee tuning. See ai_tuning.h.
#include "Game/ai/ai_tuning.h"

#include <algorithm>
#include <random>
#include <cctype>
#include <optional>
#include <sstream>

#include "Assets/ini_records.h"
#include "Assets/pak.h"

namespace Game {

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool parseBool(const std::string& t)
{
    return Assets::iequals(t, "true");
}

// The clip stem, sounds, ammo and grip of a firearm record: the exe's clip names (Glock_Fire_T,
// Eagle_Fire_T, SawnOff_Fire_T, ...), the bank's SFX_WEAPON_FIRE_* / _FIRE_EMPTY_* /
// _RELOAD_* names and the CT_AMMO_* types matched by us.
void firearmLook(const std::string& record, Firearm& f)
{
    struct Look {
        const char* record;
        const char* stem;
        const char* sound;
        const char* empty;
        const char* reload;
        Firearm::Grip grip;
    };
    using G = Firearm::Grip;
    static const Look kLooks[] = {
        {"nailgun", "NailGun", "NAIL_GUN", "NAIL_GUN", "PLASTIC", G::OneHanded},
        {"glock", "Glock", "GLOCK", "GLOCK", "METAL", G::OneHanded},
        {"6shooter", "6Shooter", "GLOCK", "REVOLVER", "METAL", G::OneHanded},
        {"deserteagle", "Eagle", "GLOCK", "DESERTEAGLE", "METAL", G::OneHanded},
        {"uzi", "Uzi", "UZI", "UZI", "METAL", G::OneHanded},
        {"sawnoffshotgun", "SawnOff", "SHOTGUN", "SHOTGUN", "METAL", G::TwoHanded},
        {"shotgun", "Shotgun", "SHOTGUN", "SHOTGUN", "METAL", G::TwoHanded},
        {"sniperrifle", "Sniper", "SNIPERRIFLE", "SNIPERRIFLE", "METAL", G::Rifle},
        {"tranqrifle", "Sniper", "TRANQRIFLE", "TRANQRIFLE", "METAL", G::Rifle},
        {"coltcommando", "Commando", "COLT", "COLT", "METAL", G::TwoHanded},
    };
    for (const Look& l : kLooks)
        if (record == l.record)
        {
            f.clipStem = l.stem;
            f.sound = l.sound;
            f.emptySound = l.empty;
            f.reloadSound = l.reload;
            f.grip = l.grip;
        }
}

void setNumber(const Assets::IniRecord& r, const char* key, float32& out, float32 scale = 1.0f)
{
    if (const auto v = r.number(key))
        out = *v * scale;
}

void setBool(const Assets::IniRecord& r, const char* key, bool& out)
{
    if (const auto t = r.text(key))
        out = parseBool(*t);
}

// "KEY level value" lines, assigned in file order as the exe does (it ignores the names).
void setThresholds(const Assets::IniRecord& r, const char* key, LevelThresholds& out)
{
    std::size_t i = 0;
    for (const auto* l : r.all(key))
        if (i < out.size() && l->size() > 2)
            if (const auto v = Assets::parseIniNumber((*l)[2]))
                out[i++] = *v / 100.0f;
}

void readVision(const Assets::IniRecord& r, VisionData& v)
{
    setNumber(r, "SAFE_ZONE_%", v.safeZone, 0.01f);
    setNumber(r, "FRUSTUM_WIDTH_STAND_EASY_deg", v.widthStandEasy);
    setNumber(r, "FRUSTUM_WIDTH_WALK_EASY_deg", v.widthWalkEasy);
    setNumber(r, "FRUSTUM_WIDTH_RUN_EASY_deg", v.widthRunEasy);
    setNumber(r, "FRUSTUM_WIDTH_STAND_HARD_deg", v.widthStandHard);
    setNumber(r, "FRUSTUM_WIDTH_WALK_HARD_deg", v.widthWalkHard);
    setNumber(r, "FRUSTUM_WIDTH_RUN_HARD_deg", v.widthRunHard);
    setNumber(r, "FRUSTUM_HEIGHT_deg", v.height);
    setNumber(r, "FRUSTUM_m", v.range);
    setNumber(r, "PERIPHERAL_FRUSTUM_WIDTH_deg", v.peripheralWidth);
    setNumber(r, "PERIPHERAL_FRUSTUM_HEIGHT_deg", v.peripheralHeight);
    setNumber(r, "PERIPHERAL_FRUSTUM_m", v.peripheralRange);
    setNumber(r, "CHECK_PERIPHERAL_m", v.checkPeripheral);
    setNumber(r, "FACE_m", v.face);
    setNumber(r, "FACE_DEFINITE_m", v.faceDefinite);
    setBool(r, "FACE_LIGHT_bool", v.useFaceLight);
    setBool(r, "DISTANCE_bool", v.useDistance);
    setBool(r, "LIGHT_bool", v.useLight);
    setBool(r, "VISIBLE_bool", v.useVisible);
    setThresholds(r, "FACE_LIGHT_%", v.faceLight);
    setThresholds(r, "DISTANCE_%", v.distance);
    setThresholds(r, "LIGHT_%", v.light);
    setThresholds(r, "VISIBLE_%", v.visible);
}

void readAlert(const Assets::IniRecord& r, AlertData& a)
{
    setNumber(r, "ALERT_LEVEL_DEGRADE", a.degradeSeconds);
    for (const auto* l : r.all("SET_ALERT_LEVEL"))
    {
        bool ok = false;
        const AiAction act = l->size() > 2 ? parseAiAction((*l)[1], &ok) : AiAction::NotOnAlert;
        if (ok)
            a.setAlert[static_cast<std::size_t>(act)] = parseSenseLevel((*l)[2]);
    }
    for (const auto* l : r.all("SUSTAIN_ACTION"))
    {
        bool ok = false;
        const AiAction act = l->size() > 2 ? parseAiAction((*l)[1], &ok) : AiAction::NotOnAlert;
        if (ok)
            if (const auto v = Assets::parseIniNumber((*l)[2]))
                a.sustain[static_cast<std::size_t>(act)] = *v;
    }
    bool first = true;
    for (const auto* l : r.all("ACTION_TRANSITION"))
    {
        if (l->size() < 5)
            continue;
        bool okFrom = false, okTo = false;
        const AiAction from = parseAiAction((*l)[1], &okFrom);
        const AiAction to = parseAiAction((*l)[2], &okTo);
        if (!okFrom || !okTo)
            continue;
        if (first) // the file replaces the whole table
        {
            for (auto& row : a.transitions)
                row.fill({});
            first = false;
        }
        ActionTransition& t = a.transitions[static_cast<std::size_t>(from)][static_cast<std::size_t>(to)];
        t.valid = true;
        t.sight = parseSenseLevel((*l)[3]);
        t.hearing = parseSenseLevel((*l)[4]);
    }
}

std::optional<std::vector<Assets::IniRecord>> records(const Assets::PakFile& pak, const std::string& path)
{
    const auto text = pak.file(path);
    if (!text)
        return std::nullopt;
    return Assets::parseIniRecords(*text);
}

} // namespace

int32 materialIndex(const std::string& name)
{
    static constexpr const char* kNames[] = {
        "MATERIAL_NONE", "MATERIAL_STONE", "MATERIAL_WOOD", "MATERIAL_METAL", "MATERIAL_PAPER", "MATERIAL_RUBBLE",
        "MATERIAL_TILE", "MATERIAL_CAR", "MATERIAL_WIREMESH", "MATERIAL_PLASTICBAG", "MATERIAL_DUMPSTER",
        "MATERIAL_STREETLIGHT", "MATERIAL_OPANDTRWINDOW", "MATERIAL_PEDBARRIER", "MATERIAL_METALRAILING",
        "MATERIAL_PUDDLE", "MATERIAL_IGNORE", "MATERIAL_GLASSHARD", "MATERIAL_CERAMIC", "MATERIAL_PLASTER",
        "MATERIAL_WOODHARD", "MATERIAL_METALHARD", "MATERIAL_GRASS", "MATERIAL_MUD", "MATERIAL_BUSH", "MATERIAL_WATER",
        "MATERIAL_CARPET", "MATERIAL_PED", "MATERIAL_WEAPON", "MATERIAL_HALFOPENSHUTTER", "MATERIAL_BRICK",
        "MATERIAL_CAN", "MATERIAL_BOTTLE", "MATERIAL_WOOD_PLANK", "MATERIAL_WOOD_BOARD", "MATERIAL_WOOD_DOOR",
        "MATERIAL_WOOD_BIG_DOOR", "MATERIAL_WOOD_THIN", "MATERIAL_WOOD_TRUNK", "MATERIAL_METAL_SMALL_SHUTTER",
        "MATERIAL_METAL_LARGE_SHUTTER", "MATERIAL_METAL_CAGE_SHUTTER", "MATERIAL_METAL_THICK_METAL_PLATE",
        "MATERIAL_METAL_POLE", "MATERIAL_METAL_CONTAINER", "MATERIAL_SOFT", "MATERIAL_TYRE", "MATERIAL_PLASTIC",
        "MATERIAL_TARMAC", "MATERIAL_TRASHCAN", "MATERIAL_PEDBARRIER_WOOD", "MATERIAL_PEDBARRIER_METAL",
        "MATERIAL_PEDBARRIER_STONE", "MATERIAL_GORE", "MATERIAL_WET_MUD", "MATERIAL_WHEELY_BIN",
        "MATERIAL_SHOPPING_TROLLEY", "MATERIAL_PIGSY_GRATING", "MATERIAL_BUSH_SOLID", "MATERIAL_PEDBARRIER_CARPET",
        "MATERIAL_OPAQUEGLASS", "MATERIAL_PEDBARRIER_WIREMESH", "MATERIAL_IRON_BAR_DOOR"};
    for (std::size_t i = 0; i < std::size(kNames); ++i)
        if (Assets::iequals(name, kNames[i]))
            return static_cast<int32>(i);
    return -1;
}

std::string Firearm::aimClip(bool zoom) const
{
    const char* held = grip == Grip::Rifle ? "Sniper" : (grip == Grip::TwoHanded ? "2Handed" : "1Handed");
    return std::string(held) + (zoom ? "_ZoomAim_T" : "_Aim_T");
}

std::string Firearm::zoomMoveClip() const
{
    if (grip == Grip::Rifle)
        return {};
    return grip == Grip::TwoHanded ? "2Handed_ZoomAim_Move_T" : "1Handed_ZoomAim_Move_T";
}

float32 Firearm::hitChance(float32 distance) const
{
    if (distance > maxRange)
        return 0.0f;
    if (distance <= minRange || maxRange <= minRange)
        return 1.0f;
    return 1.0f + (farHit - 1.0f) * (distance - minRange) / (maxRange - minRange);
}

const ThrowWeapon* AiTuning::throwWeapon(const std::string& name) const
{
    const auto it = throwing.find(lower(name));
    return it == throwing.end() ? nullptr : &it->second;
}

const Firearm* AiTuning::firearm(const std::string& name) const
{
    const auto it = firearms.find(lower(name));
    return it != firearms.end() && !it->second.clipStem.empty() ? &it->second : nullptr;
}

const KickWeapon& AiTuning::kick(const std::string& name) const
{
    static const KickWeapon kDefault;
    auto it = kicks.find(lower(name.empty() ? std::string("kick_fists") : name));
    if (it == kicks.end())
        it = kicks.find("kick_fists");
    return it == kicks.end() ? kDefault : it->second;
}

const MeleeWeapon& AiTuning::meleeWeapon(const std::string& name) const
{
    const auto it = melee.find(lower(name));
    return it == melee.end() ? fists : it->second;
}

std::string voiceSamplePrefix(const std::string& voice, int32 variant)
{
    // The record's VOICE name is a class 1..27 (type +0xf8, parsed at 0x43e780); a ped with no
    // voice set (ped+0x634 = 0x31) takes one from its class's range (0x44b830): the first voice
    // plus ped+0x420 mod the count (0x44ba20; ours: `variant`). The voice ids index the table
    // the speech uses (Game/hunter_speech.h: 0 SFX_H1V1_ ... 47 SFX_T1V1_).
    static const char* const kVoices[] = {
        "H1V1", "H1V2", "H2V1", "H2V2", "H3V1", "H3V2", "I1V1", "I1V2", "I2V1", "I2V2", "I3V1", "I3V2",
        "P1V1", "P1V2", "P1V3", "P2V1", "P2V2", "P3V1", "P3V2", "K1V1", "K1V2", "K2V1", "K2V2", "K3V1",
        "K3V2", "S1V1", "S1V2", "S2V1", "S2V2", "S3V1", "S3V2", "W1V1", "W1V2", "W2V1", "W2V2", "W3V1",
        "W3V2", "C1V1", "C1V2", "C2V1", "B1V1", "Y1V1", "D1V1", "J1V1", "M1V1", "X1V1", "R1V1", "T1V1"};
    struct Class {
        const char* name;
        int32 first, count;
    };
    static const Class kClasses[] = {
        {"HOODED_SMALL", 4, 2},     {"HOODED_MEDIUM", 0, 2},    {"HOODED_BIG", 2, 2},
        {"INNOCENT_SMALL", 10, 2},  {"INNOCENT_MEDIUM", 6, 2},  {"INNOCENT_BIG", 8, 2},
        {"WARDOG_SMALL", 35, 2},    {"WARDOG_MEDIUM", 31, 2},   {"WARDOG_BIG", 33, 2},
        {"SMILEY_SMALL", 29, 2},    {"SMILEY_MEDIUM", 25, 2},   {"SMILEY_BIG", 27, 2},
        {"SKINZ_SMALL", 23, 2},     {"SKINZ_MEDIUM", 19, 2},    {"SKINZ_BIG", 21, 2},
        {"POLICE_SMALL", 17, 2},    {"POLICE_MEDIUM", 12, 3},   {"POLICE_BIG", 15, 2},
        {"CERBERUS", 37, 3},        {"TRAMP", 47, 1},           {"JOURNALIST", 43, 1},
        {"BUNNY", 40, 1},           {"CERBERUS_LEADER", 41, 1}, {"PIGGSY", 45, 1},
        {"RAMIREZ", 46, 1},         {"MONKEY", 44, 1},          {"DIRECTOR", 42, 1}};
    for (const Class& c : kClasses)
        if (Assets::iequals(voice, c.name))
            return std::string("SFX_") + kVoices[c.first + static_cast<int32>(static_cast<uint32>(variant) % static_cast<uint32>(c.count))] + "_";
    return {};
}

SenseLevel parseSenseLevel(const std::string& name)
{
    static const char* kNames[] = {"very_low", "low", "medium", "high", "very_high"};
    for (int32 i = 0; i < 5; ++i)
        if (Assets::iequals(name, kNames[i]))
            return i;
    return kNoLevel;
}

const char* senseLevelName(SenseLevel l)
{
    static const char* kNames[] = {"very_low", "low", "medium", "high", "very_high"};
    return l >= 0 && l < 5 ? kNames[l] : "none";
}

AiAction parseAiAction(const std::string& name, bool* ok)
{
    for (int32 i = 0; i < kAiActions; ++i)
        if (Assets::iequals(name, aiActionName(static_cast<AiAction>(i))))
        {
            if (ok)
                *ok = true;
            return static_cast<AiAction>(i);
        }
    if (ok)
        *ok = false;
    return AiAction::NotOnAlert;
}

const char* aiActionName(AiAction a)
{
    switch (a)
    {
    case AiAction::NotOnAlert: return "not_on_alert";
    case AiAction::Hunt: return "hunt";
    case AiAction::Investigate: return "investigate";
    case AiAction::Chase: return "chase";
    case AiAction::Combat: return "combat";
    }
    return "?";
}

AlertData::AlertData()
{
    setAlert = {kNoLevel, kNoLevel, kLow, kHigh, kVeryHigh};
    sustain = {0.0f, 0.0f, 8.0f, 3.0f, 1.0f};
    auto set = [this](AiAction from, AiAction to, SenseLevel sight, SenseLevel hearing) {
        transitions[static_cast<std::size_t>(from)][static_cast<std::size_t>(to)] = {true, sight, hearing};
    };
    using A = AiAction;
    set(A::Combat, A::Combat, kLow, kNoLevel);
    set(A::Chase, A::Combat, kVeryHigh, kNoLevel);
    set(A::Chase, A::Chase, kLow, kLow);
    set(A::Investigate, A::Combat, kVeryHigh, kNoLevel);
    set(A::Investigate, A::Chase, kHigh, kHigh);
    set(A::Investigate, A::Investigate, kLow, kLow);
    set(A::Hunt, A::Combat, kHigh, kNoLevel);
    set(A::Hunt, A::Chase, kMedium, kMedium);
    set(A::Hunt, A::Investigate, kLow, kLow);
    set(A::NotOnAlert, A::Combat, kVeryHigh, kNoLevel);
    set(A::NotOnAlert, A::Chase, kHigh, kNoLevel);
    set(A::NotOnAlert, A::Investigate, kMedium, kMedium);
}

AiTuning::AiTuning()
{
    // AISounds.ini: the sounds the player makes on foot and in a fight.
    sounds["WALKING"] = {kVeryLow, kMedium, true};
    sounds["RUNNING"] = {kLow, kMedium, true};
    sounds["SPRINTING"] = {kLow, kMedium, true};
    sounds["SNEAKING"] = {kVeryLow, kVeryLow, true};
    sounds["MELEE_SWIPE"] = {kVeryLow, kMedium, true};
}

SoundType AiTuning::sound(const std::string& name) const
{
    const auto it = sounds.find(upper(name));
    return it == sounds.end() ? SoundType{} : it->second;
}

HunterTraits AiTuning::hunter(const std::string& record) const
{
    const auto it = hunters.find(lower(record));
    return it == hunters.end() ? HunterTraits{} : it->second;
}

std::string AiTuning::read(const Assets::PakFile& pak, const std::string& level)
{
    std::ostringstream log;
    int files = 0;
    if (const auto ai = records(pak, "levels/global/data/aiTypeData.ini"))
    {
        ++files;
        if (const auto* r = Assets::findRecord(*ai, "vision_data"))
            readVision(*r, vision);
        if (const auto* r = Assets::findRecord(*ai, "VOLUME_DISTANCES"))
        {
            for (const auto& l : r->lines)
                if (l.size() > 1)
                    if (const SenseLevel lv = parseSenseLevel(l[0]); lv != kNoLevel)
                        if (const auto v = Assets::parseIniNumber(l[1]))
                            hearing.volumeDistance[static_cast<std::size_t>(lv)] = *v;
        }
        if (const auto* r = Assets::findRecord(*ai, "BSP_VOLUME_FILTERING"))
        {
            setNumber(*r, "general_reduction", hearing.bspGeneral);
            setNumber(*r, "execution_reduction", hearing.bspExecution);
            setNumber(*r, "lure_reduction", hearing.bspLure);
            setNumber(*r, "alert_extension", hearing.alertExtension);
        }
        if (const auto* r = Assets::findRecord(*ai, "hearing_data"))
        {
            setNumber(*r, "WALK_%", hearing.walkFraction, 0.01f);
            setNumber(*r, "RUN_%", hearing.runFraction, 0.01f);
            if (const auto t = r->text("CONFUSION"))
                hearing.confusion = parseSenseLevel(*t);
        }
        if (const auto* r = Assets::findRecord(*ai, "default"))
            readAlert(*r, alert);
        if (const auto* r = Assets::findRecord(*ai, "investigate_action"); r && !r->lines.empty())
            alert.investigateRuns = !Assets::iequals(r->lines.front().front(), "walk");
        if (const auto* r = Assets::findRecord(*ai, "search_parameters"))
        {
            setNumber(*r, "RADIUS_m", alert.searchRadius);
            setNumber(*r, "DARKNODE_%", alert.darkNode, 0.01f);
            setNumber(*r, "DENSITY_%", alert.searchDensity, 0.01f);
            setNumber(*r, "DOUBLE_SEARCH_%", alert.doubleSearch, 0.01f);
            setNumber(*r, "DOUBLE_SEARCH_DELAY_s", alert.doubleSearchDelay);
        }
    }
    if (const auto snd = records(pak, "levels/global/data/AISounds.ini"))
    {
        ++files;
        if (const auto* r = Assets::findRecord(*snd, "INTERESTING_SOUNDS"))
            for (const auto& l : r->lines)
                if (l.size() >= 4)
                    sounds[upper(l[0])] = {parseSenseLevel(l[1]), parseSenseLevel(l[2]), parseBool(l[3])};
    }
    if (const auto weapons = records(pak, "levels/global/data/WeaponTypeData.ini"))
    {
        ++files;
        const auto shots = records(pak, "levels/global/data/ShotTypeData.ini");
        for (const Assets::IniRecord& r : *weapons)
        {
            const auto cls = r.text("CLASS");
            if (cls && Assets::iequals(*cls, "WC_MELEE_KICK"))
            {
                KickWeapon k;
                setNumber(r, "COMBAT_DELAYS", k.kickAt);
                if (const auto v = r.number("COMBAT_DELAYS", 2))
                    k.stompAt = *v;
                k.shot.strikeAt = k.kickAt;
                if (const auto name = r.text("SHOT_TYPE_1"); shots && name)
                    if (const auto* s = Assets::findRecord(*shots, *name))
                    {
                        setNumber(*s, "SIZE", k.shot.size);
                        setNumber(*s, "DAMAGE", k.shot.damage);
                        setNumber(*s, "PLAYER_DAMAGE", k.shot.playerDamage);
                    }
                kicks[lower(r.name)] = k;
                continue;
            }
            if (cls && Assets::iequals(*cls, "WC_THROWING"))
            {
                ThrowWeapon t;
                if (const auto name = r.text("SHOT_TYPE_1"); shots && name)
                    if (const auto* s = Assets::findRecord(*shots, *name))
                    {
                        setNumber(*s, "DAMAGE", t.damage);
                        setNumber(*s, "PLAYER_DAMAGE", t.playerDamage);
                    }
                throwing[lower(r.name)] = t;
                continue;
            }
            if (cls && Assets::iequals(*cls, "WC_AMMO"))
            {
                // A record without a key keeps the nail gun's value, except the look (only the
                // guns we know are fired) and NUM_HEAD_SHOTS (the 6shooter has none: one, ours).
                Firearm f;
                f.clipStem.clear();
                f.sound.clear();
                f.emptySound.clear();
                f.reloadSound.clear();
                f.headShots = 1;
                setNumber(r, "MIN_FIRE_RANGE", f.minRange);
                setNumber(r, "MAX_FIRE_RANGE", f.maxRange);
                setNumber(r, "MAX_FIRE_RANGE_HIT_PROBABILITY", f.farHit);
                auto setCount = [&](const char* key, int32& out, int32 least) {
                    float32 v = static_cast<float32>(out);
                    setNumber(r, key, v);
                    out = std::max(least, static_cast<int32>(v));
                };
                setCount("CLIP_AMMO", f.clipAmmo, 1);
                setNumber(r, "RELOAD_LOOP", f.reloadLoopStart);
                if (const auto v = r.number("RELOAD_LOOP", 2))
                    f.reloadLoopEnd = *v;
                setCount("MAX_CLIPS", f.maxClips, 0);
                setCount("DROPPED_AMMO", f.droppedAmmo, 0);
                setCount("MAX_SHOTS", f.pellets, 1);
                setCount("NUM_HEAD_SHOTS", f.headShots, 0);
                setNumber(r, "SHOT_SPREAD", f.spread);
                setNumber(r, "RECOIL_DIST", f.recoilUp);
                if (const auto v = r.number("RECOIL_DIST", 2))
                    f.recoilSide = *v;
                setNumber(r, "RECOIL_TIME", f.recoilTime);
                setNumber(r, "RECOIL_VERT_ACCUMULATOR", f.recoilAccumulator);
                setNumber(r, "MAX_RECOIL_VERT_DIST", f.maxRecoilUp);
                setNumber(r, "LOCKON_TARGET_RANGES", f.autoHeadRange);
                if (const auto v = r.number("LOCKON_TARGET_RANGES", 2))
                    f.headRange = *v;
                setNumber(r, "TORSO_DAMAGE_MULTIPLIER", f.torsoMultiplier);
                setNumber(r, "ARM_DAMAGE_MULTIPLIER", f.armMultiplier);
                setNumber(r, "LEG_DAMAGE_MULTIPLIER", f.legMultiplier);
                if (const auto name = r.text("SHOT_TYPE_1"); shots && name)
                    if (const auto* s = Assets::findRecord(*shots, *name))
                    {
                        setNumber(*s, "DAMAGE", f.damage);
                        setNumber(*s, "PLAYER_DAMAGE", f.playerDamage);
                        setNumber(*s, "SIZE", f.range);
                        f.flashFx = s->text("FLASH_FX").value_or("");
                        f.bodyHitFx = s->text("CHARACTER_HIT_FX").value_or("");
                        f.hardHitFx = s->text("HARD_BSP_HIT_FX").value_or("");
                        f.smokeHitFx = s->text("SMOKE_BSP_HIT_FX").value_or("");
                        float32 bodies = static_cast<float32>(f.bodies);
                        setNumber(*s, "BODIES_HIT", bodies);
                        f.bodies = std::max(1, static_cast<int32>(bodies));
                    }
                firearmLook(lower(r.name), f);
                firearms[lower(r.name)] = f;
                continue;
            }
            if (cls && Assets::iequals(*cls, "WC_EXPLODER"))
            {
                Exploder x;
                if (const auto name = r.text("SHOT_TYPE_1"); shots && name)
                    if (const auto* s = Assets::findRecord(*shots, *name))
                    {
                        setNumber(*s, "SIZE", x.radius);
                        setNumber(*s, "DAMAGE", x.damage);
                        setNumber(*s, "PLAYER_DAMAGE", x.playerDamage);
                        x.flashFx = s->text("FLASH_FX").value_or("");
                    }
                exploders[lower(r.name)] = x;
                continue;
            }
            if (!cls || !Assets::iequals(*cls, "WC_MELEE"))
                continue;
            MeleeWeapon w;
            auto pair = [&](const char* key, float32& quick, float32& hard) {
                setNumber(r, key, quick);
                if (const auto v = r.number(key, 2))
                    hard = *v;
            };
            pair("COMBAT_DELAYS", w.quick.strikeAt, w.hard.strikeAt);
            pair("CHAIN_POINTS", w.quick.chainPoint, w.hard.chainPoint);
            pair("FIRE_WINDOW_STARTS", w.quick.fireWindowStart, w.hard.fireWindowStart);
            pair("FIRE_WINDOW_LENGTHS", w.quick.fireWindowLength, w.hard.fireWindowLength);
            for (std::size_t i = 0; i < w.comboPauses.size(); ++i)
                if (const auto v = r.number("COMBO_PAUSES", i + 1))
                    w.comboPauses[i] = *v;
            const std::string meleeClass = upper(r.text("MELEE_CLASS").value_or("FISTS"));
            w.clipStem = meleeClass.rfind("BLADE", 0) == 0 ? "Blade"
                       : meleeClass == "BEATER2"          ? "Beater2"
                       : meleeClass.rfind("BEATER", 0) == 0 ? "Beater"
                                                           : "Fist";
            setNumber(r, "BLOCK_SPEED", w.blockSpeed);
            setNumber(r, "GRAB_TIME", w.grabTime);
            for (std::size_t i = 0; i < 3; ++i)
            {
                if (const auto v = r.number("BLOCK_DAMAGE", i + 1))
                    w.blockDamage[i] = *v;
                if (const auto v = r.number("GRAPPLE_DAMAGE", i + 1))
                    w.grappleDamage[i] = *v;
                if (const auto v = r.number("GRAPPLE_DAMAGE_PLAYER", i + 1))
                    w.grappleDamagePlayer[i] = *v;
                if (const auto v = r.number("GRAPPLE_DAMAGE_TIMES", i + 1))
                    w.grappleDamageTimes[i] = *v;
            }
            setNumber(r, "GRAPPLE_FIRE_WINDOW_STARTS", w.grappleWindowStart);
            setNumber(r, "GRAPPLE_FIRE_WINDOW_LENGTHS", w.grappleWindowLength);
            // The shots: SHOT_TYPE_1 quick, SHOT_TYPE_2 hard (ShotTypeData: SIZE, DAMAGE, PLAYER_DAMAGE).
            auto readShot = [&](const char* key, MeleeAttack& a) {
                const auto name = r.text(key);
                if (!shots || !name)
                    return;
                if (const auto* s = Assets::findRecord(*shots, *name))
                {
                    setNumber(*s, "SIZE", a.size);
                    setNumber(*s, "DAMAGE", a.damage);
                    setNumber(*s, "PLAYER_DAMAGE", a.playerDamage);
                }
            };
            readShot("SHOT_TYPE_1", w.quick);
            readShot("SHOT_TYPE_2", w.hard);
            melee[lower(r.name)] = w;
        }
        if (const auto it = melee.find("fists"); it != melee.end())
            fists = it->second;
    }
    if (const auto phys = records(pak, "levels/global/data/physicsTypeData.ini"))
    {
        ++files;
        for (const Assets::IniRecord& r : *phys)
        {
            PhysicsType t;
            if (const auto c = r.text("CLASS"))
            {
                static constexpr std::pair<const char*, int32> kClasses[] = {
                    {"PC_POINT_MASS", 0}, {"PC_RIGID_CUBOID", 1}, {"PC_RIGID_SPHERE", 2},
                    {"PC_CHARACTER", 3},  {"PC_DOOR", 4},         {"PC_HANGING", 5}};
                for (const auto& [name, cls] : kClasses)
                    if (Assets::iequals(*c, name))
                        t.cls = cls;
            }
            for (std::size_t i = 0; i < 3; ++i)
                if (const auto v = r.number("CENTRE_OF_MASS", i + 1))
                    (i == 0 ? t.centreOfMass.x : i == 1 ? t.centreOfMass.y : t.centreOfMass.z) = *v;
            setNumber(r, "MASS", t.mass);
            if (const auto f = r.text("FRICTION_TYPE"))
                t.properFriction = Assets::iequals(*f, "PROPER_FRICTION");
            setNumber(r, "FRICTION", t.friction);
            setNumber(r, "AIR_RESISTANCE", t.airResistance);
            setNumber(r, "SPIN_RESISTANCE", t.spinResistance);
            setNumber(r, "BOUNCE", t.bounce);
            setNumber(r, "MAX_SPEED", t.maxSpeed);
            t.turnedOn = r.line("TURNED_ON") != nullptr;
            t.noGravity = r.line("NO_GRAVITY") != nullptr;
            t.immovable = r.line("IMMOVABLE") != nullptr;
            physicsTypes[lower(r.name)] = t;
        }
    }
    if (const auto ents = records(pak, "levels/" + level + "/entityTypeData.ini"))
    {
        for (const Assets::IniRecord& r : *ents)
            if (const auto p = r.text("PHYSICS"))
                if (const auto it = physicsTypes.find(lower(*p)); it != physicsTypes.end())
                    propPhysics[lower(r.name)] = {it->second, r.line("MADEOFIRON") != nullptr,
                                                  std::max(0, materialIndex(r.text("MATERIAL").value_or("MATERIAL_NONE")))};
    }
    if (const auto ents = records(pak, "levels/" + level + "/entityTypeData.ini"))
    {
        ++files;
        std::unordered_map<std::string, std::string> models; // record -> MODEL
        for (const Assets::IniRecord& r : *ents)
            if (const auto m = r.text("MODEL"))
                models[lower(r.name)] = *m;
        std::mt19937 hideRng(0x48494445u); // HIDE_HEALTH draws
        for (const Assets::IniRecord& r : *ents)
        {
            for (const auto* l : r.all("SKIN_TEX"))
                if (l->size() > 1)
                    skins[lower(r.name)].push_back((*l)[1]);
            const auto cls = r.text("CLASS");
            if (cls && Assets::iequals(*cls, "EC_PLAYER"))
            {
                setNumber(r, "HIT_POINTS", playerHitPoints);
                setNumber(r, "KO_TIME", playerKoTime);
                setNumber(r, "WIGGLE_TIME_DECAY", wiggleTimeDecay);
                setNumber(r, "STAMINA_TOTAL_SPRINT_TIME", stamina.totalSprintTime);
                setNumber(r, "STAMINA_NO_SPRINT_TIME", stamina.noSprintTime);
                setNumber(r, "STAMINA_RECOVERY_RUNNING", stamina.recoveryRunning);
                setNumber(r, "STAMINA_RECOVERY_MOVING", stamina.recoveryMoving);
                setNumber(r, "STAMINA_RECOVERY_STILL", stamina.recoveryStill);
                setNumber(r, "STAMINA_RECOVERY_PAUSE", stamina.recoveryPause);
                for (std::size_t i = 0; i < 2; ++i)
                    if (const auto v = r.number("EXECUTE_STAGE_TIMES", i + 1))
                        executeStageTimes[i] = *v;
                setNumber(r, "THROW_CHARGE_TIME", throwChargeTime);
                for (std::size_t i = 0; i < 3; ++i)
                {
                    float32* lo[3] = {&minThrowVelocity.x, &minThrowVelocity.y, &minThrowVelocity.z};
                    float32* hi[3] = {&maxThrowVelocity.x, &maxThrowVelocity.y, &maxThrowVelocity.z};
                    if (const auto v = r.number("MIN_THROW_VELOCITY", i + 1))
                        *lo[i] = *v;
                    if (const auto v = r.number("MAX_THROW_VELOCITY", i + 1))
                        *hi[i] = *v;
                }
                setNumber(r, "AIM_LOCKON_ANGLES", aim.lockonWidth);
                if (const auto v = r.number("AIM_LOCKON_ANGLES", 2))
                    aim.lockonHeight = *v;
                setNumber(r, "AIM_LOCKON_DIST", aim.lockonDist);
                for (std::size_t i = 0; i < aim.crosshair.size(); ++i)
                    if (const auto v = r.number("CROSSHAIR_SIZES", i + 1))
                        aim.crosshair[i] = *v;
                setNumber(r, "CROUCH_RECOIL", aim.crouchRecoil);
                for (std::size_t i = 0; i < 2; ++i)
                {
                    if (const auto v = r.number("ZOOM_LEVELS", i + 1))
                        aim.zoomLevels[i] = *v;
                    if (const auto v = r.number("ZOOM_MOVE_SCALES", i + 1))
                        aim.zoomMoveScales[i] = *v;
                }
                setNumber(r, "ZOOM_SPEED", aim.zoomSpeed);
            }
            if (!cls || !Assets::iequals(*cls, "EC_HUNTER"))
                continue;
            HunterTraits t;
            if (const auto m = r.text("MODEL"))
                t.model = *m;
            if (const auto h = r.text("HEAD"))
                if (const auto it = models.find(lower(*h)); it != models.end())
                    t.head = it->second;
            // Skins are looked up by the model too (a record named otherwise: Cerberus).
            if (!t.model.empty() && !skins.count(lower(t.model)) && skins.count(lower(r.name)))
                skins[lower(t.model)] = skins[lower(r.name)];
            setNumber(r, "HIT_POINTS", t.hitPoints);
            setNumber(r, "SWING_PAUSE", t.swingPause);
            setNumber(r, "RUN_TIME", t.runTime);
            setNumber(r, "HIT_ACCURACY", t.hitAccuracy);
            setNumber(r, "KO_TIME", t.koTime);
            setNumber(r, "STUN_TIME", t.stunTime);
            setNumber(r, "BLOCK_DAMAGE", t.blockDamage);
            setNumber(r, "BLOCK_ACCURACY", t.blockAccuracy);
            t.hideHealth = 30.0f + 20.0f * static_cast<float32>(std::uniform_int_distribution<int32>(0, 0x7ff)(hideRng)) / 2047.0f;
            setNumber(r, "HIDE_HEALTH", t.hideHealth);
            t.hardHead = !r.all("HARDHEAD").empty();
            if (const auto v = r.text("VOICE"))
                t.voice = upper(*v);
            for (const auto* l : r.all("SKIN_TEX"))
                if (l->size() > 1)
                    t.skinTextures.push_back((*l)[1]);
            hunters[lower(r.name)] = std::move(t);
        }
    }
    fromPak = files > 0;
    log << files << " tuning files, " << sounds.size() << " sounds, " << hunters.size() << " hunter records; vision "
        << vision.range << " m, fists " << fists.quick.damage << "/" << fists.quick.playerDamage << " at "
        << fists.quick.strikeAt << " s";
    return log.str();
}

} // namespace Game
