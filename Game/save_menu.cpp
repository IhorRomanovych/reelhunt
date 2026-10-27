// Game/save_menu.cpp - PLAY, LOAD GAME, SAVE GAME, a new game, GAME COMPLETED and the credits
// (MainMenu, see main_menu.h).
//
// The exe:
//   PLAY (0x600af0 row 0): the scene 0x5d6900 gives (Game/progress.h), or before any game a new
//   game: the brightness page (page 17), DIFFICULTY (page 3), then SAVE GAME for its slot
//   (page 6, 0x7c8700 = 3), and scene 1 whether it was saved or not (0x5d68a0). START A NEW GAME
//   on LOAD GAME and an open scene picked before any game take the same way (0x7c86f4).
//   LOAD GAME (page 4, 0x6008a0 / 0x600950): LOAD GAME and START A NEW GAME centred at y 0.44 and
//   0.5. Its slots (page 5, 0x600360 / 0x6005d0) and SAVE GAME's (page 6, 0x601010 / 0x601310):
//   eight rows from y 0.19, 0.06 apart, LCD at 0.7: a save's "SCENE n" at x 0.15 and the scene's
//   name at x 0.3 (CORRUPT DATA / EMPTY there for the others), the chosen row in a box 0.69 wide
//   (in a level SAVE GAME's box is (0x32, 0x32, 0x46)); under them the chosen save's state
//   (IN PROGRESS / COMPLETE) at x 0.05 and difficulty at x 0.3 (0x5daaf0). Footer: ~up~ ~down~
//   Select, Back without load / Continue without save, Confirm and load / Confirm and save.
//   A loaded save starts the scene 0x606b00 gives; a finished scene's SAVE GAME goes on to the
//   next one (0x4d7f90), after the last story scene to GAME COMPLETED (page 16).
//   GAME COMPLETED (0x5ff990 / 0x5ff9d0): FINAL SCORE centred at y 0.4 and "n / 100" with a star
//   (the best ratings of the 20 story scenes, 0x5daac0) under it; Confirm: the credits (page 21).
//   The credits (0x5ff1f0, roll 0x5d6f30, start 0x5d7000): 16 lines of CRED<n> in font 1, centred,
//   grey 0x80, from y 0.05 + 0.06 a line ('@'s at a line's start each add a line of space), the
//   whole rolling up 0.002 a frame from a screen low; when the top line has gone by the list
//   moves on one. From <END> on 15 lines, and once the roll is 1.3 up, the main menu. Black
//   bands 0.052 high fade the top and bottom.
#include "Game/main_menu.h"

#include "Game/exe_tables.h"
#include "Game/game_text.h"
#include "Renderer/texture.h"

#include <algorithm>
#include <iostream>

