// Game/ai/ai_director.h - the AI set-up a level script builds, and the goal decision.
//
// The game's hunters are organised by the level script (see jury_turf.mls OnCreate):
//   AIAddEntity(hunter)                         - the hunter takes part in the AI
//   AISetEntityAsLeader(leader) + subpacks      - an (invisible) leader owns subpacks of hunters
//   AIDefine*Goal / AIAddGoalForSubPack         - named goals given to subpacks
//   AISetHunterIdle* / AISetIdlePatrolStop      - what a hunter does with no goal
//   aiMakeEntityBlind / Deaf, AISetHunterOnRadar, SetHunterRunTime
// Priorities: AISCRIPT_VERYHIGH 0 < HIGH 1 < MEDIUM 2 < LOW 3 (smaller wins).
#pragma once
#include "engine_types.h"

#include <optional>
#include <string>
#include <vector>

#include "Math/vector.h"

namespace Game {

enum class GoalType {
    GotoNodeIdle, HuntEnemy, GuardDirection, GotoEntity, GotoVector, GuardLookAtEntity, ShootVector, OrbitEntity, MeleeAttackVector,
    BeBuddy,            // AIDefineGoalBeBuddy: follow / wait by a friend (cAI_ObjectiveBeBuddy)
    SeekCoverBackwards, // AIDefineGoalSeekCoverBackwards: cover away from the enemy
    Hide,               // AIDefineGoalHideUnnamedHunters / HideNamedHunter: out of the enemy's sight
};

struct GoalDef {
    std::string name;
    GoalType type = GoalType::GotoNodeIdle;
    int32 priority = 1;
    std::string node;                          // GotoNodeIdle / GuardDirection
    bool run = false;                          // AISCRIPT_RUNMOVESPEED
    std::string target;                        // HuntEnemy
    Math::Vector3 direction{0.0f, 0.0f, -1.0f}; // GuardDirection (scene space)
    float32 minRadius = 0.0f, maxRadius = 0.0f; // GuardDirection arguments 4 and 5
    int32 extra = 0;                           // the remaining integer argument
    std::string entity;                        // GotoEntity / GuardLookAtEntity / OrbitEntity: who
    Math::Vector3 point{0.0f, 0.0f, 0.0f};     // GotoVector / MeleeAttackVector (scene space)
    float32 inner = 0.0f, outer = 0.0f;        // OrbitEntity: the radii it keeps between
    bool flag = false;                         // the last boolean argument (OrbitEntity: it aims)
    float32 radius = 0.0f;                     // BeBuddy: its buddy radius (> 0)
    bool crouch = false;                       // AIModifyGoalCrouch
    bool aim = false;                          // AIModifyGoalAim (goal +0xc4): it idles aiming
};

enum class IdleKind : int32 { WanderSearch = 0, Patrol = 1, StandStill = 2, Speech = 3, MoveAnims = 4, StandAnims = 5 };

struct IdleAction {
    IdleKind kind = IdleKind::StandStill;
    int32 priority = 3;
    float32 minTime = 1.0f, maxTime = 2.0f; // seconds (patrol: milliseconds in the script)
    float32 radius = 0.0f;                  // WanderSearch
    std::string path;                       // Patrol
};

struct PatrolStop {
    std::string node;
    float32 wait = 0.0f; // seconds
    bool flag = false;
};

struct HunterSetup {
    std::string name;
    bool added = false;         // AIAddEntity
    bool blind = false, deaf = false;
    bool onRadar = true;
    bool leader = false, invisible = false;
    float32 runTime = 10.0f;    // SetHunterRunTime: seconds a hunt continues without seeing the target
    bool ignoreBodies = false;  // AIEntityIgnoreDeadBodies (AI entity +0x108): it does not notice bodies
    bool ignoreIfDead = false;  // AIIgnoreEntityIfDead: its body is not noticed
    bool hostage = false;       // AISetHunterAsHostage: slumped where it is (Idle_Hostage_Slump), no AI
    bool buddyFollow = true;    // AISetBuddyFollow / AISetBuddyWait (0x51c3a0 / 0x51c310: mode 1 / 0)
    bool homeDirectionSet = false;
    Math::Vector3 homeDirection{0.0f, 0.0f, -1.0f}; // AISetHunterHomeNodeDirection / IdleDirection (scene)
    std::string lookAt;         // AILookAtEntity: whom it faces when it has nothing else to do
    std::string homeNode;       // '' = where it stands
    std::vector<IdleAction> idle;
    std::vector<PatrolStop> stops;
    std::string leaderName, subpack; // membership
};

// How a subpack fights (AISetSubpackCombatType; script constants COMBATTYPEID_MELEE 0, _OPEN 1,
// _COVER 2, _OPEN_MELEE 3). The levels give their gun squads COVER and the fist squads MELEE.
enum class CombatType : int32 { Unset = -1, Melee = 0, Open = 1, Cover = 2, OpenMelee = 3 };

struct Subpack {
    std::string name;
    CombatType combatType = CombatType::Unset;
    std::vector<std::string> goals;
    std::vector<std::string> hunters;
    std::vector<std::string> areas;
    bool stayInTerritory = false;
    std::string leader;
    bool stayInHunt = false; // AIStayInHuntEnemy: its HuntEnemy goal counts even when not hunting
};

class AiDirector {
public:
    void clear();

