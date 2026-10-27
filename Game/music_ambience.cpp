// Game/music_ambience.cpp - the level music's ambience channels. See music_ambience.h.
#include "Game/music_ambience.h"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {

constexpr float32 kFrameSeconds = 1.0f / 30.0f; // 0x7a9e20: 33.3 ms
constexpr float32 kBedSeconds = 0.5f;           // 0x7a9e38
constexpr float32 kPlacedSpeed = 381.5f;        // 0x3ec3126f: 0.3815 a millisecond

// Rounded as the exe's ROUND and fix-up do (to nearest).
int32 roundi(float32 v) { return static_cast<int32>(std::lround(v)); }

} // namespace

std::array<int32, 2> MusicAmbience::slotDefaults(int32 sample)
{
    switch (sample)
    {
    case kGeneratorStart:
    case kGeneratorStop: return {90, 30};
    case kCraneStart:
    case kCraneStop: return {127, 30};
    case kLiftStart:
    case kLiftStop: return {100, 55};
    case kToggleElectro: return {63, 12};
    case 0x36e9: // PISS
    case 0x36ea: return {90, 25}; // ZIP
    case 0x36f6: return {70, 30};  // TRAMP_BUZZER
    case 0x36f8: return {100, 30}; // WALL_FALL
    case 0x36f9: return {127, 30}; // BODY_BAG
    case 0x36fa:                   // PHONE
    case 0x36fb: return {100, 60}; // PHONE_FILTERED
    default: return {-1, -1};
    }
}

int32 MusicAmbience::falloff(int32 volume, float32 range, float32 distance)
{
    const float32 near = range * 0.1f;
    if (distance < near)
        return volume;
    const float32 t = std::min(1.0f, (distance - near) / std::max(range - near, 1e-3f));
    return roundi((std::cos(t * 3.14159265f) + 1.0f) / 2.0f * static_cast<float32>(volume));
}

void MusicAmbience::reset(int32 level, const Math::Vector3& chair)
{
    *this = MusicAmbience{};
    level_ = level;
    generator_.volume = 40;
    crane_.volume = 127;
    lift_.volume = 40;
    electro_.volume = 40;
    electro_.at = chair;
}

void MusicAmbience::slotPlayed(int32 sample, int32 volume, int32 range, uint32 entity, const Math::Vector3* at, float32 now)
{
    Machine* m = nullptr;
    bool on = false;
    switch (sample)
    {
    case kGeneratorStart: on = true; [[fallthrough]];
    case kGeneratorStop: m = &generator_; break;
    case kCraneStart: on = true; [[fallthrough]];
    case kCraneStop: m = &crane_; break;
    case kLiftStart: on = true; [[fallthrough]];
    case kLiftStop: m = &lift_; break;
    case kToggleElectro:
    {
        // 0x5a6550: over, with the volume and range; a fade of 4 frames.
        const std::array<int32, 2> own = slotDefaults(sample);
        electro_.on = !electro_.on;
        electro_.volume = volume < 0 ? own[0] : volume;
        electro_.range = range < 0 ? own[1] : range;
        electro_.fade = 4;
        return;
    }
    default: return;
    }
    const std::array<int32, 2> own = slotDefaults(sample);
    m->on = on;
    m->volume = volume < 0 ? own[0] : volume;
    m->range = range < 0 ? own[1] : range;
    // Followed from an entity, else at the place given; a stop forgets both.
    m->entity = on ? entity : 0;
    m->at = on && !entity && at ? *at : Math::Vector3{0.0f, -1000.0f, 0.0f};
    m->since = now;
    if (m == &generator_)
    {
        m->delay = on ? 0x25 : 0; // the start sample first
        m->fade = on ? 4 : 0xb;
    }
    else if (m == &crane_)
    {
        m->delay = on ? 0x21 : 0;
        m->fade = on ? 0x21 : 0x11;
    }
}

bool MusicAmbience::where(const Machine& m, const Inputs& in, Math::Vector3& at) const
{
    if (m.entity)
        return in.entityAt && in.entityAt(m.entity, at);
    at = m.at;
    return m.at.y != -1000.0f;
}

void MusicAmbience::fadeFrames(Channel& c, int32 target, int32& fade, int32 current)
{
    // 0x5a2730 and its kin: an unchanged volume sets the fade back to 10 frames; a new one goes
    // there over the fade.
    if (current == target)
    {
        fade = 10;
        return;
    }
    c.set = true;
    c.volume = target;
    c.seconds = static_cast<float32>(fade) * kFrameSeconds;
}

