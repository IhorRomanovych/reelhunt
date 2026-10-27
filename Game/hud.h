// Game/hud.h - the in-game HUD: the status icon and the health and stamina bars (the radar is
// Game/radar.h).
//
// Art: pictures/frontend_pc.txd (mansneak / manrun silhouettes, radar, health bar, weapon icons).
// The status icon (exe 0x5ef990, drawn at 0x5f085e): mansneak while crouched, manrun
// otherwise, at x 0.84, y 0.768, 0.21875 tall (frontend units, globals 0x7ce254 / 0x7ce250 /
// 0x7ce248). Its colours follow the level's hint text ("Your Status Icon goes blue when you are
// safely hidden in the shadows", "The icon will flash red when you are in danger or have been
// spotted"); the exact shades are ours.
//
// The context: what the Use key would do now (0x45eb00, player +0x970) replaces the figure with
// its own over `mantri` (0x5f0700..): 0 off the wall `mansquashen`, 1 put the body down `mandrop`,
// 2 / 3 a buddy's follow / stop `manFollow` / `manStop`, 4 and 7 pick up (a body, an item)
// `manpickup`, 5 use `manUse`, 6 onto the wall `mansquashst`, 8 kick `manKick`; 9 none.
//
// The bars (0x5ef990, HUD_HEALTH / HUD_STAMINA): `hs_bar` frames 0.75 * 0.0625 wide and 0.195312
// tall at x 0.929, y 0.762 (health) and 1/64 further right (stamina), with an `hs_barfill`
// filling up from the bottom to 0.189453 when full. Health is (0x70, 0x91, 0x45): its fill is the
// share of HIT_POINTS left in percent, at least 1 while he lives, plus 3, at most 100. Stamina is
// (0xa7, 0x9e, 0x59), its fill the stamina level (Game/stamina.h). A flashing part (the
// tutorial's HUDToggleFlashFlags) blinks every 256 ms (frame-time bit 0x100).
#pragma once
#include "engine_types.h"

#include <array>
#include <string>
#include <vector>

#include "Game/texture_set.h"
#include "Renderer/ui.h"

namespace Game {

// A bar beside his (0x5ef990's loop over up to three): the buddy's (0x7d34d0, set by the BeBuddy
// objective through 0x40c100) or a boss's (Ramirez on scene 13, Piggsy on scene 19, once he is
// fighting him). Drawn 1/64 further right each at y 0.15, the frame in the health bar's colour,
// the fill the entity's share of its HIT_POINTS in `r, g, b` (0, 0xa0, 0; a buddy with ped +0x13c
// set 2, 0x12, 0x44), its name beside it (the session prints it).
struct HudBar {
    std::string name;
    float32 health = 1.0f;
    uint8 r = 0, g = 0xa0, b = 0;
};

struct HudState {
    bool showIcon = true, showHealth = true, showStamina = true;
    bool flashHealth = false, flashStamina = false;
    float32 health = 1.0f;  // share of HIT_POINTS left
    float32 stamina = 1.0f; // 0..1
    bool crouched = false;
    int32 context = 9;     // the Use key's action (above), 9 none
    bool hidden = false;   // in a safe zone
    bool spotted = false;  // a hunter is chasing or fighting him
    bool flash = false;    // the scripts point the icon out (HUD_MAN flash, SetPlayerStatusFlash)
    bool godMode = false;  // the God mode cheat (0x5ee4f0: 0x7cf298): his health fill shows red
    std::vector<HudBar> bars;
};

class Hud {
public:
    bool load(const std::string& gameRoot);
    void draw(Renderer::UiBatch& ui, const HudState& s, float32 time) const;
    // Where extra bar `i` (0-based) sits: its left edge and its top (frontend units).
    [[nodiscard]] static float32 barX(std::size_t i);
    static constexpr float32 kExtraBarY = 0.15f;
    // A texture of frontend_pc.txd (e.g. The game text font's atlas, ExeTables::fontAtlas), or nullptr.
    [[nodiscard]] const Renderer::Texture* texture(const std::string& name) const { return textures_.find(name); }
    [[nodiscard]] const TextureSet& textures() const noexcept { return textures_; }

private:
    TextureSet textures_;
    const Renderer::Texture* sneak_ = nullptr;
    const Renderer::Texture* run_ = nullptr;
    const Renderer::Texture* tri_ = nullptr;
    std::array<const Renderer::Texture*, 9> context_{}; // by context
    const Renderer::Texture* bar_ = nullptr;
    const Renderer::Texture* barFill_ = nullptr;
};

} // namespace Game
