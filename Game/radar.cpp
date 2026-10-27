// Game/radar.cpp - the HUD radar. See radar.h.
#include "Game/radar.h"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {

constexpr float32 kAspect = 0.75f;                 // 480 / 640: frontend x units per y unit
constexpr float32 kRy = 0.231f * 0.5f;             // the disc's half height
constexpr float32 kRx = kRy * kAspect;             // ... and half width
constexpr float32 kCx = 0.04f + kRx, kCy = 0.8365f; // its middle
constexpr float32 kTick = 1.0f / 30.0f;            // the exe's frame step for the rings
constexpr float32 kLevelGap = 2.0f;                // above / below him (ours)

struct Rgb {
    uint8 r, g, b;
};
// Hunters by what they know of him (0x7ca4c0), script MAP_COLORs (0x5e20a0).
constexpr Rgb kHunter[3] = {{150, 170, 0}, {150, 85, 0}, {110, 0, 0}};
constexpr Rgb kScript[8] = {{0, 0, 0}, {164, 164, 164}, {150, 0, 0},  {150, 85, 0},
                            {150, 170, 0}, {0, 150, 0}, {64, 64, 210}, {0, 150, 150}};

uint8 dim(uint8 c) { return static_cast<uint8>(std::max(0, static_cast<int32>(c) - 20)); }

struct Screen {
    float32 x, y;  // frontend units
    float32 dist;  // metres from the middle
};

Screen toScreen(const RadarView& v, const Math::Vector3& p)
{
    const float32 dx = p.x - v.centre.x, dz = p.z - v.centre.z;
    const float32 s = std::sin(v.viewYaw), c = std::cos(v.viewYaw);
    const float32 ahead = dx * -s + dz * -c; // along the view
    const float32 side = dx * c + dz * -s;   // to its right
    return {kCx + side / Radar::kRange * kRx, kCy - ahead / Radar::kRange * kRy, std::sqrt(dx * dx + dz * dz)};
}

// A sprite `sizeM` metres tall at (x, y), its top turned to screen direction (dx, dy).
void sprite(Renderer::UiBatch& ui, const Renderer::Texture* tex, float32 x, float32 y, float32 sizeM, float32 dx, float32 dy,
            Rgb c, uint8 a)
{
    const float32 h = sizeM / Radar::kRange * kRy * 0.5f; // half height in y units
    const float32 len = std::sqrt(dx * dx + dy * dy);
    const float32 ux = len > 1e-6f ? dx / len : 0.0f, uy = len > 1e-6f ? dy / len : -1.0f; // up, square units
    const float32 rx = -uy, ry = ux;                                                    // right
    Renderer::UiQuad q;
    q.freeForm = true;
    const float32 cu[4] = {0.0f, 1.0f, 1.0f, 0.0f}, cv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    for (int k = 0; k < 4; ++k)
    {
        const float32 sr = cu[k] * 2.0f - 1.0f, su = 1.0f - cv[k] * 2.0f; // -1..1 right, up
        q.px[k] = x + (rx * sr + ux * su) * h * kAspect;
        q.py[k] = y + (ry * sr + uy * su) * h;
        q.pu[k] = cu[k];
        q.pv[k] = cv[k];
    }
    q.r = c.r;
    q.g = c.g;
    q.b = c.b;
    q.a = a;
    q.texture = tex;
    ui.add(q);
}

} // namespace

bool Radar::load(const TextureSet& frontend)
{
    ring_ = frontend.find("radar");
    dot_ = frontend.find("mappos");
    arrow_ = frontend.find("mapdir");
    tri_ = frontend.find("maptri");
    noise_ = frontend.find("mapnoiseent");
    return ring_ && dot_ && arrow_ && tri_;
}

void Radar::addNoise(const Math::Vector3& at, float32 radius)
{
    if (radius <= 0.0f)
        return;
    for (Ring& r : rings_)
        if (!r.on)
        {
            r = Ring{true, false, at, 0.0f, std::min(radius, kRange), 70.0f};
            return;
        }
}

void Radar::update(float32 dt, const Math::Vector3& playerFeet, float32 playerNoise)
{
    const float32 ticks = dt / kTick;
    bool playerRing = false;
    for (Ring& r : rings_)
    {
        if (!r.on)
            continue;
        if (r.player)
        {
            playerRing = true;
            r.at = playerFeet; // his ring follows him
        }
        if (r.radius < r.max)
            r.radius = std::min(r.max, r.radius + r.max / kRange * ticks);
        else if ((r.alpha -= 20.0f * ticks) <= 0.0f)
            r.on = false;
    }
    if (!playerRing && playerNoise > 0.0f)
        for (Ring& r : rings_)
            if (!r.on)
            {
                r = Ring{true, true, playerFeet, 0.0f, std::min(playerNoise, kRange), 70.0f};
                break;
            }
}

