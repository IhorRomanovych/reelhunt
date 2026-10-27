// Game/bonus_menu.cpp - BONUS FEATURES and its pictures (MainMenu, see main_menu.h).
//
// The exe's BONUS FEATURES (page 14, update 0x6040f0, draw 0x604370) is SELECT SCENE's grid
// (Game/scene_menu.cpp) for the 20 bonus pictures, bonus_ep.txd's "1".."20" ("closed" until
// open): picture i opens with 3 stars or more on story scene i (0x5d6e60; the cheat 0x7c8f34
// opens all). Under each, in the name's place, the stars it takes: three, filled up to the
// scene's best rating. The groups 1-4 .. 17-20 as on the scene list (no BONUS).
// A picture (page 15, 0x5fe800 / 0x5fe920) is bpic<n>_ep.txd's "p<n>" over the whole screen in
// grey 180; with 5 stars on its scene, under a black bar from y 0.85 (alpha 0xa4), one half of
// a cheat (the letters of 0x7c8188, ", " between them): the first half of a pair of pictures
// ends "...", the second starts "...", centred at y 0.88 in font 1, grey 0x80. Confirm or ESC
// goes back.
#include "Game/main_menu.h"

#include "Game/exe_tables.h"
#include "Assets/txd.h"
#include "Renderer/texture.h"

#include <algorithm>

namespace Game {

namespace {

constexpr UiColor kText{180, 180, 180, 255};
constexpr UiColor kSelectedText{0, 0, 0, 255};
constexpr float32 kScale = 0.7f;
constexpr float32 kGroupStart[5] = {0.48f, 0.436f, 0.389f, 0.343f, 0.294f}; // 0x7d6a1c
constexpr float32 kGroupStep[5] = {0.0f, 0.074f, 0.08f, 0.085f, 0.09f};     // 0x7d6a80..
constexpr const char* kGroupLabel[5] = {"1-4", "5-8", "9-12", "13-16", "17-20"};
// 0x7c8188: four letters per picture.
constexpr const char* kCodes[20] = {"URUN", "FAST", "ALLR", "DEAF", "HEAL", "BACK", "THEY", "BOOM", "UGOT", "ARMS",
                                    "UHIT", "HARD", "BUNY", "SUIT", "HELL", "SUIT", "EVIL", "EYES", "PIGG", "SUIT"};

} // namespace

bool MainMenu::pictureOpen(int idx) const
{
    return idx < GameProgress::kStory && progress_ && progress_->scene(idx).best > 2;
}

MenuAction MainMenu::bonusInput(MenuInput in)
{
    MenuAction act;
    if (page_ == Page::BonusPicture)
    {
        if (in == MenuInput::Confirm || in == MenuInput::Back)
        {
            bonusPicture_.reset();
            page_ = Page::Bonus;
        }
        return act;
    }
    auto open = [&](int g) { return g == 0 || (progress_ && progress_->groupOpen(g)); };
    int groups = 1;
    for (int g = 1; g <= 4; ++g)
        groups += open(g) ? 1 : 0;
    switch (in)
    {
    case MenuInput::Up:
        bonusRow_ = bonusRow_ == 0 ? 2 : bonusRow_ - 1;
        break;
    case MenuInput::Down:
        bonusRow_ = bonusRow_ == 2 ? 0 : bonusRow_ + 1;
        break;
    case MenuInput::Left:
    case MenuInput::Right:
        if (bonusRow_ < 2)
            bonusCol_ ^= 1;
        else if (groups > 1)
        {
            const int dir = in == MenuInput::Left ? -1 : 1;
            do
                bonusGroup_ = (bonusGroup_ + dir + groups) % groups;
            while (bonusGroup_ >= 1 && !open(bonusGroup_));
            bonusCol_ = 0;
        }
        break;
    case MenuInput::Confirm:
        if (bonusRow_ < 2)
        {
            const int idx = bonusGroup_ * 4 + bonusRow_ * 2 + bonusCol_;
            if (pictureOpen(idx))
            {
                picture_ = idx;
                bonusPicture_ = std::make_unique<TextureSet>();
                if (auto txd = Assets::Txd::load(gameRoot_ + "pictures/MENU/bpic" + std::to_string(idx + 1) + "_ep.txd"))
                    bonusPicture_->add(*txd);
                page_ = Page::BonusPicture;
            }
        }
        break;
    case MenuInput::Back:
        goTo(Page::Main);
        break;
    }
    return act;
}

void MainMenu::drawBonus(Renderer::UiBatch& ui) const
{
    drawChrome(ui, Page::Bonus, gxt_.text("BONFEA"));
    const UiText text = font(Page::Bonus);
    const float32 w = aspect_ * 0.2f;
    const float32 lh = text.lineHeight(kScale);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 2; ++col)
        {
            const int idx = bonusGroup_ * 4 + row * 2 + col;
            if (idx >= GameProgress::kStory)
                continue;
            const float32 x = col == 0 ? 0.2f : 0.8f - w;
            const float32 y = row == 0 ? 0.14f : 0.44f;
            const bool chosen = bonusRow_ == row && bonusCol_ == col;
            if (const Renderer::Texture* thumb = bonusArt_->textures.find(pictureOpen(idx) ? std::to_string(idx + 1) : "closed"))
            {
                Renderer::UiQuad q;
                q.x0 = x; q.y0 = y; q.x1 = x + w; q.y1 = y + 0.2f;
                q.r = q.g = q.b = chosen ? 200 : 96;
                q.texture = thumb;
                ui.add(q);
            }
            // The stars it takes, filled up to the scene's best.
            const int best = progress_ ? std::min(progress_->scene(idx).best, 3) : 0;
            std::string stars;
            for (int i = 0; i < 3; ++i)
                stars += static_cast<char>(i < best ? 0x9d : 0x9c);
            const float32 sw = text.width(stars, kScale);
            const float32 tx = x + w * 0.5f - sw * 0.5f;
            const float32 ty = y + 0.2f + 0.0005f;
            const uint8 box = chosen ? 150 : 0;
            ui.rect(tx - 0.01f, ty - 0.0005f, tx + sw + 0.01f, ty - 0.0005f + lh + 0.005f, box, box, box, 255);
            text.draw(ui, stars, tx, ty, kScale, chosen ? kSelectedText : kText);
        }
    auto open = [&](int g) { return g == 0 || (progress_ && progress_->groupOpen(g)); };
    int opened = 0;
    for (int g = 1; g <= 4; ++g)
        opened += open(g) ? 1 : 0;
    float32 x = kGroupStart[opened];
    for (int g = 0; g <= 4; ++g)
    {
        if (!open(g))
            continue;
        x += kGroupStep[g];
        drawItem(ui, text, kGroupLabel[g], x, 0.78f, kScale, bonusRow_ == 2 && bonusGroup_ == g);
    }
    drawFooter(ui, "", gxt_.text("IBACK"), "");
}

