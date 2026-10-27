// Game/coronas.cpp - see coronas.h.
#include "Game/coronas.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Game {

namespace {

constexpr float32 kRange = 60.0f;     // 0x7a7b4c
constexpr float32 kNearSize = 6.0f;   // 0x7a7b40
constexpr float32 kFarSize = 0.2f;    // 0x7a7b44
constexpr float32 kAspect = 0.75f;    // 0x7d3458
constexpr float32 kPull = 0.55f;      // 0x7a7c94
constexpr float32 kNearest = 5.0f;    // 0x7a7c9c
constexpr float32 kBright = 30.0f;    // 0x7a7cac (also the lens flare's reach)
// The lens flare's ghosts: their place along the line through the middle, size, and colour shares.
constexpr float32 kGhostAt[15] = {-0.7f, -0.4f, 0.1f, 0.3f, 0.35f, 0.5f, 0.65f, 0.8f, 1.1f, 1.5f, 1.7f, 1.85f, 2.2f, 2.7f, 3.0f};
constexpr float32 kGhostSize[15] = {0.9f, 0.7f, 0.5f, 0.8f, 0.2f, 1.2f, 1.0f, 0.9f, 1.1f, 0.8f, 0.8f, 1.1f, 0.3f, 1.1f, 0.7f};
constexpr float32 kGhostR[15] = {0.144f, 0.12f, 0.144f, 0.12f, 0.12f, 0.12f, 0.144f, 0.144f, 0.12f, 0.144f, 0.12f, 0.12f, 0.12f, 0.12f, 0.144f};
constexpr float32 kGhostG[15] = {0.12f, 0.144f, 0.12f, 0.144f, 0.12f, 0.12f, 0.12f, 0.144f, 0.144f, 0.12f, 0.12f, 0.144f, 0.144f, 0.12f, 0.12f};
constexpr float32 kGhostB[15] = {0.12f, 0.144f, 0.12f, 0.12f, 0.144f, 0.12f, 0.144f, 0.12f, 0.12f, 0.12f, 0.12f, 0.144f, 0.12f, 0.144f, 0.12f};

} // namespace

Coronas::Light Coronas::fromParams(const std::vector<int32>& params, const Math::Vector3& at, uint16 seed)
{
    Light l;
    l.at = at;
    auto f = [&](std::size_t i, float32 fallback) { return i < params.size() ? std::bit_cast<float32>(params[i]) : fallback; };
    auto toByte = [](float32 c) { return static_cast<int32>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f)); };
    l.r = toByte(f(3, 1.0f));
    l.g = toByte(f(4, 1.0f));
    l.b = toByte(f(5, 1.0f));
    const float32 size = f(13, 1.0f);
    l.nearW = kNearSize * size * kAspect;
    l.nearH = kNearSize * size;
    l.farW = kFarSize * size * kAspect;
    l.farH = kFarSize * size;
    l.range = kRange;
    l.intensity = params.size() > 14 ? static_cast<float32>(params[14]) : 0.0f;
    auto flag = [&](std::size_t i) { return i < params.size() && (params[i] & 1) != 0; };
    l.flags = 5u | (flag(10) ? 0x20u : 0u) | (flag(11) ? 0x40u : 0u) | (flag(12) ? 0x80u : 0u);
    l.seed = seed;
    return l;
}

bool Coronas::add(const Light& light)
{
    if (lights_.size() >= kMax)
        return false;
    lights_.push_back(light);
    return true;
}

