// Game/main_menu.cpp - the frontend menus. See main_menu.h for what was reconstructed from where.
#include "Game/main_menu.h"

#include "Game/exe_tables.h"
#include "Assets/ini_records.h"
#include "Assets/txd.h"
#include "Game/save_system.h"
#include "Renderer/texture.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>

namespace Game {

namespace {

constexpr UiColor kText{180, 180, 180, 255};
constexpr UiColor kDim{80, 80, 80, 255};
constexpr UiColor kSelectedText{0, 0, 0, 255};
constexpr uint8 kHighlight = 150;

constexpr float32 kItemX = 0.58f;
constexpr float32 kItemY = 0.40f;
constexpr float32 kItemStep = 0.06f;
constexpr float32 kItemScale = 0.7f;
constexpr int kListRows = 8;

} // namespace

// ---------------------------------------------------------------- MenuCursor

void MenuCursor::reset(int count, int visible) noexcept
{
    count_ = std::max(count, 0);
    visible_ = std::max(visible, 1);
    index_ = 0;
    first_ = 0;
}

void MenuCursor::up() noexcept
{
    if (count_ == 0)
        return;
    index_ = (index_ - 1 + count_) % count_;
    scroll();
}

void MenuCursor::down() noexcept
{
    if (count_ == 0)
        return;
    index_ = (index_ + 1) % count_;
    scroll();
}

void MenuCursor::scroll() noexcept
{
    if (index_ < first_)
        first_ = index_;
    else if (index_ >= first_ + visible_)
        first_ = index_ - visible_ + 1;
}

// ---------------------------------------------------------------- loading

bool MainMenu::load(const std::string& gameRoot, const std::string& saveDir)
{
    saveDir_ = saveDir;
    gameRoot_ = gameRoot;

    if (auto fonts = Assets::loadFonts(gameRoot + "initscripts/FONTS/font.dat"))
        fonts_ = std::move(*fonts);
    else
        std::cerr << "menu: " << fonts.error().message << "\n";
    for (const Assets::Font& f : fonts_)
        if (f.id == 2) // the LCD menu font (matches the "lcd" atlas layout)
            lcdFont_ = &f;
        else if (f.id == 1)
            textFont_ = &f;
        else if (f.id == 3)
            loadFont_ = &f;
    if (auto txd = Assets::Txd::load(gameRoot + "pictures/LOAD/load_pc.txd"))
        loadTextures_.add(*txd);
    else
        std::cerr << "menu: " << txd.error().message << "\n";
    if (auto txd = Assets::Txd::load(gameRoot + "pictures/frontend_pc.txd"))
        frontendTextures_.add(*txd);
    else
        std::cerr << "menu: " << txd.error().message << "\n";

    auto loadArt = [&](const char* page) {
        auto a = std::make_unique<PageArt>();
        if (auto txd = Assets::Txd::load(gameRoot + "pictures/MENU/" + page + "_ep.txd"))
            a->textures.add(*txd);
        else
            std::cerr << "menu: " << txd.error().message << "\n";
        a->background = a->textures.find("background");
        a->frame = a->textures.find("frame");
        a->lcd = a->textures.find("lcd");
        return a;
    };
    mainArt_ = loadArt("main");
    sceneArt_ = loadArt("scene");
    settingsArt_ = loadArt("set");
    bonusArt_ = loadArt("bonus");
    pauseArt_ = loadArt("igame");
    logo_ = mainArt_->textures.find("logo");
    closedThumb_ = sceneArt_->textures.find("closed");

    reloadText(); // the game text and the scene titles from it
    sceneThumbs_.clear();
    for (const SceneInfo& s : scenes_)
        sceneThumbs_.push_back(sceneArt_->textures.find(std::to_string(s.number)));

    open();
    const bool ok = lcdFont_ && mainArt_->lcd && !scenes_.empty();
    std::cerr << "menu: " << scenes_.size() << " scenes, " << gxt_.size() << " text entries"
              << (ok ? "" : " (incomplete: missing font, page art or levels.txt)") << "\n";
    return ok;
}

void MainMenu::open()
{
    inGame_ = false;
    question_ = false;
    newGame_ = false;
    if (creditsNext_)
    {
        creditsNext_ = false;
        startCredits();
        return;
    }
    goTo(Page::Main);
}

// ---------------------------------------------------------------- navigation

std::vector<std::string> MainMenu::items(Page p) const
{
    switch (p)
    {
    case Page::Main:
        return {gxt_.text("PLAY"), gxt_.text("SELSCE"), gxt_.text("LOADG"), gxt_.text("SETT"), gxt_.text("BONFEA"),
                gxt_.text("QUITPRG")};
    case Page::QuitConfirm:
        return {gxt_.text("QUESYES"), gxt_.text("QUESNO")};
    default:
        return {};
    }
}

void MainMenu::goTo(Page p)
{
    if (settingsPage(p))
    {
        openSettings(p);
        return;
    }
    page_ = p;
    cursor_.reset(static_cast<int>(items(p).size()), p == Page::Main ? 6 : kListRows);
    if (p == Page::QuitConfirm)
        cursor_.down(); // default to NO
}

MenuAction MainMenu::input(MenuInput in)
{
    if (settingsPage(page_))
        return settingsInput(in);
    if (page_ == Page::Pause)
        return pauseInput(in);
    if (page_ == Page::SceneSelect)
        return sceneInput(in);
    if (page_ >= Page::LoadChoice && page_ <= Page::Credits)
        return slotsInput(in);
    if (page_ == Page::Bonus || page_ == Page::BonusPicture)
        return bonusInput(in);
    if (page_ == Page::Results || page_ == Page::Failed)
        return resultsInput(in);
    MenuAction act;
    switch (in)
    {
    case MenuInput::Up:
        cursor_.up();
        return act;
    case MenuInput::Down:
        cursor_.down();
        return act;
    case MenuInput::Left:
    case MenuInput::Right:
        return act;
    case MenuInput::Back:
        goTo(page_ == Page::Main ? Page::QuitConfirm : Page::Main);
        return act;
    case MenuInput::Confirm:
        break;
    }

    const int i = cursor_.index();
    switch (page_)
    {
    case Page::Main:
        switch (i)
        {
        case 0: return playInput(); // PLAY (0x5d6900)
        case 1: goTo(Page::SceneSelect); break;
        case 2: goTo(Page::LoadChoice); break;
        case 3: goTo(Page::Settings); break;
        case 4: goTo(Page::Bonus); break;
        case 5: goTo(Page::QuitConfirm); break;
        default: break;
        }
        break;
    case Page::QuitConfirm:
        if (i == 0)
            act.kind = MenuAction::Kind::Quit;
        else
            goTo(Page::Main);
        break;
    default:
        break;
    }
    return act;
}

void MainMenu::update(float32 dt)
{
    clock_ += dt;
    resultsTime_ += dt;
    if (page_ == Page::Credits)
    {
        // The roll (0x5d6f30), 0.002 a frame at 30 frames a second (ours).
        creditsTick_ += dt;
        for (; creditsTick_ >= 1.0f / 30.0f; creditsTick_ -= 1.0f / 30.0f)
            stepCredits();
    }
    std::uniform_real_distribution<float32> j(-0.008f, 0.008f); // background ghost jitter
    for (auto& g : jitter_)
    {
        g[0] = j(rng_);
        g[1] = j(rng_);
    }
}

// ---------------------------------------------------------------- drawing

const MainMenu::PageArt& MainMenu::art(Page p) const
{
    switch (p)
    {
    case Page::SceneSelect: return *sceneArt_;
    case Page::Bonus:
    case Page::BonusPicture: return *bonusArt_;
    case Page::SaveSlots:
    case Page::GameComplete: return *settingsArt_; // the exe's page switch gives them SET_EP (0x5d7141)
    case Page::Pause: return *pauseArt_;
    default: return settingsPage(p) ? *settingsArt_ : *mainArt_;
    }
}

UiText MainMenu::font(Page p) const
{
    const PageArt& a = art(p);
    return UiText(lcdFont_, a.lcd ? a.lcd : mainArt_->lcd, aspect_);
}

void MainMenu::drawChrome(Renderer::UiBatch& ui, Page p, const std::string& title) const
{
    const PageArt& a = art(p);
    ui.rect(0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, 255);
    if (a.background)
    {
        Renderer::UiQuad bg;
        bg.x0 = 0.0f; bg.y0 = 0.0f; bg.x1 = 1.0f; bg.y1 = 1.0f;
        bg.texture = a.background;
        ui.add(bg);
        for (const auto& g : jitter_) // faint jittered copies: the VHS wobble
        {
            Renderer::UiQuad ghost = bg;
            ghost.x0 += g[0]; ghost.x1 += g[0];
            ghost.y0 += g[1]; ghost.y1 += g[1];
            ghost.a = 8;
            ui.add(ghost);
        }
    }
    if (a.frame)
    {
        Renderer::UiQuad fr;
        fr.x0 = 0.0f; fr.y0 = 0.0f; fr.x1 = 1.0f; fr.y1 = 1.0f;
        fr.texture = a.frame;
        fr.blend = Renderer::UiBlend::Additive;
        ui.add(fr);
    }

    if (title.empty())
        return; // the in-game menu has no header
    // Camcorder header (0x5d5740): a black box at (0.36, 0.036), 0.28 wide, with the running
    // HH:MM:SS:FF and CAM1; under it the page title, centred in a black box at least as wide.
    const UiText text = font(p);
    const float32 s = kItemScale;
    const float32 lh = text.lineHeight(s);
    ui.rect(0.36f, 0.036f - 0.0005f, 0.36f + 0.28f, 0.036f + lh + 0.005f, 0, 0, 0, 255);
    const int total = static_cast<int>(clock_ * 30.0f);
    const int parts[4] = {total / 108000, (total / 1800) % 60, (total / 30) % 60, total % 30};
    for (int i = 0; i < 4; ++i)
    {
        char num[8];
        std::snprintf(num, sizeof(num), "%02d", parts[i]);
        text.draw(ui, num, 0.38f + 0.046f * static_cast<float32>(i), 0.036f, s, kText);
        if (i < 3)
            text.draw(ui, ":", 0.414f + 0.046f * static_cast<float32>(i), 0.036f, s, kText);
    }
    text.draw(ui, gxt_.text("CAM1"), 0.506f + 0.057f, 0.036f, s, kText);
    const float32 w = text.width(title, s);
    const float32 box = w > 0.28f ? w + aspect_ * 0.05f : 0.28f;
    ui.rect(0.5f - box * 0.5f, lh + 0.036f - 0.0005f, 0.5f + box * 0.5f, lh + 0.036f + lh + 0.005f, 0, 0, 0, 255);
    text.draw(ui, title, 0.5f - w * 0.5f, lh + 0.036f, s, kText);
}

void MainMenu::drawFooter(Renderer::UiBatch& ui, const std::string& help, const std::string& back, const std::string& ok,
                          const std::string& left) const
{
    // 0x5d5bb0: a grey bar from y 0.848, the help centred (lower in Spanish), the buttons
    // right-aligned to 0.926, all in font 1.
    constexpr float32 kScale = 0.8f;
    constexpr UiColor kButton{200, 200, 200, 255};
    const UiText text(textFont_, frontendTextures_.find(gameExe().fontAtlas), aspect_);
    ui.rect(0.0f, 0.848f, 1.0f, 1.0f, 0x41, 0x41, 0x41, 0x96);
    const float32 helpY = settings_ && settings_->language == 4 ? 0.91f : 0.86f;
    if (!help.empty())
        text.draw(ui, help, 0.5f - text.width(help, kScale) * 0.5f, helpY, kScale, kButton);
    if (!left.empty())
        text.draw(ui, left, 0.074f, 0.86f, kScale, kButton);
    if (!back.empty())
        text.draw(ui, back, 0.926f - text.width(back, kScale), 0.86f, kScale, kButton);
    if (!ok.empty())
        text.draw(ui, ok, 0.926f - text.width(ok, kScale), 0.86f + 0.05f, kScale, kButton);
}

void MainMenu::drawList(Renderer::UiBatch& ui, const UiText& text, const std::vector<std::string>& list, const MenuCursor& cursor,
                        float32 x, float32 y, float32 scale) const
{
    const float32 h = text.lineHeight(scale);
    const int end = std::min(cursor.first() + cursor.visible(), static_cast<int>(list.size()));
    for (int i = cursor.first(); i < end; ++i)
    {
        const std::string& s = list[static_cast<std::size_t>(i)];
        const bool selected = i == cursor.index();
        if (selected) // highlight bar behind the selected entry
            ui.rect(x - 0.01f, y - 0.004f, x + text.width(s, scale) + 0.01f, y + h + 0.002f, kHighlight, kHighlight, kHighlight, 255);
        text.draw(ui, s, x, y, scale, selected ? kSelectedText : kText);
        y += kItemStep * scale / kItemScale;
    }
    if (cursor.first() > 0)
        text.draw(ui, "^", x - 0.04f, kItemY, scale, kDim);
}

void MainMenu::drawLoading(Renderer::UiBatch& ui, const std::string& levelFolder)
{
    // 0x5eead0: black, the level's PICLOAD picture over the screen, the scene's name (LVL_n and
    // LVL_nA unless "-") in font 3 at 1.5 from (0.1, 0.1) in grey 0xa0 over a black copy 0.005
    // up and left and a little larger, and the "mhlogo" at (0.1, 0.83).
    ui.rect(0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, 255);
    loadPicture_ = std::make_unique<TextureSet>();
    if (auto txd = Assets::Txd::load(gameRoot_ + "levels/" + levelFolder + "/picload_pc.txd"))
        loadPicture_->add(*txd);
    if (const Renderer::Texture* pic = loadPicture_->find("PICLOAD"))
    {
        Renderer::UiQuad q;
        q.x0 = 0.0f; q.y0 = 0.0f; q.x1 = 1.0f; q.y1 = 1.0f;
        q.texture = pic;
        ui.add(q);
    }
    int number = 0;
    for (const SceneInfo& s : scenes_)
        if (Assets::iequals(s.folder, levelFolder))
            number = s.number;
    const UiText font(loadFont_, loadTextures_.find("loading"), aspect_);
    if (number > 0 && font.valid())
    {
        const std::string first = gxt_.text("LVL_" + std::to_string(number));
        std::string second = gxt_.text("LVL_" + std::to_string(number) + "A");
        if (second == "-")
            second.clear();
        const float32 nudge = aspect_ * 0.005f;
        const float32 lh = font.lineHeight(1.5f);
        const UiColor shadow{0, 0, 0, 255}, grey{0xa0, 0xa0, 0xa0, 200};
        auto line = [&](const std::string& s, float32 y) {
            font.draw(ui, s, 0.1f - nudge, y - 0.005f, (1.5f + nudge * 2.0f) * aspect_, 1.51f, shadow);
            font.draw(ui, s, 0.1f, y, 1.5f, grey);
        };
        line(first, 0.1f);
        if (!second.empty())
            line(second, lh + 0.1f);
    }
    if (const Renderer::Texture* logo = loadTextures_.find("mhlogo"))
    {
        Renderer::UiQuad q;
        q.x0 = 0.1f; q.y0 = 0.83f; q.x1 = 0.1f + aspect_ * 0.33333334f; q.y1 = 0.83f + 0.041666668f;
        q.r = q.g = q.b = 0xa0;
        q.a = 200;
        q.texture = logo;
        ui.add(q);
    }
}

void MainMenu::draw(Renderer::UiBatch& ui) const
{
    if (settingsPage(page_))
    {
        drawSettings(ui);
        return;
    }
    if (page_ == Page::Pause)
    {
        drawPause(ui);
        return;
    }
    if (page_ == Page::SceneSelect)
    {
        drawScenes(ui);
        return;
    }
    switch (page_)
    {
    case Page::LoadChoice: drawLoadChoice(ui); return;
    case Page::LoadSlots:
    case Page::SaveSlots: drawSlots(ui); return;
    case Page::GameComplete: drawGameComplete(ui); return;
    case Page::Credits: drawCredits(ui); return;
    case Page::Bonus: drawBonus(ui); return;
    case Page::BonusPicture: drawBonusPicture(ui); return;
    default: break;
    }
    if (page_ == Page::Results)
    {
        drawResults(ui);
        return;
    }
    const UiText text = font(page_);
    switch (page_)
    {
    case Page::Main:
        drawChrome(ui, page_, gxt_.text("MAINM"));
        if (logo_)
        {
            Renderer::UiQuad q;
            q.x0 = 0.1f; q.y0 = 0.38f; q.x1 = 0.1f + 0.375f; q.y1 = 0.38f + 0.125f;
            q.r = q.g = q.b = 180;
            q.texture = logo_;
            ui.add(q);
        }
        drawList(ui, text, items(page_), cursor_, kItemX, kItemY, kItemScale);
        break;
    case Page::QuitConfirm:
        drawChrome(ui, page_, gxt_.text("QUIT"));
        text.draw(ui, gxt_.text("QUITQUE"), 0.08f, 0.32f, 0.45f, kText);
        drawList(ui, text, items(page_), cursor_, kItemX, kItemY, kItemScale);
        break;
    default:
        break;
    }
    drawFooter(ui, "", page_ == Page::Main ? std::string() : gxt_.text("IBACK"), "");
}

} // namespace Game
