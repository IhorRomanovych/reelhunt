// Game/ai/ai_director.cpp - the script-built AI set-up. See ai_director.h.
#include "Game/ai/ai_director.h"

#include <algorithm>
#include <cctype>

namespace Game {

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void addUnique(std::vector<std::string>& v, const std::string& s)
{
    if (std::find(v.begin(), v.end(), s) == v.end())
        v.push_back(s);
}

} // namespace

void AiDirector::clear()
{
    *this = AiDirector{};
}

HunterSetup& AiDirector::at(const std::string& name)
{
    const std::string key = lower(name);
    for (HunterSetup& h : hunters_)
        if (h.name == key)
            return h;
    hunters_.push_back(HunterSetup{});
    hunters_.back().name = key;
    return hunters_.back();
}

const HunterSetup* AiDirector::hunter(const std::string& name) const
{
    const std::string key = lower(name);
    for (const HunterSetup& h : hunters_)
        if (h.name == key)
            return &h;
    return nullptr;
}

Subpack* AiDirector::findSubpack(const std::string& subpack)
{
    const std::string key = lower(subpack);
    for (Subpack& s : subpacks_)
        if (s.name == key)
            return &s;
    return nullptr;
}

const Subpack* AiDirector::findSubpack(const std::string& subpack) const
{
    const std::string key = lower(subpack);
    for (const Subpack& s : subpacks_)
        if (s.name == key)
            return &s;
    return nullptr;
}

void AiDirector::addEntity(const std::string& name) { at(name).added = true; }
void AiDirector::setLeader(const std::string& name) { at(name).leader = true; }
void AiDirector::setLeaderInvisible(const std::string& name) { at(name).invisible = true; }
void AiDirector::addLeaderEnemy(const std::string& leader, const std::string& enemy)
{
    const std::string key = lower(leader);
    for (auto& [l, list] : enemies_)
        if (l == key)
        {
            addUnique(list, lower(enemy));
            return;
        }
    enemies_.emplace_back(key, std::vector<std::string>{lower(enemy)});
}

void AiDirector::removeLeaderEnemy(const std::string& leader, const std::string& enemy)
{
    for (auto& [l, list] : enemies_)
        if (l == lower(leader))
            std::erase(list, lower(enemy));
}

void AiDirector::addPackAsEnemies(const std::string& leader, const std::string& pack)
{
    for (const HunterSetup& h : hunters_)
        if (h.leaderName == lower(pack))
            addLeaderEnemy(leader, h.name);
}

bool AiDirector::isEnemy(const std::string& leader, const std::string& entity) const
{
    for (const auto& [l, list] : enemies_)
        if (l == lower(leader))
            return std::find(list.begin(), list.end(), lower(entity)) != list.end();
    return false;
}

bool AiDirector::hostileToPlayer(const std::string& hunterName) const
{
    const HunterSetup* h = hunter(hunterName);
    if (!h)
        return true;
    if (h->hostage)
        return false;
    const std::string& leader = h->leaderName.empty() ? h->name : h->leaderName;
    for (const auto& [l, list] : enemies_)
        if (l == leader)
            return std::find(list.begin(), list.end(), "player") != list.end();
    return true;
}

void AiDirector::removeSubpack(const std::string&, const std::string& subpack)
{
    const std::string key = lower(subpack);
    for (HunterSetup& h : hunters_)
        if (h.subpack == key)
            h.subpack.clear();
    std::erase_if(subpacks_, [&](const Subpack& s) { return s.name == key; });
}

void AiDirector::removeAllGoals(const std::string&, const std::string& subpack)
{
    if (Subpack* s = findSubpack(subpack))
        s->goals.clear();
}

void AiDirector::deleteGoal(const std::string& goalName)
{
    const std::string key = lower(goalName);
    std::erase_if(goals_, [&](const GoalDef& g) { return g.name == key; });
    for (Subpack& s : subpacks_)
        std::erase(s.goals, key);
}

bool AiDirector::isInSubpack(const std::string& subpack, const std::string& hunterName) const
{
    const Subpack* s = findSubpack(subpack);
    return s && std::find(s->hunters.begin(), s->hunters.end(), lower(hunterName)) != s->hunters.end();
}