std::vector<Coronas::Sprite> Coronas::frame(const View& view, std::mt19937& rng, const std::vector<Light>* extra) const
{
    std::vector<Sprite> out;
    std::vector<const Light*> all;
    for (const Light& l : lights_)
        all.push_back(&l);
    if (extra)
        for (const Light& l : *extra)
            all.push_back(&l);
    for (const Light* lp : all)
    {
        const Light& l = *lp;
        const float32 range = std::min(l.range, view.farClip);
        if ((l.flags & 3u) == 0)
            continue;
        if ((l.flags & 0x20u) && ((static_cast<uint32>(l.seed) * 0x76cu + view.timeMs) & 0x1fffu) < 2000u &&
            ((view.frame + l.seed) & 3u) != 0)
            continue; // flickered off (0x59e080)
        const Math::Vector3 toLight = l.at - view.eye;
        const float32 d = toLight.length();
        if (d > range || d < 1e-3f)
            continue;
        Math::Vector3 at = l.at;
        if (d > kPull)
        {
            at = view.eye + toLight * ((d - kPull) / d);
            if (view.clear && !view.clear(at, l.at))
                at = l.at;
        }
        if (view.clear && !view.clear(view.eye, l.at))
            continue;
        const auto screen = view.project ? view.project(at) : std::nullopt;
        if (!screen)
            continue;
        const float32 nearest = std::max(d, kNearest);
        const float32 w = std::max(l.nearW / nearest * (view.fog + 1.0f), l.farW);
        const float32 h = std::max(l.nearH / nearest * (view.fog + 1.0f), l.farH);
        if (l.flags & 0x100u)
        {
            // A torch's beam: 13 sprites stepping out along its axis.
            float32 t = 0.0f, step = 0.077f, along = 0.0f;
            for (int32 k = 0; k < 13; ++k)
            {
                t += step;
                step -= 0.002f;
                const float32 bw = w * t * 0.274f, bh = h * t * 0.274f;
                const auto p = view.project ? view.project(l.at + l.axis * along) : std::nullopt;
                if (!p)
                    break;
                along += t * 0.274f * 0.1f * p->z;
                Sprite b;
                b.x0 = p->x - bw * 0.5f;
                b.y0 = p->y - bh * 0.5f;
                b.x1 = b.x0 + bw;
                b.y1 = b.y0 + bh;
                b.r = static_cast<uint8>(std::clamp(l.r, 0, 255));
                b.g = static_cast<uint8>(std::clamp(l.g, 0, 255));
                b.b = static_cast<uint8>(std::clamp(l.b, 0, 255));
                out.push_back(b);
            }
            continue;
        }
        float32 bright = l.intensity - l.intensity * d / range;
        if (!view.paused)
            bright *= static_cast<float32>(rng() & 0xffu) * 0.0005859375f + 0.85f;
        bright = std::max(bright, l.minIntensity);
        const float32 k = bright / 255.0f * (view.scripted ? 0.25f : 1.0f);
        int32 r = static_cast<int32>(std::lround(static_cast<float32>(l.r) * k));
        int32 g = static_cast<int32>(std::lround(static_cast<float32>(l.g) * k));
        int32 b = static_cast<int32>(std::lround(static_cast<float32>(l.b) * k));
        if (static_cast<float32>((r + g + b) / 3) > kBright)
        {
            const int32 top = std::max({r, g, b, 1});
            const float32 s = static_cast<float32>(rng() % 5u + 25u) / static_cast<float32>(top);
            r = static_cast<int32>(std::lround(static_cast<float32>(r) * s));
            g = static_cast<int32>(std::lround(static_cast<float32>(g) * s));
            b = static_cast<int32>(std::lround(static_cast<float32>(b) * s));
        }
        // The lens flare (0x40, GAME DETAIL on, nearer than 30 m).
        if ((l.flags & 0x40u) && view.detail > 0 && screen->z < kBright)
        {
            const float32 dx = 0.5f - screen->x, dy = 0.5f - screen->y;
            const float32 off = std::sqrt(dx * dx + dy * dy) * screen->z;
            float32 strength = off < 1.0f ? 0.0f : off < 5.0f ? (off - 1.0f) / 4.0f : 1.0f;
            if (screen->z > 20.0f)
                strength *= 1.0f - (screen->z - 20.0f) / 10.0f;
            float32 gw = w, gh = h;
            if (gw > 0.5f || gh > 0.5f)
            {
                const float32 k2 = 0.5f / std::max(gw, gh);
                gw *= k2;
                gh *= k2;
            }
            for (int32 i = view.detail == 1 ? 1 : 0; i < 15 && strength > 0.0f; i += view.detail == 1 ? 2 : 1)
            {
                Sprite g2;
                g2.x0 = dx * kGhostAt[i] + screen->x - gw * 0.5f * kGhostSize[i];
                g2.y0 = dy * kGhostAt[i] + screen->y - gh * 0.5f * kGhostSize[i];
                g2.x1 = g2.x0 + gw * kGhostSize[i];
                g2.y1 = g2.y0 + gh * kGhostSize[i];
                g2.r = static_cast<uint8>(std::clamp(static_cast<int32>(std::lround(static_cast<float32>(l.r) * strength * kGhostR[i])), 0, 255));
                g2.g = static_cast<uint8>(std::clamp(static_cast<int32>(std::lround(static_cast<float32>(l.g) * strength * kGhostG[i])), 0, 255));
                g2.b = static_cast<uint8>(std::clamp(static_cast<int32>(std::lround(static_cast<float32>(l.b) * strength * kGhostB[i])), 0, 255));
                out.push_back(g2);
            }
        }
        if (r <= 0 && g <= 0 && b <= 0)
            continue;
        Sprite s;
        s.x0 = screen->x - w * 0.5f;
        s.y0 = screen->y - h * 0.5f;
        s.x1 = s.x0 + w;
        s.y1 = s.y0 + h;
        s.r = static_cast<uint8>(std::clamp(r, 0, 255));
        s.g = static_cast<uint8>(std::clamp(g, 0, 255));
        s.b = static_cast<uint8>(std::clamp(b, 0, 255));
        out.push_back(s);
    }
    return out;
}

} // namespace Game
