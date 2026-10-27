// Game/keymap.cpp - parse the real key-mapping file (config/keymap.txt).
#include "Game/keymap.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace
{
    std::string toUpper(std::string s)
    {
        for (char& c : s)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - ('a' - 'A'));
        return s;
    }

    // Strip the comment portion (from the first '#') and leading/trailing whitespace.
    std::string cleanLine(std::string line)
    {
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        const auto b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
            return std::string();
        const auto e = line.find_last_not_of(" \t\r\n");
        return line.substr(b, e - b + 1);
    }

    std::vector<std::string> tokenize(const std::string& line)
    {
        std::vector<std::string> toks;
        std::string cur;
        for (char c : line)
        {
            if (c == ' ' || c == '\t')
            {
                if (!cur.empty())
                {
                    toks.push_back(cur);
                    cur.clear();
                }
            }
            else
            {
                cur.push_back(c);
            }
        }
        if (!cur.empty())
            toks.push_back(cur);
        return toks;
    }
}

namespace Game
{
int Keymap::keyToScancode(const std::string& key)
{
    const std::string k = toUpper(key);
    // A-Z
    if (k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z')
        return 4 + (k[0] - 'A'); // SDL_SCANCODE_A = 4
    // 0-9
    if (k.size() == 1 && k[0] >= '0' && k[0] <= '9')
        return (k[0] == '0') ? 39 : 29 + (k[0] - '0'); // 1=30 .. 9=38, 0=39
    if (k == "RETURN" || k == "ENTER") return 40;
    if (k == "ESCAPE" || k == "ESC")   return 41;
    if (k == "SPACE")                  return 44;
    if (k == "LEFT")   return 80;
    if (k == "RIGHT")  return 79;
    if (k == "UP")     return 82;
    if (k == "DOWN")   return 81;
    // F1-F24
    if (k.size() >= 2 && k[0] == 'F')
    {
        int n = 0;
        bool digits = k.size() >= 2;
        for (size_t i = 1; i < k.size(); ++i)
        {
            if (k[i] < '0' || k[i] > '9')
            {
                digits = false;
                break;
            }
            n = n * 10 + (k[i] - '0');
        }
        if (digits && n >= 1 && n <= 12)
            return 57 + n;          // SDL_SCANCODE_F1 = 58 .. F12 = 69
        if (digits && n >= 13 && n <= 24)
            return 104 + (n - 13);  // SDL_SCANCODE_F13 = 104 .. F24 = 115 (not contiguous with F12)
    }
    return -1;
}

const AnimActionDef* Keymap::findAction(const std::string& name) const
{
    for (const auto& a : actions_)
        if (a.name == name)
            return &a;
    return nullptr;
}

std::string Keymap::actionForKey(int scancode) const
{
    if (scancode < 0 || scancode >= kMaxScancode)
        return std::string();
    const int idx = keyToAction_[static_cast<size_t>(scancode)];
    if (idx < 0 || idx >= static_cast<int>(actions_.size()))
        return std::string();
    return actions_[static_cast<size_t>(idx)].name;
}

bool Keymap::load(const std::string& path)
{
    path_ = path;
    actions_.clear();
    bindings_.clear();
    keyToAction_ = makeUnbound();

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
    {
        std::fprintf(stderr, "[keymap] cannot open %s\n", path.c_str());
        return false;
    }

    char line[1024];
    int lineNo = 0;
    while (std::fgets(line, sizeof(line), f))
    {
        ++lineNo;
        const std::string clean = cleanLine(line);
        if (clean.empty())
            continue;
        std::vector<std::string> t = tokenize(clean);
        if (t.empty())
            continue;

        if (t[0] == "ACTION")
        {
            if (t.size() < 4)
            {
                std::fprintf(stderr, "[keymap] line %d: ACTION needs <name> <clip> <oneshot> [baseAfter]\n", lineNo);
                continue;
            }
            if (findAction(t[1]))
            {
                std::fprintf(stderr, "[keymap] line %d: duplicate action '%s' ignored\n", lineNo, t[1].c_str());
                continue;
            }
            AnimActionDef a;
            a.name = t[1];
            a.clip = t[2];
            a.oneShot = (t[3] == "1");
            if (t.size() >= 5)
                a.baseAfter = t[4];
            actions_.push_back(std::move(a));
        }
        else if (t[0] == "KEY")
        {
            if (t.size() < 3)
            {
                std::fprintf(stderr, "[keymap] line %d: KEY needs <key> <action>\n", lineNo);
                continue;
            }
            const int sc = keyToScancode(t[1]);
            if (sc < 0)
            {
                std::fprintf(stderr, "[keymap] line %d: unknown key '%s'\n", lineNo, t[1].c_str());
                continue;
            }
            const AnimActionDef* a = findAction(t[2]);
            if (!a)
            {
                std::fprintf(stderr, "[keymap] line %d: key '%s' -> unknown action '%s'\n", lineNo, t[1].c_str(), t[2].c_str());
                continue;
            }
            const int idx = static_cast<int>(a - actions_.data());
            int& slot = keyToAction_[static_cast<size_t>(sc)];
            if (slot >= 0)
                std::fprintf(stderr, "[keymap] line %d: key '%s' rebinds '%s' -> '%s'\n",
                             lineNo, t[1].c_str(), actions_[static_cast<size_t>(slot)].name.c_str(), t[2].c_str());
            slot = idx;
            bindings_.push_back({sc, t[1], t[2]});
        }
        else
        {
            std::fprintf(stderr, "[keymap] line %d: unknown directive '%s' (expected ACTION or KEY)\n", lineNo, t[0].c_str());
        }
    }
    std::fclose(f);

    std::fprintf(stderr, "[keymap] loaded %s: %d actions, %d key bindings\n",
                 path.c_str(), static_cast<int>(actions_.size()), static_cast<int>(bindings_.size()));
    return true;
}
} // namespace Game
