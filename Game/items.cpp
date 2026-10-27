// Game/items.cpp - weapons and collectables. See items.h.
#include "Game/items.h"

#include <algorithm>
#include <cctype>

namespace Game {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

float32 number(const std::vector<std::string>& line, std::size_t i, float32 fallback = 0.0f)
{
    if (i >= line.size())
        return fallback;
    return Assets::parseIniNumber(line[i]).value_or(fallback);
}

} // namespace

int32 inventorySlot(const std::string& holsterSlot)
{
    static const char* kSlots[] = {"BACK", "BELT_RIGHT", "BELT_LEFT", "BELT_REAR", "SPECIAL"};
    for (int32 i = 0; i < 5; ++i)
        if (Assets::iequals(holsterSlot, kSlots[i]))
            return i + 1;
    return 0;
}

const ExecutionLevel* ItemType::execution(int32 level) const
{
    const auto i = static_cast<std::size_t>(level - 1);
    if (level < 1 || i >= executions.size() || executions[i].tvp.empty())
        return nullptr;
    return &executions[i];
}

bool ItemType::canExecute() const
{
    return std::any_of(executions.begin(), executions.end(), [](const ExecutionLevel& e) { return !e.tvp.empty(); });
}

void ItemCatalog::read(const std::vector<Assets::IniRecord>& records, const Script::ConstantTable& constants)
{
    items_.clear();
    for (const Assets::IniRecord& r : records)
    {
        const auto cls = r.text("CLASS");
        // EC_WEAPON / EC_COLLECTABLE, and responders that can be carried (Severed_Head_(CT): CT_HEAD).
        const bool responder = cls && Assets::iequals(*cls, "EC_RESPONDER") && r.text("COLLECTABLE_TYPE");
        if (!cls || (!Assets::iequals(*cls, "EC_WEAPON") && !Assets::iequals(*cls, "EC_COLLECTABLE") && !responder))
            continue;
        ItemType t;
        t.record = r.name;
        t.weapon = Assets::iequals(*cls, "EC_WEAPON") || (responder && r.text("WEAPON"));
        t.smashable = r.line("SMASHABLE") != nullptr || r.line("SMASHABLE2") != nullptr;
        t.material = r.text("MATERIAL").value_or("");
        t.model = r.text("MODEL").value_or(r.name);
        t.collectable = r.text("COLLECTABLE_TYPE").value_or("");
        if (const auto it = constants.find(lower(t.collectable)); it != constants.end())
            t.ct = static_cast<int32>(it->second);
        t.weaponType = r.text("WEAPON").value_or("");
        t.kickWeapon = r.text("KICK_WEAPON").value_or("");
        t.obstructWeapon = r.text("OBSTRUCT_WEAPON").value_or("");
        t.hotspot = r.number("HOTSPOT_RADIUS").value_or(0.75f);
        t.displayScale = r.number("DISPLAY_SCALE").value_or(1.0f);
        t.boundsBoost = r.number("BND_SPH_BOOST").value_or(1.0f);
        t.displayAnim = r.text("MODEL_DISPLAY_ANIM").value_or("");
        t.holsterSlot = r.text("HOLSTER_SLOT").value_or("");
        if (const auto* l = r.line("HOLSTER_TRANSLATION"))
            t.holsterTranslation = {number(*l, 1), number(*l, 2), number(*l, 3)};
        if (const auto* l = r.line("HOLSTER_ROTATION"))
            t.holsterRotation = {number(*l, 1), number(*l, 2), number(*l, 3), number(*l, 4, 1.0f)};
        auto vec = [](const std::vector<std::string>& l) { return Math::Vector3{number(l, 1), number(l, 2), number(l, 3)}; };
        t.torch = r.line("TORCH_LIGHT") != nullptr;
        if (const auto* l = r.line("TORCH_LIGHT_POS"))
            t.torchPos = vec(*l);
        if (const auto* l = r.line("TORCH_LIGHT_ORIEN"))
            t.torchOrien = vec(*l);
        GunLook& g = t.gun;
        g.flashModel = r.text("FLASH_MODEL").value_or("");
        if (const auto* l = r.line("FLASH_POS"))
            g.flashPos = vec(*l);
        g.shellModel = r.text("SHELL_MODEL").value_or("");
        g.shellNumber = static_cast<int32>(r.number("SHELL_NUMBER").value_or(1.0f));
        for (const auto* l : r.all("SHELL_EJECT_POS"))
            g.ejectPos.push_back(vec(*l));
        g.ejectDelayFire = r.number("SHELL_EJECT_DELAY_FIRE").value_or(-1.0f);
        g.ejectDelayReload = r.number("SHELL_EJECT_DELAY_RELOAD").value_or(-1.0f);
        g.randDelay = r.number("SHELL_RAND_DELAY").value_or(0.0f);
        if (const auto* l = r.line("SHELL_VELOCITY"))
            g.velocity = vec(*l);
        g.velRand = r.number("SHELL_VEL_RAND").value_or(0.0f);
        if (const auto* l = r.line("SHELL_SPIN"))
            g.spin = vec(*l);
        g.radius = r.number("SHELL_RADIUS").value_or(0.01f);
        g.shellsFired = r.line("SHELLS_FIRED") != nullptr;
        g.ct = t.ct;

        // Executions, in file order: NEXT_EXECUTION closes one and starts the next.
        t.executions.emplace_back();
        for (const auto& line : r.lines)
        {
            if (line.empty())
                continue;
            const std::string& key = line[0];
            ExecutionLevel& e = t.executions.back();
            if (Assets::iequals(key, "NEXT_EXECUTION"))
                t.executions.emplace_back();
            else if (Assets::iequals(key, "EXECUTE_TVP") && line.size() > 1)
                e.tvp = line[1];
            else if (Assets::iequals(key, "EXECUTE_MODEL") && line.size() > 1)
                e.model = line[1];
            else if (Assets::iequals(key, "EXECUTE_MODEL_ANIM") && line.size() > 1)
                e.modelAnim = line[1];
            else if (Assets::iequals(key, "EXECUTE_BONE"))
                e.bone = static_cast<int32>(number(line, 1));
            else if (Assets::iequals(key, "EXECUTE_HUNTER_WEAPON_DROP_TIME"))
                e.hunterWeaponDrop = number(line, 1);
            else if (Assets::iequals(key, "EXECUTE_PLAYER_WEAPON_DESTROY_TIME"))
                e.playerWeaponDestroy = number(line, 1);
            else if (Assets::iequals(key, "EXECUTE_HEAD_SEVER_TIME"))
                e.headSever = number(line, 1);
            else if (Assets::iequals(key, "EXECUTE_HEAD_EXPLODE_TIME"))
                e.headExplode = number(line, 1);
        }
        t.halo = pickupHalo(t.ct, t.holsterSlot);
        items_.push_back(std::move(t));
    }
}

const ItemType* ItemCatalog::find(const std::string& record) const
{
    for (const ItemType& t : items_)
        if (Assets::iequals(t.record, record))
            return &t;
    return nullptr;
}

const ItemType* ItemCatalog::byCollectable(int32 ct) const
{
    for (const ItemType& t : items_)
        if (t.ct == ct)
            return &t;
    return nullptr;
}

} // namespace Game
