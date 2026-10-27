// Game/keymap.h - parse the real key-mapping file (config/keymap.txt).
//
// The file binds keyboard keys to animation actions and defines what each action
// plays (a clip + one-shot/loop behaviour). Keeping this in a data file means keys and
// actions can be rebound without a code change. Pure file parsing: no SDL / Renderer
// dependency (key names map to scancode values by a built-in table).
#pragma once

#include "engine_types.h"

#include <array>
#include <string>
#include <vector>

namespace Game
{
    // One animation action: which clip to play and its behaviour.
    struct AnimActionDef
    {
        std::string name;     // action name (referenced by KEY lines)
        std::string clip;     // IFP animation clip to play
        bool oneShot = false; // true = play once then return to baseAfter; false = loop stance
        std::string baseAfter;// for one-shots: action to return to (empty = current base)
    };

    // A single key -> action binding (kept for display / testing).
    struct KeyBinding
    {
        int scancode = -1;    // SDL scancode value (-1 if the key name was unknown)
        std::string keyName;  // the key name as written in the file
        std::string action;   // the action it triggers
    };

    // Parsed key-mapping file: action definitions + key -> action bindings.
    class Keymap
    {
    public:
        // Parse the file at `path`. Returns false (and logs) if it cannot be read.
        bool load(const std::string& path);

        [[nodiscard]] const std::vector<AnimActionDef>& actions() const noexcept { return actions_; }
        [[nodiscard]] const std::vector<KeyBinding>& bindings() const noexcept { return bindings_; }
        [[nodiscard]] const AnimActionDef* findAction(const std::string& name) const;
        // The action bound to an SDL scancode, or "" if none.
        [[nodiscard]] std::string actionForKey(int scancode) const;
        [[nodiscard]] const std::string& path() const noexcept { return path_; }

        // SDL key name -> SDL3 scancode value (-1 if unknown). Public for testing.
        static int keyToScancode(const std::string& key);

    private:
        static constexpr int kMaxScancode = 128; // covers letters, digits, F1-F24, arrows, space, enter

        std::string path_;
        std::vector<AnimActionDef> actions_;
        std::vector<KeyBinding> bindings_;
        std::array<int, kMaxScancode> keyToAction_ = makeUnbound(); // scancode -> index into actions_

        static constexpr std::array<int, kMaxScancode> makeUnbound()
        {
            std::array<int, kMaxScancode> a{};
            a.fill(-1); // `int x[128] = {-1}` only set element 0; the rest were 0 (= action 0)
            return a;
        }
    };
}