void Radar::draw(Renderer::UiBatch& ui, const RadarView& view) const
{
    // The disc.
    constexpr int kSegments = 24;
    auto rim = [](int i, float32 scale) {
        const float32 a = static_cast<float32>(i) / kSegments * 6.2831853f;
        return std::pair<float32, float32>{kCx + std::cos(a) * kRx * scale, kCy + std::sin(a) * kRy * scale};
    };
    for (int i = 0; i < kSegments; ++i)
    {
        const auto [ax, ay] = rim(i, 1.0f);
        const auto [bx, by] = rim(i + 1, 1.0f);
        ui.triangle(kCx, kCy, ax, ay, bx, by, 0x51, 0x51, 0x51, 0x80);
    }
    // Noise rings, clipped to it.
    if (noise_)
        for (const Ring& r : rings_)
        {
            if (!r.on || r.radius <= 0.0f)
                continue;
            const Screen s = toScreen(view, r.at);
            const float32 rx = r.radius / kRange * kRx, ry = r.radius / kRange * kRy;
            const uint8 a = static_cast<uint8>(std::clamp(r.alpha, 0.0f, 255.0f));
            auto point = [&](int i) {
                const float32 t = static_cast<float32>(i) / kSegments * 6.2831853f;
                float32 x = s.x + std::cos(t) * rx, y = s.y + std::sin(t) * ry;
                const float32 nx = (x - kCx) / kRx, ny = (y - kCy) / kRy, d = std::sqrt(nx * nx + ny * ny);
                if (d > 1.0f)
                {
                    x = kCx + nx / d * kRx;
                    y = kCy + ny / d * kRy;
                }
                return std::array<float32, 4>{x, y, 0.5f + (x - s.x) / (2.0f * rx), 0.5f + (y - s.y) / (2.0f * ry)};
            };
            float32 cxs = s.x, cys = s.y;
            if (const float32 nx = (cxs - kCx) / kRx, ny = (cys - kCy) / kRy, d = std::sqrt(nx * nx + ny * ny); d > 1.0f)
            {
                cxs = kCx + nx / d * kRx;
                cys = kCy + ny / d * kRy;
            }
            const float32 cu = 0.5f + (cxs - s.x) / (2.0f * rx), cv = 0.5f + (cys - s.y) / (2.0f * ry);
            for (int i = 0; i < kSegments; ++i)
            {
                const auto p = point(i), q = point(i + 1);
                ui.triangle(cxs, cys, p[0], p[1], q[0], q[1], static_cast<uint8>(a / 2), static_cast<uint8>(a / 5),
                            static_cast<uint8>(a / 5), 255, noise_, cu, cv, p[2], p[3], q[2], q[3], Renderer::UiBlend::Additive);
            }
        }
    // The ring.
    Renderer::UiQuad ring;
    ring.x0 = 0.04f - 0.75f * 0.005f;
    ring.y0 = 0.716f;
    ring.x1 = ring.x0 + 0.75f * 0.005f * 2.0f + kRx * 2.0f;
    ring.y1 = ring.y0 + 0.241f;
    ring.r = ring.g = ring.b = 0;
    ring.texture = ring_;
    ui.add(ring);
    // The player in the middle.
    sprite(ui, dot_, kCx, kCy, 3.2f, 0.0f, -1.0f, {dim(0xa4), dim(0xa4), dim(0xa4)}, 180);
    // Blips.
    for (const RadarBlip& b : view.blips)
    {
        Screen s = toScreen(view, b.position);
        const float32 dy = b.position.y - view.centre.y;
        const int32 level = dy > kLevelGap ? 2 : (dy < -kLevelGap ? 1 : 0); // 2 above, 1 below
        uint8 alpha = 180;
        if (b.kind == RadarBlip::Kind::Script)
        {
            const Rgb c = kScript[std::clamp(b.colour, 0, 7)];
            const Rgb shade{dim(c.r), dim(c.g), dim(c.b)};
            if (s.dist >= kRange)
            {
                const float32 k = kRange / std::max(s.dist, 1e-3f);
                sprite(ui, dot_, kCx + (s.x - kCx) * k, kCy + (s.y - kCy) * k, 5.8f, 0.0f, -1.0f, shade, alpha);
                continue;
            }
            if (level != 0)
                sprite(ui, tri_, s.x, s.y, 5.8f, 0.0f, level == 2 ? -1.0f : 1.0f, shade, 160);
            else
                sprite(ui, dot_, s.x, s.y, 4.8f, 0.0f, -1.0f, shade, alpha);
            continue;
        }
        if (s.dist >= kRange)
            continue; // hunters past the rim are left off
        Rgb c{0, 0, 0};
        if (b.kind == RadarBlip::Kind::Hunter)
        {
            const Rgb t = kHunter[std::clamp(b.state, 0, 2)];
            c = {dim(t.r), dim(t.g), dim(t.b)};
            alpha = static_cast<uint8>(std::min(180.0f, 255.0f * std::clamp(b.visibility, 0.0f, 1.0f)));
        }
        if (alpha == 0)
            continue;
        if (level != 0)
        {
            sprite(ui, tri_, s.x, s.y, 5.8f, 0.0f, level == 2 ? -1.0f : 1.0f, c, std::min<uint8>(alpha, 160));
            continue;
        }
        // Its heading on the radar: our forward (-sin yaw, -cos yaw) against the view's.
        const float32 rel = b.yaw - view.viewYaw;
        sprite(ui, arrow_, s.x, s.y, 5.8f, -std::sin(rel), -std::cos(rel), c, alpha);
    }
}

} // namespace Game
