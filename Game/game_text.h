// Game/game_text.h - the level's on-screen text: DisplayGameText hint boxes, director subtitles
// and the level goals.
//
// Strings come from the level's text file, levels/<level>/pc_text/pc_<level>.gxt ("GATE",
// "H_RUN", "BRN1A1", ...). The level script sets the box up once (Jury_Turf: position 0.05, 0.05,
// size 0.40 x 0.40, teletype off) and before each DisplayGameText the time it stays up
// (SetGameTextDisplayTime, ms). Strings carry colour tokens: ~green~ / ~red~ / ~orange~ /
// ~yellow~ / ~blue~ start a colour, ~colour~ (also misspelt ~colur~) ends it; other ~tokens~
// (button glyphs, ~!~) are dropped. A director line shows its subtitle at the bottom of the
// screen while each part plays (DirectorSpeech::subtitleKey).
#pragma once
#include "engine_types.h"

#include <string>
#include <vector>

#include "Assets/gxt.h"
#include "Game/ui_text.h"
#include "Renderer/ui.h"

namespace Game {

class GameText {
public:
    // Loads the level's strings; false when the level has no text file.
    bool load(const std::string& levelDir, const std::string& levelName, const std::string& suffix = {});
    [[nodiscard]] std::string text(const std::string& key) const;

    // --- script settings (frontend units: the screen is 1 x 1) ---
    void setBoxPosition(float32 x, float32 y) noexcept { boxX_ = x; boxY_ = y; }
    void setBoxSize(float32 w, float32 h) noexcept { boxW_ = w; boxH_ = h; }
    void setDisplayTime(float32 seconds) noexcept { displaySeconds_ = seconds; }
    void setTeletype(bool on) noexcept { teletype_ = on; }

    // Shows the string `key` in the box for the display time (replacing what is shown).
    void display(const std::string& key);
    // Shows `text` itself (a string of the global text, e.g. CHEKPNT "Save Point Hit").
    void displayText(const std::string& key, const std::string& text);
    [[nodiscard]] bool displaying() const noexcept { return boxLeft_ > 0.0f; }
    // KillGameText: the box goes at once.
    void killBox() noexcept { boxLeft_ = 0.0f; }
    [[nodiscard]] const std::string& shownKey() const noexcept { return boxKey_; }

    // Director subtitles: `key`'s string at the bottom of the screen for `seconds`; false when the
    // level has no such string.
    bool subtitle(const std::string& key, float32 seconds);
    [[nodiscard]] bool subtitling() const noexcept { return subtitleLeft_ > 0.0f; }
    void killSubtitle() noexcept { subtitleLeft_ = 0.0f; }
    [[nodiscard]] const std::string& subtitleKey() const noexcept { return subtitleKey_; }

    // The level goals (SetLevelGoal / ClearLevelGoal; the pause screen lists them).
    void setGoal(const std::string& key);
    void clearGoal(const std::string& key);
    [[nodiscard]] const std::vector<std::string>& goals() const noexcept { return goals_; }

    void update(float32 dt);
    void draw(Renderer::UiBatch& ui, const UiText& font) const;

    // A string split into coloured runs (the ~colour~ tokens removed).
    struct Run {
        std::string text;
        UiColor color;
    };
    [[nodiscard]] static std::vector<Run> parse(const std::string& s);
    // Words of `runs` wrapped to `width` at `scale`: one vector of runs per line. With `maxLines`,
    // the last line takes the rest however long (0x5e5780: the goals' two lines).
    [[nodiscard]] static std::vector<std::vector<Run>> wrap(const std::vector<Run>& runs, const UiText& font, float32 scale,
                                                             float32 width, std::size_t maxLines = 0);

private:
    Assets::Gxt gxt_;
    bool loaded_ = false;
    float32 boxX_ = 0.05f, boxY_ = 0.05f, boxW_ = 0.40f, boxH_ = 0.40f;
    float32 displaySeconds_ = 3.0f;
    bool teletype_ = false;
    std::string boxKey_, boxText_;
    float32 boxLeft_ = 0.0f, boxShown_ = 0.0f;
    std::string subtitleKey_, subtitleText_;
    float32 subtitleLeft_ = 0.0f;
    std::vector<std::string> goals_;
};

} // namespace Game
