// tests/frontend_test.cpp - game text, fonts, scene list and main-menu navigation against the
// shipped game data.
#include "Assets/font.h"
#include "Assets/gxt.h"
#include "Assets/txd.h"
#include "Game/main_menu.h"
#include "Game/scenes.h"
#include "Game/weapon_strip.h"
#include "test_paths.h"

#include <cstdio>
#include <algorithm>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (!ok)
        ++g_failures;
}

} // namespace

int main()
{
    if (!TestPaths::haveGameData())
        return TestPaths::skipNoGameData();
    const std::string root = TestPaths::gameRoot();

    std::puts("GXT");
    auto gxt = Assets::Gxt::load(root + "levels/GLOBAL/PC_TEXT/pc_game.gxt");
    check(gxt.has_value(), "pc_game.gxt loads");
    if (!gxt)
        return 1;
    check(gxt->size() == 636, "636 entries (got " + std::to_string(gxt->size()) + ")");
    check(gxt->text("MAINM") == "MAIN MENU", "MAINM = MAIN MENU");
    check(gxt->text("LVL_1") == "BORN AGAIN", "LVL_1 = BORN AGAIN");
    check(gxt->text("NO_SUCH_KEY") == "NO_SUCH_KEY", "a missing key returns the key");

    std::puts("Fonts");
    auto fonts = Assets::loadFonts(root + "initscripts/FONTS/font.dat");
    check(fonts.has_value() && fonts->size() == 4, "font.dat holds 4 fonts");
    if (fonts)
        for (const Assets::Font& f : *fonts)
            if (f.id == 2) // the menu's LCD font
            {
                check(f.glyphs.size() == 100, "LCD font has 100 glyphs");
                const Assets::FontGlyph* zero = f.glyph('0');
                check(zero && static_cast<int>(zero->u0 * 256) == 2 && static_cast<int>(zero->v0 * 256) == 33,
                      "'0' starts row 2 of the lcd atlas at (2, 33)");
            }

    std::puts("Scenes");
    auto scenes = Game::loadScenes(root + "initscripts/LEVELS/levels.txt", &*gxt);
    check(scenes.has_value() && scenes->size() == 24, "levels.txt lists 24 levels");
    if (scenes && scenes->size() >= 20)
    {
        check((*scenes)[0].folder == "jury_turf" && (*scenes)[0].title == "BORN AGAIN", "scene 1 = jury_turf, BORN AGAIN");
        check((*scenes)[10].folder == "asylum" && (*scenes)[10].title == "MOUTH OF MADNESS", "scene 11 = asylum");
        check((*scenes)[15].title == "WRONG SIDE OF THE TRACKS", "two-line titles are joined");
        check((*scenes)[19].folder == "attic", "scene 20 = attic");
    }
    check(Game::sceneNumberFromSaveTitle("1)SCENE 1-(IN PROGRESS)") == 1, "save title -> scene number");
    check(Game::sceneNumberFromSaveTitle("EMPTY") == 0, "no scene number in an unrelated title");

    std::puts("Menu cursor");
    Game::MenuCursor c;
    c.reset(20, 8);
    c.up();
    check(c.index() == 19 && c.first() == 12, "up from the top wraps to the bottom and scrolls");
    c.down();
    check(c.index() == 0 && c.first() == 0, "down from the bottom wraps to the top");

    std::puts("Weapon strip");
    {
        using Game::WeaponStrip;
        check(std::string(WeaponStrip::iconName(19)) == "shard" && std::string(WeaponStrip::iconName(35)) == "wbaseball" &&
                  std::string(WeaponStrip::iconName(16)) == "fists" && std::string(WeaponStrip::iconName(5)) == "na",
              "icons by collectable type (0x5df9a0): shard, wbaseball, fists, na for the rest");
        // Every icon is in frontend_pc.txd, every name in pc_game.gxt.
        auto txd = Assets::Txd::load(root + "pictures/frontend_pc.txd");
        int missingIcons = 0, missingNames = 0;
        for (int32 ct = 0; ct <= 110; ++ct)
        {
            const std::string icon = WeaponStrip::iconName(ct);
            if (txd && std::none_of(txd->begin(), txd->end(), [&](const Assets::TxdTexture& t) { return t.name == icon; }))
                ++missingIcons;
            // (CT_DOLL's key, "DOLL", is in the exe but not in pc_game.gxt: it shows no name.)
            if (const char* key = WeaponStrip::nameKey(ct); key && ct != 81 && !gxt->contains(key))
                ++missingNames;
        }
        check(txd && missingIcons == 0, "every icon is in frontend_pc.txd (" + std::to_string(missingIcons) + " missing)");
        check(txd && std::any_of(txd->begin(), txd->end(), [](const Assets::TxdTexture& t) { return t.name == "swap"; }),
              "the swap offer's arrow (0x5eaa58: \"swap\") is in frontend_pc.txd");
        check(missingNames == 0, "every item name is in pc_game.gxt (" + std::to_string(missingNames) + " missing)");

        // The name: up 9 a tick to 600, then down; alpha at most 230.
        const Game::TextureSet noArt;
        WeaponStrip strip;
        strip.load(noArt, root); // (the names only)
        strip.showName(16);
        check(strip.nameAlpha() == 0, "no name for fists");
        strip.showName(19);
        check(strip.name() == gxt->text("G_SHARD") && !strip.name().empty(), "the shard's name is G_SHARD's (" + strip.name() + ")");
        auto ticks = [&](int n) {
            for (int i = 0; i < n; ++i)
                strip.update(1.0f / 30.0f);
        };
        ticks(1);
        const int32 first = strip.nameAlpha();
        ticks(59); // 60
        const int32 held = strip.nameAlpha();
        ticks(65); // 125: on its way out
        const int32 fading = strip.nameAlpha();
        ticks(15); // 140
        check(first == 9 && held == 230 && fading > 0 && fading < 230 && strip.nameAlpha() == 0,
              "name alpha 9 after a tick, 230 held, fading by 4.2 s, gone by 4.7 s (" + std::to_string(first) + " " +
                  std::to_string(held) + " " + std::to_string(fading) + ")");

        // Next weapon by holster slot 3, 2, 1, 4 (0x5ddce0); fists for an empty slot 3.
        Game::ItemType shard, bat, glock, rear;
        const std::pair<Game::ItemType*, const char*> slots[] = {
            {&shard, "BELT_LEFT"}, {&bat, "BACK"}, {&glock, "BELT_RIGHT"}, {&rear, "BELT_REAR"}};
        for (auto [t, slot] : slots)
        {
            t->weapon = true;
            t->holsterSlot = slot;
        }
        const auto withShard = Game::weaponCycle({&bat, &rear, &glock, &shard});
        check(withShard == std::vector<const Game::ItemType*>{&shard, &glock, &bat, &rear}, "cycle: shard, glock, bat, rear slot");
        const auto noShard = Game::weaponCycle({&bat, &glock});
        check(noShard == std::vector<const Game::ItemType*>{nullptr, &glock, &bat}, "cycle without a slot-3 item: fists first");
    }

    std::puts("Main menu");
    Game::MainMenu menu;
    check(menu.load(root, TestPaths::scratch() + "no_saves_here"), "menu loads font, art and scenes");
    using In = Game::MenuInput;
    using Kind = Game::MenuAction::Kind;
    auto play = menu.input(In::Confirm);
    check(play.kind == Kind::StartScene && play.scene == 1, "PLAY starts scene 1");
    menu.input(In::Down);
    menu.input(In::Confirm); // SELECT SCENE: the 2 x 2 grid of scenes 1..4 (0x601660)
    menu.input(In::Down);    // the lower row, left: scene 3
    auto pick = menu.input(In::Confirm);
    check(pick.kind == Kind::StartScene && pick.scene == 3, "SELECT SCENE -> lower left starts scene 3");
    Game::GameProgress fresh;
    menu.setProgress(&fresh);
    pick = menu.input(In::Confirm);
    check(pick.kind == Kind::None, "a new game's scene 3 is closed");
    menu.setProgress(nullptr);
    menu.input(In::Back); // back to the main page
    menu.input(In::Back); // main page Back opens the quit prompt (cursor on NO)
    auto no = menu.input(In::Confirm);
    check(no.kind == Kind::None, "quit prompt defaults to NO");
    menu.input(In::Back);
    menu.input(In::Up);
    auto yes = menu.input(In::Confirm);
    check(yes.kind == Kind::Quit, "quit prompt YES quits");

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
