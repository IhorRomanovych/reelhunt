// Game/pause_menu.cpp - the in-game menu (MainMenu, see main_menu.h): ESC in a level.
//
// The exe's page 8 (update 0x5ffb50, draw 0x5ffd20), with IGAME_EP.TXD's background and no
// camcorder header or footer:
//   SETTINGS, SAVE GAME, QUIT, CONTINUE centred from y 0.68, 0.048 apart, LCD font at 0.7.
//   SAVE GAME only shows where the game may be saved (0x5d5580); the cursor steps over it.
//   SETTINGS opens the settings list without DIFFICULTY and LANGUAGES, which comes back here.
//   QUIT asks "Are you sure you want to quit?" (0x5d7ec0: NO first); YES ends the level.
//   CONTINUE or ESC resumes; the cursor is back on CONTINUE the next time (0x5d7825).
// Above the items, the level goals (0x5d90e0): each centred on up to two lines in font 1 at 0.9
// across and 0.8 up (0x7c9218 / 0x7c9094), a black shadow 0.003 down and left, 0.037 a line and
// 0.02035 more after each goal. A goal breaks (0x5e5780) after its last word that ends within 0.9
// across; the rest, however long, is its second line.
#include "Game/main_menu.h"

#include "Game/exe_tables.h"
#include "Game/game_text.h"

#include <algorithm>

