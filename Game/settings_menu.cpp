// Game/settings_menu.cpp - the SETTINGS pages of the frontend (MainMenu, see main_menu.h).
//
// The exe's page classes (vtables 0x7d5f00..0x7d6a20: update, draw):
//   CFEP_Settings     0x602800 / 0x6029b0  DIFFICULTY CONTROLS LANGUAGES AUDIO VIDEO BRIGHTNESS,
//                                          then DEFAULT SETTINGS a line lower
//   CFEP_Difficulty   0x5ff590 / 0x5ff730  FETISH / HARDCORE centred, the description below
//   CFEP_Controls     0x5fec30 / 0x5fedd0  INVERT Y (OFF ON), MOUSE SENSITIVITY X / Y (sliders,
//                                          20 steps), REMAP CONTROLS
//   CFEP_Language     0x5fffb0 / 0x6000f0  ENGLISH FRANCAIS ITALIANO DEUTSCH ESPANOL
//   CFEP_SettingsAudio 0x602d20 / 0x602e80 MUSIC VOLUME, SFX VOLUME (16 steps), SUBTITLES
//   CFEP_SettingsVideo 0x603290 / 0x603480 NOISE FX, CAMERA FX (OFF ON), RATIO (4:3 WIDESCREEN),
//                                          GAME DETAIL, SHADOW DETAIL (4 steps)
//   CFEP_TestPicture  0x603aa0 / 0x603bd0  the brightness test picture and slider (21 steps)
//   CFEP_SetDefaults  0x602500 / 0x6025c0  the question, NO / YES
// Items sit at x 0.15 from y 0.2, 0.06 apart, at scale 0.7 (0x7c8718..0x7c8728). Up / down pick
// a row, left / right change its value. Audio, video and brightness have OK and Cancel: Cancel
// (Back) puts the page's values back (0x5da870 / 0x5da960 / 0x5daa00). Default settings resets
// everything but the language (0x5db280, 0x604c70).
//   CFEP_RemapCtrl    0x6097d0 / 0x60a220  22 actions in two columns of 11 (the left from x 0.025,
//                                          the right from 0.525), each with its two keys (at
//                                          +0.18 / +0.34; the left's at 0.205 / 0.365); rows from
//                                          y 0.2, 0.06 / 1.1 apart; CRMOK and CRMCANC centred at x
//                                          0.4 / 0.6, y 0.9. PAUSE is shown grey and can't be
//                                          changed. Confirm on an action waits for a key (Esc:
//                                          none); it is bound as Game/bindings.h's assign
//                                          (0x5d9a20). CRMCANC puts back what the page found
//                                          (0x5d97e0); CRMOK and Back keep it.
#include "Game/main_menu.h"

#include "Game/bindings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace Game {

namespace {

constexpr UiColor kText{180, 180, 180, 255};
constexpr UiColor kSelectedText{0, 0, 0, 255};
constexpr uint8 kHighlight = 150;

constexpr float32 kX = 0.15f;      // 0x7c8718
constexpr float32 kY = 0.2f;       // 0x7c871c
constexpr float32 kScale = 0.7f;   // 0x7c8720 / 0x7c8724
constexpr float32 kStep = 0.06f;   // 0x7c8728

// Languages page rows -> language ids (0x5fffb0).
constexpr int32 kLanguageOfRow[5] = {0, 2, 3, 1, 4};
// REMAP CONTROLS: each row's slot (0x7c8e54) and GXT label.
constexpr int32 kRemapSlot[22] = {14, 15, 16, 17, 23, 24, 1, 18, 19, 31, 20, 6, 8, 2, 0, 4, 36, 37, 32, 33, 34, 35};
constexpr const char* kRemapLabel[22] = {"MFWD",    "MBACK",   "MLEFT",  "MRIGHT", "SPRINT", "SNEAK",
                                         "LOOKB",   "PEEKL",   "PEEKR",  "PERSON1", "PAUSE", "ACTION1",
                                         "ACTION2", "RELOAD",  "USE",    "INVSWAP", "INVUP", "INVDN",
                                         "INV1",    "INV2",    "INV3",   "INV4"};
constexpr int kRemapPause = 10;
constexpr const char* kLanguageKeys[5] = {"ENG", "FRA", "ITA", "GER", "ESP"};

} // namespace

