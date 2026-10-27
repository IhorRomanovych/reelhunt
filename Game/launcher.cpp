// Game/launcher.cpp - the launcher window. See launcher.h.
#include "Game/launcher.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Game {

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// The GPU driving the display whose desktop starts at (x, y): Direct3D 8 named each adapter by
// it (D3DADAPTER_IDENTIFIER8::Description, the display device's DeviceString).
std::string gpuNameAt(int x, int y)
{
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i, dd.cb = sizeof(dd))
    {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            continue;
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsExW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0) && dm.dmPosition.x == x &&
            dm.dmPosition.y == y)
            return narrow(dd.DeviceString);
    }
    return {};
}
#endif

ScreenMode toMode(const SDL_DisplayMode& m)
{
    return {static_cast<uint32>(m.w), static_cast<uint32>(m.h), static_cast<uint32>(SDL_BYTESPERPIXEL(m.format) * 8),
            m.refresh_rate};
}

} // namespace

std::vector<DisplayAdapter> enumerateAdapters()
{
    std::vector<DisplayAdapter> out;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::cerr << "launcher: SDL video: " << SDL_GetError() << "\n";
        return out;
    }
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    std::vector<SDL_DisplayID> order(ids, ids + std::max(0, count));
    SDL_free(ids);
    // Adapter 0 is the primary display (D3DADAPTER_DEFAULT), the rest in the system's order.
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    std::stable_partition(order.begin(), order.end(), [primary](SDL_DisplayID d) { return d == primary; });
    for (SDL_DisplayID id : order)
    {
        if (out.size() >= 16u)
            break; // 0x4c0910: at most 16 subsystems
        DisplayAdapter a;
        a.id = id;
        if (const char* n = SDL_GetDisplayName(id))
            a.monitor = n;
#ifdef _WIN32
        SDL_Rect r{};
        if (SDL_GetDisplayBounds(id, &r))
            a.name = gpuNameAt(r.x, r.y);
#endif
        if (a.name.empty())
            a.name = a.monitor.empty() ? "Display " + std::to_string(out.size() + 1) : a.monitor;
        int n = 0;
        if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(id, &n))
        {
            for (int i = 0; i < n; ++i)
                a.modes.push_back(toMode(*modes[i]));
            SDL_free(modes);
        }
        if (const SDL_DisplayMode* d = SDL_GetDesktopDisplayMode(id))
            a.desktop = toMode(*d);
        out.push_back(std::move(a));
    }
    // SDL video stays up: display ids are only good while it runs (the renderer's window opens
    // on one of these; its shutdown quits SDL).
    return out;
}

#ifdef _WIN32
namespace {

constexpr int kAdapterList = 1000, kModeList = 1001, kQuit = 1002;

// Dialog 104 rebuilt as an in-memory DLGTEMPLATEEX with the exe's own numbers.
class TemplateWriter {
public:
    void u16(uint16 v) { put(&v, 2); }
    void u32(uint32 v) { put(&v, 4); }
    void s16(int16 v) { put(&v, 2); }
    void text(const wchar_t* s) { put(s, (std::wcslen(s) + 1) * 2); }
    void align4()
    {
        while (bytes_.size() % 4)
            bytes_.push_back(0);
    }
    void item(uint32 style, int16 x, int16 y, int16 cx, int16 cy, uint32 id, uint16 cls, const wchar_t* title)
    {
        align4();
        u32(0); // help id
        u32(0); // ex style
        u32(style);
        s16(x);
        s16(y);
        s16(cx);
        s16(cy);
        u32(id);
        u16(0xFFFF);
        u16(cls);
        text(title);
        u16(0); // creation data
    }
    [[nodiscard]] const std::vector<uint8>& bytes() const noexcept { return bytes_; }

private:
    void put(const void* p, std::size_t n)
    {
        const auto* b = static_cast<const uint8*>(p);
        bytes_.insert(bytes_.end(), b, b + n);
    }
    std::vector<uint8> bytes_;
};

std::vector<uint8> launcherTemplate()
{
    constexpr uint16 kButton = 0x80, kStatic = 0x82, kComboBox = 0x85;
    TemplateWriter t;
    t.u16(1);
    t.u16(0xFFFF);
    t.u32(0);          // help id
    t.u32(0x00040000); // WS_EX_APPWINDOW
    t.u32(0x80C818C0); // WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER ...
    t.u16(8);          // items
    t.s16(0);
    t.s16(0);
    t.s16(272);
    t.s16(195);
    t.u16(0); // menu
    t.u16(0); // class
    t.text(L"Reelhunt");
    t.u16(8);   // point size
    t.u16(400); // weight
    t.u16(0);   // italic, charset
    t.text(L"MS Sans Serif");
    t.item(0x50210003, 77, 121, 172, 33, kAdapterList, kComboBox, L"");
    t.item(0x50210003, 77, 154, 172, 74, kModeList, kComboBox, L"");
    t.item(0x50010001, 220, 8, 42, 24, IDOK, kButton, L"Play");
    t.item(0x50010000, 220, 38, 42, 24, kQuit, kButton, L"Quit");
    t.item(0x50000007, 10, 100, 253, 82, 0xFFFFFFFFu, kButton, L"Video Settings");
    t.item(0x50020000, 17, 123, 55, 8, 0xFFFFFFFFu, kStatic, L"Display Adapter:");
    t.item(0x50020000, 17, 156, 55, 8, 0xFFFFFFFFu, kStatic, L"Screen Mode:");
    t.item(0x5000100E, 10, 9, 16, 14, 0xFFFFFFFFu, kStatic, L""); // MB_PICTURE (set at WM_INITDIALOG)
    return t.bytes();
}

struct LauncherState {
    const std::vector<DisplayAdapter>* adapters = nullptr;
    VideoChoice choice;
    std::vector<ScreenMode> rows;
    HBITMAP banner = nullptr;
};

void fillModes(HWND dlg, LauncherState& s)
{
    const HWND list = GetDlgItem(dlg, kModeList);
    SendMessageW(list, CB_RESETCONTENT, 0, 0);
    s.rows.clear();
    const auto& adapters = *s.adapters;
    if (s.choice.device < 0 || s.choice.device >= static_cast<int32>(adapters.size()))
        return;
    s.rows = launcherModes(adapters[static_cast<std::size_t>(s.choice.device)].modes);
    for (std::size_t i = 0; i < s.rows.size(); ++i)
    {
        const LRESULT at = SendMessageW(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(modeLabel(s.rows[i])).c_str()));
        SendMessageW(list, CB_SETITEMDATA, static_cast<WPARAM>(at), static_cast<LPARAM>(i));
    }
    s.choice.index = launcherModeRow(s.rows, s.choice);
    SendMessageW(list, CB_SETCURSEL, static_cast<WPARAM>(s.choice.index), 0);
}

