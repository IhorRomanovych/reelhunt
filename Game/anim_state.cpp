// Game/anim_state.cpp - animation state machine for a Character.
#include "Game/anim_state.h"
#include "Game/character.h"
#include "Game/keymap.h"

#include <cstdio>

namespace
{
    // Crossfade durations (seconds). Tunables; shorter snaps read better for one-shots.
    constexpr float32 kStanceXfade = 0.35f;
    constexpr float32 kOneShotXfade = 0.22f;
}

namespace Game
{
void AnimStateMachine::crossTo(const std::string& action, float32 dur) noexcept
{
    if (!character_)
        return;
    const AnimActionDef* def = keymap_.findAction(action);
    if (!def)
        return;
    // loop = !oneShot: stances loop forever, one-shots play once (Character holds the last pose).
    const bool ok = character_->crossfade(def->clip, dur, !def->oneShot, 1.0f);
    if (ok)
    {
        current_ = def->name;
        currentIsOneShot_ = def->oneShot;
    }
    else
    {
        std::fprintf(stderr, "[anim] clip '%s' missing or incompatible for action '%s'\n",
                     def->clip.c_str(), def->name.c_str());
    }
}

void AnimStateMachine::setBase(const std::string& action) noexcept
{
    if (!keymap_.findAction(action))
    {
        std::fprintf(stderr, "[anim] unknown base action '%s'\n", action.c_str());
        return;
    }
    base_ = action;
    crossTo(action, kStanceXfade);
}

void AnimStateMachine::trigger(const std::string& action) noexcept
{
    const AnimActionDef* def = keymap_.findAction(action);
    if (!def || !character_)
        return;

    if (def->oneShot)
    {
        // Always (re)play a one-shot; it returns to the base stance on completion.
        crossTo(action, kOneShotXfade);
        return;
    }

    // Stance: becomes the new base. Skip re-crossing if already playing it.
    base_ = action;
    if (action == current_ && character_->isPlaying())
        return;
    crossTo(action, kStanceXfade);
}

void AnimStateMachine::update(float32 dt) noexcept
{
    (void)dt;
    if (!character_)
        return;
    // A non-loop one-shot sets playing_ == false when its clip ends (holds the last pose).
    // Stances loop forever, so this only fires after a one-shot finishes. It returns to the
    // one-shot's `baseAfter` stance when the keymap names one (which also becomes the new base),
    // otherwise to the current base.
    if (currentIsOneShot_ && !character_->isPlaying())
    {
        currentIsOneShot_ = false;
        const AnimActionDef* finished = keymap_.findAction(current_);
        if (finished && !finished->baseAfter.empty() && keymap_.findAction(finished->baseAfter))
            base_ = finished->baseAfter;
        crossTo(base_, kStanceXfade);
    }
}
} // namespace Game