void MainMenu::setSettings(GameSettings* settings)
{
    settings_ = settings;
    reloadText();
}

void MainMenu::reloadText()
{
    // The language's pc_game text (0x493230), and the scene titles from it.
    const std::string suffix = settings_ ? settings_->globalTextSuffix() : std::string();
    auto gxt = Assets::Gxt::load(gameRoot_ + "levels/GLOBAL/PC_TEXT/pc_game" + suffix + ".gxt");
    if (!gxt && !suffix.empty())
        gxt = Assets::Gxt::load(gameRoot_ + "levels/GLOBAL/PC_TEXT/pc_game.gxt");
    if (gxt)
        gxt_ = std::move(*gxt);
    else
        std::cerr << "menu: " << gxt.error().message << "\n";
    if (auto scenes = loadScenes(gameRoot_ + "initscripts/LEVELS/levels.txt", &gxt_))
        scenes_ = std::move(*scenes);
    else
        std::cerr << "menu: " << scenes.error().message << "\n";
}

std::vector<MainMenu::Page> MainMenu::settingsList() const
{
    if (inGame_)
        return {Page::Controls, Page::Audio, Page::Video, Page::Brightness, Page::Defaults};
    return {Page::Difficulty, Page::Controls, Page::Language, Page::Audio, Page::Video, Page::Brightness, Page::Defaults};
}

int MainMenu::settingsRows(Page p) const
{
    switch (p)
    {
    case Page::Settings: return static_cast<int>(settingsList().size());
    case Page::Difficulty: return 2;
    case Page::Controls: return 4;
    case Page::Language: return 5;
    case Page::Audio: return 3;
    case Page::Video: return 5;
    case Page::Defaults: return 2;
    default: return 1;
    }
}

void MainMenu::openSettings(Page p)
{
    const Page from = page_;
    page_ = p;
    cursor_.reset(settingsRows(p), settingsRows(p));
    if (settings_)
    {
        // The pages with Cancel remember what to put back.
        if (p == Page::Audio || p == Page::Video || p == Page::Brightness)
            undo_ = *settings_;
        if (p == Page::Remap && bindings_)
        {
            remapUndo_ = *bindings_;
            remapRow_ = 0;
            remapCapture_ = false;
        }
        // Difficulty and Languages open on the current choice.
        int row = 0;
        if (p == Page::Difficulty)
            row = std::clamp(settings_->difficulty, 0, 1);
        if (p == Page::Language)
            for (int i = 0; i < 5; ++i)
                if (kLanguageOfRow[i] == settings_->language)
                    row = i;
        // The settings list keeps its row when a page returns to it.
        if (p == Page::Settings)
        {
            const std::vector<Page> list = settingsList();
            for (int i = 0; i < static_cast<int>(list.size()); ++i)
                if (list[i] == from || (from == Page::Remap && list[i] == Page::Controls))
                    row = i;
        }
        for (int i = 0; i < row; ++i)
            cursor_.down();
    }
}

MenuAction MainMenu::remapInput(MenuInput in)
{
    // 0x6097d0: up / down within a column, through the bottom row; left / right across.
    MenuAction none;
    if (remapCapture_)
        return none;
    int& r = remapRow_;
    const bool rightColumn = r >= 11 && r != 22;
    switch (in)
    {
    case MenuInput::Up:
        if (r == 22 || r == 23)
            r = r == 22 ? 10 : 21;
        else if (r == 0 || r == 11)
            r = r == 0 ? 22 : 23;
        else
            --r;
        break;
    case MenuInput::Down:
        if (r == 22 || r == 23)
            r = r == 22 ? 0 : 11;
        else if (r == 10 || r == 21)
            r = rightColumn ? 23 : 22;
        else
            ++r;
        break;
    case MenuInput::Left:
    case MenuInput::Right:
        if (r == 22 || r == 23)
            r = r == 22 ? 23 : 22;
        else
            r = r < 11 ? r + 11 : r - 11;
        break;
    case MenuInput::Confirm:
        if (r == 22)
            openSettings(Page::Controls); // CRMOK
        else if (r == 23)
        {
            if (bindings_)
                *bindings_ = remapUndo_; // CRMCANC (0x5d97e0)
            openSettings(Page::Controls);
        }
        else if (r != kRemapPause && bindings_)
            remapCapture_ = true;
        break;
    case MenuInput::Back:
        openSettings(Page::Controls);
        break;
    }
    return none;
}

