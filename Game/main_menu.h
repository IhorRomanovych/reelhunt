// Game/main_menu.h - the frontend: main menu, scene selection, load game, quit confirmation.
//
// Reconstructed from the exe (main menu update 0x600af0 / draw 0x600c20, page chrome
// 0x5d72f0, page loader 0x5d7dd0) and the original assets:
//   * items, in order: PLAY, SELECT SCENE, LOAD GAME, SETTINGS, BONUS FEATURES, QUIT (GXT keys
//     PLAY SELSCE LOADG SETT BONFEA QUITPRG); the cursor wraps; Back opens the quit prompt
//   * each page loads pictures/MENU/<page>_ep.txd: "background" (drawn full screen plus four
//     alpha-8 copies jittered by up to +-0.008 - the VHS wobble), "frame" (corner brackets,
//     additive) and "lcd" (the font atlas, font id 2 in font.dat); the main page adds "logo"
//   * layout in screen units: logo at (0.1, 0.38) size 0.375 x 0.125; items at x 0.58 from
//     y 0.40, 0.06 apart, font scale 0.7; text grey (180,180,180), dimmed (80,80,80)
// SETTINGS and its pages: Game/settings_menu.cpp. Bonus Features is not implemented yet and shows
// The game's own "No menu items available yet!" page.
#pragma once
#include "engine_types.h"

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Assets/font.h"
#include "Assets/gxt.h"
#include "Game/level_score.h"
#include "Game/progress.h"
#include "Game/save_slots.h"
#include "Game/scenes.h"
#include "Game/settings.h"
#include "Game/texture_set.h"
#include "Game/ui_text.h"
#include "Renderer/ui.h"

namespace Game {

enum class MenuInput { Up, Down, Left, Right, Confirm, Back };

struct MenuAction {
    // Resume: back into the paused level; QuitLevel: leave it for the main menu; Continue: on from
    // the results to the next scene.
    // Retry: the failed scene again from the start.
    enum class Kind { None, StartScene, LoadSave, Quit, Resume, QuitLevel, Continue, Retry };
    Kind kind = Kind::None;
    int scene = 0;          // StartScene / LoadSave: 1-based scene number
    bool resume = false;    // StartScene from PLAY: the scene's checkpoint if it has one (0x5d6720(scene, 1))
    std::string savePath;   // LoadSave
};

// A vertical list cursor that wraps and keeps `visible` rows scrolled into view.
class MenuCursor {
public:
    void reset(int count, int visible) noexcept;
    void up() noexcept;
    void down() noexcept;
    [[nodiscard]] int index() const noexcept { return index_; }
    [[nodiscard]] int first() const noexcept { return first_; }   // first visible row
    [[nodiscard]] int count() const noexcept { return count_; }
    [[nodiscard]] int visible() const noexcept { return visible_; }

private:
    void scroll() noexcept;
    int index_ = 0, first_ = 0, count_ = 0, visible_ = 1;
};

class MainMenu {
public:
    // Loads the game text, fonts, page art and scene list from the game root, and lists the save
    // slots (the eight slot .SAV files) in `saveDir`. Returns false if the essentials are missing.
    bool load(const std::string& gameRoot, const std::string& saveDir);
    // The settings the SETTINGS pages change (owned by the caller; the text follows its language).
    void setSettings(GameSettings* settings);
    // The player's key bindings (Settings.dat's first table), edited by REMAP CONTROLS.
    void setBindings(std::array<uint8, 0x130>* bindings) noexcept { bindings_ = bindings; }
    // The pad's table (Settings.dat's second): a pad button pressed while REMAP CONTROLS waits
    // is bound there (capturePad), by the same rule.
    void setPadBindings(std::array<uint8, 0x130>* pad) noexcept { padBindings_ = pad; }
    void capturePad(uint32 bit);
    // REMAP CONTROLS waits for a key: the next key, button or wheel step (its exe code,
    // Game/bindings.h) goes to captureKey instead of input().
    [[nodiscard]] bool capturingKey() const noexcept { return page_ == Page::Remap && remapCapture_; }
    void captureKey(int32 code);
    // Straight to REMAP CONTROLS (testing: REELHUNT_REMAP).
    void openRemap() { openSettings(Page::Remap); }
    // The scenes done (owned by the caller): SELECT SCENE's open scenes and best ratings; PLAY,
    // NEW GAME and LOAD GAME change it.
    void setProgress(GameProgress* progress) noexcept { progress_ = progress; }
    // The save slots LOAD GAME and SAVE GAME use (owned by the caller; none: no saving).
    void setSaveSlots(SaveSlots* slots) noexcept { slots_ = slots; }
    // Screen height / width (the exe's horizontal scale, 0x7d3458: 0.75 at 4:3).
    void setAspect(float32 heightOverWidth) noexcept { aspect_ = heightOverWidth; }

