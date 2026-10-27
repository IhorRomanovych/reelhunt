// tests/anim_test.cpp - verify the key-mapping file + that every bound animation action
// actually exists in the level IFP and overlaps the Cash skeleton (so Character::crossfade
// won't silently skip it). Links librw (Dff::load) + ifp + keymap. No SDL/Vulkan.
#include "Assets/dff.h"
#include "Assets/ifp/ifp.h"
#include "Assets/librw_engine.h"
#include "Game/keymap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "test_paths.h"

namespace
{
    int g_failures = 0;
    void trace(const char* msg) { std::printf("%s\n", msg); }
    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            ++g_failures;
            std::printf("  CHECK FAIL: %s\n", what);
        }
    }

    std::vector<uint8_t> readAll(const std::string& path)
    {
        std::vector<uint8_t> buf;
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return buf;
        std::fseek(f, 0, SEEK_END);
        const long len = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (len > 0)
        {
            buf.resize(static_cast<size_t>(len));
            buf.resize(std::fread(buf.data(), 1, buf.size(), f));
        }
        std::fclose(f);
        return buf;
    }

    // The scancode a binding uses for a given key name, or -1 if none.
    int scancodeForKey(const std::vector<Game::KeyBinding>& bindings, const char* name)
    {
        for (const auto& b : bindings)
            if (b.keyName == name)
                return b.scancode;
        return -1;
    }

    void testKeymap(const std::string& keymapPath)
    {
        std::printf("keymap: %s\n", keymapPath.c_str());
        Game::Keymap km;
        check(km.load(keymapPath), "keymap file loads");
        const auto& actions = km.actions();
        const auto& bindings = km.bindings();
        std::printf("  actions=%zu bindings=%zu\n", actions.size(), bindings.size());
        check(actions.size() >= 5, "at least 5 actions defined");
        check(bindings.size() >= 1, "at least 1 key bound");

        // Every binding references an existing action.
        bool allResolve = true;
        for (const auto& b : bindings)
            if (!km.findAction(b.action))
                allResolve = false;
        check(allResolve, "every key binding references an existing action");

        // Known key -> scancode mappings (SDL3).
        check(scancodeForKey(bindings, "W") == 26, "W -> scancode 26");
        check(scancodeForKey(bindings, "A") == 4, "A -> scancode 4");
        check(scancodeForKey(bindings, "SPACE") == 44, "SPACE -> scancode 44");
        check(scancodeForKey(bindings, "1") == 30, "'1' -> scancode 30");

        // The base stance + at least one one-shot exist and are well-formed.
        check(km.findAction("idle") != nullptr, "idle stance action exists");
        const Game::AnimActionDef* idle = km.findAction("idle");
        if (idle)
            check(!idle->oneShot && !idle->clip.empty(), "idle is a looping stance with a clip");
        const Game::AnimActionDef* punch = km.findAction("punch_quick1");
        if (punch)
            check(punch->oneShot, "punch_quick1 is a one-shot");
    }

    void testClipsAgainstSkeleton(const std::string& cashDff, const std::string& ifpPath,
                                  const Game::Keymap& km)
    {
        // 1. Cash skeleton bone ids (DFF needs librw).
        trace("  ensureLibrwEngine()");
        Assets::ensureLibrwEngine();
        trace("  Dff::load(cash_pc.dff)");
        auto clumps = Assets::Dff::load(cashDff);
        trace("  Dff::load returned");
        check(clumps.has_value() && !clumps->empty(), "cash_pc.dff loads");

        // Bind-pose per-axis extent: confirms which local axis is the model's "body" axis
        // (the tallest extent). Needed because flipForVulkan's rotateX(pi) is only correct for
        // a Y-up model; if the body axis is X or Z the character renders sideways/on its side.
        if (clumps.has_value() && !clumps->empty())
        {
            Math::Vector3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
            bool any = false;
            for (const auto& a : clumps->at(0).atomics)
                for (const auto& v : a.vertices)
                {
                    const auto& p = v.position;
                    if (!any) { mn = mx = p; any = true; continue; }
                    mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                    mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
                }
            if (any)
                std::printf("  cash bind-pose bbox extent: x=%.3f y=%.3f z=%.3f  "
                            "(mn=(%.3f, %.3f, %.3f)  mx=(%.3f, %.3f, %.3f))\n",
                            mx.x - mn.x, mx.y - mn.y, mx.z - mn.z,
                            mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
        }

        std::unordered_set<int32_t> skel;
        if (clumps.has_value() && !clumps->empty())
        {
            for (const auto& b : clumps->at(0).bones)
                skel.insert(b.id);
        }
        std::printf("cash skeleton bones=%zu\n", skel.size());
        check(!skel.empty(), "cash skeleton has bones");

        // 2. Level IFP: exact animation name set + a name->animation map (for overlap).
        trace("  reading IFP file");
        const std::vector<uint8_t> data = readAll(ifpPath);
        std::printf("ifp: %s (%zu bytes)\n", ifpPath.c_str(), data.size());
        check(!data.empty(), "ifp file reads");
        trace("  Assets::parse");
        auto res = Assets::parseIfp(data.data(), static_cast<uint32_t>(data.size()));
        trace("  parse returned");
        check(res.has_value(), "ifp parses");
        if (!res.has_value())
            return;
        const auto& ifp = *res;
        std::unordered_map<std::string, const Assets::IfpAnimation*> byName;
        for (const auto& block : ifp.blocks)
            for (const auto& an : block.animations)
                byName.emplace(an.name, &an); // first exact-name wins
        std::printf("  ifp animations=%zu (unique names)\n", byName.size());

        // 2b. Evenly spaced key tracks span the clip (they used to be packed into its first 10 ms,
        // freezing those bones on their last key), and the player's gaits have root motion.
        if (const Assets::IfpAnimation* walk = ifp.findAnimation("Walk_Fwd"))
        {
            const Assets::IfpBoneTrack* root = walk->findBone(Assets::kRootMotionBoneId);
            check(root && !root->frames.empty() && root->frames.back().time > walk->duration - 0.05f,
                  "Walk_Fwd root keys span the whole clip");
        }
        auto speed = [&](const char* clip) {
            const Assets::IfpAnimation* a = ifp.findAnimation(clip);
            return a ? Assets::rootGroundSpeed(*a) : -1.0f;
        };
        std::printf("  root speeds: Run_Fwd %.2f Sprint_Fwd %.2f Sneak_Walk_Fwd %.2f Sneak_Run_Fwd %.2f Stand %.2f\n",
                    speed("Run_Fwd"), speed("Sprint_Fwd"), speed("Sneak_Walk_Fwd"), speed("Sneak_Run_Fwd"), speed("Stand"));
        check(std::fabs(speed("Run_Fwd") - 6.11f) < 0.1f, "Run_Fwd covers ~6.1 m/s");
        check(speed("Sprint_Fwd") > speed("Run_Fwd") && speed("Sneak_Run_Fwd") > speed("Sneak_Walk_Fwd"),
              "sprint outpaces run, sneak-run outpaces sneak-walk");
        check(speed("Stand") == 0.0f, "Stand has no root travel");

        // Frame type 3 keeps one rotation for the whole track in its header (the hunters' idle
        // pelvis); it used to be dropped, leaving the bone in its bind orientation.
        if (const Assets::IfpAnimation* idle = ifp.findAnimation("Stand_Idle"))
        {
            const Assets::IfpBoneTrack* root = idle->findBone(Assets::kRootMotionBoneId);
            const bool rotated = root && root->frameType == 3u && !root->frames.empty() && root->frames[0].hasQuat &&
                                 std::fabs(root->frames[0].quat[1] + 0.826f) < 0.01f;
            check(rotated, "a type-3 track carries its header rotation (Stand_Idle pelvis)");
        }

        // 3. Every action's clip exists (exact) and overlaps the Cash skeleton.
        int good = 0, low = 0;
        for (const auto& a : km.actions())
        {
            auto it = byName.find(a.clip);
            if (it == byName.end())
            {
                ++g_failures;
                std::printf("  CLIP MISSING: action '%s' -> clip '%s' (not in IFP)\n",
                            a.name.c_str(), a.clip.c_str());
                continue;
            }
            int overlap = 0;
            for (const auto& tr : it->second->bones)
                if (skel.count(tr.boneId) > 0)
                    ++overlap;
            const int clipBones = static_cast<int>(it->second->bones.size());
            const bool ok = overlap >= std::max(1, clipBones / 2);
            if (overlap == 0)
            {
                ++g_failures;
                std::printf("  CLIP NO-OVERLAP: action '%s' clip '%s' dur=%.2f bones=%d overlap=0\n",
                            a.name.c_str(), a.clip.c_str(), it->second->duration, clipBones);
            }
            else if (ok)
            {
                ++good;
            }
            else
            {
                ++low;
            }
            std::printf("  %-14s %-34s dur=%5.2f bones=%3d overlap=%3d %s\n",
                        a.name.c_str(), a.clip.c_str(), it->second->duration, clipBones, overlap,
                        ok ? "ok" : (overlap == 0 ? "NO-OVERLAP" : "low-overlap"));
        }
        std::printf("  clip summary: good=%d low=%d (total actions=%zu)\n",
                    good, low, km.actions().size());
    }
}

int main()
{
    if (!TestPaths::haveGameData())
        return TestPaths::skipNoGameData();
    std::setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered: partial output survives a crash
    trace("=== anim_test start ===");
    const std::string keymapPath =
        TestPaths::sourceDir() + "config/keymap.txt";
    const std::string cashDff =
        TestPaths::gameRoot() + "levels/GLOBAL/CHARPAK/cash_pc.dff";
    const std::string ifpPath =
        TestPaths::gameRoot() + "levels/jury_turf/allanims.ifp";

    Game::Keymap km;
    trace("about to load keymap");
    if (km.load(keymapPath))
    {
        trace("keymap loaded OK");
        testKeymap(keymapPath);
        trace("testKeymap done");
        trace("about to testClipsAgainstSkeleton (Dff::load needs librw)");
        testClipsAgainstSkeleton(cashDff, ifpPath, km);
        trace("testClipsAgainstSkeleton done");
    }
    else
    {
        ++g_failures;
        trace("keymap load failed");
    }

    std::printf("%s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    trace("=== anim_test done ===");
    return g_failures == 0 ? 0 : 1;
}
