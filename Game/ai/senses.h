// Game/ai/senses.h - what a hunter notices and how it reacts, as the exe works it out.
//
// SIGHT (exe 0x42c7c0 -> sight level 0x42be70 -> threshold test 0x42be00):
//   The player is seen only inside the hunter's view frustum - FRUSTUM_WIDTH_<STAND|WALK|RUN>
//   _<EASY|HARD>_deg wide by its own gait and the difficulty, FRUSTUM_HEIGHT_deg tall,
//   FRUSTUM_m deep - or, while the hunter stands still and the player is within
//   CHECK_PERIPHERAL_m, the wide short peripheral frustum. Then:
//     - the face in view within FACE_DEFINITE_m: very_high;
//     - the face in view (within FACE_m): the FACE_LIGHT_% level of the light on the face;
//     - otherwise the average of three levels, truncated:
//         distance  (distance / FRUSTUM_m) below DISTANCE_% thresholds   (near = high)
//         light     the light on the body above LIGHT_% thresholds        (lit = high)
//         visible   the unobstructed fraction above VISIBLE_% thresholds  (exposed = high)
//       Any of the three at "none" (too dark, too hidden, too far) means not seen.
// HEARING (0x5207d0, volume left 0x522920, back to a volume 0x5228c0):
//   A sound of volume V reaches distance d with VOLUME_DISTANCES[V] - d left (inaudible past
//   it), BSP_VOLUME_FILTERING less through walls, less the loudest background noise. What is
//   left maps back to the smallest volume whose distance covers it. A confusable sound heard at
//   CONFUSION volume or quieter loses one interest level per volume step below its own.
// ACTION (aiTypeData.ini, record "default", documented in the file):
//   not_on_alert < hunt < investigate < chase < combat. ACTION_TRANSITION gives the minimum
//   sight or hearing-interest level to move up (at once) or to stay; without it an action ends
//   after SUSTAIN_ACTION seconds. Starting an action raises the alert level (SET_ALERT_LEVEL),
//   which drops one level per ALERT_LEVEL_DEGRADE seconds; with any alert left the hunter hunts
//   (searches), with none it goes back to its idle behaviour.
#pragma once
#include "engine_types.h"

#include "Game/ai/ai_tuning.h"

namespace Game {

// The exe 0x42be00: the first threshold (very_high first) that `v` exceeds.
SenseLevel levelAbove(float32 v, const LevelThresholds& t);
// The DISTANCE test inside 0x42be70: the first threshold `v` lies below (<= for very_low).
SenseLevel levelBelow(float32 v, const LevelThresholds& t);

enum class Gait { Stand, Walk, Run }; // the hunter's own movement (picks the frustum width)

struct SightInput {
    float32 distance = 0.0f;         // hunter eye -> player, metres
    float32 horizontalAngle = 0.0f;  // |angle| off the hunter's facing, radians
    float32 verticalAngle = 0.0f;    // |angle| above/below it, radians
    Gait gait = Gait::Stand;
    bool hardDifficulty = false;
    float32 visible = 1.0f;          // 0..1 of the body not hidden behind anything
    float32 light = 0.0f;            // 0..1 light on the body
    bool faceInView = false;         // the player's face turned to the hunter, unobstructed
    float32 faceLight = 0.0f;        // 0..1 light on the face
};

// The frustum test alone (true: the player is inside the main or peripheral frustum).
bool inViewFrustum(const SightInput& in, const VisionData& v);
// The sight level this frame, kNoLevel if not seen.
SenseLevel sightLevel(const SightInput& in, const VisionData& v);

struct HeardSound {
    SenseLevel volume = kNoLevel;   // how loud it arrives
    SenseLevel interest = kNoLevel; // what it means to the hunter
};

// A sound of `type` made `distance` metres away (through walls: `occluded`), over a background
// noise level of `background` (metres of volume, 0 for silence).
HeardSound hearSound(const SoundType& type, float32 distance, bool occluded, const HearingData& h,
                     float32 background = 0.0f);

// The player's footsteps for his speed (as a fraction of his full speed) and stance:
// WALK_% of full speed or more makes WALKING, RUN_% RUNNING; sneaking is ignored (AISounds.ini).
const char* footstepSound(float32 speedFraction, bool sneaking, const HearingData& h);

struct Awareness {
    AiAction action = AiAction::NotOnAlert;
    SenseLevel alert = kNoLevel;
    float32 alertTime = 0.0f;   // seconds at the current alert level without a reaction
    float32 unsustained = 0.0f; // seconds the current action's stay condition has been missing
};

// One frame: this frame's sight level and heard interest. Returns true when the action changed.
bool updateAwareness(Awareness& a, SenseLevel sight, SenseLevel hearing, float32 dt, const AlertData& d);

} // namespace Game