    // Show the main page again (e.g. after returning from a level).
    void open();
    // The in-game menu (exe page 8, Game/pause_menu.cpp) over a paused level, with the
    // level's goals (their text, ~colour~ tokens and all).
    void openPause(std::vector<std::string> goals, std::shared_ptr<const SaveFile> checkpoint = nullptr);
    // The end-of-scene results (Game/results_menu.cpp).
    void openResults(const LevelResults& results);
    // The Scene Failed screen, drawn over the scene with drawOverlay (Game/results_menu.cpp).
    void openFailed();
    void drawOverlay(Renderer::UiBatch& ui) const;

    MenuAction input(MenuInput in);
    void update(float32 dt);
    void draw(Renderer::UiBatch& ui) const;
    // The screen shown while level `levelFolder` loads (0x5eead0): its picload picture, the scene
    // name and the game logo.
    void drawLoading(Renderer::UiBatch& ui, const std::string& levelFolder);

    [[nodiscard]] const std::vector<SceneInfo>& scenes() const noexcept { return scenes_; }
    [[nodiscard]] const Assets::Gxt& text() const noexcept { return gxt_; }
    // The frontend's LCD font, for debug overlays.
    [[nodiscard]] UiText lcdFont() const { return font(Page::Main); }

private:
    enum class Page {
        Main, SceneSelect, Settings, QuitConfirm,
        LoadChoice,   // LOAD GAME / START A NEW GAME (page 4, Game/save_menu.cpp)
        LoadSlots,    // LOAD GAME's slots (page 5)
        SaveSlots,    // SAVE GAME's slots (page 6)
        GameComplete, // GAME COMPLETED (page 16)
        Credits,      // the credits roll (page 21)
        Bonus,        // BONUS FEATURES (page 14, Game/bonus_menu.cpp)
        BonusPicture, // a bonus picture (page 15)
        Pause,   // the in-game menu (Game/pause_menu.cpp)
        Results, // the end-of-scene results (Game/results_menu.cpp)
        Failed,  // Scene Failed, over the scene (Game/results_menu.cpp)
        // SETTINGS (Game/settings_menu.cpp): the exe's CFEP_Settings and its pages.
        Difficulty, Controls, Remap, Language, Audio, Video, Brightness, Defaults
    };
    [[nodiscard]] static bool settingsPage(Page p) noexcept { return p == Page::Settings || p >= Page::Difficulty; }

    struct PageArt {
        TextureSet textures;
        const Renderer::Texture* background = nullptr;
        const Renderer::Texture* frame = nullptr;
        const Renderer::Texture* lcd = nullptr;
    };
    void goTo(Page p);
    [[nodiscard]] const PageArt& art(Page p) const;
    [[nodiscard]] UiText font(Page p) const;
    void drawChrome(Renderer::UiBatch& ui, Page p, const std::string& title) const;
    // The exe's bottom bar (0x5d5bb0): help in the middle, `left` at x 0.074, `back` (Back /
    // Cancel) at the right and `ok` (OK) under it, in the game's text font (font 1).
    void drawFooter(Renderer::UiBatch& ui, const std::string& help, const std::string& back, const std::string& ok,
                    const std::string& left = {}) const;