MusicAmbience::Frame MusicAmbience::update(const Inputs& in, float32 dt)
{
    Frame f;
    const float32 frames = dt / kFrameSeconds;
    auto heard = [&](const Math::Vector3& at) { return in.heard ? in.heard(at) : 1.0f; };
    auto bed = [&](Channel& c, int32 v, float32 seconds) {
        c.set = true;
        c.volume = v;
        c.seconds = seconds;
    };
    auto placed = [&](Channel& c, int32 v) {
        c.set = true;
        c.volume = v;
        c.speed = kPlacedSpeed;
    };
    auto place = [](Channel& c, const Math::Vector3& at) {
        c.placed = true;
        c.position = at;
    };
    Channel &c8 = f[0], &c9 = f[1], &c10 = f[2], &c11 = f[3];

    // --- channels 10 / 11 ---
    switch (level_)
    {
    case 20:
    case 24: break; // Attic, Weasel: nothing
    case 16:
        // Subway (0x5a3080): a bed at 100 (1 - 0 / 1.11).
        bed(c10, 100, kBedSeconds);
        bed(c11, 100, kBedSeconds);
        break;
    case 18:
    {
        // Estate_Ext (0x5a32e0): 110 x (1 - frames since a gunshot / 300), over 33 ms while it
        // comes back.
        const int32 v = roundi(110.0f * (1.0f - hush_ / 300.0f));
        const float32 s = hush_ > 0.0f ? kFrameSeconds : kBedSeconds;
        bed(c10, v, s);
        bed(c11, v, s);
        hush_ = std::max(0.0f, hush_ - frames);
        break;
    }
    case 15:
    {
        // Journo_Streets' generator (0x5a3c30): 2/3 its volume, channel 10 within 1.5 x its
        // range (heard), channel 11 within 4 x at half.
        int32 v10 = 0, v11 = 0;
        Math::Vector3 at;
        if (generator_.on && where(generator_, in, at))
        {
            const float32 d = (at - in.player).length();
            const int32 vol = roundi(static_cast<float32>(generator_.volume) * 0.667f);
            const float32 r1 = static_cast<float32>(generator_.range) * 1.5f, r2 = static_cast<float32>(generator_.range) * 4.0f;
            if (d <= r1 || d <= r2)
            {
                if (in.frame % 10 == 2)
                    generator_.heard = heard(at);
                place(c10, at);
                place(c11, at);
            }
            if (d <= r1)
                v10 = roundi(static_cast<float32>(falloff(vol, r1, d)) * generator_.heard);
            if (d <= r2)
                v11 = roundi(static_cast<float32>(falloff(vol, r2, d)) * 0.5f);
        }
        // 0x5a2730: nothing until the delay after the slot has passed.
        if (generator_.delay > 0 && in.now - generator_.since < static_cast<float32>(generator_.delay) * kFrameSeconds)
            break;
        fadeFrames(c10, v10, generator_.fade, target_[2]);
        fadeFrames(c11, v11, generator_.fade, target_[3]);
        break;
    }
    case 5:
    {
        // Scrap2's crane (0x5a4000): channel 10 alone, within its range.
        int32 v = 0;
        Math::Vector3 at;
        if (crane_.on && where(crane_, in, at))
        {
            const float32 d = (at - in.player).length();
            if (d <= static_cast<float32>(crane_.range))
            {
                place(c10, at);
                v = falloff(crane_.volume, static_cast<float32>(crane_.range), d);
            }
        }
        if (crane_.delay > 0 && in.now - crane_.since < static_cast<float32>(crane_.delay) * kFrameSeconds)
            break;
        fadeFrames(c10, v, crane_.fade, target_[2]);
        break;
    }
    case 11:
    {
        // Asylum's electric chair switched on (0x5a4500): channel 10 within its range (heard
        // x 2 - 1), channel 11 within 3.6 x at half.
        int32 v10 = 0, v11 = 0;
        if (electro_.on)
        {
            const float32 d = (electro_.at - in.player).length();
            const float32 r1 = static_cast<float32>(electro_.range), r2 = r1 * 3.6f;
            if (d <= r1 || d <= r2)
            {
                if (in.frame % 10 == 6)
                    electro_.heard = std::max(0.0f, heard(electro_.at) * 2.0f - 1.0f);
                place(c10, electro_.at);
                place(c11, electro_.at);
            }
            if (d <= r1)
                v10 = roundi(static_cast<float32>(falloff(electro_.volume, r1, d)) * electro_.heard);
            if (d <= r2)
                v11 = roundi(static_cast<float32>(falloff(electro_.volume, r2, d)) * 0.5f);
        }
        fadeFrames(c10, v10, electro_.fade, target_[2]);
        fadeFrames(c11, v11, electro_.fade, target_[3]);
        break;
    }
    default:
    {
        // The rain (0x5a2df0).
        const int32 v = roundi(127.0f * std::clamp(in.rain, 0.0f, 1.0f));
        bed(c10, v, kBedSeconds);
        bed(c11, v, kBedSeconds);
        break;
    }
    }

    // --- channel 9 (and 8) ---
    auto nearest = [&](bool liveOnly, float32& best) {
        const Source* s = nullptr;
        best = 1000.0f;
        for (const Source& f : in.fires)
            if (!liveOnly || f.live)
                if (const float32 d = (f.position - in.player).length(); d < best)
                {
                    best = d;
                    s = &f;
                }
        return s;
    };
    switch (level_)
    {
    case 20:
    case 24: break;
    case 10:
    {
        // Pharm_Wks' lift (0x5a42c0): within its range, heard.
        int32 v = 0;
        Math::Vector3 at;
        if (lift_.on && where(lift_, in, at))
        {
            const float32 d = (at - in.player).length();
            if (d <= static_cast<float32>(lift_.range))
            {
                if (in.frame % 10 == 4)
                    lift_.heard = heard(at);
                v = roundi(static_cast<float32>(falloff(lift_.volume, static_cast<float32>(lift_.range), d)) * lift_.heard);
                place(c9, at);
            }
        }
        placed(c9, v);
        break;
    }
    case 11:
    {
        // Asylum's electric chair while it is off (0x5a4940): twice its volume (at most 127)
        // within 1.8 x its range, heard.
        int32 v = 0;
        if (!electro_.on)
        {
            const int32 vol = std::min(electro_.volume * 2, 0x7f);
            const float32 r = static_cast<float32>(electro_.range) * 1.8f;
            const float32 d = (electro_.at - in.player).length();
            if (d <= r)
            {
                if (in.frame % 10 == 1)
                    electro_.heard = heard(electro_.at);
                v = roundi(static_cast<float32>(falloff(vol, r, d)) * electro_.heard);
                place(c9, electro_.at);
            }
        }
        placed(c9, v);
        break;
    }
    case 18:
    {
        // Estate_Ext (0x5a38f0): the nearest fire (burning or not) within 30 m at 120 on 9,
        // within 15 m on 8 too.
        int32 v9 = 0, v8 = 0;
        float32 d = 0.0f;
        if (const Source* s = nearest(false, d); s && d <= 30.0f)
        {
            if (in.frame % 10 == 0)
                fireHeard_ = heard(s->position);
            v9 = roundi(static_cast<float32>(falloff(0x78, 30.0f, d)) * fireHeard_);
            place(c9, s->position);
            if (d <= 15.0f)
            {
                v8 = roundi(static_cast<float32>(falloff(0x78, 15.0f, d)) * fireHeard_);
                place(c8, s->position);
            }
        }
        placed(c9, v9);
        placed(c8, v8);
        break;
    }
    default:
    {
        // A fire's crackle (0x5a3690): the nearest burning within 16 m at 90.
        int32 v = 0;
        float32 d = 0.0f;
        if (const Source* s = nearest(true, d); s && d <= 16.0f)
        {
            if (in.frame % 10 == 0)
                fireHeard_ = heard(s->position);
            v = roundi(static_cast<float32>(falloff(0x5a, 16.0f, d)) * fireHeard_);
            place(c9, s->position);
        }
        placed(c9, v);
        break;
    }
    }

    // Only a change of volume restarts the fade (the placed channels' speed is fixed).
    for (std::size_t i = 0; i < f.size(); ++i)
    {
        if (!f[i].set)
            continue;
        if (f[i].volume == target_[i])
            f[i].set = false;
        else
            target_[i] = f[i].volume;
    }
    return f;
}

} // namespace Game
