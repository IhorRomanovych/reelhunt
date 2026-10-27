// Game/bindings.h - the player's key bindings, as the exe keeps them.
//
// 38 action slots, each with two keys (0x710bf4: column 0 at [slot], column 1 at [slot + 38]) -
// the first table of Settings.dat (SettingsFile::bindingsA); the second is the pad's (0x710d24).
// Defaults 0x710994 (Game/settings.cpp). A key code is the exe's own: ASCII for the character
// keys (0x20..0x60), 1000.. for the others (0x3e8 Esc, 0x3e9.. F1.., 0x3fb.. the arrows, 0x3ff..
// the keypad, 0x412.. Backspace / Tab / Caps / Enter / the shifts, controls, alts, windows keys),
// 10000.. the mouse (left, middle, right, X1, X2, wheel up, wheel down); 0 none. Its name on the
// REMAP page is a GXT key (0x5d9dd4: KEYA, KEYF1, KEYPEND, PCLMB, ...).
//
// Rebinding (0x5d9a20): a key already on the slot changes nothing. Otherwise it is taken off every
// other slot, the slot's column 1 key moves to column 0 and the new one goes in column 1. SPRINT
// (23) also sets slot 25, ACTION2 (8) slots 27 and 28 (their twins).
#pragma once
#include "engine_types.h"

#include <array>
#include <string>

namespace Game {

class Bindings {
public:
    enum Slot : int32 {
        Use = 0, LookBack = 1, Reload = 2, InvSwap = 4, Action1 = 6, Action2 = 8,
        Forward = 14, Back = 15, Left = 16, Right = 17, PeekLeft = 18, PeekRight = 19, Pause = 20,
        Sprint = 23, Sneak = 24, FirstPerson = 31, Inv1 = 32, Inv2 = 33, Inv3 = 34, Inv4 = 35,
        InvUp = 36, InvDown = 37,
    };
    static constexpr int32 kSlots = 38;
    static constexpr int32 kMouse = 10000; // PCLMB, PCMMB, PCRMB, PCX1MB, PCX2MB, PCMWUP, PCMWDN

    explicit Bindings(std::array<uint8, 0x130>* table = nullptr) noexcept : table_(table) {}
    [[nodiscard]] bool valid() const noexcept { return table_ != nullptr; }
    // The key in `column` (0 / 1) of `slot`, 0 none.
    [[nodiscard]] int32 key(int32 slot, int32 column) const noexcept;
    void setKey(int32 slot, int32 column, int32 code) noexcept;
    // Whether `code` is either of the slot's keys.
    [[nodiscard]] bool bound(int32 slot, int32 code) const noexcept;
    // 0x5d9a20 (the keyboard table).
    void assign(int32 slot, int32 code) noexcept;
    // A key code's GXT name key (0x5d9dd4), empty if it has none.
    [[nodiscard]] static std::string gxtName(int32 code);

private:
    std::array<uint8, 0x130>* table_ = nullptr;
};

} // namespace Game