namespace Game {

namespace {

constexpr UiColor kText{180, 180, 180, 255};
constexpr UiColor kSelectedText{0, 0, 0, 255};
constexpr float32 kScale = 0.7f;
constexpr float32 kRowStep = 0.06f;

// ~up~ ~down~ / ~cross~ / ~triangle~ name pad buttons in the shared text; ours: the keys here.
std::string keyText(std::string s)
{
    const std::pair<const char*, const char*> keys[] = {
        {"~up~", "Up"}, {"~down~", "Down"}, {"~cross~", "Enter"}, {"~triangle~", "Esc"}};
    for (const auto& [token, key] : keys)
        for (std::size_t at; (at = s.find(token)) != std::string::npos;)
            s.replace(at, std::string(token).size(), key);
    return s;
}

} // namespace

// ---------------------------------------------------------------- PLAY and a new game

MenuAction MainMenu::playInput()
{
    MenuAction act;
    const int32 scene = progress_ ? progress_->continueScene() : 0;
    if (scene < 0)
    {
        startNewGame(Page::Main);
        return act;
    }
    act.kind = MenuAction::Kind::StartScene;
    act.scene = scene + 1;
    act.resume = true;
    return act;
}

void MainMenu::startNewGame(Page from)
{
    newGame_ = true;
    newGameFrom_ = from;
    openSettings(Page::Brightness);
}

MenuAction MainMenu::finishNewGame(bool save)
{
    // 0x601010 (0x7c8700 == 3) / 0x5d68a0: the records cleared, scene 0 in state -2, saved or not.
    newGame_ = false;
    if (progress_)
    {
        progress_->newGame();
        if (save && slots_ && settings_ && !slots_->save(slotRow_, *progress_, *settings_, slotTitle(slotRow_)))
            std::cerr << "menu: could not write save slot " << slotRow_ << "\n";
    }
    MenuAction act;
    act.kind = MenuAction::Kind::StartScene;
    act.scene = 1;
    return act;
}

// ---------------------------------------------------------------- LOAD / SAVE GAME

void MainMenu::openSave(SaveMode mode)
{
    saveMode_ = mode;
    question_ = false;
    if (slots_)
        slots_->refresh();
    page_ = Page::SaveSlots;
}

std::string MainMenu::slotTitle(int32 slot) const
{
    // As the game's own saves: "0)SCENE 1-(IN PROGRESS)".
    const int32 scene = progress_ ? progress_->lastScene() : 0;
    const bool done = progress_ && progress_->sceneState() == GameProgress::kSceneDone;
    return std::to_string(slot) + ")" + gxt_.text("SCENE") + " " + std::to_string(scene + 1) + gxt_.text(done ? "COMPLT" : "INPRO");
}

MenuAction MainMenu::afterSave()
{
    MenuAction act;
    if (saveMode_ == SaveMode::NewGame)
        return finishNewGame(false);
    if (saveMode_ == SaveMode::Checkpoint)
    {
        page_ = Page::Pause; // 0x601010: back to page 8
        return act;
    }
    if (results_.scene == 19)
    {
        page_ = Page::GameComplete; // after the last story scene (0x4d7f90 -> page 16)
        return act;
    }
    act.kind = MenuAction::Kind::Continue;
    return act;
}

MenuAction MainMenu::slotsInput(MenuInput in)
{
    MenuAction act;
    switch (page_)
    {
    case Page::LoadChoice:
        if (in == MenuInput::Up || in == MenuInput::Down)
            loadChoiceRow_ ^= 1;
        else if (in == MenuInput::Back)
            goTo(Page::Main);
        else if (in == MenuInput::Confirm)
        {
            if (loadChoiceRow_ == 0)
            {
                if (slots_)
                    slots_->refresh();
                page_ = Page::LoadSlots;
            }
            else
                startNewGame(Page::LoadChoice);
        }
        break;
    case Page::LoadSlots:
    case Page::SaveSlots:
        if (in == MenuInput::Up)
            slotRow_ = (slotRow_ + SaveSlots::kSlots - 1) % SaveSlots::kSlots;
        else if (in == MenuInput::Down)
            slotRow_ = (slotRow_ + 1) % SaveSlots::kSlots;
        else if (page_ == Page::LoadSlots && in == MenuInput::Back)
            page_ = Page::LoadChoice;
        else if (page_ == Page::LoadSlots && in == MenuInput::Confirm)
        {
            // 0x600360: a save loads; an empty or corrupt slot does nothing.
            if (!slots_ || !progress_ || !settings_ || slots_->slot(slotRow_).status != SaveSlotInfo::Status::Saved ||
                !slots_->load(slotRow_, *progress_, *settings_))
                break;
            const int32 scene = progress_->resumeScene();
            act.scene = scene + 1;
            if (progress_->sceneState() >= 0)
            {
                // A save point's checkpoint: the level as it was saved.
                act.kind = MenuAction::Kind::LoadSave;
                act.savePath = slots_->slot(slotRow_).path;
            }
            else
                act.kind = MenuAction::Kind::StartScene;
        }
        else if (page_ == Page::SaveSlots && in == MenuInput::Back)
            return afterSave(); // Continue without save
        else if (page_ == Page::SaveSlots && in == MenuInput::Confirm)
        {
            if (saveMode_ == SaveMode::NewGame)
                return finishNewGame(true);
            const SaveFile* with = saveMode_ == SaveMode::Checkpoint ? checkpoint_.get() : nullptr;
            if (slots_ && progress_ && settings_ && !slots_->save(slotRow_, *progress_, *settings_, slotTitle(slotRow_), with))
                std::cerr << "menu: could not write save slot " << slotRow_ << "\n";
            return afterSave();
        }
        break;
    case Page::GameComplete:
        if (in == MenuInput::Confirm)
        {
            // Then the credits (page 21): the level is left for them.
            creditsNext_ = true;
            act.kind = MenuAction::Kind::Continue;
        }
        break;
    default:
        break; // the credits take no input (0x5ff1e0)
    }
    return act;
}

void MainMenu::drawLoadChoice(Renderer::UiBatch& ui) const
{
    drawChrome(ui, Page::LoadChoice, gxt_.text("LOADG"));
    const UiText text = font(Page::LoadChoice);
    const std::string load = gxt_.text("LOADG"), fresh = gxt_.text("NEWG");
    drawItem(ui, text, load, 0.5f - text.width(load, kScale) * 0.5f, 0.44f, kScale, loadChoiceRow_ == 0);
    drawItem(ui, text, fresh, 0.5f - text.width(fresh, kScale) * 0.5f, 0.44f + kRowStep, kScale, loadChoiceRow_ == 1);
    drawFooter(ui, "", gxt_.text("IBACK"), "");
}

void MainMenu::drawSlots(Renderer::UiBatch& ui) const
{
    const bool saving = page_ == Page::SaveSlots;
    drawChrome(ui, page_, gxt_.text(saving ? "SAVEG" : "LOADG"));
    const UiText text = font(page_);
    const float32 lh = text.lineHeight(kScale);
    float32 y = 0.25f - kRowStep;
    for (int32 i = 0; i < SaveSlots::kSlots; ++i)
    {
        const bool chosen = slotRow_ == i;
        if (chosen)
        {
            // In a level SAVE GAME's box is blue-grey (0x601310); else the highlight colour.
            const bool level = saving && saveMode_ != SaveMode::NewGame;
            ui.rect(0.15f - 0.01f, y - 0.0005f, 0.15f - 0.01f + 0.69f, y - 0.0005f + lh + 0.005f, level ? 0x32 : 150,
                    level ? 0x32 : 150, level ? 0x46 : 150, 255);
        }
        const UiColor color = chosen ? kSelectedText : kText;
        std::string name = gxt_.text("EMPTY");
        if (slots_)
        {
            const SaveSlotInfo& s = slots_->slot(i);
            if (s.status == SaveSlotInfo::Status::Corrupt)
                name = gxt_.text("CRPT_D");
            else if (s.status == SaveSlotInfo::Status::Saved)
            {
                // "SCENE n" (0x608420) and the scene's name (0x5d67f0).
                text.draw(ui, gxt_.text("SCENE") + " " + std::to_string(s.scene + 1), 0.15f, y, kScale, color);
                name = gxt_.text("LVL_" + std::to_string(s.scene + 1));
                const std::string second = gxt_.text("LVL_" + std::to_string(s.scene + 1) + "A");
                if (second != "-")
                    name += " " + second;
            }
        }
        text.draw(ui, name, 0.3f, y, kScale, color);
        y += kRowStep;
    }
    // The chosen save's state and difficulty (0x5daaf0).
    if (slots_ && slots_->slot(slotRow_).status == SaveSlotInfo::Status::Saved)
    {
        const SaveSlotInfo& s = slots_->slot(slotRow_);
        std::string state = gxt_.text(s.state == GameProgress::kSceneDone ? "COMPLT" : "INPRO");
        if (!state.empty())
            state.erase(0, 1); // the leading '-'
        text.draw(ui, state, 0.05f, y + kRowStep, kScale, kText);
        text.draw(ui, gxt_.text(s.difficulty == 1 ? "NORMAL" : "EASY"), 0.3f, y + kRowStep, kScale, kText);
    }
    drawFooter(ui, "", keyText(gxt_.text(saving ? "BWSTR" : "BACKWL")), keyText(gxt_.text(saving ? "COASAC" : "CONALC")),
               keyText(gxt_.text("UPDS")));
}

// ---------------------------------------------------------------- GAME COMPLETED, the credits

void MainMenu::drawGameComplete(Renderer::UiBatch& ui) const
{
    drawChrome(ui, Page::GameComplete, gxt_.text("GAMCOMP"));
    const UiText text = font(Page::GameComplete);
    const std::string title = gxt_.text("FINLSCR");
    text.draw(ui, title, 0.5f - text.width(title, kScale) * 0.5f, 0.4f, kScale, kText);
    // "%d / %d~star2~": the stars of the 20 story scenes out of 100.
    const std::string score = std::to_string(progress_ ? progress_->totalStars() : 0) + " / 100" + std::string(1, static_cast<char>(0x9d));
    text.draw(ui, score, 0.5f - text.width(score, kScale) * 0.5f, 0.4f + kRowStep, kScale, kText);
    drawFooter(ui, "", keyText(gxt_.text("CONTC")), "");
}

std::pair<int, std::string> MainMenu::creditsLine(int n) const
{
    std::string s = gxt_.text("CRED" + std::to_string(n));
    int lead = 0;
    while (lead < static_cast<int>(s.size()) && s[static_cast<std::size_t>(lead)] == '@')
        ++lead;
    return {lead, s.substr(static_cast<std::size_t>(lead))};
}

void MainMenu::startCredits()
{
    // 0x5d7000.
    page_ = Page::Credits;
    creditsFirst_ = 0;
    creditsEnd_ = false;
    credits_ = 1.0f;
    creditsTick_ = 0.0f;
}

void MainMenu::stepCredits()
{
    if (!creditsEnd_)
        for (int k = 1; k <= 16; ++k)
            if (creditsLine(creditsFirst_ + k).second == "<END>")
                creditsEnd_ = true;
    const float32 limit = (creditsEnd_ ? -1.3f : -(static_cast<float32>(creditsLine(creditsFirst_ + 1).first) * 0.06f + 0.06f)) + 0.002f;
    credits_ -= 0.002f;
    if (credits_ >= limit)
        return;
    credits_ = 0.0f;
    if (!creditsEnd_)
        ++creditsFirst_;
    else
        goTo(Page::Main);
}

void MainMenu::drawCredits(Renderer::UiBatch& ui) const
{
    ui.rect(0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, 255);
    const UiText text(textFont_, frontendTextures_.find(gameExe().fontAtlas), aspect_);
    if (!text.valid())
        return;
    float32 y = 0.05f;
    const int lines = creditsEnd_ ? 15 : 16;
    for (int k = 1; k <= lines; ++k)
    {
        const auto [lead, s] = creditsLine(creditsFirst_ + k);
        y += 0.06f * static_cast<float32>(lead);
        text.draw(ui, s, 0.5f - text.width(s, 1.0f) * 0.5f, y + credits_, 1.0f, UiColor{0x80, 0x80, 0x80, 255});
        y += 0.06f;
    }
    // The bands that fade the roll in and out at the top and bottom.
    constexpr float32 kBand = 0.052f;
    ui.rect(0.0f, 0.0f, 1.0f, kBand, 0, 0, 0, 255);
    ui.rect(0.0f, 1.0f - kBand, 1.0f, 1.0f, 0, 0, 0, 255);
    constexpr int kSteps = 8;
    for (int i = 0; i < kSteps; ++i)
    {
        const auto a = static_cast<uint8>(255 - 255 * (i + 1) / (kSteps + 1));
        const float32 h = kBand / kSteps;
        ui.rect(0.0f, kBand + h * static_cast<float32>(i), 1.0f, kBand + h * static_cast<float32>(i + 1), 0, 0, 0, a);
        ui.rect(0.0f, 1.0f - kBand - h * static_cast<float32>(i + 1), 1.0f, 1.0f - kBand - h * static_cast<float32>(i), 0, 0, 0, a);
    }
}

} // namespace Game
