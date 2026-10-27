// Game/director_milestones.cpp - the director's milestone commentary. See director_milestones.h.
#include "Game/director_milestones.h"

#include <cmath>
#include <cstdint>

namespace Game {

namespace {

// Milestone byte indices (0x7b7e34 + i).
constexpr int32 kX = 0, kKT = 3, kH = 6, kS = 9;
constexpr int8 kOff = -1;       // 0xff: never reached
constexpr int8 kAfterMed = -2;  // 0xfe: X HIGH once past MED with a gruesome execution done

} // namespace

int32 DirectorMilestones::randomInt(int32 lo, int32 hi, std::mt19937& rng)
{
    // 0x493b90: rand() % ((hi - lo) * 255) / 255, rounded half up, plus lo.
    if (hi == lo)
        return lo;
    const int32 r = std::uniform_int_distribution<int32>(0, (hi - lo) * 255 - 1)(rng);
    const float32 f = static_cast<float32>(r) / 255.0f + static_cast<float32>(lo);
    const int32 i = static_cast<int32>(f);
    return f - static_cast<float32>(i) > 0.5f ? i + 1 : i;
}

float32 DirectorMilestones::randomFloat(float32 lo, float32 hi, std::mt19937& rng)
{
    // 0x493c70: (rand() & 0x7ff) / 2047 between lo and hi.
    const int32 r = std::uniform_int_distribution<int32>(0, 0x7ff)(rng);
    return lo + (hi - lo) * (static_cast<float32>(r) / 2047.0f);
}

void DirectorMilestones::start(int32 scene, int32 hunters, std::mt19937& rng, const std::array<int32, kStats>& counts)
{
    scene_ = scene;
    counts_ = counts;
    played_ = {};
    executionHit_ = false;
    time_ = 0.0f;
    sDelay_ = -1.0f;
    wasSeen_ = false;
    spottedGrace_ = -1.0f;
    wasHidden_ = false;
    hideOpen_ = false;
    hideStart_ = 0.0f;
    if (scene < 0)
    {
        ms_.fill(kOff);
        return;
    }
    auto set = [this](int32 i, int32 v) { ms_[static_cast<std::size_t>(i)] = static_cast<int8>(v); };
    // A share of the level's hunters, truncated (0x493c70 times 0x7917e4, then _ftol).
    auto share = [&](float32 lo, float32 hi) { return static_cast<int32>(static_cast<float32>(hunters) * randomFloat(lo, hi, rng)); };

    // Executions and kills (0x5b6490, switch 0x6771a8).
    if (scene == 0)
    {
        set(kX, 2), set(kX + 1, 3), set(kX + 2, randomInt(4, 6, rng));
        set(kKT, 1), set(kKT + 1, randomInt(2, 3, rng)), set(kKT + 2, randomInt(4, 6, rng));
    }
    else if (scene <= 11)
    {
        set(kX, share(0.1f, 0.2f)), set(kX + 1, share(0.2f, 0.5f));
        set(kX + 2, scene <= 4 ? share(0.5f, 1.0f) : kAfterMed);
        set(kKT, share(0.1f, 0.2f)), set(kKT + 1, share(0.2f, 0.5f)), set(kKT + 2, share(0.5f, 1.0f));
    }
    else if (scene <= 19 || scene == 23)
        for (int32 i = kX; i < kH; ++i)
            set(i, kOff);
    // A milestone the count already stands on is spent.
    if (const int32 c = category(Executions))
        set(kX + (c == 1 ? 0 : c == 4 ? 1 : 2), kOff);
    if (const int32 c = category(Kills))
        set(kKT + (c == 1 ? 0 : c == 4 ? 1 : 2), kOff);

    // Hiding (0x677120) and spotted (0x6770c0).
    if (scene <= 2 || (scene >= 12 && scene <= 19) || scene == 23)
        set(kH, kOff), set(kH + 1, kOff), set(kH + 2, kOff);
    else if (scene <= 11)
        set(kH, randomInt(4, 6, rng)), set(kH + 1, randomInt(8, 12, rng)), set(kH + 2, randomInt(14, 20, rng));
    if (scene == 0)
    {
        set(kS, randomInt(1, 2, rng)), set(kS + 1, randomInt(3, 4, rng)), set(kS + 2, randomInt(5, 6, rng));
        set(kS + 3, randomInt(7, 9, rng)), set(kS + 4, randomInt(10, 14, rng));
    }
    else if (scene <= 2)
    {
        set(kS, randomInt(2, 3, rng)), set(kS + 1, randomInt(4, 5, rng)), set(kS + 2, randomInt(6, 7, rng));
        set(kS + 3, randomInt(8, 10, rng)), set(kS + 4, randomInt(12, 14, rng));
    }
    else if (scene <= 11)
    {
        set(kS, randomInt(3, 6, rng)), set(kS + 1, kOff), set(kS + 2, kOff);
        set(kS + 3, randomInt(10, 12, rng)), set(kS + 4, randomInt(15, 20, rng));
    }
    else if (scene <= 19 || scene == 23)
        for (int32 i = kS; i < kS + 5; ++i)
            set(i, kOff);
    if (const int32 c = category(Hiding))
        set(kH + (c == 1 ? 0 : c == 4 ? 1 : 2), kOff);
    // (The exe's table 0x677098 spends the H bytes for S categories 4 and 5: kept.)
    if (const int32 c = category(Spotted))
        set(c <= 3 ? kS + c - 1 : kH + c - 3, kOff);
}

int32 DirectorMilestones::category(Stat s)
{
    const int32 x = counts_[Executions], k = counts_[Kills];
    auto at = [this](int32 i) { return static_cast<int32>(ms_[static_cast<std::size_t>(i)]); };
    switch (s)
    {
    case Executions: // 0x5b6b80
        if (x == at(kX))
            return 1;
        if (x == at(kX + 1))
            return 4;
        // 0xfe: past MED (an unsigned compare, so never past an unset MED) after a gruesome one.
        if (at(kX + 2) == kAfterMed && static_cast<uint32>(x) > static_cast<uint32>(at(kX + 1)))
        {
            if (!gruesome_)
                return 0;
            ms_[kX + 2] = kOff;
            return 5;
        }
        return x == at(kX + 2) ? 5 : 0;
    case Kills: // 0x5b6bf0
        return k == at(kKT) ? 1 : k == at(kKT + 1) ? 4 : k == at(kKT + 2) ? 5 : 0;
    case Hiding: // 0x5b6c30: from scene 4, more executions or kills than a milestone spend H's
    {
        if (scene_ > 2)
        {
            if (x > at(kX + 1) || k > at(kKT + 1))
                ms_[kH + 1] = kOff;
            if (x > at(kX + 2) || k > at(kKT + 2))
                ms_[kH + 2] = kOff;
        }
        const int32 h = counts_[Hiding];
        return h == at(kH) ? 1 : h == at(kH + 1) ? 4 : h == at(kH + 2) ? 5 : 0;
    }
    case Spotted: // 0x5b6cd0
        for (int32 c = 0; c < 5; ++c)
            if (counts_[Spotted] == at(kS + c))
                return c + 1;
        return 0;
    default:
        return 0;
    }
}

int32 DirectorMilestones::speechId(Stat s, int32 cat, int32 scene)
{
    const int32 set = scene <= 4 ? 0 : scene <= 8 ? 1 : 2;
    if (s == Spotted && set == 0)
        return cat >= 1 && cat <= 5 ? 18 + cat : -1; // SLOW1, SLOW2, SLOW3, SMED, SHIGH
    const int32 step = cat == 1 ? 0 : cat == 4 ? 1 : cat == 5 ? 2 : -1;
    if (step < 0)
        return -1;
    // LOW / MED / HIGH ids by scene set: X 1.., H 10.., S 24.. (after the five of set 0), KT 30..
    const int32 base = s == Executions ? 1 : s == Hiding ? 10 : s == Spotted ? 21 : 30;
    return base + set * 3 + step;
}

void DirectorMilestones::execution(int32 level)
{
    // 0x5b62e0; then 0x5b6030 adds the kill's points, and a gruesome one sets 0x7b7e2c.
    const int32 x = ++counts_[Executions];
    if (x == ms_[kX] || x == ms_[kX + 1] || x == ms_[kX + 2])
        executionHit_ = true;
    if (level >= 4)
        gruesome_ = true;
}

void DirectorMilestones::kill()
{
    // 0x5b63a0: the kill that ends an execution which reached an X milestone does not also
    // comment as a kill.
    const int32 k = ++counts_[Kills];
    if (executionHit_)
        for (int32 i = kKT; i < kKT + 3; ++i)
            if (k == ms_[static_cast<std::size_t>(i)])
            {
                ms_[static_cast<std::size_t>(i)] = kOff;
                break;
            }
    executionHit_ = false;
}

void DirectorMilestones::spotted()
{
    ++counts_[Spotted];
}

void DirectorMilestones::hidingEnded()
{
    // 0x5b5ef0: a spell in the shadows counts when it lasted more than 5 whole seconds.
    if (std::floor(time_ - hideStart_) > 5.0f)
        ++counts_[Hiding];
}

int32 DirectorMilestones::handler(Stat s, const Frame& f, bool& talking)
{
    const int32 cat = category(s);
    if (cat == 0 || played_[s][static_cast<std::size_t>(cat)])
        return -1;
    bool& played = played_[s][static_cast<std::size_t>(cat)];
    if (s == Executions)
    {
        // 0x414090 waits out a cut-scene; spoken over, the line is lost.
        if (f.cutscene)
            return -1;
        played = true;
        if (talking)
            return -1;
    }
    else if (s == Spotted)
    {
        // 0x414300: three seconds after the milestone, and only while a hunter still sees him
        // (and, in scene 1, not inside the gladiator court "aiarena").
        if (sDelay_ < 0.0f)
        {
            sDelay_ = time_ + 3.0f;
            return -1;
        }
        if (time_ < sDelay_)
            return -1;
        sDelay_ = -1.0f;
        played = true;
        if (f.cutscene || talking || !f.seen || (scene_ == 0 && f.inArena))
            return -1;
    }
    else
    {
        played = true; // 0x4141c0, 0x414530
        if (f.cutscene || talking)
            return -1;
    }
    const int32 id = speechId(s, cat, scene_);
    if (id >= 0)
        talking = true;
    return id;
}

int32 DirectorMilestones::update(const Frame& f)
{
    time_ += f.dt;
    // OnPlayerSpotted (0x541290): the first frame a hunter is after him, unless one lost him
    // less than 10 s ago.
    if (f.seen && !wasSeen_)
    {
        if (spottedGrace_ < 0.0f || time_ >= spottedGrace_)
            spotted();
        spottedGrace_ = -1.0f;
    }
    else if (f.seen)
        spottedGrace_ = -1.0f;
    else if (wasSeen_)
        spottedGrace_ = time_ + 10.0f;
    wasSeen_ = f.seen;

    // Hiding (0x49a98a OnEnteredSafeZone / 0x49a9e7 OnLeftSafeZone): a spell starts when he
    // steps into a safe zone unseen and ends when he leaves it or a hunter gets onto him.
    if (f.hidden && !wasHidden_ && !f.seen && !hideOpen_)
    {
        hideOpen_ = true;
        hideStart_ = time_;
    }
    else if (!f.hidden && wasHidden_ && hideOpen_)
    {
        hideOpen_ = false;
        hidingEnded();
    }
    if (hideOpen_ && f.seen)
    {
        hideOpen_ = false;
        hidingEnded();
    }
    wasHidden_ = f.hidden;

    // 0x413eb0: X, H, S, KT in turn while the scene index is 0..12. Once one speaks the rest
    // see the director talking (and lose their line).
    if (scene_ < 0 || scene_ > 12)
        return -1;
    bool talking = f.talking;
    int32 out = -1;
    for (Stat s : {Executions, Hiding, Spotted, Kills})
        if (const int32 id = handler(s, f, talking); id >= 0 && out < 0)
            out = id;
    return out;
}

} // namespace Game
