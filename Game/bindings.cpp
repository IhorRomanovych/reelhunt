// Game/bindings.cpp - the player's key bindings. See bindings.h.
#include "Game/bindings.h"

#include <cstring>

namespace Game {

int32 Bindings::key(int32 slot, int32 column) const noexcept
{
    if (!table_ || slot < 0 || slot >= kSlots || column < 0 || column > 1)
        return 0;
    uint32 v = 0;
    std::memcpy(&v, table_->data() + 4 * (slot + column * kSlots), 4);
    return static_cast<int32>(v);
}

void Bindings::setKey(int32 slot, int32 column, int32 code) noexcept
{
    if (!table_ || slot < 0 || slot >= kSlots || column < 0 || column > 1)
        return;
    const uint32 v = static_cast<uint32>(code);
    std::memcpy(table_->data() + 4 * (slot + column * kSlots), &v, 4);
}

bool Bindings::bound(int32 slot, int32 code) const noexcept
{
    return code != 0 && (key(slot, 0) == code || key(slot, 1) == code);
}

void Bindings::assign(int32 slot, int32 code) noexcept
{
    if (!table_ || slot < 0 || slot >= kSlots || bound(slot, code))
        return;
    for (int32 s = 0; s < kSlots; ++s)
        if (s != slot)
            for (int32 c = 0; c < 2; ++c)
                if (key(s, c) == code)
                    setKey(s, c, 0);
    if (key(slot, 1) != 0)
        setKey(slot, 0, key(slot, 1));
    const int32 old = key(slot, 0);
    setKey(slot, 1, code);
    // The twins: SPRINT's slot 25, ACTION2's 27 and 28.
    if (slot == Sprint)
    {
        setKey(25, 0, old);
        setKey(25, 1, code);
    }
    if (slot == Action2)
        for (int32 s : {27, 28})
        {
            setKey(s, 0, old);
            setKey(s, 1, code);
        }
}

std::string Bindings::gxtName(int32 code)
{
    // 0x5d9dd4's chain.
    if (code >= 'A' && code <= 'Z')
        return std::string("KEY") + static_cast<char>(code);
    if (code >= '0' && code <= '9')
        return std::string("KEY") + static_cast<char>(code);
    if (code >= 0x3e9 && code <= 0x3f4)
        return "KEYF" + std::to_string(code - 0x3e8);
    switch (code)
    {
    case 0x20: return "KEYSPC";
    case 0x23: return "KEYHASH";
    case 0x27: return "KEYHYP2";
    case 0x2c: return "KEYCOMM";
    case 0x2d: return "KEYMINU";
    case 0x2e: return "KEYFSTO";
    case 0x2f: return "KEYFSLA";
    case 0x3b: return "KEYSEMI";
    case 0x3d: return "KEYEQU";
    case 0x5b: return "KEYLBR";
    case 0x5c: return "KEYBSLA";
    case 0x5d: return "KEYRBR";
    case 0x60: return "KEYHYP1";
    case 0x3e8: return "KEYESC";
    case 0x3f5: return "KEYINS";
    case 0x3f6: return "KEYDEL";
    case 0x3f7: return "KEYHOME";
    case 0x3f8: return "KEYEND";
    case 0x3f9: return "KEYPGUP";
    case 0x3fa: return "KEYPGDN";
    case 0x3fb: return "KEYUP";
    case 0x3fc: return "KEYDOWN";
    case 0x3fd: return "KEYLEFT";
    case 0x3fe: return "KEYRIGH";
    case 0x3ff: return "KEYPDIV";
    case 0x400: return "KEYPTIM";
    case 0x401: return "KEYPPLU";
    case 0x402: return "KEYPMIN";
    case 0x403: return "KEYPDEL";
    case 0x404: return "KEYPEND";
    case 0x405: return "KEYPDOW";
    case 0x406: return "KEYPPGD";
    case 0x407: return "KEYPLEF";
    case 0x408: return "KEYPAD5";
    case 0x409: return "KEYNLOC";
    case 0x40a: return "KEYPRIG";
    case 0x40b: return "KEYPHOM";
    case 0x40c: return "KEYPUP";
    case 0x40d: return "KEYPPGU";
    case 0x40e: return "KEYPINS";
    case 0x40f: return "KEYPENT";
    case 0x412: return "KEYBS";
    case 0x413: return "KEYTAB";
    case 0x414: return "KEYCAPS";
    case 0x415: return "KEYENT";
    case 0x416: return "KEYLSH";
    case 0x417: return "KEYRSH";
    case 0x418: return "KEYSH";
    case 0x419: return "KEYLCTR";
    case 0x41a: return "KEYRCTR";
    case 0x41b: return "KEYLALT";
    case 0x41c: return "KEYRALT";
    case 0x41d: return "KEYLWIN";
    case 0x41e: return "KEYRWIN";
    case 0x41f: return "KEYRCLI";
    case kMouse + 0: return "PCLMB";
    case kMouse + 1: return "PCMMB";
    case kMouse + 2: return "PCRMB";
    case kMouse + 3: return "PCX1MB";
    case kMouse + 4: return "PCX2MB";
    case kMouse + 5: return "PCMWUP";
    case kMouse + 6: return "PCMWDN";
    default: return {};
    }
}

} // namespace Game
