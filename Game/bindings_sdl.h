// Game/bindings_sdl.h - the exe's key codes (Game/bindings.h) against SDL's keyboard and mouse.
#pragma once
#include "engine_types.h"

#include <SDL3/SDL.h>

#include "Game/bindings.h"

#include <array>

namespace Game {

// The SDL scancode of an exe key code (SDL_SCANCODE_UNKNOWN for the mouse, none, or KEYSH).
[[nodiscard]] SDL_Scancode scancodeOf(int32 code) noexcept;
// Whether the key or mouse button `code` is down now.
[[nodiscard]] bool codeHeld(int32 code) noexcept;
// Whether either of `slot`'s keys is down.
[[nodiscard]] bool slotHeld(const Bindings& b, int32 slot) noexcept;
// The exe key code an SDL key-down, mouse-button-down or wheel event is (0 for anything else).
[[nodiscard]] int32 codeOfEvent(const SDL_Event& e) noexcept;
// Whether `e` presses one of `slot`'s keys (key down without repeat, a button down, a wheel step).
[[nodiscard]] bool slotPressed(const Bindings& b, int32 slot, const SDL_Event& e) noexcept;
// Whether `e` lets go of one of `slot`'s keys (key up, button up).
[[nodiscard]] bool slotReleased(const Bindings& b, int32 slot, const SDL_Event& e) noexcept;

// The pad. The exe keeps a second table for it (0x710d24, Settings.dat's second table; all
// unbound by default): each slot's two columns hold one of the pad's bits, read against what the
// pad has down (0x461c00 against 0x725684). The platform layer sets the bits (0x491db0): its
// buttons as DirectInput numbers them - ours: the face buttons 0..3 (south, east, west, north),
// the shoulders 4 / 5, back 6, start 7, the stick clicks 8 / 9 as DirectInput numbers an XInput
// pad, the triggers 10 / 11 and the d-pad 12..15 - and the sticks' directions (the left stick's
// 0x200000 up, 0x100000 down, 0x400000 left, 0x800000 right, the right stick's 0x2000000 /
// 0x1000000 / 0x4000000 / 0x8000000). The sticks also drive him and his view as analog values
// (0x491db0 cases 0 / 1 -> ped +0x498 / +0x4a0). slotHeld / slotPressed / slotReleased read the
// pad through this table too.
void setPadTable(std::array<uint8, 0x130>* table) noexcept;
// SDL_EVENT_GAMEPAD_ADDED / _REMOVED: the first pad found is the one read.
void padEvent(const SDL_Event& e) noexcept;
// The pad's bits now (0 without a pad).
[[nodiscard]] uint32 padBits() noexcept;
// A stick (0 left, 1 right): x right, y down, each -1..1; zero inside the dead zone (ours: a
// quarter of its throw).
struct PadStick {
    float32 x = 0.0f, y = 0.0f;
};
[[nodiscard]] PadStick padStick(int32 which) noexcept;
// The pad bit a gamepad button-down event presses (0 for any other event).
[[nodiscard]] uint32 padBitOfEvent(const SDL_Event& e) noexcept;
// Testing (REELHUNT_VPAD): attach SDL's virtual gamepad, and tilt its left stick (-1..1 each).
void attachTestPad() noexcept;
void setTestPadStick(float32 x, float32 y) noexcept;

} // namespace Game
