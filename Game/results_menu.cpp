// Game/results_menu.cpp - the end-of-scene results (MainMenu, see main_menu.h).
//
// The exe draws them over the faded-out scene (game state 5 -> 6, 0x4d7c00; results state
// 2, 0x5dc320), in font 1:
//   "Scene completed!" centred at y 0.15, 1.5 wide x 0.9 high, green (0xa8, 0xc8, 0xa8).
//   Then from y 0.28, labels at x 0.19 and values right-aligned to 0.8, 1.0 x 0.9, grey 0xa8:
//   TIME (m:ss, h:mm:ss from an hour, 9:59:59 at most), TIME BONUS (a star, filled under par),
//   a gap, HUNTERS KILLED, HUNTERS EXECUTED, STYLE POINTS [Fetish / Hardcore] with 3 / 4 stars
//   (1.1 high), and a row lower FINAL (1.0 x 1.0) with 5 stars. Rows 0.06 apart. Stars are font
//   1's glyphs 0x9c (empty) and 0x9d (filled).
//   After 30 frames the footer: "Quit to menu" and "Continue" (or "Save" where the scene may be
//   saved, 0x5d55a0). Confirm goes on to the next scene (0x4d7f90); ESC asks "Are you sure you
//   want to quit?" and YES returns to the menu. The bonus scenes (index 20..23, 0x5d6ad0) show
//   the title only.
// Scene Failed (results state 1, 0x5dd110; input 0x474170): 4 s after the player died
// (0x46f880) "Scene Failed!" centred at y 0.41, 1.7 wide x 1.2 high, over the scene as it goes
// on (the backdrop 0x5ddba0 draws with colour 0x7a1264, never set: nothing). The same footer and
// delay; Confirm plays the scene again, ESC asks before quitting to the menu.
#include "Game/main_menu.h"

#include "Game/exe_tables.h"

#include <cstdio>

