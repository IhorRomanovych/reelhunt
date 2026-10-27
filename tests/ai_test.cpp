// tests/ai_test.cpp - hunter AI building blocks against Jury_Turf's own data: the navigation
// graph (mapAI.grf), the senses, and the AI set-up the level script builds when it runs.
#include "Assets/grf.h"
#include "Assets/pak.h"
#include "Assets/mls.h"
#include "Game/ai/ai_director.h"
#include "Game/ai/ai_natives.h"
#include "Game/ai/nav_graph.h"
#include "Game/ai/ai_tuning.h"
#include "Game/ai/gun_combat.h"
#include "Game/ai/melee_traits.h"
#include "Game/ai/search.h"
#include "Game/ai/senses.h"
#include "Game/ai/stealth.h"
#include "Game/director_milestones.h"
#include "Game/rats.h"
#include "Game/crows.h"
#include "Game/hand_cam.h"
#include "Game/litter.h"
#include "Game/crane.h"
#include "Game/physics_props.h"
#include "Game/doors.h"
#include "Game/entity_audio.h"
#include "Game/music_ambience.h"
#include "Game/material_types.h"
#include "Game/coronas.h"
#include "Game/sample_levels.h"
#include "Game/weapon_audio.h"
#include "Game/acoustic_probe.h"
#include "Game/player_audio.h"
#include "Game/ped_anims.h"
#include "Game/director_speech.h"
#include "Game/hunter_speech.h"
#include "Assets/rib.h"
#include "Game/camera_splines.h"
#include "Game/execution.h"
#include "Game/items.h"
#include "Game/game_text.h"
#include "Game/gunplay.h"
#include "Game/level_setup.h"
#include "Game/melee_combo.h"
#include "Math/math.h"
#include "Game/script/script_engine.h"
#include "test_paths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (!ok)
        ++g_failures;
}

bool linked(const Game::NavGraph& g, int32_t a, int32_t b)
{
    for (int32_t n : g.node(a).links)
        if (n == b)
            return true;
    return false;
}

} // namespace

