// Game/bindings_sdl.cpp - the exe's key codes against SDL. See bindings_sdl.h.
#include "Game/bindings_sdl.h"

namespace Game {

namespace {

struct CodeScan {
    int32 code;
    SDL_Scancode scan;
};

constexpr CodeScan kSpecial[] = {
    {0x20, SDL_SCANCODE_SPACE}, {0x23, SDL_SCANCODE_NONUSHASH}, {0x27, SDL_SCANCODE_APOSTROPHE},
    {0x2c, SDL_SCANCODE_COMMA}, {0x2d, SDL_SCANCODE_MINUS}, {0x2e, SDL_SCANCODE_PERIOD},
    {0x2f, SDL_SCANCODE_SLASH}, {0x3b, SDL_SCANCODE_SEMICOLON}, {0x3d, SDL_SCANCODE_EQUALS},
    {0x5b, SDL_SCANCODE_LEFTBRACKET}, {0x5c, SDL_SCANCODE_BACKSLASH}, {0x5d, SDL_SCANCODE_RIGHTBRACKET},
    {0x60, SDL_SCANCODE_GRAVE}, {0x3e8, SDL_SCANCODE_ESCAPE}, {0x3f5, SDL_SCANCODE_INSERT},
    {0x3f6, SDL_SCANCODE_DELETE}, {0x3f7, SDL_SCANCODE_HOME}, {0x3f8, SDL_SCANCODE_END},
    {0x3f9, SDL_SCANCODE_PAGEUP}, {0x3fa, SDL_SCANCODE_PAGEDOWN}, {0x3fb, SDL_SCANCODE_UP},
    {0x3fc, SDL_SCANCODE_DOWN}, {0x3fd, SDL_SCANCODE_LEFT}, {0x3fe, SDL_SCANCODE_RIGHT},
    {0x3ff, SDL_SCANCODE_KP_DIVIDE}, {0x400, SDL_SCANCODE_KP_MULTIPLY}, {0x401, SDL_SCANCODE_KP_PLUS},
    {0x402, SDL_SCANCODE_KP_MINUS}, {0x403, SDL_SCANCODE_KP_PERIOD}, {0x404, SDL_SCANCODE_KP_1},
    {0x405, SDL_SCANCODE_KP_2}, {0x406, SDL_SCANCODE_KP_3}, {0x407, SDL_SCANCODE_KP_4},
    {0x408, SDL_SCANCODE_KP_5}, {0x409, SDL_SCANCODE_NUMLOCKCLEAR}, {0x40a, SDL_SCANCODE_KP_6},
    {0x40b, SDL_SCANCODE_KP_7}, {0x40c, SDL_SCANCODE_KP_8}, {0x40d, SDL_SCANCODE_KP_9},
    {0x40e, SDL_SCANCODE_KP_0}, {0x40f, SDL_SCANCODE_KP_ENTER}, {0x412, SDL_SCANCODE_BACKSPACE},
    {0x413, SDL_SCANCODE_TAB}, {0x414, SDL_SCANCODE_CAPSLOCK}, {0x415, SDL_SCANCODE_RETURN},
    {0x416, SDL_SCANCODE_LSHIFT}, {0x417, SDL_SCANCODE_RSHIFT}, {0x419, SDL_SCANCODE_LCTRL},
    {0x41a, SDL_SCANCODE_RCTRL}, {0x41b, SDL_SCANCODE_LALT}, {0x41c, SDL_SCANCODE_RALT},
    {0x41d, SDL_SCANCODE_LGUI}, {0x41e, SDL_SCANCODE_RGUI}, {0x41f, SDL_SCANCODE_APPLICATION},
};

int32 mouseButtonCode(uint8 button) noexcept
{
    switch (button)
    {
    case SDL_BUTTON_LEFT: return Bindings::kMouse + 0;
    case SDL_BUTTON_MIDDLE: return Bindings::kMouse + 1;
    case SDL_BUTTON_RIGHT: return Bindings::kMouse + 2;
    case SDL_BUTTON_X1: return Bindings::kMouse + 3;
    case SDL_BUTTON_X2: return Bindings::kMouse + 4;
    default: return 0;
    }
}

} // namespace

SDL_Scancode scancodeOf(int32 code) noexcept
{
    if (code >= 'A' && code <= 'Z')
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (code - 'A'));
    if (code >= '1' && code <= '9')
        return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (code - '1'));
    if (code == '0')
        return SDL_SCANCODE_0;
    if (code >= 0x3e9 && code <= 0x3f4)
        return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (code - 0x3e9));
    for (const CodeScan& c : kSpecial)
        if (c.code == code)
            return c.scan;
    return SDL_SCANCODE_UNKNOWN;
}