namespace Game {

namespace {

constexpr UiColor kTitleColor{0xa8, 0xc8, 0xa8, 200};
constexpr UiColor kRowColor{0xa8, 0xa8, 0xa8, 200};
constexpr float32 kLabelX = 0.19f;  // 0x7c9848
constexpr float32 kValueX = 0.8f;   // 0x7c9698: values end here
constexpr float32 kRow = 0.06f;     // 0x7c9708
constexpr float32 kRowHigh = 0.9f;  // 0x7c9840
constexpr float32 kStarHigh = 1.1f; // 0x7c9854
constexpr float32 kInputDelay = 1.0f; // ours: the exe's 30 frames (0x715ca4 > 0x1d)
const std::string kEmptyStar(1, static_cast<char>(0x9c));
const std::string kFullStar(1, static_cast<char>(0x9d));

// ~cross~ / ~triangle~ name pad buttons in the shared text; ours: the keys that do it here.
std::string keyNames(std::string s)
{
    auto swap = [&](const std::string& token, const std::string& key) {
        for (std::size_t at; (at = s.find(token)) != std::string::npos;)
            s.replace(at, token.size(), key);
    };
    swap("~cross~", "Enter");
    swap("~triangle~", "Esc");
    return s;
}

std::string stars(int32 count, int32 filled)
{
    std::string s;
    for (int32 i = 0; i < count; ++i)
        s += i < filled ? kFullStar : kEmptyStar;
    return s;
}

} // namespace

void MainMenu::openFailed()
{
    resultsTime_ = 0.0f;
    question_ = false;
    page_ = Page::Failed;
}

void MainMenu::drawOverlay(Renderer::UiBatch& ui) const
{
    if (page_ != Page::Failed)
        return;
    const UiText font(textFont_, frontendTextures_.find(gameExe().fontAtlas), aspect_);
    if (!font.valid())
        return;
    const std::string title = gxt_.text("LEVEL_F");
    font.draw(ui, title, 0.5f - font.width(title, 1.7f) * 0.5f, 0.41f, 1.7f * aspect_, 1.2f, kRowColor);
    if (resultsTime_ >= kInputDelay)
        drawFooter(ui, {}, keyNames(gxt_.text("QUITT")), keyNames(gxt_.text("CONTC")));
    if (question_)
        drawQuestion(ui, gxt_.text("UQUIT?"), gxt_.text("NO"), gxt_.text("YES"));
}

void MainMenu::openResults(const LevelResults& results)
{
    results_ = results;
    resultsTime_ = 0.0f;
    question_ = false;
    page_ = Page::Results;
}

MenuAction MainMenu::resultsInput(MenuInput in)
{
    MenuAction act;
    if (question_)
    {
        if (in == MenuInput::Up || in == MenuInput::Down)
            questionRow_ ^= 1;
        else if (in == MenuInput::Back)
            question_ = false;
        else if (in == MenuInput::Confirm)
        {
            question_ = false;
            if (questionRow_ == 1)
                act.kind = MenuAction::Kind::QuitLevel;
        }
        return act;
    }
    if (resultsTime_ < kInputDelay)
        return act;
    if (in == MenuInput::Confirm)
    {
        if (page_ == Page::Failed)
            act.kind = MenuAction::Kind::Retry;
        else if (slots_ && results_.scene >= 0 && results_.scene < GameProgress::kStory)
            openSave(SaveMode::SceneEnd); // a story scene: SAVE GAME first (0x4d7f90 -> page 6)
        else
            act.kind = MenuAction::Kind::Continue;
    }
    else if (in == MenuInput::Back && (page_ == Page::Failed || results_.scene != 19))
    {
        question_ = true;
        questionRow_ = 0;
    }
    return act;
}

void MainMenu::drawResults(Renderer::UiBatch& ui) const
{
    ui.rect(0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, 255);
    const UiText font(textFont_, frontendTextures_.find(gameExe().fontAtlas), aspect_);
    if (!font.valid())
        return;
    // UiText's two-scale draw takes the width scale without the aspect.
    auto text = [&](const std::string& s, float32 x, float32 y, float32 sx, float32 sy, UiColor c) {
        font.draw(ui, s, x, y, sx * aspect_, sy, c);
    };
    auto right = [&](const std::string& s, float32 y, float32 sy) { text(s, kValueX - font.width(s, 1.0f), y, 1.0f, sy, kRowColor); };

    const std::string title = gxt_.text("LEVEL_C");
    text(title, 0.5f - font.width(title, 1.5f) * 0.5f, 0.15f, 1.5f, kRowHigh, kTitleColor);

    const LevelScore& s = results_.score;
    float32 y = 0.1f + kRow * 3.0f;
    if (results_.scene < 20)
    {
        // TIME
        const int32 total = s.timeMs / 1000;
        int32 hours = total / 3600, minutes = (total / 60) % 60, seconds = total % 60;
        if (hours > 9)
        {
            hours = 9;
            minutes = 59;
            seconds = 59;
        }
        char buf[32];
        if (hours == 0)
            std::snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
        else
            std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", hours, minutes, seconds);
        text(gxt_.text("TIME"), kLabelX, y, 1.0f, kRowHigh, kRowColor);
        right(buf, y, kRowHigh);
        y += kRow;
        text(gxt_.text("TIMBON"), kLabelX, y, 1.0f, kRowHigh, kRowColor);
        right(results_.timeBonus ? kFullStar : kEmptyStar, y, kRowHigh);
        y += kRow * 2.0f - 0.02f;
        // HUNTERS KILLED / EXECUTED
        text(gxt_.text("HUN_KIL"), kLabelX, y, 1.0f, kRowHigh, kRowColor);
        right(std::to_string(s.kills < 0 ? 0 : s.kills), y, kRowHigh);
        y += kRow;
        text(gxt_.text("HUN_EXE"), kLabelX, y, 1.0f, kRowHigh, kRowColor);
        right(std::to_string(s.executions < 0 ? 0 : s.executions), y, kRowHigh);
        y += kRow;
        // STYLE POINTS [difficulty] and its stars (the time bonus star not among them)
        const std::string label = gxt_.text("STL_PTS");
        const std::string diff = gxt_.text(results_.hard ? "PENN" : "PENE");
        text(label, kLabelX, y, 1.0f, kRowHigh, kRowColor);
        const float32 x = kLabelX + font.width(label, 1.0f) + 0.02f;
        const float32 bracket = font.width("[", 1.0f) * 1.5f;
        text("[", x, y, 1.0f, kRowHigh, kRowColor);
        text(diff, x + bracket, y, 1.0f, kRowHigh, kRowColor);
        text("]", x + bracket * 1.2f + font.width(diff, 1.0f), y, 1.0f, kRowHigh, kRowColor);
        right(stars(results_.hard ? 4 : 3, results_.stars - (results_.timeBonus ? 1 : 0)), y, kStarHigh);
        y += kRow * 2.0f;
        // FINAL
        text(gxt_.text("FINAL"), kLabelX, y, 1.0f, 1.0f, kRowColor);
        right(stars(5, results_.stars), y, kStarHigh);
    }

    if (resultsTime_ >= kInputDelay)
        drawFooter(ui, {}, results_.scene == 19 ? std::string() : keyNames(gxt_.text("QUITT")), keyNames(gxt_.text("CONTC")));
    if (question_)
        drawQuestion(ui, gxt_.text("UQUIT?"), gxt_.text("NO"), gxt_.text("YES"));
}

} // namespace Game