std::string AiDirector::subpackMember(const std::string& subpack, int32 index) const
{
    const Subpack* s = findSubpack(subpack);
    if (!s || index < 1 || index > static_cast<int32>(s->hunters.size()))
        return {};
    return s->hunters[static_cast<std::size_t>(index - 1)];
}

void AiDirector::setStayInHunt(const std::string& subpack, bool stay)
{
    if (Subpack* s = findSubpack(subpack))
        s->stayInHunt = stay;
}

void AiDirector::setHostage(const std::string& hunterName, bool on) { at(hunterName).hostage = on; }
bool AiDirector::leaderHasSubpack(const std::string& leader, const std::string& subpack) const
{
    const Subpack* sp = findSubpack(subpack);
    return sp && lower(sp->leader) == lower(leader);
}

void AiDirector::copySubpackAreas(const std::string& from, const std::string& to)
{
    const Subpack* src = findSubpack(from);
    Subpack* dst = findSubpack(to);
    if (!src || !dst)
        return;
    for (const std::string& a : src->areas)
        if (std::find(dst->areas.begin(), dst->areas.end(), a) == dst->areas.end())
            dst->areas.push_back(a);
}

void AiDirector::setBuddyFollow(const std::string& hunterName, bool follow) { at(hunterName).buddyFollow = follow; }
void AiDirector::setIgnoreIfDead(const std::string& hunterName) { at(hunterName).ignoreIfDead = true; }
void AiDirector::setHomeDirection(const std::string& hunterName, const Math::Vector3& dir)
{
    HunterSetup& h = at(hunterName);
    h.homeDirection = dir;
    h.homeDirectionSet = true;
}
void AiDirector::setLookAt(const std::string& hunterName, const std::string& entity) { at(hunterName).lookAt = lower(entity); }
void AiDirector::setGoalCrouch(const std::string& goalName, bool crouch)
{
    for (GoalDef& g : goals_)
        if (g.name == lower(goalName))
            g.crouch = crouch;
}

void AiDirector::setGoalAim(const std::string& goalName, bool aim)
{
    for (GoalDef& g : goals_)
        if (g.name == lower(goalName))
            g.aim = aim;
}

void AiDirector::addSubpack(const std::string& leader, const std::string& subpack)
{
    if (!findSubpack(subpack))
    {
        subpacks_.push_back(Subpack{});
        subpacks_.back().name = lower(subpack);
        subpacks_.back().leader = lower(leader);
    }
    at(leader).leader = true;
}

void AiDirector::addGoalToSubpack(const std::string& leader, const std::string& subpack, const std::string& goalName)
{
    addSubpack(leader, subpack);
    addUnique(findSubpack(subpack)->goals, lower(goalName));
}

void AiDirector::removeGoalFromSubpack(const std::string&, const std::string& subpack, const std::string& goalName)
{
    if (Subpack* s = findSubpack(subpack))
        std::erase(s->goals, lower(goalName));
}

bool AiDirector::subpackHasGoal(const std::string&, const std::string& subpack, const std::string& goalName) const
{
    const Subpack* s = findSubpack(subpack);
    return s && std::find(s->goals.begin(), s->goals.end(), lower(goalName)) != s->goals.end();
}

void AiDirector::addHunterToSubpack(const std::string& leader, const std::string& subpack, const std::string& hunterName)
{
    addSubpack(leader, subpack);
    for (Subpack& s : subpacks_)
        std::erase(s.hunters, lower(hunterName)); // a hunter belongs to one subpack
    addUnique(findSubpack(subpack)->hunters, lower(hunterName));
    HunterSetup& h = at(hunterName);
    h.leaderName = lower(leader);
    h.subpack = lower(subpack);
}

void AiDirector::removeHunterFromSubpack(const std::string&, const std::string& subpack, const std::string& hunterName)
{
    if (Subpack* s = findSubpack(subpack))
        std::erase(s->hunters, lower(hunterName));
    HunterSetup& h = at(hunterName);
    if (h.subpack == lower(subpack))
        h.subpack.clear();
}