bool codeHeld(int32 code) noexcept
{
    if (code == 0)
        return false;
    if (code >= Bindings::kMouse)
    {
        const SDL_MouseButtonFlags m = SDL_GetMouseState(nullptr, nullptr);
        switch (code - Bindings::kMouse)
        {
        case 0: return (m & SDL_BUTTON_LMASK) != 0;
        case 1: return (m & SDL_BUTTON_MMASK) != 0;
        case 2: return (m & SDL_BUTTON_RMASK) != 0;
        case 3: return (m & SDL_BUTTON_X1MASK) != 0;
        case 4: return (m & SDL_BUTTON_X2MASK) != 0;
        default: return false; // the wheel is never held
        }
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (code == 0x418) // KEYSH: either shift
        return keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    const SDL_Scancode s = scancodeOf(code);
    return s != SDL_SCANCODE_UNKNOWN && keys[s];
}

namespace {

std::array<uint8, 0x130>* g_padTable = nullptr;
SDL_Gamepad* g_pad = nullptr;
constexpr float32 kPadDeadZone = 0.25f;

uint32 padMask(int32 slot) noexcept
{
    if (!g_padTable)
        return 0;
    const Bindings pad(g_padTable);
    return static_cast<uint32>(pad.key(slot, 0)) | static_cast<uint32>(pad.key(slot, 1));
}

uint32 padButtonBit(int32 button) noexcept
{
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH: return 1u << 0;
    case SDL_GAMEPAD_BUTTON_EAST: return 1u << 1;
    case SDL_GAMEPAD_BUTTON_WEST: return 1u << 2;
    case SDL_GAMEPAD_BUTTON_NORTH: return 1u << 3;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 1u << 4;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 1u << 5;
    case SDL_GAMEPAD_BUTTON_BACK: return 1u << 6;
    case SDL_GAMEPAD_BUTTON_START: return 1u << 7;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return 1u << 8;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return 1u << 9;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 1u << 12;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 1u << 13;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 1u << 14;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 1u << 15;
    default: return 0;
    }
}

} // namespace

void setPadTable(std::array<uint8, 0x130>* table) noexcept
{
    g_padTable = table;
}

void padEvent(const SDL_Event& e) noexcept
{
    if (e.type == SDL_EVENT_GAMEPAD_ADDED && !g_pad)
        g_pad = SDL_OpenGamepad(e.gdevice.which);
    else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && g_pad && SDL_GetGamepadID(g_pad) == e.gdevice.which)
    {
        SDL_CloseGamepad(g_pad);
        g_pad = nullptr;
    }
}

PadStick padStick(int32 which) noexcept
{
    PadStick s;
    if (!g_pad)
        return s;
    const SDL_GamepadAxis ax = which == 0 ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX;
    const SDL_GamepadAxis ay = which == 0 ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY;
    s.x = static_cast<float32>(SDL_GetGamepadAxis(g_pad, ax)) / 32767.0f;
    s.y = static_cast<float32>(SDL_GetGamepadAxis(g_pad, ay)) / 32767.0f;
    if (s.x * s.x + s.y * s.y < kPadDeadZone * kPadDeadZone)
        s = PadStick{};
    return s;
}

