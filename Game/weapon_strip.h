// Game/weapon_strip.h - the HUD's weapon strip (HUD_INVENTORY), as the exe draws it (0x5de320).
//
// Bottom right, only the holster slot in his hand (HOLSTER_SLOT: BACK 1, BELT_RIGHT 2, BELT_LEFT 3,
// BELT_REAR 4; with BELT_LEFT empty, slot 3 is his fists):
//   `invstrap` at x 0.829, y 0.613, 0.75 * 0.5 wide and 0.132812 tall (it runs off the right edge),
//     tinted by the slot: (0x80, 0x36, 0x36) red, (0x3e, 0x62, 0x78) blue, (0x62, 0x85, 0x31) green,
//     (0x80, 0x80, 0) yellow;
//   the item's icon (0x5df9a0, by eCollectableType: "shard", "wbaseball", "fists", "na" for the rest)
//     at x 0.8805, y 0.5988, 0.75 * 0.11 wide and 0.11 tall in (0x30, 0x30, 0x20), added on
//     (blend ONE, ONE; the icons are white on black);
//   a gun's rounds (not the player's chainsaw): "clip / spare" right-aligned to x 0.969 at
//     y 0.708, the numbers green (0x78, 200, 0x78) when full (the clip at CLIP_AMMO, the spare at
//     (MAX_CLIPS - 1) x CLIP_AMMO), else (200, 200, 200), over a black shadow 0.00225 left and
//     0.003 down.
// Its text is font 1 at 0.8 x 0.7 of the exe's scale (glyphs are advance x 0.75 x 0.8 wide and the
// line height x 0.7 tall).
//
// The name of an item he picks up (0x5dfb60 -> 0x5df9a0, not for fists) shows above the strip,
// right-aligned to x 0.969 at y 0.553 over its shadow: a timer rises 9 per 1/30 s tick to 600, then
// falls back to 0; the text is grey at that alpha, at most 230 (about 0.85 s in, 2.2 s up, 0.85 s
// out). The name is the pc_game.gxt string of the type (0x4c5870: "G_SHARD", "WBASBAT" ...).
//
// Next weapon (0x5ddce0) goes through the slots 3, 2, 1, 4 (0x7c9b54), skipping empty ones but
// slot 3 (fists when it is empty): see weaponCycle.
//
// The swap offer (0x5df980, set each frame while he stands on an item for the slot he holds filled):
// a second strap 0.15 up at x 0.7216 in the slot's tint, then added on (ONE, ONE) the held item's
// icon at x 0.8805, the new item's at x 0.7535 (both y 0.4488), and the `swap` arrow
// (frontend_pc.txd, 0x5eaa58) at x 0.8434, y 0.4840, 0.75 x 0.09 / 2.5 wide and 0.036 tall in
// (0x80, 0x80, 0x80). Only for the slot in his hand (0x7c9ca8 == the strip's slot).
#pragma once
#include "engine_types.h"

#include <string>
#include <vector>

#include "Assets/gxt.h"
#include "Game/items.h"
#include "Game/texture_set.h"
#include "Game/ui_text.h"
#include "Renderer/ui.h"

namespace Game {

// What he holds, for the strip.
struct WeaponStripView {
    int32 slot = 3;     // its holster slot, 1..4
    int32 ct = 16;      // its eCollectableType (CT_FISTS 16)
    bool gun = false;   // a firearm: its rounds show
    int32 clip = 0, spare = 0;
    int32 clipAmmo = 0, maxClips = 0;
    int32 swapSlot = 0, swapCt = 0; // an item he stands on for a filled slot (0: none)
};

// The weapons "next weapon" goes through, in order (nullptr = fists): each holster slot of
// 3, 2, 1, 4 in turn, slot 3 as fists when he carries nothing for it. Ours keeps every weapon he
// picks up (the exe swaps), so a slot holding several goes through them in pickup order; weapons
// without a slot 1..4 come last.
std::vector<const ItemType*> weaponCycle(const std::vector<const ItemType*>& inventory);

class WeaponStrip {
public:
    static constexpr int32 kFists = 16;
    static constexpr int32 kChainsawPlayer = 108; // CT_CHAINSAW_PLAYER: a "gun" without rounds shown

    // The icons (frontend_pc.txd, already loaded by the HUD) and the item names (pc_game.gxt).
    bool load(const TextureSet& frontend, const std::string& gameRoot);
    // An item was picked up: its name fades in (fists and types without a name show nothing).
    void showName(int32 ct);
    // The name's fade; the exe moves it in the strip's draw, so call it only while it is drawn.
    void update(float32 dt);
    void draw(Renderer::UiBatch& ui, const UiText& font, const WeaponStripView& view) const;

    // The name shown and its alpha (0..230; 0 = none).
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] int32 nameAlpha() const;

    // 0x5df9a0: the frontend_pc.txd icon of an eCollectableType ("na" for types without one).
    [[nodiscard]] static const char* iconName(int32 ct);
    // 0x4c5870: the pc_game.gxt key of its name; nullptr when it has none.
    [[nodiscard]] static const char* nameKey(int32 ct);

private:
    const TextureSet* frontend_ = nullptr;
    const Renderer::Texture* strap_ = nullptr;
    const Renderer::Texture* swap_ = nullptr;
    Assets::Gxt gxt_;
    bool nameOn_ = false, falling_ = false;
    float32 timer_ = 0.0f;
    std::string name_;
};

} // namespace Game