void MainMenu::drawBonusPicture(Renderer::UiBatch& ui) const
{
    ui.rect(0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, 255);
    if (const Renderer::Texture* pic = bonusPicture_ ? bonusPicture_->find("p" + std::to_string(picture_ + 1)) : nullptr)
    {
        Renderer::UiQuad q;
        q.x0 = 0.0f; q.y0 = 0.0f; q.x1 = 1.0f; q.y1 = 1.0f;
        q.r = q.g = q.b = 180;
        q.texture = pic;
        ui.add(q);
    }
    if (!progress_ || progress_->scene(picture_).best != 5)
        return;
    // Half a cheat: the first picture of a pair ends "...", the second starts "...".
    const bool second = picture_ % 2 == 1;
    std::string line = second ? "... " : "";
    for (int i = 0; i < 4; ++i)
    {
        line += kCodes[picture_][i];
        if (i != 3)
            line += ", ";
    }
    if (!second)
        line += " ...";
    ui.rect(0.0f, 0.85f, 1.0f, 1.0f, 0, 0, 0, 0xa4);
    const UiText text(textFont_, frontendTextures_.find(gameExe().fontAtlas), aspect_);
    text.draw(ui, line, 0.5f - text.width(line, 1.0f) * 0.5f, 0.88f, 1.0f, UiColor{0x80, 0x80, 0x80, 255});
}

} // namespace Game