uint32 padBits() noexcept
{
    if (!g_pad)
        return 0;
    uint32 bits = 0;
    for (int32 b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
        if (SDL_GetGamepadButton(g_pad, static_cast<SDL_GamepadButton>(b)))
            bits |= padButtonBit(b);
    if (SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16384)
        bits |= 1u << 10;
    if (SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384)
        bits |= 1u << 11;
    // The sticks' directions (0x491db0).
    const PadStick l = padStick(0), r = padStick(1);
    if (l.x != 0.0f || l.y != 0.0f)
        bits |= 0x40000000u | (l.x < 0.0f ? 0x400000u : l.x > 0.0f ? 0x800000u : 0u) | (l.y < 0.0f ? 0x200000u : l.y > 0.0f ? 0x100000u : 0u);
    if (r.x != 0.0f || r.y != 0.0f)
        bits |= 0x80000000u | (r.x < 0.0f ? 0x4000000u : r.x > 0.0f ? 0x8000000u : 0u) | (r.y < 0.0f ? 0x2000000u : r.y > 0.0f ? 0x1000000u : 0u);
    return bits;
}

uint32 padBitOfEvent(const SDL_Event& e) noexcept
{
    return e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? padButtonBit(e.gbutton.button) : 0u;
}

namespace {
SDL_JoystickID g_testPad = 0;
}

void attachTestPad() noexcept
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "REELHUNT_VPAD";
    g_testPad = SDL_AttachVirtualJoystick(&desc);
}

void setTestPadStick(float32 x, float32 y) noexcept
{
    if (!g_testPad)
        return;
    if (SDL_Joystick* j = SDL_GetJoystickFromID(g_testPad))
    {
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTX, static_cast<Sint16>(x * 32767.0f));
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTY, static_cast<Sint16>(y * 32767.0f));
    }
}

bool slotHeld(const Bindings& b, int32 slot) noexcept
{
    return codeHeld(b.key(slot, 0)) || codeHeld(b.key(slot, 1)) || (padBits() & padMask(slot)) != 0;
}

int32 codeOfEvent(const SDL_Event& e) noexcept
{
    if (e.type == SDL_EVENT_KEY_DOWN)
    {
        const SDL_Scancode s = e.key.scancode;
        if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z)
            return 'A' + (s - SDL_SCANCODE_A);
        if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9)
            return '1' + (s - SDL_SCANCODE_1);
        if (s == SDL_SCANCODE_0)
            return '0';
        if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12)
            return 0x3e9 + (s - SDL_SCANCODE_F1);
        for (const CodeScan& c : kSpecial)
            if (c.scan == s)
                return c.code;
        return 0;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        return mouseButtonCode(e.button.button);
    if (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.y != 0.0f)
        return Bindings::kMouse + (e.wheel.y > 0.0f ? 5 : 6);
    return 0;
}

bool slotPressed(const Bindings& b, int32 slot, const SDL_Event& e) noexcept
{
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.repeat)
        return false;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        return (padButtonBit(e.gbutton.button) & padMask(slot)) != 0;
    const int32 code = codeOfEvent(e);
    if (code == 0)
        return false;
    if (b.bound(slot, code))
        return true;
    // KEYSH bound: either shift presses it.
    return (code == 0x416 || code == 0x417) && b.bound(slot, 0x418);
}

bool slotReleased(const Bindings& b, int32 slot, const SDL_Event& e) noexcept
{
    int32 code = 0;
    if (e.type == SDL_EVENT_KEY_UP)
    {
        SDL_Event down = e;
        down.type = SDL_EVENT_KEY_DOWN;
        code = codeOfEvent(down);
    }
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
        code = mouseButtonCode(e.button.button);
    else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
        return (padButtonBit(e.gbutton.button) & padMask(slot)) != 0;
    if (code == 0)
        return false;
    return b.bound(slot, code) || ((code == 0x416 || code == 0x417) && b.bound(slot, 0x418));
}

} // namespace Game
