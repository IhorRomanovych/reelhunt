// Game/anim_state.h - animation state machine for a Character.
//
// Drives stance loops and one-shot reactions from key-mapped actions (see keymap.h),
// with crossfades. One-shot actions (attacks, reactions, ...) play once and then
// automatically return to the current base stance when their clip finishes.
#pragma once

#include "engine_types.h"
#include <string>

namespace Game
{
    class Character;
    class Keymap;

    class AnimStateMachine
    {
    public:
        explicit AnimStateMachine(Keymap& km) noexcept : keymap_(km) {}

        // Bind the model to drive. Call after Character::load/loadAnimations.
        void attach(Character& c) noexcept { character_ = &c; }

        // Initial stance (e.g. "idle"). Crosses to it immediately.
        void setBase(const std::string& action) noexcept;

        // Trigger a key-mapped action. Stances become the new base; one-shots play once and
        // return to the base stance on completion. Safe to call on any key press.
        void trigger(const std::string& action) noexcept;

        // Advance: when the current one-shot clip has finished, cross back to the base stance.
        // Call once per frame, before Character::update.
        void update(float32 dt) noexcept;

        [[nodiscard]] const std::string& currentAction() const noexcept { return current_; }
        [[nodiscard]] const std::string& baseAction() const noexcept { return base_; }

    private:
        void crossTo(const std::string& action, float32 dur) noexcept;

        Keymap& keymap_;
        Character* character_ = nullptr;
        std::string base_;          // current stance (looping) action name
        std::string current_;       // current action (stance or one-shot)
        bool currentIsOneShot_ = false;
    };
}
