// Game/scene_menu.cpp - SELECT SCENE (MainMenu, see main_menu.h).
//
// The exe's scene list (update 0x601660, draw 0x6019c0): four scenes at a time, a 2 x 2
// grid of thumbnails (scene_ep.txd "1".."24", "closed" for a scene not yet open), and under it
// the groups 1-4, 5-8, 9-12, 13-16, 17-20 and BONUS, each shown once open (Game/progress.h).
//   Thumbnails aspect x 0.2 by 0.2 at x 0.2 / 0.8 - w, y 0.14 / 0.44, grey 96 (200 chosen).
//   A done story scene shows its best rating over the thumbnail: five stars (LCD glyphs 0x9c /
//   0x9d) at (x + w x 0.05, y + 0.15), 0.66, white at alpha 100.
//   Under it the name, LVL_n and LVL_nA (LVL_CLO when closed), LCD font at 0.7, centred on the
//   thumbnail at y + 0.2005 in a box (the highlight colour when chosen, else black).
//   The groups at y 0.78 from x 0.48 / 0.436 / 0.389 / 0.343 / 0.294 by how many are open
//   (half of BONUS's width + 0.027 further left when it is), then 0.074, 0.08, 0.085, 0.09 and
//   0.1 apart.
//   Up / down move between the two thumbnail rows and the groups; left / right between the
//   columns, or along the open groups (back to the left column). Confirm starts an open scene.
#include "Game/main_menu.h"

#include "Renderer/texture.h"

#include <algorithm>