    // --- the SETTINGS pages (Game/settings_menu.cpp) ---
    void openSettings(Page p);
    MenuAction settingsInput(MenuInput in);
    void drawSettings(Renderer::UiBatch& ui) const;
    void drawItem(Renderer::UiBatch& ui, const UiText& text, const std::string& label, float32 x, float32 y, float32 scale,
                  bool selected) const;
    void drawSlider(Renderer::UiBatch& ui, const UiText& text, float32 x, float32 y, int32 steps, int32 value) const;
    void reloadText();
    [[nodiscard]] int settingsRows(Page p) const;
    // The SETTINGS list: in a level it has no DIFFICULTY or LANGUAGES (0x602800).
    [[nodiscard]] std::vector<Page> settingsList() const;

    // --- the in-game menu (Game/pause_menu.cpp) ---
    MenuAction pauseInput(MenuInput in);
    void drawPause(Renderer::UiBatch& ui) const;
    void drawGoals(Renderer::UiBatch& ui) const;
    void drawQuestion(Renderer::UiBatch& ui, const std::string& question, const std::string& no, const std::string& yes) const;

    // --- SELECT SCENE (Game/scene_menu.cpp) ---
    MenuAction sceneInput(MenuInput in);
    void drawScenes(Renderer::UiBatch& ui) const;
    [[nodiscard]] int sceneCount() const;           // 20, or 24 once a bonus scene is open
    [[nodiscard]] bool scenePlayable(int idx) const;

    // --- LOAD / SAVE GAME, a new game, GAME COMPLETED, the credits (Game/save_menu.cpp) ---
    // Why SAVE GAME is up (0x7c8700): a new game after its difficulty, a finished scene, or a
    // save point's checkpoint from the in-game menu.
    enum class SaveMode { NewGame, SceneEnd, Checkpoint };
    MenuAction playInput();                        // PLAY (0x600af0 row 0)
    void startNewGame(Page from);                  // the brightness page, DIFFICULTY, SAVE GAME
    [[nodiscard]] MenuAction finishNewGame(bool save);
    void openSave(SaveMode mode);
    MenuAction slotsInput(MenuInput in);
    MenuAction afterSave();                        // where SAVE GAME goes once done
    void drawLoadChoice(Renderer::UiBatch& ui) const;
    void drawSlots(Renderer::UiBatch& ui) const;
    void drawGameComplete(Renderer::UiBatch& ui) const;
    void drawCredits(Renderer::UiBatch& ui) const;
    void stepCredits();
    void startCredits();
    // The '@'s that lead a credits line (each a line of space) and the text after them.
    [[nodiscard]] std::pair<int, std::string> creditsLine(int n) const;
    [[nodiscard]] std::string slotTitle(int32 slot) const; // "1)SCENE 2-(COMPLETE)"

    // --- BONUS FEATURES (Game/bonus_menu.cpp) ---
    MenuAction bonusInput(MenuInput in);
    void drawBonus(Renderer::UiBatch& ui) const;
    void drawBonusPicture(Renderer::UiBatch& ui) const;
    [[nodiscard]] bool pictureOpen(int idx) const; // 0x5d6e60

    // --- the results (Game/results_menu.cpp) ---
    MenuAction resultsInput(MenuInput in);
    void drawResults(Renderer::UiBatch& ui) const;
    void drawList(Renderer::UiBatch& ui, const UiText& text, const std::vector<std::string>& items, const MenuCursor& cursor,
                  float32 x, float32 y, float32 scale) const;
    [[nodiscard]] std::vector<std::string> items(Page p) const;