void MainMenu::captureKey(int32 code)
{
    // Esc (1000) leaves it as it was; anything else is bound (0x5d9a20).
    if (!remapCapture_)
        return;
    remapCapture_ = false;
    if (code == 1000 || !bindings_ || remapRow_ < 0 || remapRow_ >= 22)
        return;
    Bindings b(bindings_);
    b.assign(kRemapSlot[remapRow_], code);
    std::cerr << "menu: " << kRemapLabel[remapRow_] << " bound to " << Bindings::gxtName(code) << std::endl;
}

void MainMenu::capturePad(uint32 bit)
{
    if (!remapCapture_)
        return;
    remapCapture_ = false;
    if (bit == 0 || !padBindings_ || remapRow_ < 0 || remapRow_ >= 22)
        return;
    Bindings pad(padBindings_);
    pad.assign(kRemapSlot[remapRow_], static_cast<int32>(bit));
    std::cerr << "menu: " << kRemapLabel[remapRow_] << " bound to pad bit 0x" << std::hex << bit << std::dec << std::endl;
}

MenuAction MainMenu::settingsInput(MenuInput in)
{
    MenuAction none;
    if (!settings_)
    {
        if (in == MenuInput::Back || in == MenuInput::Confirm)
            goTo(inGame_ ? Page::Pause : Page::Main);
        return none;
    }
    GameSettings& s = *settings_;
    const int row = cursor_.index();
    auto step = [&](int32& v, int dir, int32 count, bool wrap) {
        v += dir;
        if (wrap)
            v = (v % count + count) % count;
        else
            v = std::clamp(v, 0, count - 1);
    };
    const int dir = in == MenuInput::Left ? -1 : (in == MenuInput::Right ? 1 : 0);

    switch (in)
    {
    case MenuInput::Up:
        cursor_.up();
        return none;
    case MenuInput::Down:
        cursor_.down();
        return none;
    default:
        break;
    }

    switch (page_)
    {
    case Page::Settings:
        if (in == MenuInput::Back)
        {
            // Back to where SETTINGS was opened from: the in-game menu (0x7c89ec) or the main page.
            if (inGame_)
                page_ = Page::Pause;
            else
                goTo(Page::Main);
        }
        else if (in == MenuInput::Confirm)
        {
            const std::vector<Page> list = settingsList();
            openSettings(list[static_cast<std::size_t>(std::clamp(row, 0, static_cast<int>(list.size()) - 1))]);
        }
        break;
    case Page::Difficulty:
        if (in == MenuInput::Confirm)
            s.difficulty = row;
        if (newGame_)
        {
            // A new game (0x5ff590 with 0x7c86f0): on to SAVE GAME for its slot, or back to the
            // brightness page.
            if (in == MenuInput::Confirm)
            {
                if (slots_)
                    openSave(SaveMode::NewGame);
                else
                    return finishNewGame(false);
            }
            else if (in == MenuInput::Back)
                openSettings(Page::Brightness);
            break;
        }
        if (in == MenuInput::Confirm || in == MenuInput::Back)
            openSettings(Page::Settings);
        break;
    case Page::Controls:
        if (dir != 0)
        {
            if (row == 0)
                step(s.invertY, dir, 2, true);
            if (row == 1)
                step(s.mouseX, dir, GameSettings::kMouseSteps, false);
            if (row == 2)
                step(s.mouseY, dir, GameSettings::kMouseSteps, false);
        }
        if (in == MenuInput::Back || (in == MenuInput::Confirm && row < 3))
            openSettings(Page::Settings);
        else if (in == MenuInput::Confirm && row == 3)
            openSettings(Page::Remap);
        break;
    case Page::Remap:
        return remapInput(in);
    case Page::Language:
        if (in == MenuInput::Confirm)
        {
            s.language = kLanguageOfRow[std::clamp(row, 0, 4)];
            reloadText();
        }
        if (in == MenuInput::Confirm || in == MenuInput::Back)
            openSettings(Page::Settings);
        break;
    case Page::Audio:
        if (dir != 0)
        {
            if (row == 0)
                step(s.musicVolume, dir, GameSettings::kVolumeSteps, false);
            if (row == 1)
                step(s.sfxVolume, dir, GameSettings::kVolumeSteps, false);
            if (row == 2)
                step(s.subtitles, dir, 2, true);
        }
        if (in == MenuInput::Back)
        {
            s.sfxVolume = undo_.sfxVolume;
            s.musicVolume = undo_.musicVolume;
            s.subtitles = undo_.subtitles;
        }
        if (in == MenuInput::Back || in == MenuInput::Confirm)
            openSettings(Page::Settings);
        break;
    case Page::Video:
        if (dir != 0)
        {
            if (row == 0)
                step(s.noiseFx, dir, 2, true);
            if (row == 1)
                step(s.cameraFx, dir, 2, true);
            if (row == 2)
                step(s.ratio, dir, 2, true);
            if (row == 3)
                step(s.gameDetail, dir, GameSettings::kDetailSteps, false);
            if (row == 4)
                step(s.shadowDetail, dir, GameSettings::kDetailSteps, false);
        }
        if (in == MenuInput::Back)
        {
            s.noiseFx = undo_.noiseFx;
            s.cameraFx = undo_.cameraFx;
            s.ratio = undo_.ratio;
            s.gameDetail = undo_.gameDetail;
            s.shadowDetail = undo_.shadowDetail;
        }
        if (in == MenuInput::Back || in == MenuInput::Confirm)
            openSettings(Page::Settings);
        break;
    case Page::Brightness:
        if (dir != 0)
            step(s.brightness, dir, GameSettings::kBrightnessSteps, false);
        if (in == MenuInput::Back)
            s.brightness = undo_.brightness;
        if (newGame_)
        {
            // A new game (0x603aa0 with 0x7c86f0): OK goes on to DIFFICULTY, Back to where it began.
            if (in == MenuInput::Confirm)
                openSettings(Page::Difficulty);
            else if (in == MenuInput::Back)
            {
                newGame_ = false;
                goTo(newGameFrom_);
            }
            break;
        }
        if (in == MenuInput::Back || in == MenuInput::Confirm)
            openSettings(Page::Settings);
        break;
    case Page::Defaults:
        if (dir != 0)
            dir < 0 ? cursor_.up() : cursor_.down();
        if (in == MenuInput::Confirm && row == 1)
        {
            // 0x604c70(0): every value back to its default but the language.
            const int32 language = s.language;
            s = GameSettings{};
            s.language = language;
        }
        if (in == MenuInput::Confirm || in == MenuInput::Back)
            openSettings(Page::Settings);
        break;
    default:
        break;
    }
    return none;
}