namespace Game {

namespace {

constexpr UiColor kText{180, 180, 180, 255};
constexpr UiColor kSelectedText{0, 0, 0, 255};
constexpr float32 kScale = 0.7f;
constexpr float32 kGroupStart[5] = {0.48f, 0.436f, 0.389f, 0.343f, 0.294f}; // 0x7d6554
constexpr float32 kGroupStep[6] = {0.0f, 0.074f, 0.08f, 0.085f, 0.09f, 0.1f};  // 0x7d65d8..
constexpr const char* kGroupLabel[5] = {"1-4", "5-8", "9-12", "13-16", "17-20"};

} // namespace

int MainMenu::sceneCount() const
{
    return progress_ && progress_->groupOpen(5) ? GameProgress::kScenes : GameProgress::kStory;
}

bool MainMenu::scenePlayable(int idx) const
{
    return progress_ ? progress_->playable(idx) : idx < GameProgress::kStory;
}

MenuAction MainMenu::sceneInput(MenuInput in)
{
    MenuAction act;
    auto open = [&](int g) { return !progress_ || progress_->groupOpen(g); };
    int groups = 1;
    for (int g = 1; g <= 4; ++g)
        groups += open(g) ? 1 : 0;
    if (open(5))
        groups = 6;
    switch (in)
    {
    case MenuInput::Up:
        sceneRow_ = sceneRow_ == 0 ? 2 : sceneRow_ - 1;
        break;
    case MenuInput::Down:
        sceneRow_ = sceneRow_ == 2 ? 0 : sceneRow_ + 1;
        break;
    case MenuInput::Left:
    case MenuInput::Right:
    {
        const int dir = in == MenuInput::Left ? -1 : 1;
        if (sceneRow_ < 2)
            sceneCol_ ^= 1;
        else if (groups > 1)
        {
            // Along the groups, over the closed ones (0x601660).
            do
                sceneGroup_ = (sceneGroup_ + dir + groups) % groups;
            while (sceneGroup_ >= 1 && sceneGroup_ <= 4 && !open(sceneGroup_));
            sceneCol_ = 0;
        }
        break;
    }
    case MenuInput::Confirm:
        if (sceneRow_ < 2)
        {
            const int idx = sceneGroup_ * 4 + sceneRow_ * 2 + sceneCol_;
            if (idx < sceneCount() && scenePlayable(idx))
            {
                // Before any game, a new game first (0x601660: 0x7d6ae4 == -1).
                if (progress_ && progress_->lastScene() == -1)
                {
                    startNewGame(Page::SceneSelect);
                    return act;
                }
                act.kind = MenuAction::Kind::StartScene;
                act.scene = idx + 1;
            }
        }
        break;
    case MenuInput::Back:
        goTo(Page::Main);
        break;
    }
    return act;
}

void MainMenu::drawScenes(Renderer::UiBatch& ui) const
{
    drawChrome(ui, Page::SceneSelect, gxt_.text("SCESEL"));
    const UiText text = font(Page::SceneSelect);
    const float32 w = aspect_ * 0.2f;
    const float32 lh = text.lineHeight(kScale);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 2; ++col)
        {
            const int idx = sceneGroup_ * 4 + row * 2 + col;
            if (idx >= sceneCount())
                continue;
            const float32 x = col == 0 ? 0.2f : 0.8f - w;
            const float32 y = row == 0 ? 0.14f : 0.44f;
            const bool chosen = sceneRow_ == row && sceneCol_ == col;
            const bool open = scenePlayable(idx);
            const Renderer::Texture* thumb = closedThumb_;
            if (open && static_cast<std::size_t>(idx) < sceneThumbs_.size() && sceneThumbs_[static_cast<std::size_t>(idx)])
                thumb = sceneThumbs_[static_cast<std::size_t>(idx)];
            if (thumb)
            {
                Renderer::UiQuad q;
                q.x0 = x; q.y0 = y; q.x1 = x + w; q.y1 = y + 0.2f;
                q.r = q.g = q.b = chosen ? 200 : 96;
                q.texture = thumb;
                ui.add(q);
            }
            // The best rating of a done story scene.
            if (progress_ && idx < GameProgress::kStory && progress_->scene(idx).done)
            {
                std::string stars;
                for (int i = 0; i < 5; ++i)
                    stars += static_cast<char>(i < progress_->scene(idx).best ? 0x9d : 0x9c);
                text.draw(ui, stars, x + w * 0.05f, y + 0.15f, 0.66f, UiColor{255, 255, 255, 100});
            }
            // The name under it.
            std::string first = gxt_.text("LVL_CLO"), second;
            if (open)
            {
                first = gxt_.text("LVL_" + std::to_string(idx + 1));
                second = gxt_.text("LVL_" + std::to_string(idx + 1) + "A");
                if (second == "-")
                    second.clear();
            }
            const float32 mw = std::max(text.width(first, kScale), text.width(second, kScale));
            const float32 tx = x + w * 0.5f - mw * 0.5f;
            const float32 ty = y + 0.2f + 0.0005f;
            const float32 boxH = second.empty() ? lh : lh * 2.0f + 0.005f;
            const uint8 box = chosen ? 150 : 0;
            ui.rect(tx - 0.01f, ty - 0.0005f, tx + mw + 0.01f, ty - 0.0005f + boxH + 0.005f, box, box, box, 255);
            text.draw(ui, first, tx, ty, kScale, chosen ? kSelectedText : kText);
            if (!second.empty())
                text.draw(ui, second, tx, ty + lh + 0.005f, kScale, chosen ? kSelectedText : kText);
        }

    // The groups.
    auto open = [&](int g) { return !progress_ || progress_->groupOpen(g); };
    int opened = 0;
    for (int g = 1; g <= 4; ++g)
        opened += open(g) ? 1 : 0;
    float32 x = kGroupStart[opened];
    const std::string bonus = gxt_.text("BONUSSC");
    if (open(5))
        x -= (text.width(bonus, kScale) + 0.027f) / 2.0f;
    for (int g = 0; g <= 5; ++g)
    {
        if (!open(g))
            continue;
        if (g > 0)
            x += kGroupStep[g];
        drawItem(ui, text, g == 5 ? bonus : std::string(kGroupLabel[g]), x, 0.78f, kScale, sceneRow_ == 2 && sceneGroup_ == g);
    }
    drawFooter(ui, "", gxt_.text("IBACK"), "");
}

} // namespace Game