    Assets::Gxt gxt_;
    std::vector<Assets::Font> fonts_;
    const Assets::Font* lcdFont_ = nullptr;
    const Assets::Font* textFont_ = nullptr; // font 1: the footer's
    const Assets::Font* loadFont_ = nullptr; // font 3: the loading screen's
    TextureSet loadTextures_;                // pictures/LOAD/load_pc.txd: font 3's "loading" atlas, "mhlogo"
    std::unique_ptr<TextureSet> loadPicture_; // the loading level's picload_pc.txd
    TextureSet frontendTextures_;            // pictures/frontend_pc.txd: font 1's atlas
    std::unique_ptr<PageArt> mainArt_, sceneArt_, settingsArt_, bonusArt_, pauseArt_;
    std::unique_ptr<TextureSet> bonusPicture_; // the open bpic<n>_ep.txd
    const Renderer::Texture* logo_ = nullptr;
    std::vector<const Renderer::Texture*> sceneThumbs_; // index = scene number - 1
    const Renderer::Texture* closedThumb_ = nullptr;

    std::vector<SceneInfo> scenes_;
    std::string gameRoot_;
    std::string saveDir_;
    GameSettings* settings_ = nullptr;
    std::array<uint8, 0x130>* bindings_ = nullptr;
    std::array<uint8, 0x130>* padBindings_ = nullptr;
    std::array<uint8, 0x130> remapUndo_{}; // what CRMCANC puts back (0x5d97e0)
    int remapRow_ = 0;                    // 0..21 the actions, 22 CRMOK, 23 CRMCANC (0x7c8e40)
    bool remapCapture_ = false;           // waiting for a key (0x7c8e4c)
    MenuAction remapInput(MenuInput in);
    GameProgress* progress_ = nullptr;
    SaveSlots* slots_ = nullptr;
    // LOAD / SAVE GAME (0x7c87c0 the chooser's row; 0x7c87c8 / 0x7c89d0 the slot rows).
    int loadChoiceRow_ = 0;
    int slotRow_ = 0;
    SaveMode saveMode_ = SaveMode::SceneEnd;
    // A new game on its way through the brightness page and DIFFICULTY (0x7c86f0), and the page it
    // started from (0x7c86f4: main, scene list, the LOAD chooser).
    bool newGame_ = false;
    Page newGameFrom_ = Page::Main;
    bool creditsNext_ = false;     // open() rolls the credits (after GAME COMPLETED)
    float32 credits_ = 1.0f;       // the roll's offset (0x7c8f64)
    int creditsFirst_ = 0;         // its first line (0x7c8f4c)
    bool creditsEnd_ = false;      // <END> has come (0x7c8f50)
    float32 creditsTick_ = 0.0f;   // ours: the roll moves 30 times a second
    // BONUS FEATURES's grid cursor (0x7c8a5c row, 0x7c8a58 column, 0x7c8a60 group) and picture.
    int bonusRow_ = 2, bonusCol_ = 0, bonusGroup_ = 0;
    int picture_ = 0;
    // SELECT SCENE's cursor (0x7c89dc row: 0, 1 the thumbnails, 2 the groups; 0x7c89d8 column;
    // 0x7c89e0 group).
    int sceneRow_ = 0, sceneCol_ = 0, sceneGroup_ = 0;
    GameSettings undo_;        // the values a page's Cancel puts back (audio, video, brightness)
    float32 aspect_ = 0.75f;
    // The in-game menu: open over a level (SETTINGS returns to it), its row (0x7c8a68: settings,
    // save game, quit, continue), the YES / NO question (0x5d7ec0) and its row (0 = NO).
    bool inGame_ = false;
    int pauseRow_ = 3;
    bool question_ = false;
    int questionRow_ = 0;
    std::vector<std::string> goals_;
    std::shared_ptr<const SaveFile> checkpoint_; // the level's save point checkpoint (in-game SAVE GAME)
    LevelResults results_;
    float32 resultsTime_ = 0.0f; // since the results came up (input waits a moment)

    Page page_ = Page::Main;
    MenuCursor cursor_;
    float32 clock_ = 0.0f;               // camcorder counter
    float32 jitter_[4][2]{};             // per-frame background ghost offsets
    std::mt19937 rng_{0x4d48u};
};

} // namespace Game
