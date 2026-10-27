// Game/ai/gun_combat.h - how gun hunters fight: the squad's orders and the hunter's cover fight.
//
// Traced from the exe with objdump (addresses are the exe's; trace notes in PLAN.md):
//
// The squad. AISetSubpackCombatType picks the subpack's combat controller (0x506586, table
// 0x674440): MELEE and OPEN_MELEE get the melee one, OPEN and COVER both get
// cAI_ControllerCombatCover (the exe has no "open" gun controller: cAI_ControllerCombatOpen and
// CPedActGunCombat exist only as name strings). Each tick it bands every member by its distance
// to the enemy, <8, <16, <20, <25, <36 m or beyond (0x4fd2e0, squared 64 .. 1296) and orders:
//   < 25 m    fight (CPedActGunCoverCombat, AI state "Cover Combat") while fewer than 6 of the
//             subpack fight (0x500f80; the 6 is passed at 0x506e2e); one already fighting goes on;
//             over the 6 it gets no new order (0x4fd6b0 acts only on a boundary) and goes on as it was
//   25..36 m  6 fighting already: "Stay Back" (0x501180); else as beyond
//   beyond    one fighting goes on only while it is on its way to cover (ped+0x6ec, set while
//             CPedActCoverSeek runs), the rest run at him ("Combat Chase", 0x4fd940)
//
// The hunter (CPedActGunCoverCombat 0x583350, constants 0x79ed58..): it fights between 3 and 20 m
// of him (Start 0x583630; the band is kept at least 2 m wide, 0x583720) and decides 1.5 s after it
// starts (0x5838d2). It looks for cover (CPedActCoverSeek, 0x586350) between 1.5 m and its max less
// 1 m from him, nearest first; a point is cover when the line from his eye (his feet + 1.83 m) to it
// is blocked (0x5587e0 / 0x4de4d0), and a point one hunter took is not given to another (the
// reserved set 0x79bd0c). It is there within 0.75 m (0x583cd2); he within 3 m of the point spoils
// it (0x583c38) and it looks again within (min + 1, max - 1) (0x586630). In cover it waits
// rand(1.5, 3) s before it shows itself (0x5864b0; the exe also re-decides every second, here only
// at the end of the wait); after an assessment it waits rand(0.5, 0.7) s (0x586db0); standing in
// the open lasts 3 s (0x586e50).
// Leaning out to fire is CPedActPeek (vtable 0x79ef08, sub-states 0x676210). Its point (0x571c20)
// needs him within 20 m (400 squared, 0x571d39) and is tried from the hunter's chest (feet +
// 1.2 m) at his head (feet + 1.7 m): straight over the cover first, then 1.5 m to its right,
// then 1.5 m to its left (0x79c844); the first clear line picks it (up: (0,1,0), right (1,0,0),
// left (-1,0,0)). Then: settle into the crouch within 2 s (0x57006e), lean (ped flag 0x10000 left
// / 0x20000 right, lean -1 / +1 at ped+0x490), out 3 s over the top or 2 s leaning (0x57029e /
// 0x57037a), firing; its other timers are rand(1.2, 2), rand(0.2, 0.6), rand(0.2, 1) and
// rand(1.5, 4) s (0x5712b0 .. 0x571635; that a shot re-arms the out time to rand(1.5, 4) is our
// reading, here once). Lean back, and no new peek for 4 s (0x571ae6). The cover fight drops a
// peek when he is beyond 20 m or within 1 m (0x584be7). The clips are Peek_L_ / Peek_R_ Sneak or
// Stand (indices 0x11 .. 0x14 of the clip table 0x689690; there is no Peek_F_Stand in the data).
// No peek from the cover: it leaves it (CPedActCoverExit, vtable 0x79eccc; sets ped+0x6ec like
// CoverSeek, 0x5822bc) for a point (min + 1) .. (max - 1) m from him (0x586bf7), the next
// decision 0.5 s on (0x586c2b).
//
// He in the shadows, his place known (cAI_ControllerCombatBlind 0x7a7eac, started by the squad
// coordinator 0x506490 on a message flag, msg+0x10): each member is banded by its distance to where
// he was (the same 8 / 16 / 20 / 25 / 36 m, 0x5a01f0; the place is taken again when he moved more
// than 0.2 squared m, 0x59faa5). Within 16 m (bands 0, 1) a hunter with a gun blind-fires
// ("Blind Gun[Open]" / "[Cover]" by the subpack's type, 0x5a0780 / 0x5a0830), one without one
// swings blind; beyond, "Get Closer" (0x5a0320), re-ordered at most every second (0x5a0160).
// CPedActBlindGunCombat (vtable 0x79bb1c, update 0x5873d0): it closes in until the line from its
// gun to the place is clear, then stands and fires once it faces it within acos 0.9 (0x587672);
// an order lasts 5 s (0x5876d1).
//
// He lost (cAI_CoordinateKillEnemy 0x506660): melee squads chase (0x5066b0); gun squads go to the
// gunfire (cAI_ControllerGotoGunfire, vtable 0x7c55cc) when 0x507330 holds, else chase too. The
// gunfire place is the squad's last fix on him (0x5068c0) raised 0.6 m (0x5c9e10: +0.5 -0.7 +0.8).
// Per member (state at +0x408, AI states 58 .. 61):
//   Goto Wait    to the route node 20 short of the place (0x5cac1a); a shorter route: Wait there
//   Wait         facing the place; the first to wait starts 3 s (0x5ca85a). Once that ran out and
//                two members wait (or the only one does, 0x5ca10a), they go on
//   Goto Shoot   each to its own spot (0x5cb120, random, none twice until all were given): graph
//                nodes within 5 m of the place (0x7c5540) whose point 0.8 m up sees it (0x5cb06b),
//                16 at most; with one or none, the place's node itself
//   Quit         3 s after the first member's test (0x5cacb0) all quit (0x5cae10): they stand
//                facing the place 4 s (0x5caade) and go back to the squad (0x514050)
//
// Not traced (ours): what the blind message flag is (here: he hides in the shadows while it does
// not see him); what 0x507330 and 0x5cacb0 test (the entity at +0x14 and the ped flag 0x800000 of
// *(ped+0x698); here: gun squads always go, and the quit clock starts when the first reaches its
// spot); the speech the gunfire controller plays (0x514590); what a blind shot hits (here: him
// within 1 m of the place, kBlindHit); the lean's side offset for the shot (here the 1.5 m probe
// point); where CoverExit goes (here the nearest graph node in the band that sees him).
//
// Orbit and evade (CPedActOrbit / CPedActEvade) are not part of it: the squads never order them
// themselves. Orbit comes only from a scripted "Orbit" action (cAI_ControllerOrbit; inner/outer
// radius from the script, it closes in beyond the outer, backs off inside the inner one) and Evade
// from a subpack boundary (0x4fd830; it goes 20 m from him, 0x6848e8, and settles on the 25 / 15 /
// 5 m ring it reaches, re-evading when he comes inside it). A squad kept to its territory
// (AISubpackStayInTerritory) meeting him outside it goes to the territory node nearest him and
// holds there (0x5017a0 -> 0x4fd6b0: a goto order); with none it can reach it evades
// (Hunter::holdBoundary / evade; that 0x4fdc20 asks "is he inside" is our reading).
//
// Under fire (0x587ee0: he aims at it, locked on or within 8 degrees, and fired in the last 0.5 s)
// a hunter on its way to cover seeks again away from him (mode 1, 0x584c30), and so does one
// standing in the open (ours: the exe's state 1, 0x585120, does it when it cannot see him).
//
// The five role slots (0x79eeb8 .. 0x79eec8, each with a release time and a "say it" time; given
// on entering cover 0x5864b0, re-seeking 0x586630 -> 0x5868b0, seeking 0x586800, advancing
// 0x586040; expired by 0x584200) only time the squad's role lines: when a member's slot comes due
// the squad tick (0x5138e8) has it say line 0x36 / 0x22 / 0x23 / 0x24 / 0x25 (0x514590, an order
// of type 0xe; the lines are the Communications enum, HunterSpeech::lineSlot). See RoleSlots.
//
// The cover points (traced 2026-10-22). A ped's CNavigation (ped+0x680) keeps its nearby list:
// breadth first from its node over walkable links, the nodes within 5.2 m of it (0x79c208), 64 at
// most, each with its link depth (+0x50 / +0x150, built by 0x563fd0 and again once it moved 2.5 m).
// Its point finder (ped+0x69c, 0x5587e0) tests entries 1 .. 7 of that list (never its own node, and
// never more than seven) from his eye (feet + 1.83 m; moved to the side while he leans): a blocked
// line puts the node on the hidden list (+0x707c), a clear one (and two more traces, 0x4ded30) on
// the visible list (+0x7180). Both keep the depth order. CoverSeek's mode 2 (0x581d30, what the
// cover fight mostly asks for) takes, from the lowest depth that has one, the hidden node nearest
// the hunter that no one took (0x559420; its fallback toward or away from him searches the same
// list, so it finds nothing more); CoverExit (0x582e10, mode 2) the visible node nearest it
// (0x559040), not its own cover nor any hunter's goal node. Either fails when the node it picked
// lies outside the band from him; no other node is tried.
//
// Not traced (ours): the height the hidden test aims at (0x5587e0 raises the node 0.25 m, or by a
// height less 1 m, 0x558ca0; here a crouching hunter's head, 1 m); the modes 0 / 1 (toward / away
// from him, 0x559210 / 0x558e30) in the states not followed here; how often it fires (the aim
// component decides that, 0x569c20 on); giving up on a point it cannot reach; the crouch (the
// player's Sneak_Stand).
#pragma once
#include "engine_types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Game/ai/nav_graph.h"
#include "Math/vector.h"