// ---------------------------------------------------------------- drawing

void MainMenu::drawItem(Renderer::UiBatch& ui, const UiText& text, const std::string& label, float32 x, float32 y,
                        float32 scale, bool selected) const
{
    // 0x5d55c0: the highlight box, 0.01 round the text; 0x5d5b30: the text.
    if (selected)
    {
        const float32 h = text.lineHeight(scale);
        ui.rect(x - 0.01f, y - 0.0005f, x + text.width(label, scale) + 0.01f, y + h + 0.005f, kHighlight, kHighlight,
                kHighlight, 255);
    }
    text.draw(ui, label, x, y, scale, selected ? kSelectedText : kText);
}

void MainMenu::drawSlider(Renderer::UiBatch& ui, const UiText& text, float32 x, float32 y, int32 steps, int32 value) const
{
    // 0x5d61e0: a 0.3 x 0.02 bar in a highlight-coloured frame with a tick every 0.03, filled to
    // the value; "-" and "+" above its ends.
    constexpr float32 kWidth = 0.3f;
    const float32 pad = aspect_ * 0.0072f;
    ui.rect(x - pad, y - 0.0072f, x + kWidth + pad, y - 0.0072f + 0.0344f, kHighlight, kHighlight, kHighlight, 255);
    const float32 tick = aspect_ * 0.004f;
    for (float32 t = x; t < x + kWidth; t += 0.03f)
        ui.rect(t - tick * 0.5f, y - 0.0072f - 0.004f, t + tick * 0.5f, y - 0.0072f, kHighlight, kHighlight, kHighlight, 255);
    ui.rect(x, y, x + kWidth, y + 0.02f, 0, 0, 0, 255);
    const float32 fill = steps > 1 ? kWidth / static_cast<float32>(steps - 1) * static_cast<float32>(value) : 0.0f;
    ui.rect(x, y, x + fill, y + 0.02f, kHighlight, kHighlight, kHighlight, 255);
    text.draw(ui, "-", x - pad, y - 0.06f, kScale, kText);
    text.draw(ui, "+", x + kWidth - text.width("+", kScale) + pad, y - 0.06f, kScale, kText);
}

