// Game/weapon_strip.cpp - the HUD's weapon strip. See weapon_strip.h.
#include "Game/weapon_strip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Game/game_text.h"

namespace Game {

namespace {

constexpr float32 kAspect = 0.75f; // 480 / 640: frontend x units per y unit (0x7d3458)
constexpr float32 kStrapX = 0.829f, kStrapY = 0.613f, kStrapW = kAspect * 0.5f, kStrapH = 0.132812f;
constexpr float32 kIconX = 0.8805f, kIconY = 0.5988f, kIconH = 0.11f, kIconW = kAspect * kIconH;
constexpr float32 kRight = kStrapX + 0.14f;                 // the text's right edge (0.969)
constexpr float32 kShadowX = 0.003f * kAspect, kShadowY = 0.003f;
constexpr float32 kTextSx = 0.8f * kAspect, kTextSy = 0.7f; // the exe's 0.8 x 0.7
constexpr float32 kNameY = kStrapY - 0.06f;
constexpr float32 kAmmoY = kStrapY + 0.035f + 0.04f + 0.02f;
constexpr float32 kAmmoGap = kAspect * 0.01f - 0.005f; // after the spare rounds
constexpr float32 kTick = 1.0f / 30.0f;
constexpr float32 kNameStep = 9.0f, kNamePeak = 600.0f;
constexpr int32 kNameAlphaMax = 230;

struct Rgb {
    uint8 r, g, b;
};
constexpr Rgb kSlot[4] = {{0x80, 0x36, 0x36}, {0x3e, 0x62, 0x78}, {0x62, 0x85, 0x31}, {0x80, 0x80, 0x00}};
constexpr UiColor kShadow{0, 0, 0, 255}, kRounds{200, 200, 200, 255}, kFull{0x78, 200, 0x78, 255};

void sprite(Renderer::UiBatch& ui, const Renderer::Texture* tex, float32 x, float32 y, float32 w, float32 h, Rgb c,
            Renderer::UiBlend blend)
{
    Renderer::UiQuad q;
    q.x0 = x;
    q.y0 = y;
    q.x1 = x + w;
    q.y1 = y + h;
    q.r = c.r;
    q.g = c.g;
    q.b = c.b;
    q.texture = tex;
    q.blend = blend;
    ui.add(q);
}

} // namespace

std::vector<const ItemType*> weaponCycle(const std::vector<const ItemType*>& inventory)
{
    std::vector<const ItemType*> out;
    auto add = [&](const ItemType* t) {
        if (std::find(out.begin(), out.end(), t) == out.end())
            out.push_back(t);
    };
    for (const int32 slot : {3, 2, 1, 4})
    {
        const std::size_t before = out.size();
        for (const ItemType* t : inventory)
            if (t->weapon && inventorySlot(t->holsterSlot) == slot)
                add(t);
        if (slot == 3 && out.size() == before)
            out.push_back(nullptr); // fists
    }
    for (const ItemType* t : inventory)
        if (t->weapon && (inventorySlot(t->holsterSlot) < 1 || inventorySlot(t->holsterSlot) > 4))
            add(t);
    return out;
}

const char* WeaponStrip::iconName(int32 ct)
{
    switch (ct)
    {
    case 16: return "fists";
    case 18: return "knife";
    case 19: return "shard";
    case 22: return "bottle";
    case 24: return "cleaver";
    case 26: return "crowbar";
    case 27: return "sickle";
    case 28: return "nightstick";
    case 30: return "axe";
    case 31: return "icepick";
    case 32: return "machete";
    case 33: return "smallbat";
    case 34: return "baseball";
    case 35: return "wbaseball";
    case 38: return "batblades";
    case 39: return "revolver";
    case 40: return "glock";
    case 43: return "uzi";
    case 44: return "shotgun";
    case 45: return "shotgunlamp";
    case 46: return "desert";
    case 47: return "coltcom";
    case 48: return "sniper";
    case 49: return "tranqrifle";
    case 50: return "sawnoff";
    case 56: return "brick";
    case 58: return "bag";
    case 88:
    case 108: return "chainsaw";
    case 89: return "nailgun";
    case 90: return "wire";
    case 91: return "can";
    case 92:
    case 96: return "woodspike";
    case 93: return "sniper";
    case 94: return "shardpig";
    case 95: return "wirepig";
    case 97: return "hammer";
    case 101: return "head";
    default: return "na";
    }
}

const char* WeaponStrip::nameKey(int32 ct)
{
    switch (ct)
    {
    case 2: return "GASTANK";
    case 8:
    case 9: return "PAINKIL";
    case 16: return "FISTS";
    case 18: return "KNIFE";
    case 19: return "G_SHARD";
    case 22: return "BOTTLE";
    case 24: return "MEAT_C";
    case 26: return "CROWBAR";
    case 27: return "SICKLE";
    case 28: return "NITSTIK";
    case 30: return "AXE";
    case 31: return "ICE_PIK";
    case 32: return "MACHETE";
    case 33: return "BLAKJAK";
    case 34: return "MBASBAT";
    case 35: return "WBASBAT";
    case 38: return "BAT_SPI";
    case 39: return "REVLVR";
    case 40: return "GLOCK";
    case 43: return "UZI";
    case 44: return "SHOTGUN";
    case 45: return "SHO_TOR";
    case 46: return "D_EAGLE";
    case 47: return "COLT_C";
    case 48: return "SNI_RIF";
    case 49: return "TRANQ_R";
    case 50: return "SAWNOFF";
    case 56: return "BRICK";
    case 58: return "PLA_BAG";
    case 70:
    case 83: return "KEY";
    case 81: return "DOLL";
    case 88:
    case 108: return "CHAINSW";
    case 89: return "NAILGUN";
    case 90: return "WIRE";
    case 91: return "CAN";
    case 92:
    case 96: return "SPIKE";
    case 93: return "SSNIRIF";
    case 94: return "PIG_SHA";
    case 95: return "PIG_WIR";
    case 97: return "HAMMER";
    case 98: return "DOLL_1";
    case 99: return "DOLL_2";
    case 100: return "DOLL_3";
    case 101: return "HEAD";
    case 102: return "NAILS";
    case 103: return "SHELLS";
    case 104: return "HB_BUL";
    case 105: return "A_CLIP";
    case 106: return "TDARTS";
    case 107: return "RIFLE_A";
    case 109: return "DVTAPE";
    case 110: return "HANDCAM";
    default: return nullptr;
    }
}

bool WeaponStrip::load(const TextureSet& frontend, const std::string& gameRoot)
{
    frontend_ = &frontend;
    strap_ = frontend.find("invstrap");
    swap_ = frontend.find("swap");
    if (auto gxt = Assets::Gxt::load(gameRoot + "levels/GLOBAL/PC_TEXT/pc_game.gxt"))
        gxt_ = std::move(*gxt);
    return strap_ != nullptr;
}

void WeaponStrip::showName(int32 ct)
{
    if (ct == kFists)
        return;
    const char* key = nameKey(ct);
    if (!key || !gxt_.contains(key))
        return;
    name_.clear();
    for (const GameText::Run& r : GameText::parse(gxt_.text(key)))
        name_ += r.text;
    nameOn_ = true;
    falling_ = false; // (the timer carries on from where it is)
}

void WeaponStrip::update(float32 dt)
{
    if (!nameOn_)
        return;
    const float32 step = dt / kTick * kNameStep;
    if (!falling_)
    {
        timer_ += step;
        if (timer_ >= kNamePeak)
            falling_ = true;
    }
    else
    {
        timer_ -= step;
        if (timer_ <= 0.0f)
        {
            timer_ = 0.0f;
            falling_ = false;
            nameOn_ = false;
        }
    }
}

int32 WeaponStrip::nameAlpha() const
{
    if (!nameOn_)
        return 0;
    return std::clamp(static_cast<int32>(std::lround(timer_)), 0, kNameAlphaMax);
}

void WeaponStrip::draw(Renderer::UiBatch& ui, const UiText& font, const WeaponStripView& view) const
{
    if (const int32 a = nameAlpha(); a > 0 && font.valid())
    {
        const auto c = static_cast<uint8>(a);
        const float32 w = font.width(name_, kTextSx);
        font.draw(ui, name_, kRight - kShadowX - w, kNameY + kShadowY, kTextSx, kTextSy, UiColor{0, 0, 0, c});
        font.draw(ui, name_, kRight - w, kNameY, kTextSx, kTextSy, UiColor{c, c, c, c});
    }
    if (!strap_ || view.slot < 1 || view.slot > 4)
        return;
    sprite(ui, strap_, kStrapX, kStrapY, kStrapW, kStrapH, kSlot[view.slot - 1], Renderer::UiBlend::Alpha);
    if (const Renderer::Texture* icon = frontend_->find(iconName(view.ct)))
        sprite(ui, icon, kIconX, kIconY, kIconW, kIconH, {0x30, 0x30, 0x20}, Renderer::UiBlend::Additive);
    if (view.swapSlot == view.slot && view.swapCt != 0)
    {
        // The swap offer (0x5de320 with 0x7c9ca4): the strap again 0.15 up, the held and the new
        // icons on it and the arrow between.
        constexpr float32 kSwapY = 0.4488f;
        sprite(ui, strap_, 0.7215781f, kStrapY - 0.15f, kStrapW, kStrapH, kSlot[view.slot - 1], Renderer::UiBlend::Alpha);
        if (const Renderer::Texture* held = frontend_->find(iconName(view.ct)))
            sprite(ui, held, kIconX, kSwapY, kIconW, kIconH, {0x30, 0x30, 0x20}, Renderer::UiBlend::Additive);
        if (const Renderer::Texture* offered = frontend_->find(iconName(view.swapCt)))
            sprite(ui, offered, 0.7535469f, kSwapY, kIconW, kIconH, {0x30, 0x30, 0x20}, Renderer::UiBlend::Additive);
        if (swap_)
            sprite(ui, swap_, 0.8433906f, 0.4839562f, kAspect * 0.09f / 2.5f, 0.036f, {0x80, 0x80, 0x80}, Renderer::UiBlend::Additive);
    }
    if (!view.gun || view.ct == kChainsawPlayer || !font.valid())
        return;
    // "clip / spare", right-aligned: the spare rounds, then " / " and the clip to their left.
    auto number = [](int32 n) {
        char s[16];
        std::snprintf(s, sizeof(s), "%d", n);
        return std::string(s);
    };
    const bool spareFull = view.spare >= view.maxClips * view.clipAmmo - view.clipAmmo;
    const bool clipFull = view.clip >= view.clipAmmo;
    const std::string parts[3] = {number(view.spare), " / ", number(view.clip)};
    const UiColor colours[3] = {spareFull ? kFull : kRounds, kRounds, clipFull ? kFull : kRounds};
    float32 offset = kAmmoGap;
    for (int k = 0; k < 3; ++k)
    {
        offset += font.width(parts[k], kTextSx);
        font.draw(ui, parts[k], kRight - kShadowX - offset, kAmmoY + kShadowY, kTextSx, kTextSy, kShadow);
        font.draw(ui, parts[k], kRight - offset, kAmmoY, kTextSx, kTextSy, colours[k]);
    }
}

} // namespace Game