INT_PTR CALLBACK launcherProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* s = reinterpret_cast<LauncherState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        s = reinterpret_cast<LauncherState*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        // The banner: the static with SS_BITMAP sizes itself to the picture.
        HWND picture = nullptr;
        for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            if ((GetWindowLongW(c, GWL_STYLE) & SS_TYPEMASK) == SS_BITMAP)
                picture = c;
        if (picture && s->banner)
            SendMessageW(picture, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(s->banner));
        // 0x4bef40: the adapters, the saved one selected, then its modes.
        const HWND adapters = GetDlgItem(dlg, kAdapterList);
        const std::vector<std::string> labels = adapterLabels(*s->adapters);
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            SendMessageW(adapters, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(labels[i]).c_str()));
            SendMessageW(adapters, CB_SETITEMDATA, i, static_cast<LPARAM>(i));
        }
        if (s->choice.device < 0 || s->choice.device >= static_cast<int32>(labels.size()))
            s->choice.device = 0;
        SendMessageW(adapters, CB_SETCURSEL, static_cast<WPARAM>(s->choice.device), 0);
        fillModes(dlg, *s);
        SetFocus(adapters);
        return FALSE; // focus set here
    }
    case WM_COMMAND:
    {
        const int id = LOWORD(wp);
        const int code = HIWORD(wp);
        if (id == IDOK && code == BN_CLICKED)
        {
            const LRESULT row = SendMessageW(GetDlgItem(dlg, kModeList), CB_GETCURSEL, 0, 0);
            if (row >= 0 && static_cast<std::size_t>(row) < s->rows.size())
            {
                s->choice.index = static_cast<int32>(row);
                s->choice.mode = s->rows[static_cast<std::size_t>(row)];
            }
            EndDialog(dlg, 1);
            return TRUE;
        }
        if ((id == kQuit && code == BN_CLICKED) || id == IDCANCEL)
        {
            EndDialog(dlg, 0);
            return TRUE;
        }
        if (id == kAdapterList && code == CBN_SELCHANGE)
        {
            const LRESULT row = SendMessageW(GetDlgItem(dlg, kAdapterList), CB_GETCURSEL, 0, 0);
            if (row >= 0 && row != s->choice.device)
            {
                s->choice.device = static_cast<int32>(row);
                fillModes(dlg, *s);
            }
            return TRUE;
        }
        if (id == kModeList && code == CBN_SELCHANGE)
        {
            const LRESULT row = SendMessageW(GetDlgItem(dlg, kModeList), CB_GETCURSEL, 0, 0);
            if (row >= 0 && static_cast<std::size_t>(row) < s->rows.size())
            {
                s->choice.index = static_cast<int32>(row);
                s->choice.mode = s->rows[static_cast<std::size_t>(row)];
            }
            return TRUE;
        }
        return FALSE;
    }
    default:
        return FALSE;
    }
}

} // namespace
#endif

std::optional<VideoChoice> runLauncher(const std::string& gameExePath, const std::vector<DisplayAdapter>& adapters,
                                       const VideoChoice& saved)
{
#ifdef _WIN32
    LauncherState s;
    s.adapters = &adapters;
    s.choice = saved;
    // The banner comes from the user's own exe (the exe sets no window icon).
    const std::wstring exe = widen(gameExePath);
    HMODULE res = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (res)
    {
        s.banner = static_cast<HBITMAP>(LoadImageW(res, L"MB_PICTURE", IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR));
    }
    else
        std::cerr << "launcher: cannot read the banner from " << gameExePath << "\n";
    // 0x4c0910: the cursor goes to the middle of the screen first.
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2);
    const std::vector<uint8> tpl = launcherTemplate();
    const INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(tpl.data()),
                                              nullptr, launcherProc, reinterpret_cast<LPARAM>(&s));
    if (s.banner)
        DeleteObject(s.banner);
    if (res)
        FreeLibrary(res);
    if (r == -1)
    {
        std::cerr << "launcher: dialog failed (" << GetLastError() << "): using the saved choice\n";
        return saved;
    }
    if (r != 1)
        return std::nullopt;
    return s.choice;
#else
    (void)gameExePath;
    (void)adapters;
    return saved;
#endif
}

} // namespace Game