namespace Game {

namespace gun {

// The squad (cAI_ControllerCombatCover).
inline constexpr float32 kBands[] = {8.0f, 16.0f, 20.0f, 25.0f, 36.0f}; // 0x7926a8 (squared)
inline constexpr int32 kMaxFighting = 6;                               // 0x506e2e
inline constexpr int32 kFightBand = 3;                                 // bands 0..3: within 25 m

// The hunter (CPedActGunCoverCombat).
inline constexpr float32 kMinRange = 3.0f, kMaxRange = 20.0f; // 0x79ed60, 0x79ed5c
inline constexpr float32 kMinBandWidth = 2.0f;               // 0x79ed64
inline constexpr float32 kFirstDecision = 1.5f;              // 0x5838d2
inline constexpr float32 kDecisionGap = 1.0f;                // 0x586436
inline constexpr float32 kAssessMin = 0.5f, kAssessMax = 0.7f; // 0x586e08
inline constexpr float32 kStandTime = 3.0f;                  // 0x586eb3
inline constexpr float32 kCoverNear = 1.5f;                  // 0x5863fa: cover at least this far from him
inline constexpr float32 kCoverArrive = 0.75f;               // 0x583cd2
inline constexpr float32 kCoverSpoiled = 3.0f;               // 0x583c38
inline constexpr float32 kHideMin = 1.5f, kHideMax = 3.0f;   // 0x586529 / 0x586523
inline constexpr float32 kPeekMin = 1.5f, kPeekMax = 4.0f;   // CPedActPeek 0x571635 (our reading)

// Leaning out (CPedActPeek).
inline constexpr float32 kPeekReach = 20.0f;   // 0x79c874 (squared 400): him within 20 m
inline constexpr float32 kPeekNear = 1.0f;     // 0x584c01: a peek ends with him within 1 m
inline constexpr float32 kPeekChest = 1.2f;    // 0x79c858: the line leaves its chest ...
inline constexpr float32 kPeekHead = 1.7f;     // 0x79c828: ... for his head
inline constexpr float32 kPeekSide = 1.5f;     // 0x79c844: the lean probes 1.5 m to its side
inline constexpr float32 kPeekUpOut = 3.0f;    // 0x57029e: out over the top
inline constexpr float32 kPeekLeanOut = 2.0f;  // 0x57037a: out leaning
inline constexpr float32 kPeekSettle = 2.0f;   // 0x57006e: into the crouch within 2 s
inline constexpr float32 kPeekCooldown = 4.0f; // 0x571ae6: no new peek for 4 s

// Blind fire (cAI_ControllerCombatBlind, CPedActBlindGunCombat).
inline constexpr float32 kBlindMoved = 0.2f;   // 0x7a7de0: squared metres he moves for a new place
inline constexpr int32 kBlindFireBand = 1;     // bands 0, 1 (< 16 m): blind fire (0x676b4c)
inline constexpr float32 kBlindReorder = 1.0f; // 0x5a0382: Get Closer at most every second
inline constexpr float32 kBlindOrder = 5.0f;   // 0x5876d1: a blind-fire order lasts 5 s
inline constexpr float32 kBlindFacing = 0.9f;  // 0x79edc8: fires facing the place within acos 0.9
inline constexpr float32 kBlindHit = 1.0f;     // ours: a blind shot can find him this near the place

// The lost enemy (cAI_ControllerGotoGunfire).
inline constexpr int32 kGunfireWaitNodes = 20;    // 0x5cac1a: waits 20 route nodes short of it
inline constexpr float32 kGunfireWait = 3.0f;     // 0x5ca85a: the first to wait starts 3 s
inline constexpr float32 kGunfireLook = 3.0f;     // 0x5cad44: 3 s after the first test, all quit
inline constexpr float32 kGunfireQuit = 4.0f;     // 0x5caade: they stand 4 s, then go back
inline constexpr float32 kGunfireSpotRadius = 5.0f; // 0x7c5540: shoot spots within 5 m of it
inline constexpr std::size_t kGunfireSpots = 16;  // 0x5cb09b
inline constexpr float32 kGunfireRise = 0.6f;     // 0x5c9e10: the place, above his feet
inline constexpr float32 kGunfireSpotEye = 0.8f;  // 0x7c550c: a spot sees it from 0.8 m up
inline constexpr float32 kEnemyEye = 1.83f;                  // 0x79c394: above his feet
inline constexpr float32 kCoverHead = 1.0f;                  // ours: the hidden test aims here
inline constexpr float32 kNearbyRadius = 5.2f;              // 0x79c208: a ped's nearby nodes
inline constexpr std::size_t kNearbyCap = 64;                // 0x563fd0: at most 64 of them
inline constexpr std::size_t kCoverTested = 7;               // 0x5587e0: entries 1 .. 7 are tested
inline constexpr float32 kSeekGiveUp = 10.0f;                // ours: seconds to reach it before it gives up

// The distance band of a member (0x4fd2e0): 0 .. 5.
[[nodiscard]] inline int32 squadBand(float32 distance) noexcept
{
    int32 b = 0;
    while (b < 5 && distance >= kBands[b])
        ++b;
    return b;
}

enum class SquadOrder {
    Fight,    // fight it out (cover combat)
    Keep,     // no new order: it goes on as it was (chasing)
    Chase,    // run at him
    StayBack, // hold back
};

// The squad's order for one member at `distance` from the enemy. `fighting`: it is in cover
// combat now; `seekingCover`: on its way to cover; `others`: the members of its subpack that
// fight now, itself left out.
[[nodiscard]] inline SquadOrder squadOrder(float32 distance, bool fighting, bool seekingCover, int32 others) noexcept
{
    const int32 band = squadBand(distance);
    const int32 count = others + (fighting ? 1 : 0); // the controller counts every fighting member
    if (band <= kFightBand)
    {
        if (fighting)
            return SquadOrder::Fight;
        return count < kMaxFighting ? SquadOrder::Fight : SquadOrder::Keep;
    }
    if (band == kFightBand + 1 && count >= kMaxFighting)
        return SquadOrder::StayBack;
    return fighting && seekingCover ? SquadOrder::Fight : SquadOrder::Chase;
}

// The fighting band kept at least kMinBandWidth wide (0x583720).
[[nodiscard]] inline std::pair<float32, float32> fightBand(float32 minRange = kMinRange, float32 maxRange = kMaxRange) noexcept
{
    maxRange = std::max(maxRange, kMinBandWidth);
    if (maxRange - minRange < kMinBandWidth)
        maxRange = minRange + kMinBandWidth;
    return {minRange, maxRange};
}

inline float32 flatDistance(const Math::Vector3& a, const Math::Vector3& b) noexcept
{
    const float32 dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// The tested entries of a hunter's nearby list (0x5587e0), split by whether his eye sees them
// (`seen(node)`); each keeps its link depth, in list order.
struct CoverPoints {
    std::vector<std::pair<int32, int32>> hidden, visible;
};
template <class Seen>
[[nodiscard]] CoverPoints coverPoints(const NavGraph& nav, const Math::Vector3& hunter, Seen&& seen)
{
    CoverPoints out;
    const int32 here = nav.nearest(hunter);
    if (here < 0)
        return out;
    const auto nearby = nav.ranked(here, hunter, kNearbyRadius, kNearbyCap);
    for (std::size_t i = 1; i < nearby.size() && i <= kCoverTested; ++i)
        (seen(nearby[i].first) ? out.visible : out.hidden).push_back(nearby[i]);
    return out;
}

// From the lowest depth that has one, the node of `points` nearest `hunter` that `taken(node)`
// does not rule out (0x559420 / 0x559040); -1 when there is none.
template <class Taken>
[[nodiscard]] int32 nearestPoint(const NavGraph& nav, const std::vector<std::pair<int32, int32>>& points,
                                 const Math::Vector3& hunter, Taken&& taken)
{
    int32 best = -1, bestDepth = 0;
    float32 bestDist = 0.0f;
    for (const auto& [n, depth] : points)
    {
        if (best >= 0 && depth > bestDepth)
            break;
        if (taken(n))
            continue;
        const float32 d = (nav.node(n).position - hunter).length();
        if (best < 0 || d < bestDist)
        {
            best = n;
            bestDepth = depth;
            bestDist = d;
        }
    }
    return best;
}

// Cover for a hunter at `hunter` from an enemy at `enemy` (feet), CoverSeek mode 2: the nearest
// hidden point (his eye cannot see it: !`seen(node)`), when it lies `nearest`..`farthest` m from
// him. -1 when there is none.
template <class Seen, class Taken>
[[nodiscard]] int32 findCover(const NavGraph& nav, const Math::Vector3& hunter, const Math::Vector3& enemy, float32 nearest,
                              float32 farthest, Seen&& seen, Taken&& taken)
{
    const int32 n = nearestPoint(nav, coverPoints(nav, hunter, seen).hidden, hunter, taken);
    if (n < 0)
        return -1;
    const float32 d = (nav.node(n).position - enemy).length();
    return d >= nearest && d <= farthest ? n : -1;
}

// CoverSeek modes 0 / 1 (0x562b80 / 0x562b00 -> 0x559210): from the lowest depth that has one, the
// hidden point farthest along `dir` from the hunter (toward him: mode 0; away: mode 1), its
// projection above `minDot` (the order's cosine: 0 for the cover fight's mode-1 seeks, 0x79ed88);
// then the band test. -1 when there is none.
template <class Seen, class Taken>
[[nodiscard]] int32 findCoverAlong(const NavGraph& nav, const Math::Vector3& hunter, const Math::Vector3& enemy,
                                  const Math::Vector3& dir, float32 minDot, float32 nearest, float32 farthest, Seen&& seen,
                                  Taken&& taken)
{
    const auto points = coverPoints(nav, hunter, seen).hidden;
    int32 best = -1, bestDepth = 0;
    float32 bestDot = 0.0f;
    for (const auto& [n, depth] : points)
    {
        if (best >= 0 && depth > bestDepth)
            break;
        if (taken(n))
            continue;
        const float32 d = (nav.node(n).position - hunter).dot(dir);
        if (d > minDot && (best < 0 || d > bestDot))
        {
            best = n;
            bestDepth = depth;
            bestDot = d;
        }
    }
    if (best < 0)
        return -1;
    const float32 d = (nav.node(best).position - enemy).length();
    return d >= nearest && d <= farthest ? best : -1;
}

// Under fire (0x587ee0): he aims at it and fired within the last half second. Aiming at it
// (0x587c90): locked on to it, else the angle between his aim and the line to it under 8 degrees
// (3 + 5; 9 + 5 or 5 + 5 for guns with weapon flags 0x40 / 4 - ours: always 3 + 5).
inline constexpr float32 kUnderFireWindow = 0.5f; // 0x79ef84

// The cover fight's other moves (CPedActGunCoverCombat sub-states):
//   8  close to the gun's range (0x586200, from the decision 0x585610): no hidden point near it,
//      he beyond the gun's far range (0x551220) - it walks at him (half pace) to the far range, or
//      to the near one when an odd number of the fighters are deciding (count 0x79ee7c & 1);
//      within it, it decides again; he too near or going off past the band: it stands (0x585d90).
//   7  advance on his place (0x586040): he lost, it was seeking again (state 6, 0x585310); it runs
//      (walks once 3 advance) to where he was when lost (0x583a20 takes his place then). Held back
//      while he is still within 4 m of that place before the squad's advance time (0x79eeb4), and
//      while more than one advances until 10 s past it. The advance time is set on seeking again
//      (0x586630): while he holds a gun (his brain's gun mode), rand(12, 17) s on for a cop or a
//      Cerberus (stats +0xa8 kind 1 / 2, bodyKind), else rand(6, 12); with no gun in his hand
//      1.5 s.
//   9  follow (0x586280, from standing 0x5859a0): he going off past the band - after him (running
//      while at most two follow) until within (max - 1), then it seeks cover again (0x585de0).
//   13 rush (0x585fb0, from standing): he inside the band's near end and it cannot see him - to
//      within 7 m of him (0x79eda8), firing when it can; there it decides (0x585ef0).
// "Too near" (0x586fb0): within the band's near end, standing or moving at him; "going off"
// (0x5870d0): past its far end, standing or moving away.
inline constexpr float32 kAdvanceNear = 4.0f;  // 0x79eda4: he still near the place he was lost
inline constexpr float32 kAdvanceHold = 10.0f; // 0x79ed90: more than one advancing waits this past it
inline constexpr float32 kAdvancePace = 1.5f;  // 0x79ed6c: the advance time when he is not locked on
inline constexpr float32 kAdvancePaceMin = 6.0f, kAdvancePaceMax = 12.0f; // 0x79ed78 / 0x79edb8
inline constexpr float32 kAdvancePaceTroopMin = 12.0f, kAdvancePaceTroopMax = 17.0f; // 0x79edb8 / 0x79edb4

// The kind the ped stats keep at +0xa8 (0x41ca40, by the body model's name): 1 the cops
// (Cop_BodS1 / M1 / B1), 2 the Cerberus (Cer_Bod*), 3 the hostages (Hos_Bod1 / 2), 4 Bun_Bod, 5
// Hel_Bod, 6 Piggsy (Pig_BodF1); any other 0.
[[nodiscard]] inline int32 bodyKind(const std::string& body)
{
    auto is = [&](const char* name) { return body.size() >= std::char_traits<char>::length(name) && body.compare(0, std::char_traits<char>::length(name), name) == 0; };
    if (is("Cop_BodS1") || is("Cop_BodM1") || is("Cop_BodB1"))
        return 1;
    if (is("Cer_BodS1") || is("Cer_BodM1") || is("Cer_BodB1"))
        return 2;
    if (is("Pig_BodF1"))
        return 6;
    if (is("Hos_Bod1") || is("Hos_Bod2"))
        return 3;
    if (is("Bun_Bod"))
        return 4;
    if (is("Hel_Bod"))
        return 5;
    return 0;
}
inline constexpr float32 kRushTo = 7.0f;       // 0x79eda8

// A gun's near and far range (0x551220, by its collectable type): pistols and the nail gun
// 7.5 .. 10, shotguns 7.5 .. 9, the Colt Commando 7 .. 20, the rifles 15 .. 60, the rest 7 .. 15.
[[nodiscard]] inline std::pair<float32, float32> weaponRange(int32 ct) noexcept
{
    switch (ct)
    {
    case 39: case 40: case 42: case 46: case 89: return {7.5f, 10.0f};
    case 44: case 45: case 50: return {7.5f, 9.0f};
    case 47: return {7.0f, 20.0f};
    case 48: case 49: case 93: return {15.0f, 60.0f};
    default: return {7.0f, 15.0f};
    }
}
inline constexpr float32 kUnderFireAngle = 8.0f;  // 0x79ef94 + 0x79ef78, degrees

// Out of cover (CoverExit mode 2): the nearest point his eye sees, `nearest`..`farthest` m from him.
template <class Seen, class Taken>
[[nodiscard]] int32 findExit(const NavGraph& nav, const Math::Vector3& hunter, const Math::Vector3& enemy, float32 nearest,
                             float32 farthest, Seen&& seen, Taken&& taken)
{
    const int32 n = nearestPoint(nav, coverPoints(nav, hunter, seen).visible, hunter, taken);
    if (n < 0)
        return -1;
    const float32 d = (nav.node(n).position - enemy).length();
    return d >= nearest && d <= farthest ? n : -1;
}

// Where a hunter in cover leans out from (CPedActPeek 0x571c20). `clear(offset)`: the line from its
// chest moved `offset` m to its right (0: straight; negative: left) to his head is clear.
enum class PeekSide { None, Up, Right, Left };
template <class Clear>
[[nodiscard]] PeekSide peekSide(float32 distance, Clear&& clear)
{
    if (distance > kPeekReach)
        return PeekSide::None;
    if (clear(0.0f))
        return PeekSide::Up;
    if (clear(kPeekSide))
        return PeekSide::Right;
    if (clear(-kPeekSide))
        return PeekSide::Left;
    return PeekSide::None;
}

// A gun hunter's blind order at `distance` from where he was (cAI_ControllerCombatBlind 0x59fc47).
enum class BlindOrder { Fire, GetCloser };
[[nodiscard]] inline BlindOrder blindOrder(float32 distance) noexcept
{
    return squadBand(distance) <= kBlindFireBand ? BlindOrder::Fire : BlindOrder::GetCloser;
}

// The gunfire controller's member states (+0x408; AI states 58 .. 61).
enum class Gunfire : int32 { None = -1, GotoWait = 0, Wait = 1, GotoShoot = 2, Quit = 3 };

// The route node a member waits at, `routeNodes` long to the place (both ends counted): the one 20
// short of it (0x5cab10 pops nodes while 20 are left); -1: the route is shorter, it waits where it is.
[[nodiscard]] inline int32 gunfireWaitIndex(std::size_t routeNodes) noexcept
{
    return routeNodes >= static_cast<std::size_t>(kGunfireWaitNodes) ? static_cast<int32>(routeNodes) - kGunfireWaitNodes : -1;
}

// The squad goes on to shoot (0x5ca080): two of its gunfire members wait, or its only one does.
[[nodiscard]] inline bool gunfireReady(int32 members, int32 waiting) noexcept
{
    return (members == 1 && waiting == 1) || (members >= 2 && waiting >= 2);
}

// The shoot spots round the gunfire place at graph node `node` (0x5cafa0): nodes within 5 m of it that
// `sees(n)` passes (its point 0.8 m up sees the place), 16 at most; with one or none, `node` too.
template <class Sees>
[[nodiscard]] std::vector<int32> gunfireSpots(const NavGraph& nav, int32 node, Sees&& sees)
{
    std::vector<int32> spots;
    if (node < 0)
        return spots;
    for (int32 n : nav.within(node, kGunfireSpotRadius))
    {
        if (spots.size() >= kGunfireSpots)
            break;
        if (n != node && sees(n))
            spots.push_back(n);
    }
    if (spots.size() <= 1)
        spots.push_back(node);
    return spots;
}

// One gunfire controller's shared state: the place, its spots, and the squad clocks.
struct GunfireSquad {
    Math::Vector3 place{0.0f, 0.0f, 0.0f}; // his last fix, raised kGunfireRise
    int32 node = -1;                       // the place's graph node (+0x488)
    std::vector<int32> spots;              // shoot spots (+0x4b8)
    std::vector<uint8> given;              // spots handed out (+0x4bc)
    float32 waitEnds = -1.0f;              // 3 s after the first waited (+0x4ac)
    bool ready = false;                    // on to the shoot spots (+0x49c)
    float32 quitAt = -1.0f;                // when all quit (+0x4b0)
    float32 releaseAt = -1.0f;             // when the quitters go back (+0x4b4)
    float32 touched = 0.0f;

    // A spot for one member (0x5cb120): a random one not handed out yet (all handed out: they
    // start over); none: the place's node.
    int32 deal(uint32 random)
    {
        const std::size_t n = std::min(spots.size(), given.size());
        std::size_t out = 0;
        for (std::size_t i = 0; i < n; ++i)
            out += given[i] ? 1u : 0u;
        if (out == n)
        {
            std::fill(given.begin(), given.end(), uint8{0});
            out = 0;
        }
        if (n > out)
        {
            std::size_t k = random % (n - out);
            for (std::size_t i = 0; i < n; ++i)
                if (!given[i] && k-- == 0)
                {
                    given[i] = 1;
                    return spots[i];
                }
        }
        return node;
    }
};

// Who of a squad fights now and which cover points are taken (the exe keeps the taken points in
// one set, 0x79bd0c). Entries are refreshed every frame by their hunter; one not refreshed for
// `kStale` seconds (a dead hunter, another level) drops out.
// The five role slots (0x79eeb8 .. 0x79eec8: A B C D E), shared by every squad: a holder, the
// time it is released (0x584200) and the time it is "due" (the getters 0x584300 / 0x584340 /
// 0x5842c0 / 0x584380 give the holder only from then; D at once). A hunter holds one at most.
//   A: seeking cover (0x586800: due now, released in 10 s); re-seeking on a coin (0x5868b0:
//      due in 3 s, released in 4 s).
//   B: re-seeking with one hunter fighting (0x79eea4 == 1); C: re-seeking otherwise.
//   D: advancing when it is the only one advancing (0x79ee84 == 1; released in 4 s, 0x586040).
//   E: entering cover with more than one fighting (due in rand(1.5, 4.5) s, released in 9 s,
//      0x5864b0).
// A member whose role (0..4 = A..E, 5 none: 0x5156f0) changes says line 0x36 / 0x22 / 0x23 /
// 0x24 / 0x25 (0x5138e8).
class RoleSlots {
public:
    enum Role : int32 { A, B, C, D, E, None };
    static constexpr int32 kLines[5] = {0x36, 0x22, 0x23, 0x24, 0x25};

    void expire(float32 now)
    {
        for (Slot& s : slots_)
            if (!s.holder.empty() && now > s.release)
                s.holder.clear();
    }
    void seek(const std::string& who, float32 now) { give(A, who, now, now + 10.0f); }
    void reseek(const std::string& who, float32 now, bool coin, int32 fighting)
    {
        if (coin && give(A, who, now + 3.0f, now + 4.0f))
            return;
        if (fighting == 1 && give(B, who, now + 3.0f, now + 4.0f))
            return;
        give(C, who, now + 3.0f, now + 4.0f);
    }
    void advance(const std::string& who, float32 now, int32 advancing)
    {
        if (advancing == 1)
            give(D, who, now, now + 4.0f);
    }
    void cover(const std::string& who, float32 now, float32 due, int32 fighting)
    {
        if (fighting > 1)
            give(E, who, now + due, now + 9.0f);
    }
    // Its role now (A..E once due, None).
    [[nodiscard]] Role roleOf(const std::string& who, float32 now) const
    {
        for (int32 r = A; r <= E; ++r)
            if (slots_[r].holder == who && now >= slots_[r].due)
                return static_cast<Role>(r);
        return None;
    }
    void clear() { slots_ = {}; }

private:
    struct Slot {
        std::string holder;
        float32 due = 0.0f, release = 0.0f;
    };
    bool give(int32 r, const std::string& who, float32 due, float32 release)
    {
        if (!slots_[r].holder.empty())
            return false;
        for (const Slot& s : slots_)
            if (s.holder == who)
                return false;
        slots_[r] = {who, due, release};
        return true;
    }
    std::array<Slot, 5> slots_{};
};

class SquadRoster {
public:
    static constexpr float32 kStale = 1.0f;

    struct Entry {
        std::string subpack;
        bool fighting = false;
        int32 sub = -1; // its cover-fight sub-state (the exe's numbers), -1 none
        int32 cover = -1;
        float32 seen = 0.0f;
        float32 sawHim = -1e9f; // when it last saw the enemy
        Math::Vector3 sawHimAt{0.0f, 0.0f, 0.0f};
        Gunfire gunfire = Gunfire::None;
    };

    void update(const std::string& hunter, const std::string& subpack, bool fighting, int32 cover, float32 now,
                float32 sawHim = -1e9f)
    {
        Entry& e = entries_[hunter];
        e.subpack = subpack;
        e.fighting = fighting;
        e.cover = cover;
        e.seen = now;
        e.sawHim = sawHim;
    }
    void remove(const std::string& hunter) { entries_.erase(hunter); }
    void setSub(const std::string& hunter, int32 sub) { entries_[hunter].sub = sub; }
    // How many fight, every squad (the fighting list 0x79eea4).
    [[nodiscard]] int32 fightingCount() const
    {
        int32 n = 0;
        for (const auto& [name, e] : entries_)
            n += e.fighting ? 1 : 0;
        return n;
    }
    // How many fight in sub-state `sub`, every squad (the exe's counts 0x79ee68 are global).
    [[nodiscard]] int32 countIn(int32 sub, const std::string& except = {}) const
    {
        int32 n = 0;
        for (const auto& [name, e] : entries_)
            n += (e.sub == sub && name != except) ? 1 : 0;
        return n;
    }
    float32 advancePace = -1e9f; // 0x79eeb4
    void prune(float32 now)
    {
        for (auto it = entries_.begin(); it != entries_.end();)
            if (now - it->second.seen > kStale || it->second.seen > now + kStale)
                it = entries_.erase(it);
            else
                ++it;
        for (auto it = gunfire_.begin(); it != gunfire_.end();)
            if (gunfireMembers(it->first) == 0 && (now - it->second.touched > kStale || it->second.touched > now + kStale))
                it = gunfire_.erase(it);
            else
                ++it;
    }
    // It saw him at `at` (his feet).
    void spotted(const std::string& hunter, const Math::Vector3& at, float32 now)
    {
        Entry& e = entries_[hunter];
        e.sawHim = now;
        e.sawHimAt = at;
    }
    // Where a member of `subpack` last saw him; false: none did.
    [[nodiscard]] bool lastSpot(const std::string& subpack, Math::Vector3& at) const
    {
        float32 t = -1e9f;
        for (const auto& [name, e] : entries_)
            if (e.subpack == subpack && e.sawHim > t)
            {
                t = e.sawHim;
                at = e.sawHimAt;
            }
        return t > -1e9f;
    }

    // The gunfire controller of `subpack` (cAI_ControllerGotoGunfire): its members' states and the
    // shared state, made on first use.
    void setGunfire(const std::string& hunter, Gunfire state)
    {
        if (auto it = entries_.find(hunter); it != entries_.end())
            it->second.gunfire = state;
    }
    [[nodiscard]] int32 gunfireMembers(const std::string& subpack, Gunfire only = Gunfire::None) const
    {
        int32 n = 0;
        for (const auto& [name, e] : entries_)
            n += (e.subpack == subpack && e.gunfire != Gunfire::None && (only == Gunfire::None || e.gunfire == only)) ? 1 : 0;
        return n;
    }
    GunfireSquad& gunfire(const std::string& subpack, float32 now)
    {
        GunfireSquad& g = gunfire_[subpack];
        g.touched = now;
        return g;
    }
    [[nodiscard]] bool hasGunfire(const std::string& subpack) const { return gunfire_.count(subpack) != 0; }
    // Members of `subpack` fighting now, `hunter` left out.
    [[nodiscard]] int32 fighting(const std::string& subpack, const std::string& hunter) const
    {
        int32 n = 0;
        for (const auto& [name, e] : entries_)
            n += (e.fighting && e.subpack == subpack && name != hunter) ? 1 : 0;
        return n;
    }
    // The last time a member of `subpack` saw the enemy (-1e9: none did).
    [[nodiscard]] float32 lastSighting(const std::string& subpack) const
    {
        float32 t = -1e9f;
        for (const auto& [name, e] : entries_)
            if (e.subpack == subpack)
                t = std::max(t, e.sawHim);
        return t;
    }
    // Another hunter holds (or is on its way to) cover point `node`.
    [[nodiscard]] bool taken(int32 node, const std::string& by) const
    {
        for (const auto& [name, e] : entries_)
            if (e.cover == node && name != by)
                return true;
        return false;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_map<std::string, GunfireSquad> gunfire_;
};

} // namespace gun

} // namespace Game
