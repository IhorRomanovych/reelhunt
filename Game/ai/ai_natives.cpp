// Game/ai/ai_natives.cpp - level-script AI functions. See ai_natives.h.
#include "Game/ai/ai_natives.h"

namespace Game {

using Script::NativeCall;
using Script::Value;

void bindAiNatives(Script::Engine& e, AiDirector& d, std::function<Math::Vector3(const Math::Vector3&)> toScene)
{
    auto none = [] { return Value::nil(); };
    if (!toScene)
        toScene = [](const Math::Vector3& v) { return v; };
    e.bind("aiaddentity", [&d, none](NativeCall& c) { d.addEntity(c.string(0)); return none(); });
    e.bind("aisetentityasleader", [&d, none](NativeCall& c) { d.setLeader(c.string(0)); return none(); });
    e.bind("aisetleaderinvisible", [&d, none](NativeCall& c) { d.setLeaderInvisible(c.string(0)); return none(); });
    e.bind("aiaddleaderenemy", [&d, none](NativeCall& c) { d.addLeaderEnemy(c.string(0), c.string(1)); return none(); });
    e.bind("aiaddsubpackforleader", [&d, none](NativeCall& c) { d.addSubpack(c.string(0), c.string(1)); return none(); });
    e.bind("aiaddgoalforsubpack", [&d, none](NativeCall& c) { d.addGoalToSubpack(c.string(0), c.string(1), c.string(2)); return none(); });
    e.bind("airemovegoalfromsubpack", [&d, none](NativeCall& c) { d.removeGoalFromSubpack(c.string(0), c.string(1), c.string(2)); return none(); });
    e.bind("aiisgoalnameinsubpack", [&d](NativeCall& c) { return Value::boolean(d.subpackHasGoal(c.string(0), c.string(1), c.string(2))); });
    e.bind("aiaddhuntertoleadersubpack", [&d, none](NativeCall& c) { d.addHunterToSubpack(c.string(0), c.string(1), c.string(2)); return none(); });
    e.bind("airemovehunterfromleadersubpack", [&d, none](NativeCall& c) { d.removeHunterFromSubpack(c.string(0), c.string(1), c.string(2)); return none(); });
    e.bind("aiaddareaforsubpack", [&d, none](NativeCall& c) { d.addAreaForSubpack(c.string(0), c.string(1)); return none(); });
    e.bind("aisetsubpackcombattype", [&d, none](NativeCall& c) {
        const int64 t = c.integer(2);
        d.setCombatType(c.string(0), c.string(1), t >= 0 && t <= 3 ? static_cast<CombatType>(t) : CombatType::Unset);
        return none();
    });
    e.bind("aisubpackstayinterritory", [&d, none](NativeCall& c) { d.setStayInTerritory(c.string(0), c.string(1), c.boolean(2)); return none(); });
    e.bind("aiassociateoneactiveareawithplayerarea", [&d, none](NativeCall& c) { d.associateArea(c.string(0), c.string(1)); return none(); });
    e.bind("aiclearallactiveareaassociations", [&d, none](NativeCall&) { d.clearAreaAssociations(); return none(); });

    // AIDefineGoalGotoNodeIdle(goal, who, priority, node, speed, flag)
    e.bind("aidefinegoalgotonodeidle", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::GotoNodeIdle;
        g.priority = static_cast<int32>(c.integer(2));
        g.node = c.string(3);
        g.run = c.integer(4) == 0; // AISCRIPT_RUNMOVESPEED = 0, WALK = 1
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalGotoNode(goal, hunter, priority, node, speed, flag): as GotoNodeIdle (the
    // hunter argument and the flag are not told apart yet).
    e.bind("aidefinegoalgotonode", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::GotoNodeIdle;
        g.priority = static_cast<int32>(c.integer(2));
        g.node = c.string(3);
        g.run = c.integer(4) == 0;
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalGuard(goal, priority, node, minRadius, maxRadius, n): GuardDirection with no
    // direction of its own (it keeps the way it faces there).
    e.bind("aidefinegoalguard", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::GuardDirection;
        g.priority = static_cast<int32>(c.integer(1));
        g.node = c.string(2);
        g.minRadius = c.real(3);
        g.maxRadius = c.real(4);
        g.extra = static_cast<int32>(c.integer(5));
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalHuntEnemy(goal, target, flag, n)
    e.bind("aidefinegoalhuntenemy", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::HuntEnemy;
        g.priority = 0;
        g.target = c.string(1);
        g.extra = static_cast<int32>(c.integer(3));
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalGuardDirection(goal, priority, node, minRadius, maxRadius, n, direction)
    e.bind("aidefinegoalguarddirection", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::GuardDirection;
        g.priority = static_cast<int32>(c.integer(1));
        g.node = c.string(2);
        g.minRadius = c.real(3);
        g.maxRadius = c.real(4);
        g.extra = static_cast<int32>(c.integer(5));
        g.direction = c.vec3(6);
        d.defineGoal(std::move(g));
        return none();
    });

    // AIDefineGoalGotoEntity(goal, who, priority, entity, speed, flag): go to the entity. The
    // _Idle and _StayOnPath forms take the same arguments (told apart: not yet).
    auto gotoEntity = [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::GotoEntity;
        g.priority = static_cast<int32>(c.integer(2));
        g.entity = c.string(3);
        g.run = c.integer(4) == 0;
        g.flag = c.boolean(5);
        d.defineGoal(std::move(g));
        return none();
    };
    e.bind("aidefinegoalgotoentity", gotoEntity);
    e.bind("aidefinegoalgotoentityidle", gotoEntity);
    e.bind("aidefinegoalgotoentitystayonpath", gotoEntity);
    // AIDefineGoalGotoNodeStayOnPath(goal, who, priority, node, speed, flag): as GotoNode.
    e.bind("aidefinegoalgotonodestayonpath", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::GotoNodeIdle;
        g.priority = static_cast<int32>(c.integer(2));
        g.node = c.string(3);
        g.run = c.integer(4) == 0;
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalGotoVector(goal, who, priority, name, position, speed, flag): go there (and the
    // _Idle form).
    auto gotoVector = [&d, none, toScene](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::GotoVector;
        g.priority = static_cast<int32>(c.integer(2));
        g.point = toScene(c.vec3(4));
        g.run = c.integer(5) == 0;
        g.flag = c.boolean(6);
        d.defineGoal(std::move(g));
        return none();
    };
    e.bind("aidefinegoalgotovector", gotoVector);
    e.bind("aidefinegoalgotovectoridle", gotoVector);
    // AIDefineGoalMeleeAttackVector(goal, who, priority, node, position): at the node, at the
    // position (Piggsy's door, Weasel's glass; ours: it faces it, swings not yet).
    e.bind("aidefinegoalmeleeattackvector", [&d, none, toScene](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::MeleeAttackVector;
        g.priority = static_cast<int32>(c.integer(2));
        g.node = c.string(3);
        g.point = toScene(c.vec3(4));
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalGuardLookAtEntity(goal, priority, node, minRadius, maxRadius, n, entity): guard
    // the node, looking at the entity.
    e.bind("aidefinegoalguardlookatentity", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::GuardLookAtEntity;
        g.priority = static_cast<int32>(c.integer(1));
        g.node = c.string(2);
        g.minRadius = c.real(3);
        g.maxRadius = c.real(4);
        g.extra = static_cast<int32>(c.integer(5));
        g.entity = c.string(6);
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalShootVector(goal, who, priority, node, direction): from the node, fire that way.
    e.bind("aidefinegoalshootvector", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::ShootVector;
        g.priority = static_cast<int32>(c.integer(2));
        g.node = c.string(3);
        g.direction = c.vec3(4);
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalOrbitEntity(goal, who, priority, entity, inner, outer, flag): keep between the
    // radii about the entity (CPedActOrbit 0x577d30; flag: it aims at it inside the inner one).
    e.bind("aidefinegoalorbitentity", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::OrbitEntity;
        g.priority = static_cast<int32>(c.integer(2));
        g.entity = c.string(3);
        g.inner = c.real(4);
        g.outer = c.real(5);
        g.flag = c.boolean(6);
        d.defineGoal(std::move(g));
        return none();
    });

    // AISetHunterIdleActionMinMax(hunter, kind, priority, min, max) - seconds
    e.bind("aisethunteridleactionminmax", [&d, none](NativeCall& c) {
        IdleAction a;
        a.kind = static_cast<IdleKind>(c.integer(1));
        a.priority = static_cast<int32>(c.integer(2));
        a.minTime = c.real(3);
        a.maxTime = c.real(4);
        d.addIdleAction(c.string(0), a);
        return none();
    });
    // AISetHunterIdleActionMinMaxRadius(hunter, kind, priority, min, max, radius)
    e.bind("aisethunteridleactionminmaxradius", [&d, none](NativeCall& c) {
        IdleAction a;
        a.kind = static_cast<IdleKind>(c.integer(1));
        a.priority = static_cast<int32>(c.integer(2));
        a.minTime = c.real(3);
        a.maxTime = c.real(4);
        a.radius = c.real(5);
        d.addIdleAction(c.string(0), a);
        return none();
    });
    // AISetHunterIdlePatrol(hunter, kind, priority, minMs, maxMs, path)
    e.bind("aisethunteridlepatrol", [&d, none](NativeCall& c) {
        IdleAction a;
        a.kind = IdleKind::Patrol;
        a.priority = static_cast<int32>(c.integer(2));
        a.minTime = c.real(3) / 1000.0f;
        a.maxTime = c.real(4) / 1000.0f;
        a.path = c.string(5);
        d.addIdleAction(c.string(0), a);
        return none();
    });
    e.bind("aicancelhunteridleaction", [&d, none](NativeCall& c) {
        d.cancelIdleAction(c.string(0), static_cast<IdleKind>(c.integer(1)));
        return none();
    });
    // AISetIdlePatrolStop(hunter, node, seconds, flag)
    e.bind("aisetidlepatrolstop", [&d, none](NativeCall& c) {
        d.addPatrolStop(c.string(0), PatrolStop{c.string(1), c.real(2), c.boolean(3)});
        return none();
    });
    e.bind("aisetidlehomenode", [&d, none](NativeCall& c) { d.setHomeNode(c.string(0), c.string(1)); return none(); });

    // --- who is whose enemy, subpacks and goals ---
    e.bind("airemoveleaderenemy", [&d, none](NativeCall& c) { d.removeLeaderEnemy(c.string(0), c.string(1)); return none(); });
    // AIAddAllHuntersInPackAsLeaderEnemies(leader, pack): the pack's hunters become its enemies.
    e.bind("aiaddallhuntersinpackasleaderenemies", [&d, none](NativeCall& c) {
        d.addPackAsEnemies(c.string(0), c.string(1));
        return none();
    });
    e.bind("aiisenemy", [&d](NativeCall& c) { return Value::boolean(d.isEnemy(c.string(0), c.string(1))); });
    // AIIsInSubPack(leader, subpack, hunter).
    e.bind("aiisinsubpack", [&d](NativeCall& c) { return Value::boolean(d.isInSubpack(c.string(1), c.string(2))); });
    // AIReturnSubpackEntityName(leader, subpack, n, out name): the n-th member (from 1); FALSE when none.
    e.bind("aireturnsubpackentityname", [&d](NativeCall& c) {
        const std::string name = d.subpackMember(c.string(1), static_cast<int32>(c.integer(2)));
        c.set(3, Value::string(name));
        return Value::boolean(!name.empty());
    });
    e.bind("aideletegoaldefinition", [&d, none](NativeCall& c) { d.deleteGoal(c.string(0)); return none(); });
    e.bind("airemoveallgoalsfromsubpack", [&d, none](NativeCall& c) { d.removeAllGoals(c.string(0), c.string(1)); return none(); });
    e.bind("airemovesubpackfromleader", [&d, none](NativeCall& c) { d.removeSubpack(c.string(0), c.string(1)); return none(); });
    // AIStayInHuntEnemy(leader, subpack, on, speed): its HuntEnemy goal stays on (ours: the speed unused).
    e.bind("aistayinhuntenemy", [&d, none](NativeCall& c) { d.setStayInHunt(c.string(1), c.boolean(2)); return none(); });
    e.bind("aimodifygoalcrouch", [&d, none](NativeCall& c) { d.setGoalCrouch(c.string(0), c.boolean(1)); return none(); });

    // --- hunters ---
    // AISetHunterIdleAction(hunter, kind, priority): an idle action with the default times.
    e.bind("aisethunteridleaction", [&d, none](NativeCall& c) {
        IdleAction a;
        a.kind = static_cast<IdleKind>(c.integer(1));
        a.priority = static_cast<int32>(c.integer(2));
        d.addIdleAction(c.string(0), a);
        return none();
    });
    auto direction = [toScene](const Math::Vector3& v) {
        Math::Vector3 dir = toScene(v) - toScene(Math::Vector3{0.0f, 0.0f, 0.0f});
        dir.y = 0.0f;
        const float32 l = dir.length();
        return l > 1e-4f ? dir * (1.0f / l) : Math::Vector3{0.0f, 0.0f, -1.0f};
    };
    // AISetHunterHomeNodeDirection(hunter, node, direction): its home node, and the way it faces there.
    e.bind("aisethunterhomenodedirection", [&d, none, direction](NativeCall& c) {
        d.setHomeNode(c.string(0), c.string(1));
        d.setHomeDirection(c.string(0), direction(c.vec3(2)));
        return none();
    });
    e.bind("aisethunteridledirection", [&d, none, direction](NativeCall& c) {
        d.setHomeDirection(c.string(0), direction(c.vec3(1)));
        return none();
    });
    e.bind("ailookatentity", [&d, none](NativeCall& c) { d.setLookAt(c.string(0), c.string(1)); return none(); });
    e.bind("aiignoreentityifdead", [&d, none](NativeCall& c) { d.setIgnoreIfDead(c.string(0)); return none(); });
    e.bind("aisethunterashostage", [&d, none](NativeCall& c) { d.setHostage(c.string(0), c.boolean(1)); return none(); });
    // AISetBuddyFollow / AISetBuddyWait(buddy) (natives 0x16d / 0x16c: 0x51c3a0 / 0x51c310, follow
    // mode 1 / 0).
    e.bind("aisetbuddyfollow", [&d, none](NativeCall& c) { d.setBuddyFollow(c.string(0), true); return none(); });
    e.bind("aisetbuddywait", [&d, none](NativeCall& c) { d.setBuddyFollow(c.string(0), false); return none(); });
    // Accepted without effect: the search parameters (AISetSearchParams / AISetSubPackSearchParams:
    // radius, flags and times of a named search set), keeping noise positions, staying on a path.
    for (const char* name : {"aisetsearchparams", "aisetsubpacksearchparams", "aisetentitystorenoisepositions", "aisetentitystayonpath"})
        e.bind(name, [none](NativeCall&) { return none(); });

    // --- goals ---
    // AIDefineGoalBeBuddy(goal, buddy, friend, home node, radius > 0) (0x53a020: a goal of type 7).
    e.bind("aidefinegoalbebuddy", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::BeBuddy;
        g.priority = 0;
        g.entity = c.string(1);
        g.target = c.string(2);
        g.node = c.string(3);
        g.radius = c.real(4);
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalSeekCoverBackwards(goal, who, priority, enemy, n).
    e.bind("aidefinegoalseekcoverbackwards", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::SeekCoverBackwards;
        g.priority = static_cast<int32>(c.integer(2));
        g.target = c.string(3);
        d.defineGoal(std::move(g));
        return none();
    });
    // AIDefineGoalHideUnnamedHunters(goal, priority, enemy, n) / HideNamedHunter(goal, who, priority, enemy, n, m).
    e.bind("aidefinegoalhideunnamedhunters", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::Hide;
        g.priority = static_cast<int32>(c.integer(1));
        g.target = c.string(2);
        d.defineGoal(std::move(g));
        return none();
    });
    e.bind("aidefinegoalhidenamedhunter", [&d, none](NativeCall& c) {
        GoalDef g;
        g.name = c.string(0);
        g.type = GoalType::Hide;
        g.priority = static_cast<int32>(c.integer(2));
        g.target = c.string(3);
        d.defineGoal(std::move(g));
        return none();
    });
    // aiMakeEntityBlind/Deaf(hunter, sense): 0 switches the sense off, 1 back on (the car-park
    // hunters are made blind for the intro and given their eyes back when the encounter starts).
    e.bind("aimakeentityblind", [&d, none](NativeCall& c) { d.setBlind(c.string(0), c.integer(1) == 0); return none(); });
    e.bind("aimakeentitydeaf", [&d, none](NativeCall& c) { d.setDeaf(c.string(0), c.integer(1) == 0); return none(); });
    e.bind("aisethunteronradar", [&d, none](NativeCall& c) { d.setOnRadar(c.string(0), c.boolean(1)); return none(); });
    e.bind("aientityignoredeadbodies", [&d, none](NativeCall& c) { d.setIgnoreBodies(c.string(0), c.boolean(1)); return none(); });
    e.bind("sethunterruntime", [&d, none](NativeCall& c) { d.setRunTime(c.string(0), c.real(1)); return none(); });
}

} // namespace Game