    // --- script calls (names are case-insensitive) ---
    void addEntity(const std::string& name);
    void setLeader(const std::string& name);
    void setLeaderInvisible(const std::string& name);
    void addLeaderEnemy(const std::string& leader, const std::string& enemy);
    void removeLeaderEnemy(const std::string& leader, const std::string& enemy);
    // AIAddAllHuntersInPackAsLeaderEnemies(leader, pack): every hunter of `pack`'s subpacks.
    void addPackAsEnemies(const std::string& leader, const std::string& pack);
    // AIIsEnemy(leader, entity).
    [[nodiscard]] bool isEnemy(const std::string& leader, const std::string& entity) const;
    // Whether `hunter` is after the player: its leader (or itself, a leader) has him among its
    // enemies; a leader with no enemies given at all leaves it hostile (ours: scripts that never
    // call AIAddLeaderEnemy). Hostages are not.
    [[nodiscard]] bool hostileToPlayer(const std::string& hunter) const;
    void removeSubpack(const std::string& leader, const std::string& subpack);
    void removeAllGoals(const std::string& leader, const std::string& subpack);
    void deleteGoal(const std::string& goal);
    [[nodiscard]] bool isInSubpack(const std::string& subpack, const std::string& hunter) const;
    // The `index`-th (1-based) member of `subpack`, '' when there is none.
    [[nodiscard]] std::string subpackMember(const std::string& subpack, int32 index) const;
    void setStayInHunt(const std::string& subpack, bool stay);
    void setHostage(const std::string& hunter, bool on);
    void setBuddyFollow(const std::string& hunter, bool follow);
    void setIgnoreIfDead(const std::string& hunter);
    void setHomeDirection(const std::string& hunter, const Math::Vector3& dir);
    void setLookAt(const std::string& hunter, const std::string& entity);
    void setGoalCrouch(const std::string& goal, bool crouch);
    void setGoalAim(const std::string& goal, bool aim);
    void addSubpack(const std::string& leader, const std::string& subpack);
    void addGoalToSubpack(const std::string& leader, const std::string& subpack, const std::string& goal);
    void removeGoalFromSubpack(const std::string& leader, const std::string& subpack, const std::string& goal);
    [[nodiscard]] bool subpackHasGoal(const std::string& leader, const std::string& subpack, const std::string& goal) const;
    void addHunterToSubpack(const std::string& leader, const std::string& subpack, const std::string& hunter);
    void removeHunterFromSubpack(const std::string& leader, const std::string& subpack, const std::string& hunter);
    void addAreaForSubpack(const std::string& subpack, const std::string& area);
    void setStayInTerritory(const std::string& leader, const std::string& subpack, bool stay);
    void setCombatType(const std::string& leader, const std::string& subpack, CombatType type);
    // The combat type of `hunter`'s subpack (Unset when it has none or none was set).
    [[nodiscard]] CombatType combatTypeOf(const std::string& hunter) const;
    void defineGoal(GoalDef goal);
    void addIdleAction(const std::string& hunter, IdleAction action);
    void cancelIdleAction(const std::string& hunter, IdleKind kind);
    void addPatrolStop(const std::string& hunter, PatrolStop stop);
    void setHomeNode(const std::string& hunter, const std::string& node);
    void setBlind(const std::string& hunter, bool blind);
    void setDeaf(const std::string& hunter, bool deaf);
    void setOnRadar(const std::string& hunter, bool on);
    void setIgnoreBodies(const std::string& hunter, bool ignore);
    void setRunTime(const std::string& hunter, float32 seconds);
    void associateArea(const std::string& activeArea, const std::string& playerArea);
    void clearAreaAssociations() { areaLinks_.clear(); }

    // --- queries ---
    [[nodiscard]] const HunterSetup* hunter(const std::string& name) const;
    [[nodiscard]] const GoalDef* goal(const std::string& name) const;
    [[nodiscard]] const Subpack* subpackOf(const std::string& hunter) const;
    // The goal `hunter` pursues now: the most important of its subpack's goals, where
    // HuntEnemy counts only while it is hunting. nullptr = idle behaviour.
    [[nodiscard]] const GoalDef* goalFor(const std::string& hunter, bool hunting) const;
    // The idle action with the best priority of the given kinds (or any kind when empty).
    [[nodiscard]] const IdleAction* idleAction(const std::string& hunter) const;
    [[nodiscard]] std::size_t goalCount() const noexcept { return goals_.size(); }
    [[nodiscard]] const std::vector<HunterSetup>& hunters() const noexcept { return hunters_; }
    // AIDoesLeaderHaveSubpack(leader, subpack).
    [[nodiscard]] bool leaderHasSubpack(const std::string& leader, const std::string& subpack) const;
    // AICopySubpacksAreasToAnotherSubpack(from, to): `to` gets `from`'s areas too.
    void copySubpackAreas(const std::string& from, const std::string& to);

private:
    HunterSetup& at(const std::string& name);
    Subpack* findSubpack(const std::string& subpack);
    [[nodiscard]] const Subpack* findSubpack(const std::string& subpack) const;

    std::vector<HunterSetup> hunters_;
    std::vector<GoalDef> goals_;
    std::vector<Subpack> subpacks_;
    std::vector<std::pair<std::string, std::string>> areaLinks_;
    std::vector<std::pair<std::string, std::vector<std::string>>> enemies_; // leader -> its enemies
};

} // namespace Game