int main()
{
    const std::string level = TestPaths::gameRoot() + "levels/jury_turf/";

    std::puts("Senses (exe rules, shipped aiTypeData.ini values)");
    {
        const Game::AiTuning t; // defaults = the shipped files
        const Game::VisionData& v = t.vision;
        check(Game::levelAbove(0.95f, v.visible) == Game::kVeryHigh && Game::levelAbove(0.6f, v.visible) == Game::kMedium &&
                  Game::levelAbove(0.05f, v.visible) == Game::kNoLevel,
              "VISIBLE: >90% very_high, >55% medium, <=10% not seen");
        check(Game::levelAbove(0.7f, v.light) == Game::kVeryHigh && Game::levelAbove(0.2f, v.light) == Game::kMedium &&
                  Game::levelAbove(0.05f, v.light) == Game::kNoLevel,
              "LIGHT: >50% very_high, >6% medium, darker is not seen");
        check(Game::levelBelow(0.1f, v.distance) == Game::kVeryHigh && Game::levelBelow(0.5f, v.distance) == Game::kMedium &&
                  Game::levelBelow(0.9f, v.distance) == Game::kVeryLow,
              "DISTANCE: <20% of 30 m very_high, <60% medium, the rest very_low");

        Game::SightInput in;
        in.distance = 10.0f;
        in.visible = 1.0f;
        in.light = 0.3f; // medium
        // visible 4 + light 2 + distance (10/30 -> high 3) = 9 / 3 = 3
        check(Game::sightLevel(in, v) == Game::kHigh, "the three levels average (4 + 2 + 3) / 3 -> high");
        in.light = 0.7f; // very_high: (4 + 4 + 3) / 3 = 3.67 -> truncated to high
        check(Game::sightLevel(in, v) == Game::kHigh, "the average is truncated, not rounded");
        in.light = 0.03f;
        check(Game::sightLevel(in, v) == Game::kNoLevel, "a player in darkness is not seen at all");
        in.distance = 3.0f;
        in.faceInView = true;
        in.faceLight = 0.03f;
        check(Game::sightLevel(in, v) == Game::kVeryLow, "his face within FACE_m: only the face light counts (3% -> very_low)");
        in.distance = 1.5f;
        check(Game::sightLevel(in, v) == Game::kVeryHigh, "his face within FACE_DEFINITE_m: very_high");
        in = {};
        in.distance = 10.0f;
        in.light = 0.7f;
        in.horizontalAngle = Math::radians(40.0f);
        check(Game::sightLevel(in, v) == Game::kNoLevel, "outside the 75-degree frustum of a standing hunter");
        in.hardDifficulty = true;
        check(Game::sightLevel(in, v) != Game::kNoLevel, "the hard difficulty's 95-degree frustum sees him");
        in.hardDifficulty = false;
        in.distance = 2.5f;
        in.horizontalAngle = Math::radians(80.0f);
        check(Game::sightLevel(in, v) != Game::kNoLevel, "peripheral vision: 170 degrees within 3 m when standing");
        in.gait = Game::Gait::Walk;
        check(Game::sightLevel(in, v) == Game::kNoLevel, "no peripheral vision while walking");
        in = {};
        in.distance = 31.0f;
        in.light = 1.0f;
        check(Game::sightLevel(in, v) == Game::kNoLevel, "nothing beyond FRUSTUM_m (30 m)");

        const Game::HearingData& h = t.hearing;
        const Game::SoundType running = t.sound("RUNNING"), walking = t.sound("WALKING");
        check(Game::hearSound(walking, 4.0f, false, h).interest == Game::kMedium, "footsteps (very_low, 5 m) heard at 4 m");
        check(Game::hearSound(walking, 6.0f, false, h).volume == Game::kNoLevel, "...and not at 6 m");
        check(Game::hearSound(running, 4.0f, false, h).interest == Game::kMedium, "running (low, 15 m) close by: medium interest");
        check(Game::hearSound(running, 12.0f, false, h).interest == Game::kLow,
              "running heard faintly (very_low) is confusable: one interest level less");
        check(Game::hearSound(running, 4.0f, true, h).volume == Game::kNoLevel, "through a wall 15 m less: 11 m left, gone");
        check(std::string(Game::footstepSound(0.73f, false, h)) == "WALKING" && std::string(Game::footstepSound(1.0f, false, h)) == "RUNNING" &&
                  Game::footstepSound(0.3f, false, h) == nullptr,
              "WALK_% 50 / RUN_% 100 of full speed");

        const Game::AlertData& d = t.alert;
        Game::Awareness a;
        Game::updateAwareness(a, Game::kLow, Game::kNoLevel, 0.1f, d);
        check(a.action == Game::AiAction::NotOnAlert, "a low sighting does not alert an idle hunter");
        Game::updateAwareness(a, Game::kNoLevel, Game::kMedium, 0.1f, d);
        check(a.action == Game::AiAction::Investigate && a.alert == Game::kLow, "a medium-interest noise: investigate, alert low");
        Game::updateAwareness(a, Game::kHigh, Game::kNoLevel, 0.1f, d);
        check(a.action == Game::AiAction::Chase && a.alert == Game::kHigh, "a high sighting: chase, alert high");
        Game::updateAwareness(a, Game::kVeryHigh, Game::kNoLevel, 0.1f, d);
        check(a.action == Game::AiAction::Combat && a.alert == Game::kVeryHigh, "very_high while chasing: combat");
        for (int i = 0; i < 9; ++i)
            Game::updateAwareness(a, Game::kNoLevel, Game::kNoLevel, 0.1f, d);
        check(a.action == Game::AiAction::Combat, "combat is kept for SUSTAIN_ACTION (1 s) without a sighting");
        Game::updateAwareness(a, Game::kNoLevel, Game::kNoLevel, 0.3f, d);
        check(a.action == Game::AiAction::Hunt, "...then the hunter hunts (searches) while on alert");
        for (int i = 0; i < 5 * 20 * 10 + 10; ++i)
            Game::updateAwareness(a, Game::kNoLevel, Game::kNoLevel, 0.1f, d);
        check(a.action == Game::AiAction::NotOnAlert && a.alert == Game::kNoLevel,
              "the alert drops a level every 20 s; at none it stands down");
    }

    std::puts("Fist combos (WeaponTypeData.ini fists)");
    {
        const Game::MeleeWeapon fists; // shipped values
        auto len = [](const std::string& clip) { return clip.find("Hard") != std::string::npos ? 1.1f : 0.9f; };
        const float dt = 1.0f / 100.0f;
        // Run `seconds`, recording clip starts and landed blows.
        auto run = [&](Game::MeleeCombo& c, float seconds, std::vector<std::string>& clips, int& blows) {
            for (float t = 0.0f; t < seconds; t += dt)
            {
                const auto st = c.update(dt, fists, true, len);
                if (!st.startClip.empty())
                    clips.push_back(st.startClip);
                if (st.landed)
                    ++blows;
            }
        };
        {
            Game::MeleeCombo c;
            std::vector<std::string> clips;
            int blows = 0;
            c.press();
            c.release();
            run(c, 1.2f, clips, blows);
            check(clips.size() == 1 && clips[0] == "Fist_Stance_Fire_Quick1" && blows == 1, "a tap: one quick punch, one blow");
        }
        {
            Game::MeleeCombo c;
            std::vector<std::string> clips;
            int blows = 0;
            c.press();
            c.release();
            run(c, 0.3f, clips, blows); // inside the 0.24..0.84 s fire window
            c.press();
            c.release();
            run(c, 0.5f, clips, blows); // Quick2 starts at the 0.5 s chain point; 0.3 s into it
            c.press();
            c.release();
            run(c, 1.5f, clips, blows);
            check(clips.size() == 3 && clips[1] == "Fist_Stance_Fire_Quick2" && clips[2] == "Fist_Stance_Fire_Quick3" && blows == 3,
                  "taps inside the fire window chain Quick1 -> Quick2 -> Quick3");
        }
        {
            Game::MeleeCombo c;
            std::vector<std::string> clips;
            int blows = 0;
            c.press();
            c.release();
            run(c, 0.1f, clips, blows); // before FIRE_WINDOW_STARTS (0.24 s)
            c.press();
            c.release();
            run(c, 1.5f, clips, blows);
            check(clips.size() == 1, "a tap before the fire window is dropped");
        }
        {
            Game::MeleeCombo c;
            std::vector<std::string> clips;
            int blows = 0;
            c.press();
            run(c, 0.5f, clips, blows); // held past the hard threshold
            c.release();
            run(c, 1.5f, clips, blows);
            check(clips.size() == 1 && clips[0] == "Fist_Stance_Fire_Hard1" && blows == 1, "holding the button swings Hard1");
        }
        check(Game::voiceSamplePrefix("HOODED_BIG", 0) == "SFX_H2V1_" && Game::voiceSamplePrefix("HOODED_SMALL", 1) == "SFX_H3V2_" &&
                  Game::voiceSamplePrefix("HOODED_MEDIUM", 2) == "SFX_H1V1_" && Game::voiceSamplePrefix("POLICE_MEDIUM", 2) == "SFX_P1V3_" &&
                  Game::voiceSamplePrefix("CERBERUS", 2) == "SFX_C2V1_" && Game::voiceSamplePrefix("PIGGSY", 5) == "SFX_X1V1_" &&
                  Game::voiceSamplePrefix("JOURNALIST", 0) == "SFX_J1V1_" && Game::voiceSamplePrefix("NOBODY", 0).empty(),
              "voices: the class table (0x44b830): HOODED_SMALL/MEDIUM/BIG -> H3/H1/H2, CERBERUS C1V1..C2V1, PIGGSY X1V1");
    }

    std::puts("Melee traits (exe 0x5ae450 records, 0x566a80 situations, 0x5a9a70 responses)");
    {
        const Game::MeleeTraits& hood = Game::meleeTraits(1, false);
        const Game::MeleeTraits& hoodHard = Game::meleeTraits(1, true);
        check(std::string(hood.name) == "hood" && hood.quickPause == 2.4f && hood.hardPause == 8.5f && hood.blockHold == 2.0f &&
                  hood.situations[4][0] == 90.0f && hood.situations[4][4] == 10.0f,
              "hood: quick every 2.4 s, hard 8.5 s, block 2 s; a blow coming: 90 wait / 10 block");
        check(std::string(hoodHard.name) == "hood (hard)" && hoodHard.quickPause == 2.7f &&
                  std::string(Game::meleeTraits(0, true).name) == "training",
              "hard difficulty swaps the gang records 1..12 only");
        check(std::string(Game::meleeTraits(26, false).name) == "default (balanced)" &&
                  std::string(Game::meleeTraits(99, false).name) == "default (balanced)" &&
                  std::string(Game::meleeTraits(22, false).name) == "grappler",
              "records by MTT_ value (out of range: the default)");
        check(Game::defaultMeleeTraits("Hod_BodB1") == 3 && Game::defaultMeleeTraits("hod_bods1") == 1 &&
                  Game::defaultMeleeTraits("Cop_BodM1") == 9 && Game::defaultMeleeTraits("Pig_BodF1") == 12 &&
                  Game::defaultMeleeTraits("Leo_Bod") == Game::kDefaultMeleeTraits,
              "default record by character record (0x41c470)");

        Game::MeleeView v;
        v.distance = 1.0f;
        v.facing = 5.0f;
        v.reach = 1.4f;
        v.hardLands = 0.567f;
        check(Game::classifyMeleeSituation(v) == 2, "idle, close and in front: 2");
        v.facing = 60.0f;
        check(Game::classifyMeleeSituation(v) == 1, "idle, in reach: 1");
        v.distance = 2.0f;
        check(Game::classifyMeleeSituation(v) == 0, "idle, close: 0");
        v.distance = 4.0f;
        check(Game::classifyMeleeSituation(v) == 13, "beyond 2.5 m: 13");
        v.distance = 1.0f;
        v.playerBlocking = true;
        check(Game::classifyMeleeSituation(v) == 3, "he blocks: 3");
        v.playerBlocking = false;
        v.playerSwinging = true;
        v.swingLands = 0.4f;
        v.swingTime = 0.1f;
        check(Game::classifyMeleeSituation(v) == 4, "his blow 0.3 s off: 4");
        v.swingTime = 0.35f;
        check(Game::classifyMeleeSituation(v) == 1, "a quick blow too close to answer: idle");
        v.swingHard = true;
        v.swingLands = 0.567f;
        v.swingTime = 0.5f;
        v.selfBlocking = true;
        check(Game::classifyMeleeSituation(v) == 5, "a hard blow at its guard: 5");
        v.selfBlockAge = 0.1f;
        check(Game::classifyMeleeSituation(v) == 6, "a hard blow as its guard goes up: 6");
        v.selfBlockAge = 1e9f;
        v.swingHard = false;
        v.selfBlocking = false;
        v.swingLands = 0.4f;
        v.swingTime = 0.42f;
        check(Game::classifyMeleeSituation(v) == 7, "just after his blow: a 0.11 s opening (7)");

        std::mt19937 rng(7);
        Game::MeleeRepeats rep;
        int blocks = 0, waits = 0;
        for (int i = 0; i < 40; ++i)
        {
            blocks += Game::chooseMeleeResponse(Game::meleeTraits(28, false), rep, 4, true, true, true, rng) == Game::MeleeResponse::Block;
            waits += Game::chooseMeleeResponse(Game::meleeTraits(30, false), rep, 1, true, true, true, rng) == Game::MeleeResponse::Wait;
        }
        check(blocks == 40 && waits == 40, "block only blocks a coming blow; quick only waits when quick is ruled out");
        // Default (balanced): limit 0, scale 0 - with quick allowed, a repeated blow takes its
        // block weight away (0x5a9c80); with quick ruled out there is no reweighting.
        Game::MeleeRepeats rep2;
        int allowed = 0, ruledOut = 0;
        for (int i = 0; i < 200; ++i)
        {
            allowed += Game::chooseMeleeResponse(Game::meleeTraits(26, false), rep2, 4, false, true, true, rng) == Game::MeleeResponse::Block;
            ruledOut += Game::chooseMeleeResponse(Game::meleeTraits(26, false), rep2, 4, true, true, true, rng) == Game::MeleeResponse::Block;
        }
        check(allowed == 0 && ruledOut > 30 && ruledOut < 90, "repeats reweight only past the record's limit (default: 30% block)");
        int sizes[4] = {};
        for (int i = 0; i < 400; ++i)
            ++sizes[std::min<std::size_t>(3, Game::chooseMeleeCombo(hood, rng).size())];
        check(sizes[1] > 150 && sizes[2] > 80 && sizes[3] > 10, "hood combos: one blow 55%, two 35%, three 10%");
        // The footwork's roll (0x5a9b40(4)): the hood's "his blow is coming" row, 90 wait / 10 block.
        int rolled = 0, others = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const Game::MeleeResponse r = Game::pickMeleeResponse(hood, 4, rng);
            rolled += r == Game::MeleeResponse::Block;
            others += r != Game::MeleeResponse::Block && r != Game::MeleeResponse::Wait;
        }
        check(others == 0 && rolled > 60 && rolled < 140, "hood repositions on about 1 roll in 10 (row 4: 90 wait / 10 block)");
        // The free decision (0x5a9ec0 over +0x164..+0x170): the hood 80 stand off / 20 close in; the
        // default record (26) 45 / 45 / 0 / 40, and 60 in 100 it decides afresh after backing away.
        std::array<int, 4> hoodMoves{}, defMoves{};
        for (int i = 0; i < 1000; ++i)
        {
            ++hoodMoves[static_cast<std::size_t>(Game::pickFootwork(Game::meleeFootwork(1, false), rng))];
            ++defMoves[static_cast<std::size_t>(Game::pickFootwork(Game::meleeFootwork(26, false), rng))];
        }
        check(hoodMoves[1] == 0 && hoodMoves[2] == 0 && hoodMoves[0] > 730 && hoodMoves[0] < 870, "hood footwork: 80 stand off, 20 close in");
        check(defMoves[2] == 0 && defMoves[1] > 280 && defMoves[1] < 420 && Game::meleeFootwork(26, false).redecide == 60.0f,
              "default footwork: circles about 1 in 3 (45 / 45 / 0 / 40), re-decides 60 in 100");
    }

    std::puts("Gunplay (Game/gunplay.h: zones, damage, lock-on, recoil, ammo)");
    {
        const Game::Firearm nail; // defaults = the shipped nailGun record
        const Math::Vector3 feet{0.0f, 0.0f, 0.0f};
        // A hunter at the origin facing -Z (yaw 0; +X is its right); shots from 10 m in front of it along +Z.
        const Math::Vector3 ahead{0.0f, 0.0f, 1.0f};
        auto shootAt = [&](float32 height, float32 across) {
            return Game::shotHitsBody(feet, 0.0f, Math::Vector3{across, height, -10.0f}, ahead, 16.0f);
        };
        const Game::BodyHit chest = shootAt(1.2f, 0.0f), head = shootAt(1.7f, 0.0f), leg = shootAt(0.5f, -0.1f),
                            arm = shootAt(1.2f, 0.25f), miss = shootAt(1.2f, 0.6f), over = shootAt(2.0f, 0.0f);
        check(chest.zone == Game::HitZone::Torso && std::fabs(chest.distance - 9.7f) < 1e-3f && chest.fromFront &&
                  head.zone == Game::HitZone::Head && leg.zone == Game::HitZone::Leg && leg.left &&
                  arm.zone == Game::HitZone::Arm && !arm.left && miss.zone == Game::HitZone::None && over.zone == Game::HitZone::None,
              "hit zones: chest -> torso at 9.7 m, head above 1.5 m, legs below 0.9 m, arms beside the spine, misses");
        check(Game::shotHitsBody(feet, 0.0f, Math::Vector3{0.0f, 1.2f, -20.0f}, ahead, 16.0f).zone == Game::HitZone::None &&
                  !Game::shotHitsBody(feet, Math::pi, Math::Vector3{0.0f, 1.2f, -10.0f}, ahead, 16.0f).fromFront,
              "a shot stops at the shot's SIZE (16 m); one from behind is not from the front");
        check(std::fabs(Game::zoneDamage(nail, Game::HitZone::Torso, 100.0f) - 14.5f) < 1e-4f &&
                  std::fabs(Game::zoneDamage(nail, Game::HitZone::Leg, 100.0f) - 7.25f) < 1e-4f &&
                  Game::zoneDamage(nail, Game::HitZone::Head, 100.0f) * 3.0f >= 100.0f &&
                  Game::zoneDamage(nail, Game::HitZone::Head, 100.0f) * 2.0f < 100.0f,
              "damage: 14.5 to the torso, LEG_DAMAGE_MULTIPLIER 0.5, NUM_HEAD_SHOTS 3 head shots kill");
        check(Game::lockOnHead(nail, 4.0f, false) && !Game::lockOnHead(nail, 6.0f, false) && Game::lockOnHead(nail, 6.0f, true) &&
                  !Game::lockOnHead(nail, 9.0f, true),
              "LOCKON_TARGET_RANGES 5 / 8: the head by itself within 5 m, on request within 8 m");
        const Game::PlayerAim aim;
        const Math::Vector3 eye{0.0f, 1.6f, 0.0f};
        check(Game::inLockonCone(aim, eye, 0.0f, 0.0f, Math::Vector3{3.0f, 1.2f, -10.0f}) &&
                  !Game::inLockonCone(aim, eye, 0.0f, 0.0f, Math::Vector3{12.0f, 1.2f, -10.0f}) &&
                  !Game::inLockonCone(aim, eye, 0.0f, 0.0f, Math::Vector3{0.0f, 1.2f, 10.0f}) &&
                  !Game::inLockonCone(aim, eye, 0.0f, 0.0f, Math::Vector3{0.0f, 1.2f, -31.0f}),
              "lock-on: within AIM_LOCKON_DIST 30 m and the 90 degree cone, not behind or beside him");
        std::mt19937 rng(7);
        float32 widest = 0.0f;
        for (int i = 0; i < 200; ++i)
            widest = std::max(widest, std::acos(std::min(1.0f, Game::spreadDirection(ahead, 5.0f, rng).dot(ahead))));
        check(widest > Math::radians(1.5f) && widest <= Math::radians(2.5f) + 1e-4f, "SHOT_SPREAD 5: pellets within 2.5 degrees of the aim");
        Game::Recoil r;
        r.kick(nail, 1.0f, false);
        const float32 up = r.pitch();
        r.update(0.1f);
        const float32 half = r.pitch();
        r.update(0.15f);
        Game::Recoil crouched;
        crouched.kick(nail, 0.5f, false);
        Game::Firearm uzi = nail;
        uzi.recoilAccumulator = 2.5f;
        uzi.maxRecoilUp = 11.0f;
        Game::Recoil climb;
        for (int i = 0; i < 6; ++i)
            climb.kick(uzi, 1.0f, false);
        check(std::fabs(up - Math::radians(3.0f)) < 1e-5f && std::fabs(half - Math::radians(1.5f)) < 1e-4f && r.pitch() == 0.0f &&
                  std::fabs(crouched.pitch() - Math::radians(1.5f)) < 1e-5f && std::fabs(climb.pitch() - Math::radians(11.0f)) < 1e-5f,
              "recoil: RECOIL_DIST 3 degrees back over RECOIL_TIME 0.2 s, CROUCH_RECOIL halves it, the uzi climbs to 11");
        Game::GunAmmo ammo;
        ammo.set(nail, "nailgun", 35);
        const bool split = ammo.loaded("nailgun") == 30 && ammo.spare("nailgun") == 5;
        bool fired = true;
        for (int i = 0; i < 30; ++i)
            fired = fired && ammo.fire("nailgun");
        const bool dry = !ammo.fire("nailgun");
        const int32 moved = ammo.reload(nail, "nailgun");
        check(split && fired && dry && moved == 5 && ammo.loaded("nailgun") == 5 && ammo.spare("nailgun") == 0 &&
                  !ammo.canReload(nail, "glock"),
              "ammo (exe 0x4f8b40): 35 rounds are a clip of 30 and 5 spare; empty after 30; a reload moves what "
              "is left; each gun its own rounds");
        Game::GunAmmo shells;
        shells.set(nail, "shotgun", 32);
        for (int i = 0; i < 28; ++i)
            shells.fire("shotgun");
        const bool one = shells.reloadOne(nail, "shotgun") && shells.loaded("shotgun") == 3 && shells.spare("shotgun") == 1;
        const bool last = shells.reloadOne(nail, "shotgun") && !shells.reloadOne(nail, "shotgun");
        check(one && last && shells.loaded("shotgun") == 4, "a round at a time: one from the spare per turn, none once it is empty");
        Game::GunAmmo few;
        few.set(nail, "nailgun", 10);
        const bool ten = few.loaded("nailgun") == 10 && few.spare("nailgun") == 0;
        const bool took = few.takeRounds(nail, "nailgun", 1000);
        const bool capped = few.spare("nailgun") == 150 && few.full(nail, "nailgun");
        const bool refused = !few.addBox(nail, "nailgun", 10);
        Game::GunAmmo lots;
        lots.set(nail, "nailgun", 1000);
        check(ten && took && capped && refused && lots.loaded("nailgun") == 30 && lots.spare("nailgun") == 150,
              "MAX_CLIPS 6 x 30 = 180 rounds in all, 150 of them spare; a full gun takes no box or second gun");
        const Game::AmmoBox* nails = Game::ammoBox("CT_AMMO_NAILS");
        const Game::AmmoBox* pistol = Game::ammoBox("ct_ammo_pistol");
        const Game::AmmoBox* mgun = Game::ammoBox("CT_AMMO_MGUN");
        check(nails && nails->rounds == 10 && std::string(nails->guns[0]) == "CT_NAILGUN" && !nails->guns[1] && pistol &&
                  pistol->rounds == 12 && std::string(pistol->guns[2]) == "CT_DESERT_EAGLE" && mgun && mgun->rounds == 30 &&
                  Game::ammoBox("CT_AMMO_SHOTGUN")->rounds == 8 && Game::ammoBox("CT_AMMO_SNIPER")->rounds == 6 &&
                  !Game::ammoBox("CT_BAG"),
              "ammo boxes (exe 0x463ed0): 10 nails, 8 shells, 12 pistol, 30 mgun, 6 tranq / sniper rounds, by gun");
        Game::Firearm rifle = nail;
        rifle.grip = Game::Firearm::Grip::Rifle;
        Game::Firearm shotgun = nail;
        shotgun.grip = Game::Firearm::Grip::TwoHanded;
        check(nail.aimClip(false) == "1Handed_Aim_T" && nail.aimClip(true) == "1Handed_ZoomAim_T" &&
                  nail.zoomMoveClip() == "1Handed_ZoomAim_Move_T" && rifle.aimClip(false) == "Sniper_Aim_T" &&
                  rifle.aimClip(true) == "Sniper_ZoomAim_T" && rifle.zoomMoveClip().empty() &&
                  shotgun.aimClip(true) == "2Handed_ZoomAim_T",
              "aim clips as the exe's tables name them: 1Handed_, 2Handed_, Sniper_ (no zoomed walk for the rifles)");

        // A hunter's skeleton as the game poses it standing (Ski_BodM1, read off the running game),
        // feet at the origin, facing -X: its left side is +Z.
        const std::unordered_map<int16, Math::Vector3> pose{
            {1045, {0.007f, 1.119f, -0.002f}}, {1094, {0.007f, 1.242f, 0.0f}},   {1096, {0.021f, 1.506f, 0.004f}},
            {1040, {0.036f, 1.679f, 0.007f}},  {1001, {0.012f, 1.768f, 0.004f}}, {1003, {0.030f, 1.655f, 0.058f}},
            {1039, {0.019f, 1.561f, 0.232f}},  {1020, {-0.016f, 1.304f, 0.331f}}, {1021, {-0.181f, 1.051f, 0.343f}},
            {1006, {-0.264f, 0.906f, 0.336f}}, {1057, {0.042f, 1.655f, -0.044f}}, {1093, {0.075f, 1.562f, -0.215f}},
            {1074, {0.087f, 1.305f, -0.321f}}, {1075, {0.002f, 1.023f, -0.389f}}, {1060, {-0.041f, 0.865f, -0.424f}},
            {1023, {-0.009f, 1.119f, 0.104f}}, {1002, {-0.198f, 0.593f, 0.090f}}, {1019, {-0.069f, 0.129f, 0.054f}},
            {1024, {-0.252f, -0.004f, 0.084f}}, {1077, {0.023f, 1.121f, -0.106f}}, {1056, {-0.143f, 0.595f, -0.196f}},
            {1073, {0.253f, 0.320f, -0.170f}}, {1078, {0.183f, 0.104f, -0.189f}},
        };
        const std::vector<Game::BodyPart> parts = Game::bodyParts(pose);
        const float32 facingX = Math::pi * 0.5f;
        auto at = [&](float32 height, float32 across) {
            return Game::shotHitsParts(parts, feet, facingX, Math::Vector3{-10.0f, height, across}, Math::Vector3{1.0f, 0.0f, 0.0f},
                                       16.0f);
        };
        const Game::BodyHit pHead = at(1.85f, 0.0f), pChest = at(1.35f, 0.0f), pArm = at(1.43f, 0.28f), pLeg = at(0.85f, -0.15f),
                            pMiss = at(1.3f, 0.6f), pOver = at(2.1f, 0.0f);
        auto mapped = pose;
        mapped.erase(1001);
        check(parts.size() == 17 && pHead.zone == Game::HitZone::Head && pChest.zone == Game::HitZone::Torso &&
                  pChest.fromFront && pChest.distance > 9.7f && pChest.distance < 10.0f && pArm.zone == Game::HitZone::Arm &&
                  pArm.left && pLeg.zone == Game::HitZone::Leg && !pLeg.left && pMiss.zone == Game::HitZone::None &&
                  pOver.zone == Game::HitZone::None && Game::bodyParts(mapped).empty(),
              "per-bone hit test: capsules on the posed biped bones (head, torso, left arm, right thigh, misses)");
        check(Game::inventorySlot("BACK") == 1 && Game::inventorySlot("belt_right") == 2 && Game::inventorySlot("BELT_LEFT") == 3 &&
                  Game::inventorySlot("BELT_REAR") == 4 && Game::inventorySlot("SPECIAL") == 5 && Game::inventorySlot("") == 0,
              "inventory slots by HOLSTER_SLOT (exe 0x440158): BACK 1, BELT_RIGHT 2, BELT_LEFT 3, BELT_REAR 4, SPECIAL 5");
        Game::AiDirector squads;
        squads.addSubpack("LEADER", "GUNS");
        squads.addSubpack("LEADER", "FISTS");
        squads.addHunterToSubpack("LEADER", "GUNS", "hGun");
        squads.addHunterToSubpack("LEADER", "FISTS", "hFist");
        squads.setCombatType("LEADER", "GUNS", Game::CombatType::Cover);
        check(squads.combatTypeOf("hgun") == Game::CombatType::Cover && squads.combatTypeOf("hFist") == Game::CombatType::Unset &&
                  squads.combatTypeOf("nobody") == Game::CombatType::Unset,
              "AISetSubpackCombatType: a subpack's combat type reaches its hunters (none set: unset)");
    }

    std::puts("Gun hunters (exe cAI_ControllerCombatCover, CPedActGunCoverCombat, CPedActCoverSeek)");
    {
        namespace gun = Game::gun;
        check(gun::squadBand(7.9f) == 0 && gun::squadBand(8.0f) == 1 && gun::squadBand(19.0f) == 2 && gun::squadBand(24.9f) == 3 &&
                  gun::squadBand(30.0f) == 4 && gun::squadBand(36.0f) == 5,
              "squad distance bands <8, <16, <20, <25, <36 m (0x4fd2e0)");
        check(gun::squadOrder(20.0f, false, false, 5) == gun::SquadOrder::Fight &&
                  gun::squadOrder(20.0f, false, false, 6) == gun::SquadOrder::Keep &&
                  gun::squadOrder(20.0f, true, false, 9) == gun::SquadOrder::Fight,
              "within 25 m: a sixth hunter joins the fight, a seventh gets no order, one fighting goes on");
        check(gun::squadOrder(30.0f, true, false, 2) == gun::SquadOrder::Chase && gun::squadOrder(30.0f, true, true, 2) == gun::SquadOrder::Fight &&
                  gun::squadOrder(30.0f, false, false, 6) == gun::SquadOrder::StayBack &&
                  gun::squadOrder(40.0f, false, false, 6) == gun::SquadOrder::Chase,
              "25..36 m: chase unless on the way to cover; six fighting: stay back; beyond 36 m: chase");
        const auto band = gun::fightBand();
        check(band.first == 3.0f && band.second == 20.0f && gun::fightBand(5.0f, 6.0f).second == 7.0f &&
                  gun::fightBand(0.0f, 1.0f).second == 2.0f,
              "the hunter fights 3..20 m out, the band kept 2 m wide (0x583630 / 0x583720)");
        check(gun::kFirstDecision == 1.5f && gun::kCoverNear == 1.5f && gun::kCoverArrive == 0.75f && gun::kCoverSpoiled == 3.0f &&
                  gun::kHideMin == 1.5f && gun::kHideMax == 3.0f && gun::kStandTime == 3.0f && gun::kEnemyEye == 1.83f,
              "cover fight timings and distances (0x79ed58..): decide 1.5 s, cover 1.5 m out, in cover 1.5..3 s, stand 3 s");
        check(gun::bodyKind("Cop_BodM1") == 1 && gun::bodyKind("Cer_BodB1") == 2 && gun::bodyKind("Pig_BodF1") == 6 &&
                  gun::bodyKind("Hos_Bod2") == 3 && gun::bodyKind("Hod_BodB1") == 0 && gun::bodyKind("Smi_BodB1") == 0,
              "the ped stats' kind by body (0x41ca40): cops 1, Cerberus 2, Piggsy 6, hostages 3, the rest 0");

        // A row of nodes 2 m apart along x (scene x), the enemy at x = 0; a wall hides x in 6..10.
        Assets::GrfData row;
        for (int i = 0; i < 12; ++i)
        {
            Assets::GrfNode n;
            n.name = std::to_string(i);
            n.position = Math::Vector3{2.0f * static_cast<float>(i), 0.0f, 0.0f};
            n.radius = 0.5f;
            if (i > 0)
                n.links.push_back({i - 1, 3, {}});
            if (i < 11)
                n.links.push_back({i + 1, 3, {}});
            row.nodes.push_back(n);
        }
        const Game::NavGraph line(row, Game::LevelFrame{});
        {
            // Routes are the fewest links (0x4046d0), not the shortest way: 0 -> 3 straight is one
            // long link; round by 1 and 2 it is shorter but three links.
            Assets::GrfData sq;
            const Math::Vector3 at[4] = {{0, 0, 0}, {1, 0, 4}, {9, 0, 4}, {10, 0, 0}};
            const std::vector<std::vector<int32_t>> links = {{1, 3}, {0, 2}, {1, 3}, {2, 0}};
            for (int i = 0; i < 4; ++i)
            {
                Assets::GrfNode nd;
                nd.name = "s" + std::to_string(i);
                nd.position = at[i];
                nd.radius = 0.5f;
                for (const int32_t l : links[static_cast<std::size_t>(i)])
                    nd.links.push_back({l, 3, {}});
                sq.nodes.push_back(nd);
            }
            sq.nodes[0].position = Math::Vector3{0.0f, 0.0f, 0.0f};
            const Game::NavGraph g4(sq, Game::LevelFrame{});
            const auto r = g4.route(0, 2);
            check(g4.route(0, 3) == std::vector<int32_t>{0, 3} && r.size() == 3 && r.front() == 0 && r.back() == 2,
                  "routes: breadth first, the fewest links (0x4046d0 / 0x4049c0)");
        }
        // The hunter at x = 16 (node 8): its nearby list is 8, then 7 and 9 (2 m), then 6 and 10 (4 m);
        // 5 and 11 are 6 m off, beyond 5.2 m. His eye sees everything but x 12 .. 14 (nodes 6, 7).
        const Math::Vector3 enemy{0.0f, 0.0f, 0.0f}, hunter{16.0f, 0.0f, 0.0f};
        const auto nearby = line.ranked(8, hunter, gun::kNearbyRadius, gun::kNearbyCap);
        check(nearby.size() == 5 && nearby[0] == std::make_pair(8, 0) && nearby[1].second == 1 && nearby[4].second == 2,
              "nearby list: breadth first within 5.2 m, ranked by link depth (0x563fd0)");
        auto seen = [&](int32_t node) { const float x = line.node(node).position.x; return x < 12.0f || x > 14.0f; };
        auto none = [](int32_t) { return false; };
        const int32_t first = gun::findCover(line, hunter, enemy, gun::kCoverNear, 19.0f, seen, none);
        check(first == 7, "cover: the hidden point of the lowest depth nearest the hunter (x = 14)");
        auto taken = [&](int32_t node) { return node == first; };
        check(gun::findCover(line, hunter, enemy, gun::kCoverNear, 19.0f, seen, taken) == 6,
              "a point a squad mate took is not given out again (0x79bd0c); the next depth then");
        check(gun::findCover(line, hunter, enemy, gun::kCoverNear, 13.0f, seen, none) == -1,
              "the picked point outside the band from him: no cover (no other point is tried)");
        check(gun::findExit(line, hunter, enemy, gun::kCoverNear, 19.0f, seen, none) == 9,
              "exit: the nearest point his eye sees (x = 18, depth 1)");
        auto none2 = [](int32_t) { return false; };
        check(gun::findCoverAlong(line, hunter, enemy, Math::Vector3{1.0f, 0.0f, 0.0f}, 0.0f, gun::kCoverNear, 19.0f, none2, none2) == 9 &&
                  gun::findCoverAlong(line, hunter, enemy, Math::Vector3{-1.0f, 0.0f, 0.0f}, 0.0f, gun::kCoverNear, 19.0f, none2, none2) == 7,
              "modes 1 / 0: the hidden point of the lowest depth farthest away from him / toward him (0x559210)");
        check(gun::weaponRange(39) == std::make_pair(7.5f, 10.0f) && gun::weaponRange(44) == std::make_pair(7.5f, 9.0f) &&
                  gun::weaponRange(48) == std::make_pair(15.0f, 60.0f) && gun::weaponRange(43) == std::make_pair(7.0f, 15.0f),
              "the guns' near / far ranges (0x551220: 6Shooter 7.5..10, shotgun 7.5..9, sniper 15..60, Uzi 7..15)");
        {
            gun::SquadRoster r;
            r.update("a", "s1", true, -1, 0.0f);
            r.update("b", "s2", true, -1, 0.0f);
            r.setSub("a", 7);
            r.setSub("b", 7);
            check(r.countIn(7) == 2 && r.countIn(7, "a") == 1 && r.countIn(5) == 0,
                  "per-sub-state counts span every squad (0x79ee68), itself left out on request");
        }

        gun::SquadRoster roster;
        roster.update("hA", "subInBar", true, 5, 10.0f);
        roster.update("hB", "subInBar", true, -1, 10.0f);
        roster.update("hC", "subBarGuard", true, 7, 10.0f);
        check(roster.fighting("subInBar", "hA") == 1 && roster.fighting("subInBar", "hX") == 2 && roster.taken(5, "hB") &&
                  !roster.taken(5, "hA"),
              "the roster counts a subpack's fighters and knows whose cover point is whose");
        roster.update("hA", "subInBar", true, 5, 10.8f);
        roster.prune(11.5f);
        check(roster.size() == 1 && !roster.taken(7, "hA"), "a hunter not seen for a second drops out with its cover point");

        // Leaning out (CPedActPeek 0x571c20).
        check(gun::kPeekReach == 20.0f && gun::kPeekNear == 1.0f && gun::kPeekChest == 1.2f && gun::kPeekHead == 1.7f &&
                  gun::kPeekSide == 1.5f && gun::kPeekUpOut == 3.0f && gun::kPeekLeanOut == 2.0f && gun::kPeekSettle == 2.0f &&
                  gun::kPeekCooldown == 4.0f,
              "peek: him within 20 m, chest 1.2 m to head 1.7 m, 1.5 m side probe, out 3 s up / 2 s leaning, 4 s apart");
        std::vector<float> probes;
        auto onlyLeft = [&](float offset) { probes.push_back(offset); return offset < 0.0f; };
        check(gun::peekSide(10.0f, onlyLeft) == gun::PeekSide::Left && probes.size() == 3 && probes[0] == 0.0f &&
                  probes[1] == gun::kPeekSide && probes[2] == -gun::kPeekSide,
              "peek: over the top first, then 1.5 m right, then left");
        check(gun::peekSide(10.0f, [](float) { return true; }) == gun::PeekSide::Up &&
                  gun::peekSide(10.0f, [](float o) { return o > 0.0f; }) == gun::PeekSide::Right &&
                  gun::peekSide(21.0f, [](float) { return true; }) == gun::PeekSide::None &&
                  gun::peekSide(10.0f, [](float) { return false; }) == gun::PeekSide::None,
              "peek: the first clear line wins; none beyond 20 m or with every line blocked");

        // Blind fire (cAI_ControllerCombatBlind / CPedActBlindGunCombat).
        check(gun::blindOrder(7.0f) == gun::BlindOrder::Fire && gun::blindOrder(15.9f) == gun::BlindOrder::Fire &&
                  gun::blindOrder(16.0f) == gun::BlindOrder::GetCloser && gun::blindOrder(40.0f) == gun::BlindOrder::GetCloser,
              "blind: fire within 16 m of where he was (bands 0, 1), get closer beyond (0x59fc47)");
        check(gun::kBlindMoved == 0.2f && gun::kBlindReorder == 1.0f && gun::kBlindOrder == 5.0f && gun::kBlindFacing == 0.9f,
              "blind: a new place past 0.2 sq m, closer at most every 1 s, 5 s orders, fire facing within acos 0.9");

        // The lost enemy (cAI_ControllerGotoGunfire).
        check(gun::gunfireWaitIndex(25) == 5 && gun::gunfireWaitIndex(20) == 0 && gun::gunfireWaitIndex(19) == -1 &&
                  gun::gunfireWaitIndex(0) == -1,
              "gunfire: wait at the route node 20 short of the place; a shorter route waits where it is (0x5cab10)");
        check(gun::gunfireReady(1, 1) && !gun::gunfireReady(1, 0) && !gun::gunfireReady(3, 1) && gun::gunfireReady(3, 2) &&
                  gun::gunfireReady(2, 2) && !gun::gunfireReady(0, 0),
              "gunfire: on to the spots once two wait, or the only member does (0x5ca10a)");
        check(gun::kGunfireWaitNodes == 20 && gun::kGunfireWait == 3.0f && gun::kGunfireLook == 3.0f && gun::kGunfireQuit == 4.0f &&
                  gun::kGunfireSpotRadius == 5.0f && gun::kGunfireSpots == 16 && gun::kGunfireRise == 0.6f &&
                  gun::kGunfireSpotEye == 0.8f,
              "gunfire timings and distances: wait 3 s, quit 3 s on, stand 4 s, spots within 5 m (16), seen from 0.8 m");
        // Row nodes 2 m apart: node 0's spots within 5 m are nodes 1 and 2.
        auto all = [](int32_t) { return true; };
        const std::vector<int32_t> spots = gun::gunfireSpots(line, 0, all);
        check(spots.size() == 2 && std::count(spots.begin(), spots.end(), 1) == 1 && std::count(spots.begin(), spots.end(), 2) == 1,
              "gunfire spots: the nodes within 5 m of the place that see it, not its own node");
        check(gun::gunfireSpots(line, 0, none) == std::vector<int32_t>{0} &&
                  gun::gunfireSpots(line, 0, [](int32_t n) { return n == 2; }) == (std::vector<int32_t>{2, 0}),
              "gunfire spots: with one or none, the place's own node is added (0x5cb0f1)");
        gun::GunfireSquad g;
        g.node = 0;
        g.spots = {4, 5, 6};
        g.given.assign(3, 0);
        const int32_t s1 = g.deal(7), s2 = g.deal(7), s3 = g.deal(7), s4 = g.deal(1);
        check(s1 != s2 && s2 != s3 && s1 != s3 && s4 >= 4 && s4 <= 6, "gunfire: no spot is handed out twice until all were (0x5cb120)");
        gun::GunfireSquad empty;
        empty.node = 9;
        check(empty.deal(3) == 9, "gunfire: no spots, the place's node");
        roster.update("hA", "subInBar", false, -1, 11.6f);
        roster.update("hB", "subInBar", false, -1, 11.6f);
        roster.setGunfire("hA", gun::Gunfire::Wait);
        roster.setGunfire("hB", gun::Gunfire::GotoWait);
        roster.spotted("hB", Math::Vector3{3.0f, 0.0f, 4.0f}, 11.6f);
        Math::Vector3 last{0.0f, 0.0f, 0.0f};
        check(roster.gunfireMembers("subInBar") == 2 && roster.gunfireMembers("subInBar", gun::Gunfire::Wait) == 1 &&
                  roster.lastSpot("subInBar", last) && last.x == 3.0f && last.z == 4.0f && !roster.lastSpot("subNone", last),
              "the roster counts a squad's gunfire members and keeps its last fix on him");
    }

    std::puts("Stealth (exe 0x49a6f0: SAFE_ZONE_% and dark graph nodes)");
    {
        const float safe = Game::AiTuning{}.vision.safeZone;
        check(Game::effectivePlayerLight(0.03f, true, safe) < safe, "dark next to a dark graph node: hidden");
        check(Game::effectivePlayerLight(0.03f, false, safe) > 1.0f, "dark but nowhere designed to hide: fully visible");
        check(Game::effectivePlayerLight(0.4f, true, safe) == 0.4f, "in the light the floor light is kept");
    }

    std::puts("Leaders' enemies, hostages, buddies (AIAddLeaderEnemy, AISetHunterAsHostage, AIDefineGoalBeBuddy)");
    {
        Game::AiDirector d;
        d.addHunterToSubpack("LEADER", "sStreet", "Street1");
        d.addHunterToSubpack("LEADER", "sStreet", "Street2");
        d.addHunterToSubpack("Journalist", "JournoPack", "Journalist");
        d.addHunterToSubpack("hLonely", "sLonely", "Lonely");
        d.addLeaderEnemy("LEADER", "player");
        d.addPackAsEnemies("Journalist", "LEADER");
        check(d.hostileToPlayer("street1") && !d.hostileToPlayer("journalist") && d.hostileToPlayer("lonely"),
              "after him only when its leader has him as an enemy (a leader with none: still after him, ours)");
        check(d.isEnemy("journalist", "street2") && !d.isEnemy("journalist", "player"),
              "AIAddAllHuntersInPackAsLeaderEnemies: the pack's hunters become the leader's enemies");
        d.setHostage("Street2", true);
        check(!d.hostileToPlayer("street2"), "a hostage is after no one");
        d.removeLeaderEnemy("LEADER", "player");
        check(!d.hostileToPlayer("street1"), "AIRemoveLeaderEnemy");
        check(d.subpackMember("sStreet", 2) == "street2" && d.subpackMember("sStreet", 3).empty() && d.isInSubpack("sStreet", "Street1"),
              "AIReturnSubpackEntityName counts from 1; AIIsInSubPack");
        Game::GoalDef g;
        g.name = "gBuddy";
        g.type = Game::GoalType::BeBuddy;
        d.defineGoal(g);
        d.addGoalToSubpack("Journalist", "JournoPack", "gBuddy");
        check(d.goalFor("journalist", false) != nullptr, "the buddy goal is its goal");
        d.deleteGoal("gBuddy");
        check(d.goalFor("journalist", false) == nullptr, "AIDeleteGoalDefinition takes it off every subpack too");
    }

    std::puts("Hunter speech (read from the exe)");
    {
        Game::HunterSpeech hs;
        if (TestPaths::exeTablesDir().empty() || !hs.load(TestPaths::exeTablesDir() + "hunter_speech.txt"))
            std::puts("  skip  no game executable");
        else
        {
            int32_t voice = -1, slot = -1;
            check(hs.voiceOf("SFX_P2V1_") == 15 && hs.voiceOf("SFX_H1V1_") == 0 && hs.find("SFX_H1V1_TAUNT_CHASE", voice, slot) &&
                      voice == 0 && slot == 12,
                  "voices in VOICE_ id order (POLICE2VOICE1 15); contexts by sample stem (TAUNT_CHASE slot 12)");
            check(hs.specificSlot(68) == 54 && hs.specificSlot(65) == 51 && hs.specificSlot(87) == 54 && hs.specificSlot(500) == -1,
                  "PlayHunterSpeech ids -> SPECIFIC_SPEECH slots (0x412060: P2V1 man down 68 -> 4th, getcover 87 -> 4th)");
            std::mt19937 rng(3);
            std::set<std::string> said;
            for (int i = 0; i < 3; ++i)
                if (const auto l = hs.pick(15, 51, 0.0f, rng))
                    said.insert(*l);
            check(said.size() == 3 && said.count("SFX_P2V1_SCRIPTED_JOURNO_1") == 1,
                  "a short range says each line once before any comes again (JOURNO_1..3)");
            // Every line said within the minute: the next pick repeats one, and is held back.
            const auto again = hs.pick(15, 51, 10.0f, rng);
            check(!again, "a vocal sample said within the last 60 s is not said again (nothing is said)");
        }
    }

    std::puts("Ped clip behaviours (read from the exe)");
    {
        Game::PedAnims pa;
        if (TestPaths::exeTablesDir().empty())
            std::puts("  skip  no game executable");
        else if (!pa.load(TestPaths::exeTablesDir() + "ped_anims.txt"))
            check(false, "the ped clip table loads");
        else
        {
            check(pa.clipId("Wall_Fire_1Handed_Left") == 0x339 && pa.clipId("Idle_Celebrate_01") == 0x9d,
                  "ped clips: ids from the name table 0x6851c0");
            check(pa.blend("Sneak_Stand", 0.0f) == 0.4f && pa.blend("Sneak_Turn", 0.0f) == 0.2f && pa.blend("Jump_Launch", 0.0f) == 0.1f &&
                      pa.blend("DeadBody_Pickup_Fwd", 0.0f) == 0.6f && pa.blend("Not_A_Clip", 0.33f) == 0.33f,
                  "ped clips: each behaviour's blend (table 0x68f210)");
            check(pa.blendInto("Wall_Walk_Left", "Wall_Walk_Right", 0.15f) == 0.15f && pa.blendInto("Stand_Turn_Left", "Sneak_Stand", 0.15f) == 0.4f,
                  "ped clips: the blend only on a change of behaviour");
        }
    }
    std::puts("Director speech (read from the exe)");
    {
        Game::DirectorSpeech d;
        std::vector<std::string> names; // the SFX bank in record order
        if (std::FILE* f = std::fopen((TestPaths::gameRoot() + "audio/PC/SFX/sfx.lst").c_str(), "rb"))
        {
            char buf[512];
            while (std::fgets(buf, sizeof buf, f))
            {
                std::string l(buf);
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
                    l.pop_back();
                if (l.rfind("-- ", 0) == 0)
                    names.push_back(l.substr(3));
            }
            std::fclose(f);
        }
        if (TestPaths::exeTablesDir().empty() || !d.load(TestPaths::exeTablesDir() + "director_speech.txt") || names.empty())
            std::puts("  skip  no game executable or sfx.lst");
        else
        {
            auto indexOf = [&](const std::string& n) {
                for (std::size_t i = 0; i < names.size(); ++i)
                    if (names[i] == n)
                        return i;
                return static_cast<std::size_t>(-1);
            };
            auto nameAt = [&](std::size_t i) { return i < names.size() ? names[i] : std::string(); };
            std::mt19937 rng(1);
            const auto start = d.samples(39, indexOf, nameAt, rng);
            check(start == std::vector<std::string>{"SFX_D1V1_SCRIPTED_BORN1A", "SFX_D1V1_SCRIPTED_BORN1B",
                                                    "SFX_D1V1_SCRIPTED_BORN2A", "SFX_D1V1_SCRIPTED_BORN2B"},
                  "AISPEECH 39 (Jury_Turf level start) plays BORN1A, 1B, 2A, 2B");
            check(d.samples(41, indexOf, nameAt, rng) ==
                      std::vector<std::string>{"SFX_D1V1_SCRIPTED_BORN7", "SFX_D1V1_SCRIPTED_BORN8", "SFX_D1V1_SCRIPTED_BORN9"},
                  "AISPEECH 41 (gladiator court) is the three-part line BORN7, 8, 9");
            const auto pick = d.samples(80, indexOf, nameAt, rng); // BODY10..BODY12: one of three
            check(pick.size() == 1 && pick[0].rfind("SFX_D1V1_SCRIPTED_BODY1", 0) == 0, "a range picks one of its lines");
            check(d.samples(500, indexOf, nameAt, rng).empty(), "an id without a line yields nothing");
            bool allExec = true;
            for (int i = 0; i < 20; ++i)
            {
                const auto exec = d.samples(0, indexOf, nameAt, rng); // the lock-on line
                const std::string n = exec.empty() ? std::string() : exec[0];
                allExec = allExec && exec.size() == 1 && n.rfind("SFX_D1V1_SCRIPTED_EXEC", 0) == 0 &&
                          n.find_first_not_of("0123456789", 22) == std::string::npos && n.size() > 22;
            }
            check(allExec, "AISPEECH 0 (the EXEC_RANDOM marker) plays one of EXEC1..EXEC20, never the marker");
            check(d.subtitleKey(39, 0) == "BRN1A1" && d.subtitleKey(39, 3) == "BRN1A4" && d.subtitleKey(41, 2) == "BRN4A3",
                  "subtitles: line 39 part n shows BRN1A<n>, line 41 (BORN7, 3 parts) BRN4A1..3");
            check(d.subtitleKey(1, 0).empty(), "the random reaction lines have no subtitle");
        }
    }

    std::puts("Rats (exe 0x5cdd10)");
    {
        // A flat dark floor everywhere, nothing in the way.
        Game::Rats rats;
        Game::Rats::World w;
        w.floor = [](float x, float, float, float) { (void)x; return std::optional<Game::Rats::Floor>(Game::Rats::Floor{0.0f, 0.5f}); };
        w.clear = [](const Math::Vector3&, const Math::Vector3&) { return true; };
        w.camera = {0.0f, 1.5f, 0.0f};
        w.player = {0.0f, 0.0f, 0.0f};
        std::mt19937 rng(11);
        uint32 ms = 0;
        auto run = [&](float seconds) {
            std::vector<Math::Vector3> out;
            for (int i = 0; i < static_cast<int>(seconds * 30.0f); ++i)
            {
                ms += 33;
                w.nowMs = ms;
                for (const auto& p : rats.update(1.0f / 30.0f, w, rng))
                    out.push_back(p);
            }
            return out;
        };
        rats.setLimit(4);
        run(10.0f);
        bool ring = rats.count() == 4;
        for (const auto& r : rats.rats())
            if (r.active)
                ring = ring && std::fabs(std::sqrt(r.from.x * r.from.x + r.from.z * r.from.z) - 27.0f) < 5.0f;
        check(ring, "rats: at most SetMaxNumberOfRats(4) appear, about 27 m from the camera");
        rats.setLimit(0);
        // Stand 1 m from one: it runs off 3 m away from him.
        const Game::Rats::Rat* one = nullptr;
        for (const auto& r : rats.rats())
            if (r.active && r.state == 0)
                one = &r;
        bool fled = false;
        if (one)
        {
            const Math::Vector3 at = one->pos;
            w.player = {at.x + 1.0f, 0.0f, at.z};
            run(0.1f);
            fled = one->state == 2 && one->to.x < at.x - 1.0f;
        }
        check(fled, "rats: within 3 m of him one runs (0.6 s) 3 m straight away");
        // Step right onto one: squashed.
        const Game::Rats::Rat* target = nullptr;
        for (const auto& r : rats.rats())
            if (r.active)
                target = &r;
        bool squashed = false;
        if (target)
        {
            w.player = target->pos;
            squashed = !run(0.1f).empty();
        }
        check(squashed, "rats: one within 0.35 m of the living player is squashed");
    }

    std::puts("Crows (exe 0x5cfaa0)");
    {
        Game::Crows crows;
        Game::Crows::World w;
        w.clear = [](const Math::Vector3&, const Math::Vector3&) { return true; };
        w.camera = {0.0f, 1.5f, 0.0f};
        w.player = {0.0f, 0.0f, 0.0f};
        std::mt19937 rng(5);
        uint32 ms = 0;
        auto run = [&](float seconds) {
            for (int i = 0; i < static_cast<int>(seconds * 30.0f); ++i)
            {
                ms += 33;
                w.nowMs = ms;
                crows.update(1.0f / 30.0f, w, rng);
            }
        };
        w.openSky = false;
        run(20.0f);
        const bool sheltered = crows.count() == 0;
        w.openSky = true;
        run(9.0f);
        bool flying = crows.count() >= 1;
        for (const auto& c : crows.crows())
            if (c.active)
                flying = flying && c.state == 1 && c.beatMs == 0x400;
        check(sheltered && flying, "crows: none under cover; out in the open one flies in every 256 ticks");
        // A body 25 m away draws a crow onto it; walking up startles it.
        Game::Crows perched;
        w.bodies = {{25.0f, 0.0f, 0.0f}};
        w.openSky = true;
        ms = 0;
        for (int i = 0; i < 0x65; ++i)
        {
            ms += 33;
            w.nowMs = ms;
            perched.update(1.0f / 30.0f, w, rng);
        }
        int onBody = 0;
        for (const auto& c : perched.crows())
            onBody += c.active && c.state == 0 && (c.pos - w.bodies[0]).length() < 0.7f;
        w.player = w.bodies[0] + Math::Vector3{2.0f, 0.0f, 0.0f};
        for (int i = 0; i < 3; ++i)
        {
            ms += 33;
            w.nowMs = ms;
            perched.update(1.0f / 30.0f, w, rng);
        }
        bool startled = onBody > 0;
        for (const auto& c : perched.crows())
            if (c.active)
                startled = startled && c.state != 0;
        check(onBody > 0 && startled, "crows: they gather on a body 22..30 m away and take off with him within 5 m");
        const Math::Vector3 at = perched.crows()[0].pos;
        const auto shot = perched.shoot(at - Math::Vector3{5.0f, 0.0f, 0.0f}, at + Math::Vector3{5.0f, 0.3f, 0.0f});
        check(!shot.empty(), "crows: a shot line within 0.5 m kills one");
    }

    std::puts("Hand-held camera (exe 0x591840)");
    {
        // Without kicks a still view stays still; with the defaults it wanders but stays within a
        // few degrees; a sudden turn of the real view only nudges the offset, which settles.
        Game::HandCam still;
        Game::HandCam::Set calm;
        calm.period = 0;
        std::mt19937 rng(9);
        for (int i = 0; i < 300; ++i)
            still.update(1.0f / 30.0f, 0.3f, 0.1f, calm, calm, rng);
        check(still.yaw() == 0.0f && still.pitch() == 0.0f && still.roll() == 0.0f,
              "hand-cam: no kicks, no shake");
        Game::HandCam shaky;
        const Game::HandCam::Set defaults;
        float32 most = 0.0f, moved = 0.0f;
        for (int i = 0; i < 900; ++i)
        {
            shaky.update(1.0f / 30.0f, 0.0f, 0.0f, defaults, defaults, rng);
            most = std::max({most, std::abs(shaky.yaw()), std::abs(shaky.pitch()), std::abs(shaky.roll())});
            moved = std::max(moved, std::abs(shaky.yaw()));
        }
        check(moved > 0.002f && most < Math::radians(10.0f), "hand-cam: the defaults shake it, within a few degrees");
        Game::HandCam turn;
        float32 nudge = 0.0f, after = 0.0f;
        for (int i = 0; i < 300; ++i)
        {
            turn.update(1.0f / 30.0f, i < 10 ? 0.0f : 0.5f, 0.0f, calm, calm, rng);
            nudge = std::max(nudge, std::abs(turn.yaw()));
            if (i >= 200)
                after = std::max(after, std::abs(turn.yaw()));
        }
        check(nudge > 0.0f && nudge < Math::radians(1.0f) && after <= nudge,
              "hand-cam: a turn of the real view nudges it under a degree, and it settles");
    }

    std::puts("Litter (exe 0x5c7a90)");
    {
        // Flat open ground at y = 0: only rays crossing it hit.
        Game::Litter::World w;
        w.hit = [](const Math::Vector3& a, const Math::Vector3& b) -> std::optional<Game::Litter::Hit> {
            if ((a.y > 0.0f) == (b.y > 0.0f))
                return std::nullopt;
            const float t = a.y / (a.y - b.y);
            return Game::Litter::Hit{a + (b - a) * t, {0.0f, 1.0f, 0.0f}};
        };
        w.camera = {0.0f, 1.6f, 0.0f};
        std::mt19937 rng(11);
        Game::Litter litter;
        uint32 ms = 0;
        auto run = [&](int ticks) {
            for (int i = 0; i < ticks; ++i)
            {
                ms += 33;
                w.nowMs = ms;
                litter.update(1.0f / 30.0f, w, rng);
            }
        };
        run(10);
        int laid = 0;
        bool far = true;
        for (const auto& s : litter.sheets())
            if (s.state == 1)
            {
                ++laid;
                const float d = std::sqrt(s.base.x * s.base.x + s.base.z * s.base.z);
                far = far && d >= 19.0f && d <= 20.01f && std::abs(s.base.y - 0.1f) < 1e-4f;
            }
        check(laid == 10 && far, "litter: one sheet a frame, 19..20 m out, 0.1 m above the ground");
        Game::Litter refilled;
        w.scripted = true;
        for (int i = 0; i < 2; ++i)
            refilled.update(1.0f / 30.0f, w, rng);
        w.scripted = false;
        refilled.update(1.0f / 30.0f, w, rng);
        int near = 0;
        for (const auto& s : refilled.sheets())
            near += s.state == 1 && std::sqrt(s.base.x * s.base.x + s.base.z * s.base.z) < 19.0f;
        check(near >= 25, "litter: as the scripted camera lets go 30 go down within 20 m at once");
        // A gale moves them along +x / +z by wind x 3.
        w.wind = 1.0f;
        int moved = 0;
        for (int i = 0; i < 60; ++i)
        {
            ms += 33;
            w.nowMs = ms;
            refilled.update(1.0f / 30.0f, w, rng);
            for (const auto& s : refilled.sheets())
                moved += s.state == 2 && s.type == 1 && s.driftX == 3.0f;
        }
        check(moved > 0, "litter: the wind blows sheets along");
        refilled.setOn(false);
        for (int i = 0; i < 51; ++i)
            refilled.update(1.0f / 30.0f, w, rng);
        check(refilled.alpha() == 0 && refilled.drawAlpha(0, w.camera) == 0, "litter: SwitchLitterOn(false) fades it out in 51 frames");
    }

    std::puts("Crane turn (exe 0x4d4558)");
    {
        Game::CraneTurn turn;
        float32 turned = 0.0f;
        for (int i = 0; i < 60; ++i)
            turned += turn.update(1.0f / 30.0f, false, true);
        const bool full = std::abs(turn.rate() - 2.0f) < 1e-4f;
        // 40 frames up to the full rate (0.05 a frame), then 20 at it: -18 degrees a second per unit.
        const float32 expect = -(0.05f * 40.0f * 41.0f / 2.0f + 2.0f * 20.0f) * 18.0f / 30.0f * 3.14159265f / 180.0f;
        for (int i = 0; i < 45; ++i)
            turn.update(1.0f / 30.0f, false, false);
        check(full && std::abs(turned - expect) < 0.01f && turn.rate() == 0.0f,
              "crane: the right button winds the rate up to 2 by 0.05 a frame, released it falls back to 0");
    }

    std::puts("Physics props (exe 0x48c580)");
    {
        // Flat ground at y = 0.
        Game::PhysicsProps::World w;
        w.hit = [](const Math::Vector3& a, const Math::Vector3& b) -> std::optional<Game::PhysicsProps::Hit> {
            if ((a.y > 0.0f) == (b.y > 0.0f))
                return std::nullopt;
            const float t = a.y / (a.y - b.y);
            return Game::PhysicsProps::Hit{a + (b - a) * t, {0.0f, 1.0f, 0.0f}};
        };
        Game::PhysicsType cooker;
        cooker.cls = Game::PhysicsType::RigidCuboid;
        cooker.mass = 500.0f;
        cooker.properFriction = true;
        cooker.friction = 20.0f;
        cooker.airResistance = 0.03f;
        cooker.spinResistance = 0.03f;
        cooker.bounce = 0.1f;
        Game::PhysicsProps props;
        // A 1 m cube with its pivot at its base, 5 m up.
        const int32 i = props.add(7, cooker, true, {0.0f, 5.0f, 0.0f}, Math::Matrix3x3{}, {-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f});
        props.wake(i);
        props.update(10.0f / 30.0f, w);
        const float32 fallen = 5.0f - props.origin(i).y;
        // 10 ticks of 25 m/s^2 less a little air: about 1.4 m.
        bool slept = false;
        for (int k = 0; k < 120 && !slept; ++k)
        {
            props.update(1.0f / 30.0f, w);
            slept = !props.body(i).awake;
        }
        const float32 restY = props.origin(i).y;
        // It landed at about 5.3 m/s (1.36 m at 25 m/s^2): one impact at 5.3 x 40 / 6, a fridge's
        // MATERIAL_METAL_CONTAINER sound 53..54.
        const auto impacts = props.takeImpacts();
        const int32 sound = Game::PhysicsProps::impactSound(44, 7u);
        check(!impacts.empty() && impacts.front().volume >= 30 && impacts.front().volume <= 40 && (sound == 53 || sound == 54) &&
                  Game::PhysicsProps::impactSound(0, 1u) == -1,
              "physics: landing faster than 3 m/s makes its material's impact sound");
        check(fallen > 1.0f && fallen < 1.6f && slept && std::abs(restY) < 0.06f,
              "physics: a prop falls at 25 m/s^2, lands and sleeps on the floor");
        // Dropped on a hunter: 1000 (from above).
        Game::PhysicsProps drop;
        const int32 j = drop.add(8, cooker, true, {0.0f, 3.0f, 0.0f}, Math::Matrix3x3{}, {-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f});
        drop.wake(j);
        w.characters = {{3, {0.0f, 0.0f, 0.0f}, 1.8f, 0.35f}};
        float32 damage = 0.0f;
        for (int k = 0; k < 60 && damage == 0.0f; ++k)
            for (const auto& c : drop.update(1.0f / 30.0f, w))
                if (c.character == 3)
                    damage = c.damage;
        check(damage == 1000.0f, "physics: an iron prop dropped on a character crushes it (1000)");
        // Shoved into one sideways at 1 m/s: its momentum, 500.
        Game::PhysicsProps shove;
        Game::PhysicsType floaty = cooker;
        floaty.noGravity = true;
        floaty.airResistance = 0.0f;
        const int32 m = shove.add(9, floaty, true, {-1.2f, 0.5f, 0.0f}, Math::Matrix3x3{}, {-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f});
        shove.wake(m);
        shove.body(m).v = {1.0f, 0.0f, 0.0f};
        damage = 0.0f;
        for (int k = 0; k < 60 && damage == 0.0f; ++k)
            for (const auto& c : shove.update(1.0f / 30.0f, w))
                damage = c.damage;
        check(std::abs(damage - 500.0f) < 5.0f, "physics: shoved into one, it hurts by its momentum (mass x speed)");
        // A light box (10 kg) thrown into a hunter (80 kg) at 3 m/s stops against him.
        Game::PhysicsProps toss;
        Game::PhysicsType box = floaty;
        box.mass = 10.0f;
        const int32 q = toss.add(10, box, false, {-1.2f, 0.5f, 0.0f}, Math::Matrix3x3{}, {-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f});
        toss.wake(q);
        toss.body(q).v = {3.0f, 0.0f, 0.0f};
        for (int k = 0; k < 30; ++k)
            toss.update(1.0f / 30.0f, w);
        check(toss.body(q).v.x < 0.5f && toss.origin(q).x < -0.4f, "physics: a light box thrown into a character stops against him");
        // Stacked: one cube dropped on another lying on the floor comes to rest on top of it.
        {
            w.characters.clear();
            Game::PhysicsProps stack;
            const int32 lower = stack.add(20, cooker, false, {0.0f, 0.0f, 0.0f}, Math::Matrix3x3{}, {-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f});
            const int32 upper = stack.add(21, cooker, false, {0.1f, 2.0f, 0.0f}, Math::Matrix3x3{}, {-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f});
            stack.wake(upper);
            for (int k = 0; k < 150; ++k)
                stack.update(1.0f / 30.0f, w);
            check(std::abs(stack.origin(upper).y - 1.0f) < 0.1f && std::abs(stack.origin(lower).y) < 0.06f &&
                      std::abs(stack.origin(lower).x) < 0.2f && std::abs(stack.origin(upper).x - stack.origin(lower).x - 0.1f) < 0.15f &&
                      !stack.body(upper).awake && !stack.body(lower).awake,
                  "physics: a prop dropped on another comes to rest on top of it");
            // Slid into a sleeping one at 4 m/s: it wakes and moves off.
            Game::PhysicsProps slide;
            const int32 still = slide.add(22, cooker, false, {0.0f, 0.0f, 0.0f}, Math::Matrix3x3{}, {-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f});
            const int32 moving = slide.add(23, cooker, false, {-1.2f, 0.0f, 0.0f}, Math::Matrix3x3{}, {-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f});
            slide.wake(moving);
            slide.body(moving).v = {4.0f, 0.0f, 0.0f};
            for (int k = 0; k < 20; ++k)
                slide.update(1.0f / 30.0f, w);
            const auto woke = slide.takeNewlyAwake();
            check(!woke.empty() && woke.front() == 22 && slide.origin(still).x > 0.05f,
                  "physics: one slid into a sleeping prop wakes it and pushes it on");
        }
        // The magnet's hang: pushed, it swings and settles back under its pivot.
        Game::Hanging hang;
        hang.update(1.0f / 30.0f, {0.0f, 10.0f, 0.0f}, 4.0f, 0.01f);
        hang.push({3.0f, 0.0f, 0.0f});
        float32 most = 0.0f;
        for (int k = 0; k < 600; ++k)
        {
            hang.update(1.0f / 30.0f, {0.0f, 10.0f, 0.0f}, 4.0f, 0.01f);
            most = std::max(most, hang.down().x);
        }
        check(most > 0.2f && hang.down().y < -0.99f, "physics: the hanging magnet swings when pushed and settles");
    }

    std::puts("Doors (exe 0x490450, SWINGDOOR_PHYS)");
    {
        // A 1 m door hinged at the origin, closed along +X.
        auto makeDoor = [] {
            Game::Doors::Door d;
            d.name = "door";
            d.localMin = {0.0f, 0.0f, -0.05f};
            d.localMax = {1.0f, 2.2f, 0.05f};
            d.frozen = false;
            return d;
        };
        const float32 dt = 1.0f / 30.0f;
        // Walked through at 1.5 m/s, it opens; left, its spring swings it back and it latches.
        Game::Doors doors;
        doors.add(makeDoor());
        Math::Vector3 body{0.6f, 1.0f, -1.0f}, vel{0.0f, 0.0f, 1.5f};
        float32 widest = 0.0f, shutAt = -1.0f;
        std::vector<uint16> heard;
        for (int k = 0; k < 300 && shutAt < 0.0f; ++k)
        {
            std::vector<Game::Doors::Pusher> pushers;
            if (k < 50)
            {
                body += vel * dt;
                Math::Vector3 v = vel;
                doors.collide(body, 0.35f, v);
                pushers.push_back({body, vel, 0.35f, 80.0f});
            }
            doors.update(dt, pushers);
            for (const Game::Doors::Sound& s : doors.takeSounds())
                heard.push_back(s.event);
            const Game::Doors::Door& d = doors.doors()[0];
            widest = std::max(widest, std::fabs(d.angle));
            if (widest > 0.1f && d.angle == 0.0f && !d.awake)
                shutAt = k * dt;
        }
        check(widest > 1.0f && body.z > 0.6f, "doors: a character walking into one swings it open and gets through");
        check(shutAt > 1.0f, "doors: left open, its hinge spring swings it shut and it latches");
        check(!heard.empty() && heard.front() == Game::EntityAudio::DoorPush && heard.back() == Game::EntityAudio::DoorLatch,
              "doors: pushed from shut it posts sound event 0x1b, latching 0x1c");
        // The audio manager's queue (0x4529f0): four a frame, by rank (0x6b52c6, lower first).
        {
            Game::EntityAudio audio;
            for (const uint16 e : {0x0f, 0x1b, 0x20, 0x0b, 0x1c})
                audio.post(7, e, 1.0f);
            audio.post(3, 0x21, 0.0f);
            const auto out = audio.drain();
            check(out.size() == 5 && out[0].event == 0x0b && out[1].event == 0x1b && out[2].event == 0x20 && out[3].event == 0x1c &&
                      out[4].entity == 3 && audio.drain().empty() && Game::EntityAudio::rank(0x23) == 100 &&
                      Game::EntityAudio::rank(0x5f) == 100,
                  "entity audio: up to four events an entity a frame, lowest rank first, then emptied");
            using EA = Game::EntityAudio;
            const EA::Sample shut = EA::swingDoor(EA::DoorLatch, 2, 150.0f, 0), slam = EA::swingDoor(EA::DoorLatch, 2, 500.0f, 0);
            check(shut.id == 240 && std::abs(shut.volume - 0.5f) < 1e-3f && slam.id == 243 && slam.volume == 1.0f &&
                      EA::swingDoor(EA::DoorPush, 3, 300.0f, 0).id == 238 && EA::swingDoor(EA::DoorPush, 8, 300.0f, 0).id < 0 &&
                      EA::swingDoor(EA::DoorLimit, 2, 300.0f, 0).id == 246 && EA::swingDoor(EA::DoorLatch, 3, 100.0f, 1).id == 54 - 1 &&
                      EA::swingDoor(EA::DoorLatch, 3, 100.0f, 1).range == 60.0f && EA::slideDoor(EA::SlideStart).id == 251,
                  "entity audio: a door's sample by its material and how hard (close or slam, value / 300)");
        }
        // The AI's speech-order lines to the voice table (0x527a50) and the squad role slots.
        {
            using HS = Game::HunterSpeech;
            check(HS::lineSlot(0x29) == 32 && HS::lineSlot(0x38) == 47 && HS::lineSlot(0x39) == 48 && HS::lineSlot(0x3a) == 51 &&
                      HS::lineSlot(0x36) == 13 && HS::lineSlot(0x25) == 21 && HS::lineSlot(0x2e) < 0 && HS::lineSlot(3) < 0,
                  "hunter speech: order lines map to their contexts (0x29 calling for assistance, 0x38 / 0x39 the buddy's)");
            Game::gun::RoleSlots roles;
            roles.seek("a", 0.0f);
            roles.cover("b", 0.0f, 2.0f, 3);
            roles.cover("c", 0.0f, 2.0f, 1); // (one fighting: no slot)
            roles.advance("d", 0.0f, 1);
            check(roles.roleOf("a", 0.0f) == Game::gun::RoleSlots::A && roles.roleOf("b", 1.0f) == Game::gun::RoleSlots::None &&
                      roles.roleOf("b", 2.5f) == Game::gun::RoleSlots::E && roles.roleOf("c", 3.0f) == Game::gun::RoleSlots::None &&
                      roles.roleOf("d", 0.0f) == Game::gun::RoleSlots::D,
                  "gun combat: role slots A (seek), E (cover, due later), D (advance)");
            roles.expire(9.5f);
            const bool heldA = roles.roleOf("a", 9.5f) == Game::gun::RoleSlots::A, goneE = roles.roleOf("b", 9.5f) == Game::gun::RoleSlots::None;
            roles.expire(10.5f);
            check(heldA && goneE && roles.roleOf("a", 10.5f) == Game::gun::RoleSlots::None,
                  "gun combat: role slots are released (E after 9 s, A after 10 s)");
        }
        // The music's ambience channels by level (0x5a4b80's handlers).
        {
            using MA = Game::MusicAmbience;
            MA::Inputs in;
            in.heard = [](const Math::Vector3&) { return 1.0f; };
            // Journo_Streets: the generator started at a place 10 m off is heard after 37 frames,
            // channel 10 at 2/3 of 90 by its falloff over 45 m, channel 11 at half over 120 m.
            MA gen;
            gen.reset(15, {});
            const Math::Vector3 at{10.0f, 0.0f, 0.0f};
            gen.slotPlayed(MA::kGeneratorStart, -1, -1, 0, &at, 0.0f);
            in.frame = 2;
            in.now = 0.5f;
            const MA::Frame early = gen.update(in, 1.0f / 30.0f);
            in.frame = 12;
            in.now = 1.5f;
            const MA::Frame on = gen.update(in, 1.0f / 30.0f);
            check(!early[2].set && on[2].set && on[2].volume == MA::falloff(60, 45.0f, 10.0f) && on[3].volume == (MA::falloff(60, 120.0f, 10.0f) + 1) / 2 &&
                      on[2].placed && std::abs(on[2].seconds - 4.0f / 30.0f) < 1e-4f,
                  "music ambience: Journo_Streets' generator on channels 10 / 11 after its start sample");
            // The audio manager's sample table (0x446ea0): the default, a collision, a footstep, a
            // door, a voice (from 0x13b on 80 / 40), a phone.
            {
                const auto l = [](int32 i) { return Game::sampleLevel(i); };
                check(l(0).level == 127 && l(0).range == 40.0f && l(38).level == 30 && l(38).range == 30.0f && l(79).level == 25 &&
                          l(79).range == 20.0f && l(240).level == 127 && l(14050).level == 80 && l(14050).range == 40.0f &&
                          l(0x36fa).level == 100 && l(0x36fa).range == 60.0f,
                      "sample levels: the audio manager's table (0x446ea0)");
            }
            // The weapons' sounds (0x44f620): a glock near is its tail pair then its stereo pair;
            // far the tails and its left sample at its place; a knife swings high at 0.9; the
            // uzi's burst end; the empties and shells; a silenced glock is silent.
            {
                namespace WA = Game::WeaponAudio;
                const auto nearGlock = WA::used(0x28, 0, true), farGlock = WA::used(0x28, 0, false);
                const auto knife = WA::used(0x12, 0, true), uziEnd = WA::used(0x2b, 1, false), six = WA::used(0x27, 0, false);
                check(nearGlock.size() == 4 && nearGlock[0].sample == 0xbb && nearGlock[0].tail == -1 && nearGlock[0].pan == 270 &&
                          nearGlock[1].sample == 0xbc && nearGlock[1].pan == 90 && nearGlock[2].sample == 0xb9 && nearGlock[2].pan == 270 &&
                          nearGlock[3].sample == 0xba && nearGlock[3].tail == 0 && farGlock.size() == 3 && farGlock[2].sample == 0xb9 &&
                          farGlock[2].pan == -1 && knife.size() == 1 && knife[0].sample == 0x8b && knife[0].rate == 0.9f &&
                          uziEnd.size() == 4 && uziEnd[0].sample == 0xcb && uziEnd[0].rate == 1.1f && uziEnd[3].sample == 0xc6 &&
                          six[0].rate == 1.15f && WA::used(0x29, 0, true).empty() && WA::used(0x28, 1, true).empty() &&
                          WA::used(0x2c, 0, true)[0].rate == 0.8f && WA::used(0x2c, 0, true)[2].rate == 1.0f && WA::empty(0x2e) == 0xd0 &&
                          WA::shell(0x2c) == 0x8e && WA::shell(0x59) == 0xb6 && WA::shell(0x10) == -1,
                      "weapon audio: the 0x3001 handler's samples (0x44f620 / 0x450330 / 0x450430)");
            }
            // The audio manager's look round (0x447ac0): in the open the tails are full; in a 4 m
            // room under a roof none; a wall 2.5 m to the left only (open sky) takes the left down.
            {
                Game::AcousticProbe open, room, wall;
                const Math::Vector3 eye{0.0f, 1.8f, 0.0f}, ahead{0.0f, 0.0f, -1.0f}, feet{0.0f, 0.0f, 0.0f};
                auto none = [](const Math::Vector3&, const Math::Vector3&) -> std::optional<float32> { return std::nullopt; };
                auto boxed = [](const Math::Vector3& a, const Math::Vector3& b) -> std::optional<float32> {
                    const Math::Vector3 d = b - a;
                    if (d.y > 0.0f)
                        return 1.0f; // the roof
                    const float32 len = d.length(); // walls 4 m round
                    return len > 4.0f ? std::optional<float32>(4.0f) : std::nullopt;
                };
                auto leftWall = [](const Math::Vector3& a, const Math::Vector3& b) -> std::optional<float32> {
                    // A wall at x = -2.5 (the camera faces -z, so its left is -x).
                    if (b.x >= -2.5f || a.x <= -2.5f)
                        return std::nullopt;
                    const float32 t = (a.x + 2.5f) / (a.x - b.x);
                    return t * (b - a).length();
                };
                for (int i = 0; i < 10; ++i)
                {
                    open.update(eye, ahead, feet, none);
                    room.update(eye, ahead, feet, boxed);
                    wall.update(eye, ahead, feet, leftWall);
                }
                check(open.tail(-1) == 1.0f && open.tail(1) == 1.0f && room.ceiling() == 1.0f && room.tail(-1) == 0.0f &&
                          room.tail(1) == 0.0f && wall.tail(-1) < 0.95f && wall.tail(1) > wall.tail(-1) &&
                          Game::AcousticProbe::weight(15, -1) == 1.0f && Game::AcousticProbe::weight(5, -1) == 0.0f,
                      "acoustic probe: the guns' tails by the surroundings (0x448ef0 / 0x4491c0)");
            }
            // His heartbeat and breath (0x45fe00 / 0x45ffb0): at full stamina a beat 0.27 s then
            // 0.72 s later and no breath; tired (0.3) both; locked on, the beat is boosted.
            {
                Game::PlayerAudio pa;
                std::vector<float32> beats;
                int32 breaths = 0, firstBreath = -1, boostedLevel = 0;
                Game::PlayerAudio::State s;
                for (int32 ms = 0; ms <= 3000; ms += 10)
                {
                    s.timeMs = static_cast<float32>(ms);
                    for (const auto& snd : pa.update(s))
                        if (snd.sample == 0x130)
                            beats.push_back(s.timeMs);
                }
                Game::PlayerAudio tired;
                s.stamina = 0.3f;
                s.lockedOn = true;
                for (int32 ms = 0; ms <= 3000; ms += 10)
                {
                    s.timeMs = static_cast<float32>(ms);
                    for (const auto& snd : tired.update(s))
                    {
                        if (snd.sample != 0x130)
                        {
                            ++breaths;
                            if (firstBreath < 0)
                                firstBreath = snd.sample;
                        }
                        else if (boostedLevel == 0)
                            boostedLevel = snd.level;
                    }
                }
                check(beats.size() >= 4 && std::abs(beats[1] - beats[0] - 720.0f) <= 10.0f && std::abs(beats[2] - beats[1] - 270.0f) <= 10.0f &&
                          breaths >= 4 && firstBreath == 0x124 + Game::PlayerAudio::breathStep(0.3f) / 2 && boostedLevel == 127 &&
                          Game::PlayerAudio::breathStep(0.3f) == 4 && Game::PlayerAudio::breathStep(0.95f) == 9,
                      "player audio: heartbeat and breath (0x44dab0 / 0x44d4d0)");
            }
            // Coronas (0x59c3c0): a lamp 10 m ahead, Size 3, Intensity 65, warm white.
            {
                std::vector<int32> params(15, 0);
                params[3] = std::bit_cast<int32>(1.0f);
                params[4] = std::bit_cast<int32>(0.908f);
                params[5] = std::bit_cast<int32>(0.797f);
                params[11] = 78849; // Flags 6 set (tagged)
                params[13] = std::bit_cast<int32>(3.0f);
                params[14] = 65;
                const Game::Coronas::Light l = Game::Coronas::fromParams(params, {0.0f, 0.0f, -10.0f}, 7);
                Game::Coronas cor;
                cor.add(l);
                Game::Coronas::View v;
                v.project = [](const Math::Vector3& p) -> std::optional<Math::Vector3> {
                    return Math::Vector3{0.5f + p.x / -p.z, 0.5f - p.y / -p.z, -p.z};
                };
                v.paused = true;
                std::mt19937 rng(1);
                const auto sprites = cor.frame(v, rng);
                // 10 m: 6 x 3 / 10 = 1.8 tall, x 0.75 wide; brightness 65 x (1 - 10 / 60) = 54.2.
                check(l.flags == (5u | 0x40u) && l.r == 255 && l.g == 232 && sprites.size() == 1 &&
                          std::abs((sprites[0].y1 - sprites[0].y0) - 1.8f) < 1e-3f && std::abs((sprites[0].x1 - sprites[0].x0) - 1.35f) < 1e-3f &&
                          sprites[0].r >= 25 && sprites[0].r <= 29, // (averaging 49, over 30: its brightest 25 + rand % 5)
                      "coronas: a lamp's size, brightness and flags (0x59bc20 / 0x59c3c0)");
                v.clear = [](const Math::Vector3&, const Math::Vector3&) { return false; };
                check(cor.frame(v, rng).empty(), "coronas: hidden when the world blocks the light");
                // Off the middle with GAME DETAIL on: its lens flare (0x40), every other ghost at
                // level 1, all 15 at level 2.
                Game::Coronas side;
                side.add(Game::Coronas::fromParams(params, {-3.0f, 0.0f, -10.0f}, 7));
                Game::Coronas::View vs;
                vs.project = v.project;
                vs.paused = true;
                vs.detail = 1;
                const std::size_t half = side.frame(vs, rng).size();
                vs.detail = 2;
                const std::size_t all = side.frame(vs, rng).size();
                vs.detail = 0;
                check(half == 1 + 7 && all == 1 + 15 && side.frame(vs, rng).size() == 1, "coronas: the lens flare's ghosts by GAME DETAIL");
                // A torch's beam (0x100): 13 sprites growing out along its axis, in its colour;
                // a quarter of the lamp's brightness while the scripts hold the camera.
                Game::Coronas beam;
                Game::Coronas::Light b;
                b.at = {0.0f, 0.0f, -10.0f};
                b.axis = {1.0f, 0.0f, 0.0f};
                b.r = 102;
                b.nearW = b.nearH = b.farW = b.farH = 1.0f;
                b.range = 40.0f;
                b.flags = 1u | 0x100u;
                beam.add(b);
                const auto beamSprites = beam.frame(vs, rng);
                v.clear = {};
                Game::Coronas::View vq = v;
                vq.scripted = true;
                const auto dim = cor.frame(vq, rng);
                check(beamSprites.size() == 13 && std::abs((beamSprites[0].x1 - beamSprites[0].x0) - 0.077f * 0.274f) < 1e-4f &&
                          beamSprites[12].x1 - beamSprites[12].x0 > beamSprites[11].x1 - beamSprites[11].x0 &&
                          beamSprites[12].x0 > beamSprites[0].x0 && beamSprites[5].r == 102 && dim.size() == 1 && dim[0].r < 25,
                      "coronas: a torch's beam sprites and the scripted camera's quarter (0x59c3c0)");
            }
            // Footsteps by material type (0x7abda0): concrete, guts, metal 56's single sample, none.
            {
                using MT = Game::MaterialTypes;
                check(MT::footsteps(0).first == 79 && MT::footsteps(0).last == 83 && MT::footsteps(53).first == 230 &&
                          MT::footsteps(56).first == 104 && MT::footsteps(56).last == 104 && MT::footsteps(16).first < 0 &&
                          MT::footsteps(54).first == 84,
                      "footsteps: the material types' sample ranges");
            }
            // Estate_Ext: a gunshot silences the bed; 150 frames on it is halfway back.
            MA estate;
            estate.reset(18, {});
            in.frame = 0;
            check(estate.update(in, 1.0f / 30.0f)[2].volume == 110, "music ambience: Estate_Ext's bed at 110");
            estate.gunFired();
            check(estate.update(in, 1.0f / 30.0f)[2].volume == 0, "music ambience: a gunshot silences Estate_Ext's bed");
            for (int i = 0; i < 148; ++i)
                (void)estate.update(in, 1.0f / 30.0f);
            check(estate.update(in, 1.0f / 30.0f)[2].volume == 55, "music ambience: Estate_Ext's bed comes back over 300 frames");
            // Asylum: the chair hums on 9 while off, on 10 / 11 once toggled.
            MA asylum;
            asylum.reset(11, {0.0f, 0.0f, 5.0f});
            in.frame = 1;
            const MA::Frame off = asylum.update(in, 1.0f / 30.0f);
            asylum.slotPlayed(MA::kToggleElectro, -1, -1, 3, nullptr, 0.0f);
            in.frame = 6;
            const MA::Frame chair = asylum.update(in, 1.0f / 30.0f);
            check(off[1].volume == MA::falloff(80, 54.0f, 5.0f) && off[2].volume == 0 && chair[1].volume == 0 &&
                      chair[2].volume == MA::falloff(63, 12.0f, 5.0f) && asylum.placedPair(),
                  "music ambience: Asylum's electric chair on 9 while off, on 10 / 11 switched on");
            // Attic: nothing; elsewhere the rain.
            MA attic, rain;
            attic.reset(20, {});
            rain.reset(2, {});
            in.rain = 0.5f;
            check(!attic.update(in, 0.03f)[2].set && rain.update(in, 0.03f)[2].volume == 64, "music ambience: the rain, none in the Attic");
        }
        // Swung hard at its limit it bounces back off it at HINGE_BOUNCE.
        Game::Doors limit;
        Game::Doors::Door hard = makeDoor();
        hard.openIn = 1.0f;
        hard.angle = hard.lastAngle = 0.95f;
        hard.speed = 3.0f;
        hard.awake = true;
        limit.add(hard);
        limit.update(dt, {});
        check(std::abs(limit.doors()[0].speed + 0.6f) < 0.05f && limit.doors()[0].angle <= 1.0f,
              "doors: past its open limit it bounces back (x 0.2)");
        // Its limit is heard once; again only after it has swung 0.3 rad back from it.
        auto limitHits = [](const std::vector<Game::Doors::Sound>& s) {
            return std::count_if(s.begin(), s.end(), [](const Game::Doors::Sound& x) { return x.event == 0x20; });
        };
        const auto firstHit = limitHits(limit.takeSounds());
        Game::Doors::Door& again = const_cast<Game::Doors::Door&>(limit.doors()[0]);
        again.angle = again.lastAngle = 0.95f;
        again.speed = 3.0f;
        limit.update(dt, {});
        const auto unarmed = limitHits(limit.takeSounds());
        again.angle = again.lastAngle = 0.75f;
        again.speed = -3.0f;
        limit.update(dt, {});
        again.angle = again.lastAngle = 0.95f;
        again.speed = 3.0f;
        limit.update(dt, {});
        check(firstHit == 1 && unarmed == 0 && limitHits(limit.takeSounds()) == 1,
              "doors: a limit hit is heard again only after swinging 0.3 rad back (0x4cc4b0)");
        // SetDoorOverrideAngle(-90) on a locked door: driven open, it stops dead there.
        Game::Doors over;
        Game::Doors::Door chute = makeDoor();
        chute.locked = true;
        chute.overrideAngle = Math::radians(-90.0f);
        chute.driving = true;
        over.add(chute);
        float32 at = 0.0f;
        for (int k = 0; k < 150 && over.doors()[0].driving; ++k)
        {
            over.update(dt, {});
            at = k * dt;
        }
        check(!over.doors()[0].driving && over.doors()[0].speed == 0.0f && over.doors()[0].angle <= Math::radians(-90.0f) &&
                  over.doors()[0].angle > Math::radians(-100.0f) && at > 0.3f,
              "doors: an override angle drives it there and it stops");
    }

    std::puts("Director milestones (exe 0x413eb0 / 0x5b6490)");
    {
        using M = Game::DirectorMilestones;
        std::mt19937 rng(7);
        std::set<int32_t> seen;
        bool inRange = true;
        for (int i = 0; i < 2000; ++i)
        {
            const int32_t v = M::randomInt(4, 6, rng);
            inRange = inRange && v >= 4 && v <= 6;
            seen.insert(v);
        }
        check(inRange && seen.size() == 3, "0x493b90(4, 6) draws 4, 5 and 6 and nothing else");
        check(M::speechId(M::Executions, 1, 0) == 1 && M::speechId(M::Executions, 5, 4) == 3 &&
                  M::speechId(M::Executions, 4, 8) == 5 && M::speechId(M::Executions, 5, 12) == 9,
              "X ids: LOW / MED / HIGH 1-3 in scenes 1-5, 4-6 in 6-9, 7-9 later");
        check(M::speechId(M::Hiding, 4, 6) == 14 && M::speechId(M::Kills, 1, 0) == 30 && M::speechId(M::Kills, 5, 12) == 38,
              "H ids 10-18, KT ids 30-38");
        check(M::speechId(M::Spotted, 2, 0) == 20 && M::speechId(M::Spotted, 3, 3) == 21 && M::speechId(M::Spotted, 4, 0) == 22 &&
                  M::speechId(M::Spotted, 1, 5) == 24 && M::speechId(M::Spotted, 5, 10) == 29,
              "S ids: SLOW1-3, SMED, SHIGH (19-23) in scenes 1-5, then LOW / MED / HIGH 24-26, 27-29");
        check(M::speechId(M::Executions, 2, 0) == -1 && M::speechId(M::Kills, 3, 6) == -1, "X / KT categories 2 and 3 have no line");

        // Jury_Turf is scene index 0: fixed X / KT milestones, no hiding lines.
        M m;
        m.start(0, 9, rng);
        check(m.milestone(0) == 2 && m.milestone(1) == 3 && m.milestone(2) >= 4 && m.milestone(2) <= 6,
              "scene 1: executions 2, 3, 4..6");
        check(m.milestone(3) == 1 && m.milestone(4) >= 2 && m.milestone(4) <= 3 && m.milestone(5) >= 4 && m.milestone(5) <= 6,
              "scene 1: kills 1, 2..3, 4..6");
        check(m.milestone(6) == -1 && m.milestone(7) == -1 && m.milestone(8) == -1, "scene 1: no hiding milestones");
        check(m.milestone(9) >= 1 && m.milestone(9) <= 2 && m.milestone(13) >= 10 && m.milestone(13) <= 14,
              "scene 1: spotted 1..2 ... 10..14");
        {
            // A checkpoint resumed with two executions and one kill (0x5b5a60, then 0x5b6490).
            M r;
            r.start(0, 9, rng, {2, 0, 0, 1});
            check(r.count(M::Executions) == 2 && r.count(M::Kills) == 1, "a resumed checkpoint keeps its counts");
            check(r.milestone(0) == -1 && r.milestone(1) == 3 && r.milestone(3) == -1,
                  "... and the milestones its counts stand on (X 2, KT 1) are spent");
        }
        M::Frame f;
        f.dt = 0.1f;
        check(m.update(f) == -1, "nothing to say at the start");
        m.kill();
        check(m.update(f) == 30, "the first kill: KTLOW (30)");
        check(m.update(f) == -1, "... once");
        m.execution(2);
        check(m.update(f) == -1, "one execution: no milestone");
        m.execution(2);
        f.talking = true;
        check(m.update(f) == -1, "the second execution while the director talks: lost");
        f.talking = false;
        check(m.update(f) == -1, "... and not said later (it was marked played)");
        m.execution(2);
        check(m.update(f) == 2, "the third execution: XMED (2)");

        // An execution that reaches an X milestone takes the kill milestone its death reaches.
        M e;
        e.start(0, 9, rng);
        e.setMilestone(3, 1);
        e.setMilestone(0, 1);
        e.execution(2);
        e.kill();
        check(e.milestone(3) == -1, "the execution's own kill does not also reach KT LOW");
        check(e.update(f) == 1, "... the director says XLOW (1)");

        // Spotted: the first frame a hunter is after him counts; the line waits 3 s and needs him
        // still seen, outside the court ("aiarena") in scene 1.
        M s;
        s.start(0, 9, rng);
        s.setMilestone(9, 1);
        f.seen = true;
        f.inArena = true;
        int32_t said = -1;
        for (int i = 0; i < 25 && said < 0; ++i)
            said = s.update(f);
        check(s.count(M::Spotted) == 1 && said == -1, "spotted inside the court: counted, no SLOW1");
        M s2;
        s2.start(0, 9, rng);
        s2.setMilestone(9, 1);
        f.inArena = false;
        int frames = 0;
        for (said = -1; frames < 60 && said < 0; ++frames)
            said = s2.update(f);
        check(said == 19 && frames >= 30 && frames <= 32, "spotted elsewhere: SLOW1 (19) three seconds later");
        f.seen = false;
        for (int i = 0; i < 50; ++i)
            s2.update(f); // lost for 5 s
        f.seen = true;
        s2.update(f);
        check(s2.count(M::Spotted) == 1, "seen again within 10 s of losing him: not a new spotting");
        f.seen = false;
        for (int i = 0; i < 110; ++i)
            s2.update(f);
        f.seen = true;
        s2.update(f);
        check(s2.count(M::Spotted) == 2, "seen again after 10 s: spotted twice");

        // Hiding counts from scene 4 (index 3), for spells of more than 5 whole seconds.
        M h;
        h.start(3, 20, rng);
        check(h.milestone(6) >= 4 && h.milestone(6) <= 6 && h.milestone(8) >= 14 && h.milestone(8) <= 20,
              "scene 4: hiding milestones 4..6, 8..12, 14..20");
        f.seen = false;
        f.hidden = true;
        for (int i = 0; i < 55; ++i)
            h.update(f);
        f.hidden = false;
        h.update(f);
        check(h.count(M::Hiding) == 0, "5.5 s in the shadows: not counted");
        f.hidden = true;
        for (int i = 0; i < 65; ++i)
            h.update(f);
        f.seen = true;
        h.update(f);
        check(h.count(M::Hiding) == 1, "6.5 s, then a hunter gets onto him: counted");

        // Scenes 6-12: X HIGH (0xfe) comes past MED, and only after a gruesome execution.
        M x;
        x.start(6, 20, rng);
        check(x.milestone(2) == -2, "scene 7: X HIGH is the after-MED marker 0xfe");
        x.setMilestone(0, 50);
        x.setMilestone(1, 1);
        f = M::Frame{};
        f.dt = 0.1f;
        x.execution(2);
        check(x.update(f) == 5, "scene 7: XMED (5)");
        x.execution(3);
        check(x.update(f) == -1, "past MED without a gruesome execution: nothing");
        x.execution(4);
        check(x.update(f) == 6, "after a gruesome one: XHIGH (6)");
        M late;
        late.start(13, 20, rng);
        late.kill();
        check(late.update(f) == -1, "scene 14 and later: no commentary");
    }

    std::puts("game text");
    {
        const auto runs = Game::GameText::parse("~green~Green ~colour~items can be used to ~!~execute");
        check(runs.size() == 2 && runs[0].text == "Green " && runs[0].color.g > runs[0].color.r && runs[1].text == "items can be used to execute",
              "~green~ ... ~colour~ colours a run; unknown tokens (~!~) are dropped");
        Game::GameText t;
        t.setDisplayTime(2.0f);
        t.display("GATE");
        t.update(1.5f);
        const bool during = t.displaying();
        t.update(1.0f);
        check(during && !t.displaying(), "a game text stays up for SetGameTextDisplayTime, then goes");
        t.setGoal("GOAL1");
        t.setGoal("GOAL1A");
        t.setGoal("GOAL1");
        t.clearGoal("GOAL1");
        check(t.goals().size() == 1 && t.goals()[0] == "GOAL1A", "level goals: set once, cleared by key");
    }

    std::puts("the tuning pak tuning");
    {
        auto pak = Assets::PakFile::load(TestPaths::gameRoot() + Game::gameExe().tuningPak);
        if (!pak)
            std::puts("  skip  the tuning pak not found");
        else
        {
            check(pak->file("levels/global/data/aiTypeData.ini").has_value() &&
                      pak->file("./levels/Jury_Turf/entityTypeData.ini").has_value(),
                  "the pak holds aiTypeData.ini and Jury_Turf's entityTypeData.ini");
            Game::AiTuning t;
            t.read(*pak, "jury_turf");
            const Game::AiTuning shipped;
            check(t.fromPak && t.vision.range == 30.0f && t.vision.visible == shipped.vision.visible &&
                      t.vision.light == shipped.vision.light && t.vision.distance == shipped.vision.distance,
                  "vision_data read from the pak matches the defaults");
            check(t.hearing.volumeDistance == shipped.hearing.volumeDistance && t.hearing.confusion == Game::kMedium,
                  "VOLUME_DISTANCES 5/15/30/60/200, CONFUSION medium");
            bool sameTable = true;
            for (int f = 0; f < Game::kAiActions; ++f)
                for (int to = 0; to < Game::kAiActions; ++to)
                {
                    const auto& x = t.alert.transitions[f][to];
                    const auto& y = shipped.alert.transitions[f][to];
                    sameTable = sameTable && x.valid == y.valid && x.sight == y.sight && x.hearing == y.hearing;
                }
            check(sameTable && t.alert.degradeSeconds == 20.0f && t.alert.sustain == shipped.alert.sustain,
                  "the alert/action record matches the defaults");
            check(t.fists.quick.damage == 7.5f && t.fists.quick.playerDamage == 6.0f && t.fists.hard.damage == 12.5f &&
                      std::fabs(t.fists.hard.strikeAt - 0.567f) < 1e-4f && t.fists.quick.size == 1.1f,
                  "fists: 7.5 / 6.0 quick, 12.5 hard landing at 0.567 s, 1.1 m shot sphere");
            const Game::LevelSetup setup = Game::LevelSetup::read(*pak, "jury_turf");
            check(setup.weather == "CLEAR" && setup.fog && setup.fogStart == 100.0f && setup.fogEnd == 120.0f &&
                      std::fabs(setup.skyColor.z - 5.0f / 255.0f) < 1e-5f,
                  "Jury_Turf levelSetup: weather CLEAR, linear fog 100..120 m into the sky colour (2, 2, 5)");
            check(setup.shadowCasters.size() == 4 &&
                      std::count(setup.shadowCasters.begin(), setup.shadowCasters.end(), "CJ_WASTEBIN_(F)") == 1,
                  "Jury_Turf entityTypeData: four SHADOW props (flame drum, trashcan, wastebin, trolley)");
            Game::CameraSplines splines;
            const auto ini = pak->file("levels/jury_turf/spl/splines.ini");
            const bool loaded = ini && splines.load(*ini, TestPaths::gameRoot() + "levels/jury_turf/spl/splines.spl");
            const Game::CameraSpline* follow = splines.find("FOLLOW39");
            const Game::CameraSpline* look = splines.find("lookat39");
            check(loaded && splines.size() == 50 && follow && look && follow->follow && !look->follow &&
                      follow->duration == 7.0f && follow->points.size() == 4,
                  "Jury_Turf camera splines: 50, FOLLOW39 a 7 s camera path of 4 points, LOOKAT39 its look-at path");
            if (follow)
            {
                const Math::Vector3 a = follow->at(0.0f), b = follow->at(1.0f);
                check((a - follow->points.front()).length() < 1e-4f && (b - follow->points.back()).length() < 1e-4f &&
                          std::fabs(a.x - 334.8f) < 0.1f,
                      "a spline starts on its first point (334.8, 0.5, -23.2) and ends on its last");
            }
            // Weapons and executions (G4 / G5).
            {
                Game::Script::ConstantTable ct;
                Game::Script::loadConstants(TestPaths::sourceDir() + "config/script_constants.txt", ct);
                Game::ItemCatalog items;
                if (const auto text = pak->file("levels/jury_turf/entityTypeData.ini"))
                    items.read(Assets::parseIniRecords(*text), ct);
                const Game::ItemType* bag = items.find("Bag_(CT)");
                const Game::ItemType* shard = items.find("shard_(ct)");
                const Game::ItemType* aid = items.find("G_First_Aid_(CT)");
                check(items.find("Severed_Head_(CT)") && items.find("fists") && bag && shard && aid && bag->ct == 58 && shard->ct == 19 && aid->ct == 9 &&
                          bag->weapon && !aid->weapon && bag->weaponType == "fists" && shard->weaponType == "shard",
                      "Jury_Turf items: fists, shard, bag, wire, painkillers, the severed head (CT_BAG 58, CT_SHARD 19, CT_G_FIRST_AID 9 from the exe)");
                {
                    const Game::PickupHalo smallBat = Game::pickupHalo(0, "BELT_RIGHT"), bat = Game::pickupHalo(0, "BACK"),
                                           green = Game::pickupHalo(58, "BELT_LEFT"), aidHalo = Game::pickupHalo(9, ""),
                                           ammo = Game::pickupHalo(104, ""), none = Game::pickupHalo(0, "");
                    check(smallBat.b == 80 && smallBat.r == 0 && bat.r == 80 && bat.g == 0 && green.g == 80 && green.r == 0 &&
                              aidHalo.r == 80 && aidHalo.g == 80 && aidHalo.b == 80 && aidHalo.pulses && ammo.r == 60 && ammo.g == 30 &&
                              ammo.size == 0.8f && !none.shown,
                          "halo colours (0x4c7cc0): BELT_RIGHT blue, BACK red, BELT_LEFT green, first aid white and pulsing, ammo 60/30/0 at 0.8, no slot none");
                    const Game::PickupHalo save = Game::savePointHalo(), gas = Game::gasCanHalo();
                    check(save.r == 20 && save.g == 80 && save.b == 120 && save.size == 1.5f && gas.r == 20 && gas.g == 70 && gas.b == 30 &&
                              gas.size == 0.9f,
                          "a save point's halo 20/80/120 at 1.5 (0x4d27a0), a gas can's 20/70/30 at 0.9 (0x435a50)");
                }
                check(bag && bag->canExecute() && !bag->execution(1) && bag->execution(2) && bag->execution(4) &&
                          bag->execution(2)->tvp == "EXECUTE_BAG2" && bag->execution(3)->playerWeaponDestroy == 0.43f &&
                          bag->hotspot == 0.75f,
                      "the bag: executions 2..4 (1 unused), cameras EXECUTE_BAG2.., weapon gone 0.43 s into execution 3");
                if (shard)
                {
                    const Game::ExecutionPlan p = Game::planExecution(*shard, 3, "Jury_Turf");
                    check(p.level == 3 && p.playerClip == "Knife_Sneak_Attack3" && p.hunterClip == "Damage_Knife_Sneak_Attack3" &&
                              p.dieClip == "Die_Knife_Sneak_Attack3" && p.soundFile == "SHARD/SHARD2A.RIB",
                          "a shard execution 3 plays Knife_Sneak_Attack3 / Damage_ / Die_, sound EXECUTE/SHARD/SHARD2A");
                    // The exe 0x4c93b0 / 0x5a5370: the kind of kill and the sound go by COLLECTABLE_TYPE.
                    check(Game::executionClass(32) == 6 && Game::executionClass(33) == 8 && Game::executionClass(35) == 3 &&
                              Game::executionClass(88) == -1 && std::string(Game::executionClassName(7)) == "IceAxe" &&
                              Game::executionSoundFile(32, 2, "Mall") == "CLEAVER/CLEAVE1A.RIB" &&
                              Game::executionSoundFile(33, 4, "Derelict") == "JACK/JACK3A.RIB" &&
                              Game::executionSoundFile(58, 3, "Mansion_Int") == "BAG/BAG2B.RIB" &&
                              Game::executionSoundFile(92, 1, "Attic") == "PSPIKE/PSPIKEB.RIB",
                          "machete kills as a cleaver, small bat as a blackjack, the hunters' chainsaw none; sounds by "
                          "kind, take B in the Cerberus levels");
                }
                if (bag)
                {
                    Game::ExecutionCharge charge;
                    charge.setStageTimes(t.executeStageTimes[0], t.executeStageTimes[1]);
                    charge.start();
                    charge.update(1.9f);
                    const int32 first = charge.level(*bag);
                    charge.update(0.2f);
                    const int32 second = charge.level(*bag);
                    charge.update(3.0f);
                    check(t.executeStageTimes[0] == 2.0f && t.executeStageTimes[1] == 5.0f && first == 2 && second == 3 &&
                              charge.level(*bag) == 4,
                          "the player record's EXECUTE_STAGE_TIMES 2.0, 5.0: held 2 s -> execution 3, 5 s -> 4");
                }
                {
                    const Game::ThrowWeapon* bottleThrow = t.throwWeapon("bottle");
                    const Game::ThrowWeapon* canThrow = t.throwWeapon("can");
                    const Game::ItemType* bottle = items.find("Bottle_(CT)");
                    const Game::ItemType* brick = items.find("Half_Brick_(CT)");
                    check(bottleThrow && bottleThrow->damage == 20.0f && canThrow && canThrow->damage == 10.0f && !t.throwWeapon("glock") &&
                              t.throwChargeTime == 1.5f && t.minThrowVelocity.z == 5.0f && t.maxThrowVelocity.y == 10.0f &&
                              t.maxThrowVelocity.z == 25.0f,
                          "throwing: WC_THROWING shots (bottle 20, can 10), THROW_CHARGE_TIME 1.5, velocity (1, 2, 5) to (0.8, 10, 25)");
                    check(bottle && bottle->weapon && bottle->smashable && bottle->material == "MATERIAL_BOTTLE" && brick &&
                              brick->weapon && !brick->smashable,
                          "the bottle and half brick are carried weapons (BELT_REAR); the bottle smashes (SMASHABLE2), the brick does not");
                }
                if (bag && shard && aid)
                {
                    check(bag->halo.g == 80 && bag->halo.r == 0 && shard->halo.g == 80 && aid->halo.pulses && aid->halo.r == 80 &&
                              bag->boundsBoost == 1.3f && shard->boundsBoost == 3.0f && aid->boundsBoost == 2.5f,
                          "pickup halos: green for the bag and shard (BELT_LEFT), white for the painkillers; BND_SPH_BOOST 1.3 / 3.0 / 2.5");
                    check(std::string(Game::executionNoise(*bag, 4)) == "EXECUTION_VERY_LOW",
                          "a green weapon kills almost silently (EXECUTION_VERY_LOW at any level)");
                }
                check(t.meleeWeapon("crowbar").clipStem == "Beater" && t.meleeWeapon("baseball_bat").clipStem == "Beater2" &&
                          t.kick("kick_beater").kickAt > 0.46f && t.kick("").shot.damage == 5.0f && t.wiggleTimeDecay == 0.15f &&
                          t.fists.blockDamage[0] == 0.4f && t.fists.grappleDamage[2] == 10.5f,
                      "melee: BEATER1 / BEATER2 clips, kick records (kick 0.4667 s, 5 damage), WIGGLE_TIME_DECAY, block and grapple values");
                const Game::Firearm* nail = t.firearm("nailGun");
                check(nail && nail->clipStem == "NailGun" && nail->minRange == 14.0f && nail->maxRange == 16.0f &&
                          nail->playerDamage == 9.5f && nail->clipAmmo == 30 && nail->hitChance(10.0f) == 1.0f &&
                          nail->hitChance(16.0f) > 0.12f && nail->hitChance(16.0f) < 0.13f && nail->hitChance(17.0f) == 0.0f &&
                          !t.firearm("chainsaw_player"),
                      "firearms: the nail gun's MIN/MAX_FIRE_RANGE 14 / 16 m, hit probability 0.125 at 16 m, 9.5 to the player, 30 a clip");
                check(nail && nail->damage == 14.5f && nail->range == 16.0f && nail->pellets == 1 && nail->bodies == 1 &&
                          nail->maxClips == 6 && nail->droppedAmmo == 10 && nail->headShots == 3 && nail->legMultiplier == 0.5f &&
                          nail->grip == Game::Firearm::Grip::OneHanded && nail->emptySound == "NAIL_GUN" &&
                          nail->reloadSound == "PLASTIC" && nail->maxRounds() == 180,
                      "the nail gun: DAMAGE 14.5 over a 16 m line, MAX_CLIPS 6 (180 rounds), DROPPED_AMMO 10, NUM_HEAD_SHOTS 3, legs x0.5");
                const Game::Firearm* shotgun = t.firearm("shotgun");
                const Game::Firearm* uzi = t.firearm("uzi");
                const Game::Firearm* sniper = t.firearm("sniperRifle");
                const Game::Firearm* six = t.firearm("6shooter");
                const Game::Firearm* tranq = t.firearm("tranqRifle");
                check(shotgun && shotgun->pellets == 8 && shotgun->spread == 5.0f && shotgun->range == 25.0f && shotgun->clipAmmo == 8 &&
                          shotgun->recoilUp == 15.0f && shotgun->recoilTime == 0.4f && shotgun->grip == Game::Firearm::Grip::TwoHanded,
                      "the shotgun: MAX_SHOTS 8 over SHOT_SPREAD 5, a 25 m line, 8 a clip, RECOIL_DIST 15 for 0.4 s");
                check(shotgun && shotgun->reloadsRoundByRound() && shotgun->reloadLoopStart == 0.0f &&
                          std::fabs(shotgun->reloadLoopEnd - 0.367f) < 1e-5f && six && six->reloadsRoundByRound() &&
                          std::fabs(six->reloadLoopStart - 1.067f) < 1e-5f && uzi && !uzi->reloadsRoundByRound(),
                      "RELOAD_LOOP: the shotgun loads a shell each 0..0.367 s of its reload, the 6shooter each 1.067..1.267; "
                      "the uzi a clip at once");
                check(uzi && uzi->recoilAccumulator == 2.5f && uzi->maxRecoilUp == 11.0f && uzi->recoilTime == 0.1f &&
                          sniper && sniper->headRange == 10.0f && sniper->bodies == 3 && sniper->aimClip(false) == "Sniper_Aim_T" &&
                          six && six->headShots == 1 && six->bodies == 2 && tranq && tranq->headShots == 0 && tranq->bodies == 1,
                      "the uzi climbs (RECOIL_VERT_ACCUMULATOR 2.5 to 11); the rifle locks heads within 10 m and goes through 3; "
                      "BODIES_HIT 0 / no NUM_HEAD_SHOTS read as 1");
                Game::AiTuning scrap;
                scrap.aim.lockonWidth = scrap.aim.lockonHeight = scrap.aim.lockonDist = scrap.aim.crouchRecoil = -1.0f;
                scrap.aim.crosshair = {-1.0f, -1.0f, -1.0f};
                scrap.aim.zoomLevels = scrap.aim.zoomMoveScales = {-1.0f, -1.0f};
                scrap.aim.zoomSpeed = -1.0f;
                scrap.read(*pak, "scrap");
                check(scrap.aim.lockonWidth == 90.0f && scrap.aim.lockonHeight == 90.0f && scrap.aim.lockonDist == 30.0f &&
                          scrap.aim.crosshair[0] == 0.015f && scrap.aim.crosshair[2] == 0.04f && scrap.aim.crouchRecoil == 0.5f,
                      "Scrap's player record: AIM_LOCKON_ANGLES 90, 90, AIM_LOCKON_DIST 30, CROSSHAIR_SIZES, CROUCH_RECOIL 0.5");
                const Game::MeleeWeapon& oneHanded = t.meleeWeapon("obstruct_1handedGun");
                const Game::MeleeWeapon& twoHanded = t.meleeWeapon("obstruct_2handedGun");
                check(oneHanded.quick.strikeAt == 0.4f && oneHanded.quick.damage == 4.0f && oneHanded.quick.playerDamage == 4.0f &&
                          oneHanded.quick.size == 1.1f && twoHanded.quick.strikeAt == 0.233f && t.melee.count("obstruct_2handedgun"),
                      "a gun's blow (OBSTRUCT_WEAPON obstruct_1handedGun / _2handedGun): at 0.4 / 0.233 s, 4 damage, 1.1 m");
                check(scrap.aim.zoomLevels[0] == 2.0f && scrap.aim.zoomLevels[1] == 12.0f && scrap.aim.zoomSpeed == 1.2f &&
                          scrap.aim.zoomMoveScales[0] == 0.7f && scrap.aim.zoomMoveScales[1] == 0.4f,
                      "... ZOOM_LEVELS 2, 12, ZOOM_SPEED 1.2, ZOOM_MOVE_SCALES 0.7, 0.4");
                const Game::HunterTraits hod = t.hunter("Hod_BodM1");
                check(hod.koTime == 15.0f && hod.blockAccuracy == 50.0f && t.playerKoTime == 2.0f,
                      "knocked down, a hunter lies KO_TIME 15 s and the player 2 s; BLOCK_ACCURACY 50 (the exe's default)");
                const Game::MeleeWeapon& blade = t.meleeWeapon("SHARD");
                check(blade.clipStem == "Blade" && t.meleeWeapon("fists").clipStem == "Fist" && t.meleeWeapon("nothing").clipStem == "Fist",
                      "the shard fights with the Blade_ clips, fists (and anything unknown) with the Fist_ ones");
            }
            if (const auto tvp = pak->file("levels/global/tvp/TimedVectorPairs.ini"))
            {
                const auto records = Assets::parseIniRecords(*tvp);
                const Assets::IniRecord* bag2 = Assets::findRecord(records, "EXECUTE_BAG2");
                const auto sets = bag2 ? Game::parseCameraShots(*bag2) : std::vector<std::vector<Game::CameraShot>>{};
                float32 total = 0.0f;
                for (const auto& s : sets.empty() ? std::vector<Game::CameraShot>{} : sets[0])
                    total += s.duration;
                check(sets.size() == 3 && sets[0].size() == 3 && std::fabs(total - 7.76f) < 0.01f,
                      "EXECUTE_BAG2: three sets of three shots (VECPAIR@ closes a set), 7.76 s each");
                if (!sets.empty())
                {
                    // Shot 1 of set 1: eye (-1.06, 1.43, 1.44) in the player frame; at yaw 0 ahead is -Z.
                    const Game::CameraPose pose = Game::cameraPoseAt(sets[0], 0.1f, {10.0f, 0.0f, 20.0f}, 0.0f);
                    check(pose.shot == 0 && std::fabs(pose.eye.x - (10.0f - 1.06109f)) < 1e-3f &&
                              std::fabs(pose.eye.z - (20.0f - 1.42798f)) < 1e-3f && std::fabs(pose.eye.y - 1.44385f) < 1e-3f &&
                              Game::cameraPoseAt(sets[0], 0.6f, {}, 0.0f).shot == 1,
                          "a shot's eye is placed in the player's frame (y ahead, z up); the next shot starts after its seconds");
                }
            }
            if (auto sound = Assets::loadExecutionSound(TestPaths::gameRoot() + "audio/PC/EXECUTE/BAG/BAG1A.RIB"))
            {
                check(sound->channels == 1 && std::fabs(static_cast<float32>(sound->samples.size()) / 44100.0f - 7.72f) < 0.15f,
                      "EXECUTE/BAG/BAG1A.RIB: one mono take of about 7.7 s at 44.1 kHz, inside Bag_Sneak_Attack2 (8.17 s)");
                std::size_t clipped = 0;
                for (const int16_t v : sound->samples)
                    clipped += (v >= 32760 || v <= -32760) ? 1u : 0u;
                check(clipped * 1000 < sound->samples.size(),
                      "the execution sound decodes cleanly (0x400 ADPCM chunks: under 0.1 % of the samples at full scale)");
            }
            else
                check(false, "the bag's execution sound loads");
            const Game::LevelSetup foggy = Game::LevelSetup::read(*pak, "estate_ext", 4);
            check(foggy.weather == "FOGGY" && foggy.fogStart == 10.0f && foggy.fogEnd == 40.0f,
                  "a forced weather (Estate_Ext FOGGY) uses its record: fog 10..40 m");
            const Game::HunterTraits hb = t.hunter("Hod_BodB1");
            check(hb.hitPoints == 100.0f && hb.swingPause == 1.5f && hb.runTime == 45.0f && hb.skinTextures.size() == 2,
                  "Hod_BodB1: 100 HP, SWING_PAUSE 1.5, RUN_TIME 45, two skin variants");
            check(t.fists.quick.chainPoint == 0.5f && t.fists.quick.fireWindowStart == 0.24f && t.fists.comboPauses[2] == 0.6f &&
                      std::fabs(t.alert.darkNode - 0.08f) < 1e-5f && std::fabs(t.alert.searchDensity - 0.85f) < 1e-5f,
                  "fist combo timing and search_parameters read from the pak");
            const auto skins = t.skins.find("hod_hedb1");
            check(hb.voice == "HOODED_BIG" && skins != t.skins.end() && skins->second.size() == 2 && skins->second[0] == "Hod_HedB1B_128",
                  "Hod_BodB1 speaks as HOODED_BIG; its head has skins B and C");
            check(t.sound("RUNNING").volume == Game::kLow && t.sound("GUN_SHOT").volume == Game::kVeryHigh &&
                      !t.sound("GUN_SHOT").confusion,
                  "AISounds.ini: RUNNING low, GUN_SHOT very_high and unmistakable");
        }
    }

    std::puts("Navigation graph");
    auto grf = Assets::Grf::load(level + "mapAI.grf");
    if (!grf)
    {
        std::puts("SKIP (no game data)");
        return 77;
    }
    check(grf->nodes.size() == 1358 && grf->paths.size() == 3 && grf->areas.size() == 7, "mapAI.grf: 1358 nodes, 3 paths, 7 areas");
    bool linksOk = true;
    for (const auto& n : grf->nodes)
        for (const auto& l : n.links)
            linksOk = linksOk && l.node >= 0 && static_cast<std::size_t>(l.node) < grf->nodes.size();
    check(linksOk, "every link names an existing node");
    Game::LevelFrame frame; // identity frame: scene = level coordinates (x, z, -y)
    Game::NavGraph nav(*grf, frame);
    const int32_t attak1 = nav.find("ATTAK1"), wilson = nav.find("wilson");
    check(attak1 == 498 && wilson == 773, "script node names resolve case-insensitively");
    check(std::fabs(nav.node(attak1).position.y - 0.97f) < 0.01f, "GRF positions are Z-up level coordinates (height -> scene y)");
    check(nav.path("aipath03").size() == 6 && nav.path("AIPath03").front() == 489, "named paths");
    const auto route = nav.route(attak1, wilson);
    bool routeOk = route.size() >= 2 && route.front() == attak1 && route.back() == wilson;
    for (std::size_t i = 1; routeOk && i < route.size(); ++i)
        routeOk = linked(nav, route[i - 1], route[i]);
    check(routeOk, "A* route attak1 -> wilson follows real links (" + std::to_string(route.size()) + " nodes)");
    check(nav.nearest(nav.node(wilson).position) == wilson, "the nearest node to a node is itself");
    check(nav.within(wilson, 6.0f).size() > 1, "wander-search finds neighbours within a radius");
    {
        // Link permissions: the GRF link type (3 allow everything, 0 allow nothing); the scripts
        // change them with GraphModifyConnections around a door or trigger.
        Game::NavGraph g(*grf, frame);
        const std::size_t closed = g.closedLinks();
        const Math::Vector3 trigger01{351.40f, 0.96f, -51.69f}; // Trigger01 in the identity frame
        const std::size_t near = g.setLinksNear(trigger01, 2.0f, false);
        const std::size_t closedAfter = g.closedLinks();
        g.setLinksNear(trigger01, 2.0f, true);
        check(closed == 28 && near > 0 && closedAfter > closed && g.closedLinks() < closed,
              "28 links start closed; GraphModifyConnections closes / opens those across Trigger01 (" + std::to_string(near) + ")");
        bool walksClosed = false;
        Game::NavGraph shut(*grf, frame);
        shut.setLinksNear(nav.node(wilson).position, 1000.0f, false);
        walksClosed = !shut.route(attak1, wilson).empty();
        check(!walksClosed, "a route never takes a closed link");
        // Cut wilson off: the closest reachable route ends at a reachable node nearer to wilson
        // than attak1, over real links.
        Game::NavGraph cut(*grf, frame);
        cut.setLinksNear(cut.node(wilson).position, 3.0f, false);
        const auto toward = cut.route(attak1, wilson, true);
        bool towardOk = cut.route(attak1, wilson).empty() && toward.size() >= 2 && toward.front() == attak1 &&
                        toward.back() != wilson &&
                        (cut.node(toward.back()).position - cut.node(wilson).position).length() <
                            (cut.node(attak1).position - cut.node(wilson).position).length();
        for (std::size_t i = 1; towardOk && i < toward.size(); ++i)
            towardOk = linked(cut, toward[i - 1], toward[i]);
        check(towardOk, "an unreachable goal: the route to the reachable node nearest it (" + std::to_string(toward.size()) + " nodes)");
        // The nearest node a predicate accepts (a free line): the plain nearest when it accepts all.
        const Math::Vector3 p = nav.node(wilson).position + Math::Vector3{0.3f, 0.0f, 0.2f};
        check(nav.nearestClear(p, [](const Math::Vector3&) { return true; }) == nav.nearest(p) &&
                  nav.nearestClear(p, [&](const Math::Vector3& n) { return (n - nav.node(wilson).position).length() > 0.01f; }) != wilson,
              "nearestClear skips the nodes it may not use");
    }

    {
        // search_parameters: dark nodes within 35 m, 85 % of them, others' searches respected.
        const Game::AlertData a;
        std::mt19937 rng(7);
        const Math::Vector3 at = nav.node(wilson).position;
        const auto near = nav.within(wilson, a.searchRadius);
        std::vector<float> light(nav.size(), 0.0f); // all dark ...
        for (std::size_t i = 0; i < light.size(); i += 2)
            light[i] = 0.5f;                        // ... but every other node lit
        std::size_t dark = 0;
        for (int32_t n : near)
            dark += light[static_cast<std::size_t>(n)] <= a.darkNode ? 1 : 0;
        const auto plan = Game::planSearchNodes(nav, at, a, &light, nullptr, 0.0f, rng);
        bool allDark = true;
        for (int32_t n : plan)
            allDark = allDark && light[static_cast<std::size_t>(n)] <= a.darkNode;
        check(allDark && plan.size() == static_cast<std::size_t>(std::ceil(dark * a.searchDensity)),
              "a search visits DENSITY_% (85) of the dark nodes within RADIUS_m (35 m)");
        std::unordered_map<int32_t, float> searched;
        for (int32_t n : plan)
            searched[n] = 0.0f;
        const auto second = Game::planSearchNodes(nav, at, a, &light, &searched, 1.0f, rng);
        std::size_t repeats = 0;
        for (int32_t n : second)
            repeats += searched.count(n);
        check(repeats == 0, "nodes another hunter just searched are left out");
        std::vector<float> lit(nav.size(), 1.0f);
        check(Game::planSearchNodes(nav, at, a, &lit, nullptr, 0.0f, rng) == std::vector<int32_t>{wilson},
              "with nothing dark around, it looks where the player was");
        std::vector<float> one(nav.size(), 1.0f);
        one[static_cast<std::size_t>(wilson)] = 0.0f;
        check(Game::nearSafeZoneNode(nav, one, nav.node(wilson).position, 0.06f) &&
                  !Game::nearSafeZoneNode(nav, one, nav.node(wilson).position + Math::Vector3{10.0f, 0.0f, 0.0f}, 0.06f),
              "a dark graph node counts within 2 m (or its radius), not 10 m away");
    }

    std::puts("Level script AI set-up");
    Assets::MlsFile mls;
    if (!mls.load(level + "scripts/jury_turf.mls"))
    {
        check(false, "jury_turf.mls loads");
        return 1;
    }
    Game::Script::ConstantTable constants;
    check(Game::Script::loadConstants(TestPaths::sourceDir() + "config/script_constants.txt", constants), "script constants load");
    Game::Script::Engine engine;
    engine.load(mls, constants);
    Game::AiDirector director;
    Game::bindAiNatives(engine, director);
    // Jury_Turf's entities on a new game (both save points still in the level).
    const std::set<std::string> present{"hcarpark1", "hcarpark2", "hpatrol", "hpatrol2", "hexit", "hdummy", "hbackalley",
                                        "harena", "hleader", "player", "gen_save_point", "gen_save_point02"};
    engine.bind("getentity", [&](Game::Script::NativeCall& c) {
        return present.count(c.string(0)) ? Game::Script::Value::entity(1) : Game::Script::Value::nil();
    });
    check(engine.start("jury_turf", "OnCreate"), "the level script's OnCreate starts");
    for (int i = 0; i < 10; ++i)
        engine.update(0.1);

    const Game::GoalDef* hunt = director.goal("ghunt");
    check(hunt && hunt->type == Game::GoalType::HuntEnemy && hunt->target == "player", "gHunt hunts the player");
    const Game::GoalDef* wait = director.goal("gwait");
    check(wait && wait->type == Game::GoalType::GuardDirection && wait->node == "waiting" && nav.find(wait->node) >= 0,
          "gWait guards node 'waiting', which exists in the graph");
    const Game::GoalDef* latest = director.goal("glatest");
    check(latest && latest->run && latest->node == "latest", "gLatest runs to 'latest'");
    const Game::HunterSetup* dummy = director.hunter("hdummy");
    check(dummy && dummy->added && dummy->subpack == "sdummy" && dummy->runTime == 20.0f, "hDummy: in subpack sDummy, run time 20 s");
    const Game::GoalDef* dg = director.goalFor("hdummy", false);
    check(dg && dg->name == "gdummy", "an unaware hDummy guards (gDummy)");
    const Game::GoalDef* dh = director.goalFor("hdummy", true);
    check(dh && dh->name == "ghunt", "a hunting hDummy hunts (gHunt)");
    const Game::HunterSetup* alley = director.hunter("hbackalley");
    const Game::IdleAction* patrol = director.idleAction("hbackalley");
    check(alley && patrol && patrol->kind == Game::IdleKind::Patrol && patrol->path == "aipath03" && alley->stops.size() == 2,
          "hBackAlley patrols AIPath03 with two stops at 'attak1'");
    check(director.goalFor("hbackalley", false) == nullptr, "hBackAlley has no goal, so it patrols");
    const Game::HunterSetup* arena = director.hunter("harena");
    check(arena && arena->subpack == "sarena" && director.goalFor("harena", false) && director.goalFor("harena", false)->name == "gwait",
          "hArena waits in the arena (gWait)");
    const Game::HunterSetup* car = director.hunter("hcarpark2");
    check(car && car->blind && car->deaf, "the car-park hunters start blind and deaf (save point 02 still there)");
    check(car && car->subpack.empty(), "...and join their subpack only when the car-park trigger fires");
    const Game::HunterSetup* leader = director.hunter("hleader");
    check(leader && leader->leader && leader->invisible, "hLeader is an invisible leader");
    std::printf("  goals %zu, hunters %zu, unimplemented natives %zu\n", director.goalCount(), director.hunters().size(),
                engine.missingNatives().size());

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