void AiDirector::addAreaForSubpack(const std::string& subpack, const std::string& area)
{
    if (Subpack* s = findSubpack(subpack))
        addUnique(s->areas, lower(area));
}

void AiDirector::setStayInTerritory(const std::string&, const std::string& subpack, bool stay)
{
    if (Subpack* s = findSubpack(subpack))
        s->stayInTerritory = stay;
}

void AiDirector::setCombatType(const std::string&, const std::string& subpack, CombatType type)
{
    if (Subpack* s = findSubpack(subpack))
        s->combatType = type;
}

CombatType AiDirector::combatTypeOf(const std::string& hunter) const
{
    const Subpack* s = subpackOf(hunter);
    return s ? s->combatType : CombatType::Unset;
}

void AiDirector::defineGoal(GoalDef g)
{
    g.name = lower(g.name);
    g.node = lower(g.node);
    g.target = lower(g.target);
    for (GoalDef& existing : goals_)
        if (existing.name == g.name)
        {
            existing = g;
            return;
        }
    goals_.push_back(std::move(g));
}

void AiDirector::addIdleAction(const std::string& hunterName, IdleAction action)
{
    HunterSetup& h = at(hunterName);
    std::erase_if(h.idle, [&](const IdleAction& a) { return a.kind == action.kind; });
    h.idle.push_back(std::move(action));
}

void AiDirector::cancelIdleAction(const std::string& hunterName, IdleKind kind)
{
    std::erase_if(at(hunterName).idle, [&](const IdleAction& a) { return a.kind == kind; });
}

void AiDirector::addPatrolStop(const std::string& hunterName, PatrolStop stop)
{
    stop.node = lower(stop.node);
    at(hunterName).stops.push_back(std::move(stop));
}

void AiDirector::setHomeNode(const std::string& hunterName, const std::string& node) { at(hunterName).homeNode = lower(node); }
void AiDirector::setBlind(const std::string& hunterName, bool blind) { at(hunterName).blind = blind; }
void AiDirector::setDeaf(const std::string& hunterName, bool deaf) { at(hunterName).deaf = deaf; }
void AiDirector::setOnRadar(const std::string& hunterName, bool on) { at(hunterName).onRadar = on; }
void AiDirector::setIgnoreBodies(const std::string& hunterName, bool ignore) { at(hunterName).ignoreBodies = ignore; }
void AiDirector::setRunTime(const std::string& hunterName, float32 seconds) { at(hunterName).runTime = seconds; }

void AiDirector::associateArea(const std::string& activeArea, const std::string& playerArea)
{
    areaLinks_.emplace_back(lower(activeArea), lower(playerArea));
}

const GoalDef* AiDirector::goal(const std::string& name) const
{
    const std::string key = lower(name);
    for (const GoalDef& g : goals_)
        if (g.name == key)
            return &g;
    return nullptr;
}

const Subpack* AiDirector::subpackOf(const std::string& hunterName) const
{
    const HunterSetup* h = hunter(hunterName);
    return h && !h->subpack.empty() ? findSubpack(h->subpack) : nullptr;
}

const GoalDef* AiDirector::goalFor(const std::string& hunterName, bool hunting) const
{
    const Subpack* s = subpackOf(hunterName);
    if (!s)
        return nullptr;
    const GoalDef* best = nullptr;
    for (const std::string& name : s->goals)
    {
        const GoalDef* g = goal(name);
        const bool hunt = hunting || s->stayInHunt;
        if (!g || (g->type == GoalType::HuntEnemy && !hunt))
            continue;
        // While hunting, the hunt beats every other goal.
        if (hunt && g->type == GoalType::HuntEnemy)
            return g;
        if (!best || g->priority < best->priority)
            best = g;
    }
    return best;
}

const IdleAction* AiDirector::idleAction(const std::string& hunterName) const
{
    const HunterSetup* h = hunter(hunterName);
    if (!h)
        return nullptr;
    const IdleAction* best = nullptr;
    for (const IdleAction& a : h->idle)
        if (!best || a.priority < best->priority)
            best = &a;
    return best;
}

} // namespace Game
