// Game/ai/senses.cpp - hunter perception and reaction. See senses.h.
#include "Game/ai/senses.h"

#include <algorithm>
#include <cmath>

#include "Math/math.h"

namespace Game {

SenseLevel levelAbove(float32 v, const LevelThresholds& t)
{
    for (std::size_t i = 0; i < t.size(); ++i)
        if (v > t[i])
            return kVeryHigh - static_cast<SenseLevel>(i);
    return kNoLevel;
}

SenseLevel levelBelow(float32 v, const LevelThresholds& t)
{
    for (std::size_t i = 0; i + 1 < t.size(); ++i)
        if (v < t[i])
            return kVeryHigh - static_cast<SenseLevel>(i);
    return v <= t.back() ? kVeryLow : kNoLevel;
}

bool inViewFrustum(const SightInput& in, const VisionData& v)
{
    float32 width = 0.0f;
    switch (in.gait)
    {
    case Gait::Stand: width = in.hardDifficulty ? v.widthStandHard : v.widthStandEasy; break;
    case Gait::Walk: width = in.hardDifficulty ? v.widthWalkHard : v.widthWalkEasy; break;
    case Gait::Run: width = in.hardDifficulty ? v.widthRunHard : v.widthRunEasy; break;
    }
    if (in.distance < v.range && in.horizontalAngle <= Math::radians(width * 0.5f) &&
        in.verticalAngle <= Math::radians(v.height * 0.5f))
        return true;
    // Peripheral vision: standing still, the player close by.
    return in.gait == Gait::Stand && in.distance < v.checkPeripheral && in.distance < v.peripheralRange &&
           in.horizontalAngle <= Math::radians(v.peripheralWidth * 0.5f) &&
           in.verticalAngle <= Math::radians(v.peripheralHeight * 0.5f);
}

SenseLevel sightLevel(const SightInput& in, const VisionData& v)
{
    if (in.distance >= v.range || in.visible <= 0.0f || !inViewFrustum(in, v))
        return kNoLevel;
    const bool face = in.faceInView && in.distance < v.face;
    if (face && in.distance < v.faceDefinite)
        return kVeryHigh;
    if (face && v.useFaceLight)
        return levelAbove(in.faceLight, v.faceLight);
    if (!v.useDistance && !v.useLight && !v.useVisible)
        return kNoLevel;
    int32 sum = 0, count = 0;
    auto add = [&](bool use, SenseLevel l) {
        if (!use)
            return true;
        if (l == kNoLevel)
            return false;
        sum += l;
        ++count;
        return true;
    };
    if (!add(v.useVisible, levelAbove(in.visible, v.visible)) || !add(v.useLight, levelAbove(in.light, v.light)) ||
        !add(v.useDistance, levelBelow(std::min(in.distance / v.range, 1.0f), v.distance)))
        return kNoLevel;
    return count > 0 ? sum / count : kNoLevel; // truncated, as the exe's float -> int
}

HeardSound hearSound(const SoundType& type, float32 distance, bool occluded, const HearingData& h, float32 background)
{
    HeardSound out;
    if (type.volume < 0 || type.volume > kVeryHigh || distance < 0.0f)
        return out;
    float32 left = h.volumeDistance[static_cast<std::size_t>(type.volume)] - distance;
    if (left < 0.0f)
        return out;
    if (background > 0.0f && (left -= background) <= 0.0f)
        return out;
    if (occluded && (left -= h.bspGeneral) <= 0.0f)
        return out;
    for (std::size_t i = 0; i < h.volumeDistance.size(); ++i)
        if (left <= h.volumeDistance[i])
        {
            out.volume = static_cast<SenseLevel>(i);
            break;
        }
    if (out.volume == kNoLevel)
        return out;
    out.interest = type.interest;
    if (type.confusion)
        for (SenseLevel l = out.volume; l <= h.confusion && l != type.volume && out.interest > 0; ++l)
            --out.interest;
    return out;
}

const char* footstepSound(float32 speedFraction, bool sneaking, const HearingData& h)
{
    if (sneaking)
        return "SNEAKING";
    if (speedFraction >= h.runFraction)
        return "RUNNING";
    if (speedFraction >= h.walkFraction)
        return "WALKING";
    return nullptr;
}

bool updateAwareness(Awareness& a, SenseLevel sight, SenseLevel hearing, float32 dt, const AlertData& d)
{
    const AiAction before = a.action;
    auto meets = [&](AiAction from, AiAction to) {
        const ActionTransition& t = d.transition(from, to);
        if (!t.valid)
            return false;
        return (t.sight != kNoLevel && sight != kNoLevel && sight >= t.sight) ||
               (t.hearing != kNoLevel && hearing != kNoLevel && hearing >= t.hearing);
    };
    auto start = [&](AiAction to) {
        a.action = to;
        a.unsustained = 0.0f;
        const SenseLevel raise = d.setAlert[static_cast<std::size_t>(to)];
        if (raise != kNoLevel && raise >= a.alert)
        {
            a.alert = raise;
            a.alertTime = 0.0f;
        }
    };

    // Up to the highest action whose condition is met, at once.
    for (int32 to = static_cast<int32>(AiAction::Combat); to > static_cast<int32>(a.action); --to)
        if (meets(a.action, static_cast<AiAction>(to)))
        {
            start(static_cast<AiAction>(to));
            return true;
        }

    // A reaction (investigate / chase / combat) lasts while its stay condition holds, and for
    // SUSTAIN_ACTION seconds after; the alert level does not fade meanwhile.
    if (a.action >= AiAction::Investigate)
    {
        if (meets(a.action, a.action))
            a.unsustained = 0.0f;
        else if ((a.unsustained += dt) > d.sustain[static_cast<std::size_t>(a.action)])
            a.action = AiAction::Hunt; // resolved below by the alert level
        else
            return false;
        if (a.action >= AiAction::Investigate)
            return false;
    }

    if (a.alert != kNoLevel && (a.alertTime += dt) >= d.degradeSeconds)
    {
        a.alertTime = 0.0f;
        --a.alert; // very_low drops to none (-1)
    }
    a.action = a.alert != kNoLevel ? AiAction::Hunt : AiAction::NotOnAlert;
    return a.action != before;
}

} // namespace Game
