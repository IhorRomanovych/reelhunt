// Game/hud.cpp - the in-game HUD. See hud.h.
#include "Game/hud.h"

#include <algorithm>
#include <cmath>

#include "Assets/txd.h"

namespace Game {

namespace {

constexpr float32 kIconX = 0.84f, kIconY = 0.768f, kIconH = 0.21875f;
constexpr float32 kIconW = kIconH * 0.5f * 0.75f; // a 64x128 texel icon on a 4:3 frontend
constexpr float32 kFlashHz = 3.0f;
constexpr float32 kBarX = 0.929f, kBarY = 0.762f, kBarW = 0.75f * 0.0625f, kBarH = 0.195312f;
constexpr float32 kFillFull = 0.189453f, kBarStep = 0.015625f;

// One bar: its frame, then the fill rising from the bottom (`share` of the full fill).
void drawBar(Renderer::UiBatch& ui, const Renderer::Texture* frame, const Renderer::Texture* fill, float32 x, float32 share,
             uint8 r, uint8 g, uint8 b, float32 y = kBarY, uint8 fr = 0, uint8 fg = 0, uint8 fb = 0, bool fillOwn = false)
{
    Renderer::UiQuad q;
    q.x0 = x;
    q.y0 = y;
    q.x1 = x + kBarW;
    q.y1 = y + kBarH;
    q.r = r;
    q.g = g;
    q.b = b;
    q.texture = frame;
    ui.add(q);
    const float32 h = std::clamp(share, 0.0f, 1.0f) * kFillFull;
    if (h <= 0.0f)
        return;
    if (fillOwn)
    {
        q.r = fr;
        q.g = fg;
        q.b = fb;
    }
    q.texture = fill; // added on (blend ONE, ONE: 0x5f5c90(2, 2)); the fill art has no alpha
    q.blend = Renderer::UiBlend::Additive;
    q.y0 = y + (kBarH - h);
    q.v0 = (kBarH - h) / kBarH;
    ui.add(q);
}

} // namespace

float32 Hud::barX(std::size_t i)
{
    return kBarX + kBarStep * static_cast<float32>(i + 1);
}

bool Hud::load(const std::string& gameRoot)
{
    auto txd = Assets::Txd::load(gameRoot + "pictures/frontend_pc.txd");
    if (!txd)
        return false;
    textures_.add(*txd);
    sneak_ = textures_.find("mansneak");
    run_ = textures_.find("manrun");
    tri_ = textures_.find("mantri");
    static constexpr const char* kContext[] = {"mansquashen", "mandrop", "manFollow", "manStop", "manpickup",
                                               "manUse", "mansquashst", "manpickup", "manKick"};
    for (std::size_t i = 0; i < context_.size(); ++i)
        context_[i] = textures_.find(kContext[i]);
    bar_ = textures_.find("hs_bar");
    barFill_ = textures_.find("hs_barfill");
    return sneak_ && run_;
}

void Hud::draw(Renderer::UiBatch& ui, const HudState& s, float32 time) const
{
    const bool blinkOff = std::fmod(time, 0.512f) >= 0.256f;
    if (bar_ && barFill_)
    {
        if (s.showHealth && !(s.flashHealth && blinkOff))
        {
            float32 pct = std::clamp(s.health, 0.0f, 1.0f) * 100.0f;
            if (pct > 0.0f && pct < 1.0f)
                pct = 1.0f;
            pct = std::min(100.0f, pct + 3.0f);
            if (s.godMode)
                drawBar(ui, bar_, barFill_, kBarX, pct / 100.0f, 0x70, 0x91, 0x45, kBarY, 0xff, 0, 0, true);
            else
                drawBar(ui, bar_, barFill_, kBarX, pct / 100.0f, 0x70, 0x91, 0x45);
            // The buddy's and the boss's bars, the same fill rule.
            for (std::size_t i = 0; i < s.bars.size(); ++i)
            {
                float32 share = std::clamp(s.bars[i].health, 0.0f, 1.0f) * 100.0f;
                if (share > 0.0f && share < 1.0f)
                    share = 1.0f;
                share = std::min(100.0f, share + 3.0f);
                drawBar(ui, bar_, barFill_, barX(i), share / 100.0f, 0x70, 0x91, 0x45, kExtraBarY, s.bars[i].r, s.bars[i].g,
                        s.bars[i].b, true);
            }
        }
        if (s.showStamina && !(s.flashStamina && blinkOff))
            drawBar(ui, bar_, barFill_, kBarX + kBarStep, s.stamina, 0xa7, 0x9e, 0x59);
    }
    const Renderer::Texture* icon = s.crouched ? sneak_ : run_;
    const bool context = s.context >= 0 && s.context < static_cast<int32>(context_.size()) && context_[s.context];
    if (context)
        icon = context_[s.context];
    if (!s.showIcon || !icon || (s.flash && std::fmod(time * kFlashHz, 1.0f) >= 0.5f))
        return; // a flashing icon blinks
    Renderer::UiQuad q;
    q.x0 = kIconX;
    q.y0 = kIconY;
    q.x1 = kIconX + kIconW;
    q.y1 = kIconY + kIconH;
    q.texture = icon;
    q.r = q.g = q.b = 160; // in the open
    if (s.spotted)
    {
        const bool on = std::fmod(time * kFlashHz, 1.0f) < 0.5f;
        q.r = on ? 255 : 120;
        q.g = q.b = on ? 40 : 20;
    }
    else if (s.hidden)
    {
        q.r = 60;
        q.g = 90;
        q.b = 255;
    }
    if (context && tri_)
    {
        Renderer::UiQuad t = q;
        t.texture = tri_; // its triangle behind (0x7d343c)
        ui.add(t);
    }
    ui.add(q);
}

} // namespace Game