void MainMenu::drawSettings(Renderer::UiBatch& ui) const
{
    const UiText text = font(page_);
    const int row = cursor_.index();
    const GameSettings s = settings_ ? *settings_ : GameSettings{};
    auto t = [&](const char* key) { return gxt_.text(key); };
    // Two choices side by side after a label (OFF ON, 4:3 WIDESCREEN), the current one boxed.
    auto choices = [&](float32 x, float32 y, const std::string& a, const std::string& b, int32 value) {
        drawItem(ui, text, a, x, y, kScale, value == 0);
        drawItem(ui, text, b, x + 0.09f, y, kScale, value == 1);
    };
    const std::string back = t("IBACK"), cancel = t("ICANCEL"), ok = t("IOK");

    switch (page_)
    {
    case Page::Settings:
    {
        drawChrome(ui, page_, t("SETT"));
        const std::vector<Page> list = settingsList();
        float32 y = kY;
        for (int i = 0; i < static_cast<int>(list.size()); ++i)
        {
            const char* key = "DEFSET";
            switch (list[static_cast<std::size_t>(i)])
            {
            case Page::Difficulty: key = "DIFF"; break;
            case Page::Controls: key = "CONTR"; break;
            case Page::Language: key = "LANGUA"; break;
            case Page::Audio: key = "AUDIO"; break;
            case Page::Video: key = "VIDEO"; break;
            case Page::Brightness: key = "BRIGHTN"; break;
            default: y += kStep; break; // DEFAULT SETTINGS a line lower
            }
            drawItem(ui, text, t(key), kX, y, kScale, row == i);
            y += kStep;
        }
        drawFooter(ui, "", back, "");
        break;
    }
    case Page::Difficulty:
    {
        drawChrome(ui, page_, t("DIFF"));
        // The two choices centred at y 0.44 and 0.5; the one in force above them, larger.
        const std::string easy = t("EASY"), hard = t("NORMAL");
        drawItem(ui, text, easy, 0.5f - text.width(easy, kScale) * 0.5f, 0.44f, kScale, row == 0);
        drawItem(ui, text, hard, 0.5f - text.width(hard, kScale) * 0.5f, 0.44f + kStep, kScale, row == 1);
        const std::string now = s.difficulty == 1 ? hard : easy;
        const float32 big = kScale * 1.5f;
        text.draw(ui, now, 0.5f - text.width(now, big) * 0.5f, 0.15f + text.lineHeight(kScale), big, kText);
        drawFooter(ui, t(row == 0 ? "EASYI" : "NORMALI"), back, "");
        break;
    }
    case Page::Controls:
    {
        drawChrome(ui, page_, t("CONTMOD"));
        float32 y = kY;
        drawItem(ui, text, t("INVERTY"), kX, y, kScale, row == 0);
        choices(kX + 0.36f, y, t("OFF").empty() ? "OFF" : "OFF", "ON", s.invertY);
        y += kStep * 2.0f;
        drawItem(ui, text, t("MOUSENX"), kX, y, kScale, row == 1);
        drawSlider(ui, text, kX + 0.36f, y + 0.025f, GameSettings::kMouseSteps, s.mouseX);
        y += kStep * 2.0f;
        drawItem(ui, text, t("MOUSENY"), kX, y, kScale, row == 2);
        drawSlider(ui, text, kX + 0.36f, y + 0.025f, GameSettings::kMouseSteps, s.mouseY);
        drawItem(ui, text, t("REMAPC"), kX, y + kStep * 2.0f, kScale, row == 3);
        drawFooter(ui, "", back, "");
        break;
    }
    case Page::Remap:
    {
        drawChrome(ui, page_, t("REMAPC"));
        const Bindings b(bindings_);
        constexpr float32 kRowScale = 0.7f / 1.5f; // (0.7 / 1.6 across, 0.7 / 1.4 high)
        const float32 rowStep = kStep / 1.1f;
        const bool blink = std::fmod(clock_, 0.5f) < 0.25f;
        for (int i = 0; i < 22; ++i)
        {
            const bool right = i >= 11;
            const float32 x = right ? kX + 0.4f - 0.025f : 0.025f;
            const float32 y = kY + rowStep * static_cast<float32>((right ? i - 11 : i) + 1);
            drawItem(ui, text, t(kRemapLabel[i]), x, y, kRowScale, remapRow_ == i);
            UiColor keyColour = i == kRemapPause ? UiColor{100, 100, 100, 255} : kText;
            if (remapCapture_ && remapRow_ == i)
                keyColour = blink ? UiColor{170, 170, 170, 255} : UiColor{128, 128, 128, 255};
            for (int c = 0; c < 2; ++c)
            {
                const std::string name = Bindings::gxtName(b.key(kRemapSlot[i], c));
                const float32 kx = right ? x + (c == 0 ? 0.18f : 0.34f) : (c == 0 ? 0.205f : 0.365f);
                if (!name.empty())
                    text.draw(ui, t(name.c_str()), kx, y, kRowScale, keyColour);
            }
        }
        if (!remapCapture_)
        {
            const float32 big = kRowScale * 1.4f;
            const std::string ok = t("CRMOK"), cancel = t("CRMCANC");
            drawItem(ui, text, ok, 0.4f - text.width(ok, big) * 0.5f, 0.9f, big, remapRow_ == 22);
            drawItem(ui, text, cancel, 0.6f - text.width(cancel, big) * 0.5f, 0.9f, big, remapRow_ == 23);
        }
        break;
    }
    case Page::Language:
    {
        drawChrome(ui, page_, t("LANGUA"));
        for (int i = 0; i < 5; ++i)
            drawItem(ui, text, t(kLanguageKeys[i]), kX, kY + kStep * static_cast<float32>(i), kScale, row == i);
        drawFooter(ui, "", back, "");
        break;
    }
    case Page::Audio:
    {
        drawChrome(ui, page_, t("AUDSET"));
        // 0x602e80: 0.08 left and 0.02 lower than the other pages, the values at + 0.46.
        const float32 x = kX - 0.08f;
        float32 y = kY + 0.08f - kStep;
        drawItem(ui, text, t("MUSVOL"), x, y, kScale, row == 0);
        drawSlider(ui, text, x + 0.46f, y + 0.025f, GameSettings::kVolumeSteps, s.musicVolume);
        y += kStep * 2.0f;
        drawItem(ui, text, t("SFXVOL"), x, y, kScale, row == 1);
        drawSlider(ui, text, x + 0.46f, y + 0.025f, GameSettings::kVolumeSteps, s.sfxVolume);
        y += kStep * 2.0f;
        drawItem(ui, text, t("SUBTIT"), x, y, kScale, row == 2);
        choices(x + 0.46f, y, "OFF", "ON", s.subtitles);
        drawFooter(ui, "", cancel, ok);
        break;
    }
    case Page::Video:
    {
        drawChrome(ui, page_, t("VIDSET"));
        float32 y = kY;
        drawItem(ui, text, t("NOISEFX"), kX, y, kScale, row == 0);
        choices(kX + 0.3f, y, "OFF", "ON", s.noiseFx);
        y += kStep;
        drawItem(ui, text, t("CAMFX"), kX, y, kScale, row == 1);
        choices(kX + 0.3f, y, "OFF", "ON", s.cameraFx);
        y += kStep;
        drawItem(ui, text, t("RATIO"), kX, y, kScale, row == 2);
        choices(kX + 0.3f, y, "4:3", t("WIDESC"), s.ratio);
        y += kStep * 2.0f;
        drawItem(ui, text, t("DETGAME"), kX, y, kScale, row == 3);
        drawSlider(ui, text, kX + 0.3f, y + 0.025f, GameSettings::kDetailSteps, s.gameDetail);
        y += kStep * 2.0f;
        drawItem(ui, text, t("DETSHAD"), kX, y, kScale, row == 4);
        drawSlider(ui, text, kX + 0.3f, y + 0.025f, GameSettings::kDetailSteps, s.shadowDetail);
        drawFooter(ui, "", cancel, ok);
        break;
    }
    case Page::Brightness:
    {
        drawChrome(ui, page_, t("BRIGHTN"));
        // 0x603bd0: two grey boxes in a frame - the left one at the brightness's own level
        // (b / 20 x 255 / 10), the right one 120 lighter - the slider over them, the hints.
        const float32 boxW = aspect_ * 0.15f;
        const int level = static_cast<int>(static_cast<float32>(s.brightness) / (GameSettings::kBrightnessSteps - 1) * 255.0f / 10.0f + 0.5f);
        const float32 border = aspect_ * 0.0025f;
        const float32 frameX = 0.58f - border;
        const float32 frameW = border * 2.0f + boxW;
        ui.rect(frameX, 0.2945f, frameX + frameW, 0.2945f + 0.155f, 0x50, 0x50, 0x50, 255);
        ui.rect(frameX, 0.3095f, frameX + frameW, 0.3095f + 0.125f, 0, 0, 0, 255);
        const float32 inset = aspect_ * 0.015f;
        ui.rect(frameX + inset, 0.2945f, frameX + frameW - inset, 0.2945f + 0.155f, 0, 0, 0, 255);
        const auto grey = [](int v) { return static_cast<uint8>(std::clamp(v, 0, 255)); };
        ui.rect(0.58f, 0.297f, 0.58f + boxW, 0.297f + 0.15f, grey(level), grey(level), grey(level), 255);
        ui.rect(0.774f, 0.297f, 0.774f + boxW, 0.297f + 0.15f, grey(level + 120), grey(level + 120), grey(level + 120), 255);
        float32 y = 0.17f - kStep;
        drawItem(ui, text, t("BRIGHTN"), 0.1f, y, kScale, true);
        drawSlider(ui, text, 0.58f, 0.22f, GameSettings::kBrightnessSteps, s.brightness);
        y += kStep * 2.0f;
        for (const char* key : {"BRSET1", "BRSET2", "BRSET3", "BRSET4"})
        {
            if (t(key) != "^") // "^": an empty line
                text.draw(ui, t(key), 0.1f, y, kScale, kText);
            y += kStep;
        }
        y += kStep * 1.5f - kStep;
        for (const char* key : {"BRINFO1", "BRINFO2", "BRINFO3", "BRINFO4", "BRINFO5"})
        {
            y += kStep;
            text.draw(ui, t(key), 0.1f, y, kScale, kText);
        }
        drawFooter(ui, "", cancel, ok);
        break;
    }
    case Page::Defaults:
    {
        drawChrome(ui, page_, t("DEFAULT"));
        // 0x6025c0: the question centred at y 0.45; NO and YES 2.5 times larger, 0.2 either side.
        const float32 y = kY + 0.25f;
        const std::string q = t("DEFQUE");
        float32 qs = kScale;
        if (text.width(q, qs) > 0.9f)
            qs = 0.9f / text.width(q, 1.0f); // ours: the exe wraps the long line
        text.draw(ui, q, 0.5f - text.width(q, qs) * 0.5f, y, qs, kText);
        const float32 big = kScale * 2.5f;
        const float32 oy = y + kStep * 3.0f;
        const std::string no = t("QUESNO"), yes = t("QUESYES");
        drawItem(ui, text, no, 0.5f - text.width(no, big) * 0.5f - 0.2f, oy, big, row == 0);
        drawItem(ui, text, yes, 0.5f - text.width(yes, big) * 0.5f + 0.2f, oy, big, row == 1);
        break;
    }
    default:
        break;
    }
}

} // namespace Game