namespace Game {

namespace {

constexpr float32 kItemY = 0.68f;     // 0x7d6188
constexpr float32 kItemStep = 0.048f; // 0x7d618c
constexpr float32 kItemScale = 0.7f;
constexpr int kSettingsRow = 0, kSaveRow = 1, kQuitRow = 2, kContinueRow = 3;

constexpr float32 kGoalScale = 0.9f;     // 0x7c9218
constexpr float32 kGoalShadow = 0.003f;  // 0x7c921c (and -0.003 x aspect across, 0x7c9214)
constexpr float32 kGoalLine = 0.037f;    // 0x7c9220
constexpr float32 kGoalGap = 0.02035f;   // 0x7c9224
constexpr float32 kGoalHeight = 0.8f;    // 0x7c9094
constexpr float32 kGoalWidth = 0.9f;     // 0x5e5780's width (0x7c9218)
constexpr UiColor kGoalColor{180, 180, 180, 255};


} // namespace

void MainMenu::openPause(std::vector<std::string> goals, std::shared_ptr<const SaveFile> checkpoint)
{
    inGame_ = true;
    goals_ = std::move(goals);
    checkpoint_ = std::move(checkpoint); // a save point reached: SAVE GAME (0x5d5580)
    question_ = false;
    page_ = Page::Pause;
}

MenuAction MainMenu::pauseInput(MenuInput in)
{
    MenuAction act;
    if (question_)
    {
        // 0x5d7f30: up / down swap NO and YES; ESC answers NO.
        if (in == MenuInput::Up || in == MenuInput::Down)
            questionRow_ ^= 1;
        else if (in == MenuInput::Back)
            question_ = false;
        else if (in == MenuInput::Confirm)
        {
            question_ = false;
            if (questionRow_ == 1)
            {
                inGame_ = false;
                pauseRow_ = kContinueRow;
                act.kind = MenuAction::Kind::QuitLevel;
            }
        }
        return act;
    }
    switch (in)
    {
    case MenuInput::Up:
        if (!checkpoint_ && pauseRow_ == kQuitRow)
            pauseRow_ = kSaveRow;
        pauseRow_ = pauseRow_ == 0 ? kContinueRow : pauseRow_ - 1;
        break;
    case MenuInput::Down:
        if (!checkpoint_ && pauseRow_ == kSettingsRow)
            pauseRow_ = kSaveRow;
        pauseRow_ = pauseRow_ == kContinueRow ? 0 : pauseRow_ + 1;
        break;
    case MenuInput::Confirm:
        if (pauseRow_ == kSettingsRow)
            openSettings(Page::Settings);
        else if (pauseRow_ == kSaveRow && checkpoint_)
            openSave(SaveMode::Checkpoint); // 0x5ffb50 row 1 -> page 6
        else if (pauseRow_ == kQuitRow)
        {
            question_ = true;
            questionRow_ = 0;
        }
        else if (pauseRow_ == kContinueRow)
        {
            inGame_ = false;
            act.kind = MenuAction::Kind::Resume;
        }
        break;
    case MenuInput::Back:
        inGame_ = false;
        pauseRow_ = kContinueRow;
        act.kind = MenuAction::Kind::Resume;
        break;
    default:
        break;
    }
    return act;
}

void MainMenu::drawPause(Renderer::UiBatch& ui) const
{
    drawChrome(ui, Page::Pause, {});
    drawGoals(ui);
    if (question_)
    {
        drawQuestion(ui, gxt_.text("UQUIT?"), gxt_.text("NO"), gxt_.text("YES"));
        return;
    }
    const UiText text = font(Page::Pause);
    float32 y = kItemY;
    auto item = [&](const char* key, int row) {
        const std::string label = gxt_.text(key);
        drawItem(ui, text, label, 0.5f - text.width(label, kItemScale) * 0.5f, y, kItemScale, pauseRow_ == row);
        y += kItemStep;
    };
    item("SETINGS", kSettingsRow);
    if (checkpoint_)
        item("SAVEGAM", kSaveRow);
    item("QUIT", kQuitRow);
    item("CONTINU", kContinueRow);
}

void MainMenu::drawGoals(Renderer::UiBatch& ui) const
{
    const UiText font(textFont_, frontendTextures_.find(gameExe().fontAtlas), aspect_);
    if (!font.valid())
        return;
    const UiColor plain = GameText::parse("x").front().color; // a run without a ~colour~ token
    float32 y = 0.1f;
    for (std::size_t g = 0; g < goals_.size() && g < 6; ++g)
    {
        const std::vector<std::vector<GameText::Run>> lines = GameText::wrap(GameText::parse(goals_[g]), font, kGoalScale, kGoalWidth, 2);
        bool any = false;
        for (const auto& line : lines)
        {
            float32 w = 0.0f;
            for (const GameText::Run& r : line)
                w += font.width(r.text, kGoalScale);
            if (w <= 0.0f)
                continue;
            any = true;
            // The shadow without colours, then the line with them.
            float32 x = 0.5f - aspect_ * kGoalShadow - w * 0.5f;
            for (const GameText::Run& r : line)
            {
                font.draw(ui, r.text, x, y + kGoalShadow, kGoalScale * aspect_, kGoalHeight, UiColor{0, 0, 0, 255});
                x += font.width(r.text, kGoalScale);
            }
            x = 0.5f - w * 0.5f;
            for (const GameText::Run& r : line)
            {
                const UiColor c = r.color.r == plain.r && r.color.g == plain.g && r.color.b == plain.b ? kGoalColor : r.color;
                font.draw(ui, r.text, x, y, kGoalScale * aspect_, kGoalHeight, c);
                x += font.width(r.text, kGoalScale);
            }
            y += kGoalLine;
        }
        if (any)
            y += kGoalGap;
    }
}

void MainMenu::drawQuestion(Renderer::UiBatch& ui, const std::string& question, const std::string& no, const std::string& yes) const
{
    // 0x5d8110: a black box round three lines of font 1 at 1.0 in the middle of the screen - the
    // question (200), then NO and YES, the chosen one 180 and the other 80.
    const UiText font(textFont_, frontendTextures_.find(gameExe().fontAtlas), aspect_);
    if (!font.valid())
        return;
    const float32 lh = font.lineHeight(1.0f);
    const float32 wq = font.width(question, 1.0f), wn = font.width(no, 1.0f), wy = font.width(yes, 1.0f);
    const float32 w = std::max({wq, wn, wy});
    const float32 pad = aspect_ * 0.02f;
    const float32 top = 0.5f - lh * 3.5f * 0.5f;
    ui.rect(0.5f - w * 0.5f - pad, top - 0.02f, 0.5f + w * 0.5f + pad, top - 0.02f + lh * 3.5f + 0.04f, 0, 0, 0, 255);
    font.draw(ui, question, 0.5f - wq * 0.5f, top, 1.0f, UiColor{200, 200, 200, 255});
    const float32 y = top + lh * 1.5f;
    const UiColor on{180, 180, 180, 255}, off{80, 80, 80, 255};
    font.draw(ui, no, 0.5f - wn * 0.5f, y, 1.0f, questionRow_ == 0 ? on : off);
    font.draw(ui, yes, 0.5f - wy * 0.5f, y + lh, 1.0f, questionRow_ == 1 ? on : off);
}

} // namespace Game
