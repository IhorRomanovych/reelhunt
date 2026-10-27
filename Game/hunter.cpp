// Game/hunter.cpp - hunter behaviour. See hunter.h.
#include "Game/hunter.h"
#include "Game/ped_anims.h"

#include "Game/ai/gun_combat.h"
#include "Game/ai/search.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "Math/math.h"

namespace Game {

namespace {

constexpr float32 kGravity = 22.0f;
constexpr float32 kTurnRate = 5.0f;          // rad/s
constexpr float32 kEyeHeight = 1.65f;        // above the feet
constexpr float32 kChestHeight = 1.2f;
constexpr float32 kPlayerRadius = 0.5f;      // the player COL body sphere
constexpr float32 kPlayerBodyAt = 1.15f;     // ... its centre above his feet
constexpr float32 kXfade = 0.2f;
constexpr float32 kReplan = 0.8f;            // seconds between route re-plans while chasing
// Body points tested for the VISIBLE fraction (metres above the feet, standing).
constexpr float32 kBodyPoints[] = {1.6f, 1.2f, 0.9f, 0.5f, 0.15f};
constexpr float32 kCrouchScale = 0.6f;       // a sneaking player is this much lower
// Bodies: seen within 18 m (0x683600), checked from within 1 m (0x793ae0); the errand is given up
// after 30 s on the way (ours).
constexpr float32 kBodySightRange = 18.0f;
constexpr float32 kBodyCheckReach = 1.0f;
constexpr float32 kBodyCheckGiveUp = 30.0f;
constexpr float32 kHeadCheckReach = 1.5f; // Check Head: within 1.5 m of it (0x793adc)
constexpr float32 kFaceCone = 0.5f;          // cos 60: his face is turned to the hunter
// Speech pacing (not in the data; our choice): one line at a time, taunts every so often.
constexpr float32 kSpeechGap = 2.5f;
constexpr float32 kTauntMin = 6.0f, kTauntMax = 11.0f;
constexpr float32 kMumbleMin = 18.0f, kMumbleMax = 40.0f;

float32 yawOf(const Math::Vector3& dir)
{
    return std::atan2(-dir.x, -dir.z); // yaw 0 faces -Z
}

Math::Vector3 forwardOf(float32 yaw)
{
    return {-std::sin(yaw), 0.0f, -std::cos(yaw)};
}

Math::Vector3 rightOf(float32 yaw)
{
    return {std::cos(yaw), 0.0f, -std::sin(yaw)};
}

float32 flatDistance(const Math::Vector3& a, const Math::Vector3& b)
{
    const float32 dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// A fall clip: <prefix><Fwd|Bkw>_<stem> (Fall_Land_Melee_Bkw_Beater2, ...), else the fists'
// (_Fist), else the bare one (Fall_Launch_Melee_Fwd).
std::string fallClip(const Character& model, const std::string& prefix, bool fwd, const std::string& stem)
{
    const std::string base = prefix + (fwd ? "Fwd" : "Bkw");
    for (const std::string& c : {base + "_" + stem, base + "_Fist", base})
        if (model.clipDuration(c) > 0.0f)
            return c;
    return base;
}

float32 wrapAngle(float32 a)
{
    while (a > Math::pi)
        a -= 2.0f * Math::pi;
    while (a < -Math::pi)
        a += 2.0f * Math::pi;
    return a;
}

// The cover fight's states (Hunter::gunState_). The exe's CPedActGunCoverCombat has more (its
// sub-states 0 .. 13); these are the ones followed here:
//   Assess    stands facing him, gun up, then decides (its sub-state 5)
//   Seek      on its way to a cover point (0 / 6: CPedActCoverSeek)
//   Cover     crouched behind it (2)
//   Peek      leaning out (or up over it) and firing (11: CPedActPeek)
//   Exit      no peek from there: out of cover to a point that sees him (3 / 4: CPedActCoverExit)
//   Stand     no cover: stands its ground (12)
//   StayBack  the squad holds it back ("Stay Back")
// He in the shadows (cAI_ControllerCombatBlind): Blind (CPedActBlindGunCombat), Closer ("Get Closer").
// He lost (cAI_ControllerGotoGunfire): GotoWait, Wait, GotoShoot, Quit ("Gunfire[...]").
enum GunState : int32 {
    kGunOff = 0,
    kGunAssess,
    kGunSeek,
    kGunCover,
    kGunPeek,
    kGunExit,
    kGunStand,
    kGunCloseIn, // 8: to the gun's range
    kGunAdvance, // 7: on to where he was lost
    kGunFollow,  // 9: after him, going off
    kGunRush,    // 13: at him, near and unseen
    kGunStayBack,
    kGunBlind,
    kGunCloser,
    kGunfireGotoWait,
    kGunfireWait,
    kGunfireGotoShoot,
    kGunfireQuit,
};

bool coverState(int32 s)
{
    return s >= kGunAssess && s <= kGunRush;
}
// The exe's sub-state number for ours (its per-state counts, 0x79ee68).
int32 exeSub(int32 s)
{
    switch (s)
    {
    case kGunAssess: return 5;
    case kGunSeek: return 0;
    case kGunCover: return 2;
    case kGunPeek: return 11;
    case kGunExit: return 3;
    case kGunStand: return 12;
    case kGunCloseIn: return 8;
    case kGunAdvance: return 7;
    case kGunFollow: return 9;
    case kGunRush: return 13;
    default: return -1;
    }
}
bool blindState(int32 s)
{
    return s == kGunBlind || s == kGunCloser;
}
bool gunfireState(int32 s)
{
    return s >= kGunfireGotoWait && s <= kGunfireQuit;
}
gun::Gunfire gunfireOf(int32 s)
{
    return gunfireState(s) ? static_cast<gun::Gunfire>(s - kGunfireGotoWait) : gun::Gunfire::None;
}

// Who fights and which cover points are taken, for every squad (the exe keeps this globally too:
// the fighting list 0x79eea4, the taken points 0x79bd0c).
gun::SquadRoster& gunRoster()
{
    static gun::SquadRoster roster;
    return roster;
}

gun::RoleSlots& roleSlots()
{
    static gun::RoleSlots slots;
    return slots;
}

float32 randomIn(std::mt19937& rng, float32 lo, float32 hi)
{
    return std::uniform_real_distribution<float32>(lo, hi)(rng);
}

} // namespace

Hunter::Hunter(std::string name, std::unique_ptr<Character> model, const Math::Vector3& feet, float32 yaw,
               const HunterTraits& traits, float32 health, char size, float32 bodyHeight, float32 radius)
    : name_(std::move(name)), model_(std::move(model)), traits_(traits), size_(size), bodyHeight_(bodyHeight),
      radius_(radius), center_(feet + Math::Vector3{0.0f, bodyHeight, 0.0f}), yaw_(yaw),
      health_(health > 0.0f ? health : traits.hitPoints), fullHealth_(health_), home_(feet), homeYaw_(yaw)
{
    hideHealth_ = static_cast<int32>(std::floor(traits.hideHealth));
    loop("Stand_Idle", 0.0f);
    model_->setPlacement(feet, yaw);
}

// ---------------------------------------------------------------------------------------------
// Animation

std::string Hunter::walkClip() const
{
    const char variant = (size_ == 'S' || size_ == 'B') ? size_ : 'M';
    const bool aware = awareness_.action != AiAction::NotOnAlert;
    return std::string(aware ? "Walk_Fwd_Aware_" : "Walk_Fwd_Idle_") + variant;
}

void Hunter::loop(const std::string& clip, float32 groundSpeed, bool repeat)
{
    if (oneShot_ > 0.0f)
        return;
    if (clip != loopClip_)
    {
        model_->crossfade(clip, PedAnims::shared().blendInto(loopClip_, clip, kXfade), repeat);
        loopClip_ = clip;
    }
    const float32 clipSpeed = model_->rootSpeed(clip);
    model_->setSpeed(clipSpeed > 0.0f && groundSpeed > 0.05f ? std::clamp(groundSpeed / clipSpeed, 0.5f, 1.6f) : 1.0f);
}

void Hunter::once(const std::string& clip)
{
    const float32 d = model_->clipDuration(clip);
    if (d <= 0.0f)
        return;
    model_->crossfade(clip, PedAnims::shared().blendInto(loopClip_, clip, 0.12f), false); // (table 0x68f210)
    oneShot_ = d;
    loopClip_.clear();
}

// ---------------------------------------------------------------------------------------------
// Movement

void Hunter::turnTowards(const Math::Vector3& dir, float32 dt)
{
    if (dir.x * dir.x + dir.z * dir.z < 1e-6f)
        return;
    const float32 diff = wrapAngle(yawOf(dir) - yaw_);
    const float32 step = kTurnRate * dt;
    yaw_ = wrapAngle(yaw_ + std::clamp(diff, -step, step));
}

bool Hunter::steerTo(const Math::Vector3& target, float32 speed, float32 arriveRadius, float32 dt)
{
    const Math::Vector3 f = feet();
    const Math::Vector3 to{target.x - f.x, 0.0f, target.z - f.z};
    const float32 dist = to.length();
    if (dist <= arriveRadius)
        return true;
    turnTowards(to, dt);
    // Walk along the facing, slowing while turning hard so corners are not cut through walls.
    const float32 facing = std::max(0.0f, forwardOf(yaw_).dot(to * (1.0f / dist)));
    const float32 v = speed * (0.25f + 0.75f * facing);
    const Math::Vector3 fwd = forwardOf(yaw_);
    velocity_.x = fwd.x * v;
    velocity_.z = fwd.z * v;
    speed_ = v;
    return false;
}

void Hunter::stand(float32)
{
    velocity_.x = velocity_.z = 0.0f;
    speed_ = 0.0f;
}

void Hunter::lookAround(float32 dt)
{
    stand(dt);
    turnTowards(forwardOf(yaw_ + 1.2f * dt), dt);
}

float32 Hunter::walkSpeed() const
{
    const float32 v = model_->rootSpeed(walkClip());
    return v > 0.0f ? v : 1.3f;
}

float32 Hunter::runSpeed() const
{
    const float32 v = model_->rootSpeed("Run_Fwd");
    return v > 0.0f ? v : 6.0f;
}

void Hunter::planRoute(const Math::Vector3& target, HunterWorld& w)
{
    route_.clear();
    routeIndex_ = 0;
    stuckFor_ = 0.0f;
    stuckBest_ = 1e9f;
    // The graph nodes it starts from and heads for: the nearest ones with no wall in between
    // (a free line at knee height).
    const Math::Vector3 knee{0.0f, 0.5f, 0.0f};
    auto clearOf = [&](const Math::Vector3& p) {
        return [&w, p, knee](const Math::Vector3& node) { return w.field.lineOfSight(p + knee, node + knee); };
    };
    const int32 from = w.nav.nearestClear(feet(), clearOf(feet()));
    const int32 to = w.nav.nearestClear(target, clearOf(target));
    // When the target cannot be reached (a closed gate, another part of the level), it goes as
    // near as the graph takes it rather than walking at it through the walls.
    bool reached = true;
    if (from >= 0 && to >= 0)
    {
        const std::vector<int32> nodes = w.nav.route(from, to, true);
        reached = !nodes.empty() && nodes.back() == to;
        for (int32 n : nodes)
            route_.push_back(w.nav.node(n).position);
    }
    if (w.debugLog && !reached && from >= 0 && to >= 0)
    {
        const auto& a = w.nav.node(from);
        const auto& b = w.nav.node(to);
        std::cerr << "[route] " << name_ << " no route " << from << " (" << a.position.x << ", " << a.position.y << ", "
                  << a.position.z << ") area " << a.area << " links " << a.links.size() << " -> " << to << " ("
                  << b.position.x << ", " << b.position.y << ", " << b.position.z << ") area " << b.area << " links "
                  << b.links.size() << "\n";
    }
    // Skip the first node when it lies behind us on the way to the second.
    if (route_.size() >= 2 && flatDistance(feet(), route_[1]) < flatDistance(route_[0], route_[1]))
        routeIndex_ = 1;
    if (reached)
        route_.push_back(target);
}

bool Hunter::followRoute(float32 speed, float32 dt, HunterWorld&)
{
    while (routeIndex_ < route_.size())
    {
        const bool last = routeIndex_ + 1 == route_.size();
        if (!steerTo(route_[routeIndex_], speed, last ? 0.35f : 0.6f, dt))
        {
            // Stuck (no closer by 0.1 m in 1.5 s: a corner, a door frame, a body): it gives the
            // point up and heads for the next.
            const float32 d = flatDistance(feet(), route_[routeIndex_]);
            if (d < stuckBest_ - 0.1f)
            {
                stuckBest_ = d;
                stuckFor_ = 0.0f;
                return false;
            }
            if ((stuckFor_ += dt) < 1.5f)
                return false;
        }
        stuckFor_ = 0.0f;
        stuckBest_ = 1e9f;
        ++routeIndex_;
    }
    stand(dt);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Senses and state changes

void Hunter::sense(float32 dt, HunterWorld& w)
{
    const HunterSetup* setup = w.director.hunter(name_);
    sight_ = kNoLevel;
    heard_ = {};
    // Only a hunter whose leader has him among its enemies is after him (AIAddLeaderEnemy).
    if (setup && setup->added && w.player.alive && w.director.hostileToPlayer(name_))
    {
        const VisionData& vision = w.tuning.vision;
        const Math::Vector3 eye = feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f};
        const float32 crouch = w.player.sneaking ? kCrouchScale : 1.0f;
        const Math::Vector3 chest = w.player.feet + Math::Vector3{0.0f, kChestHeight * crouch, 0.0f};
        if (!setup->blind)
        {
            SightInput in;
            const Math::Vector3 to = chest - eye;
            in.distance = to.length();
            const float32 flat = std::sqrt(to.x * to.x + to.z * to.z);
            const Math::Vector3 fwd = forwardOf(yaw_);
            in.horizontalAngle = flat > 1e-3f ? std::acos(std::clamp((fwd.x * to.x + fwd.z * to.z) / flat, -1.0f, 1.0f)) : 0.0f;
            // The view cone takes his head or chest, whichever is nearer its eye level (ours: up
            // close the chest alone falls below the cone).
            const float32 headDy = w.player.feet.y + kBodyPoints[0] * crouch - eye.y;
            in.verticalAngle = std::atan2(std::min(std::fabs(to.y), std::fabs(headDy)), std::max(flat, 1e-3f));
            in.gait = speed_ < 0.1f ? Gait::Stand : (speed_ < 2.5f ? Gait::Walk : Gait::Run);
            in.hardDifficulty = w.hardDifficulty;
            in.light = w.player.light;
            if (w.player.hidden)
            {
                // Seen going into the shadows, or searched out from close by (stealth.h); a hunter
                // fighting him keeps him in view at that range too (ours).
                const bool searching = awareness_.action == AiAction::Hunt || awareness_.action == AiAction::Investigate ||
                                       awareness_.action == AiAction::Combat;
                if ((sawHide_ && awareness_.action >= AiAction::Chase) ||
                    (searching && in.distance < vision.faceDefinite))
                    in.light = vision.safeZone + 1.0f;
            }
            in.faceLight = in.light;
            if (in.distance < vision.range && inViewFrustum(in, vision))
            {
                // VISIBLE: the share of the body the hunter has a clear line to.
                int32 clear = 0;
                bool headClear = false;
                for (std::size_t i = 0; i < std::size(kBodyPoints); ++i)
                {
                    const Math::Vector3 p = w.player.feet + Math::Vector3{0.0f, kBodyPoints[i] * crouch, 0.0f};
                    if (w.field.lineOfSight(eye, p))
                    {
                        ++clear;
                        headClear = headClear || i == 0;
                    }
                }
                in.visible = static_cast<float32>(clear) / static_cast<float32>(std::size(kBodyPoints));
                // His face: turned towards the hunter, and the head in clear view.
                const Math::Vector3 toHunter{eye.x - w.player.feet.x, 0.0f, eye.z - w.player.feet.z};
                const float32 th = toHunter.length();
                in.faceInView = headClear && th > 1e-3f && forwardOf(w.player.yaw).dot(toHunter * (1.0f / th)) > kFaceCone;
                sight_ = sightLevel(in, vision);
            }
        }
        if (!setup->deaf)
        {
            const Math::Vector3 ear = eye;
            Math::Vector3 heardAt = w.player.feet;
            auto listen = [&](const char* name, const Math::Vector3& at, const Math::Vector3& source) {
                if (!name)
                    return;
                const SoundType type = w.tuning.sound(name);
                if (type.volume == kNoLevel || (type.volume == kVeryLow && type.interest == kVeryLow))
                    return; // SNEAKING: ignored (only gravel upgrades it)
                const float32 d = (at - feet()).length();
                const bool occluded = d < w.tuning.hearing.volumeDistance[static_cast<std::size_t>(type.volume)] &&
                                      !w.field.lineOfSight(ear, source);
                const HeardSound h = hearSound(type, d, occluded, w.tuning.hearing);
                if (h.interest > heard_.interest || (h.interest == heard_.interest && h.volume > heard_.volume))
                {
                    heard_ = h;
                    heardAt = at;
                }
            };
            listen(w.player.sound, w.player.feet, chest);
            if (w.player.swiped)
                listen("MELEE_SWIPE", w.player.feet, chest);
            listen(w.player.noise, w.player.feet, chest);
            if (w.sounds)
                for (const WorldSound& s : *w.sounds)
                    listen(s.name.c_str(), s.at, s.at + Math::Vector3{0.0f, 0.3f, 0.0f});
            if (sight_ == kNoLevel && heard_.interest != kNoLevel)
                lastKnown_ = heardAt; // it goes to look where the sound was
        }
        if (sight_ != kNoLevel)
            lastKnown_ = w.player.feet;
    }
    // He slipped into the shadows in front of it.
    if (w.player.justHidden && awareness_.action >= AiAction::Chase && lastSight_ != kNoLevel)
    {
        sawHide_ = true;
        speechBusy_ = 0.0f;
        say("PLAYER_RUNS_IN_SAFEZONE", w);
    }
    lastSight_ = sight_;
    const AiAction before = awareness_.action;
    if (bodyCheck_ && awareness_.action == AiAction::Investigate)
        awareness_.unsustained = 0.0f; // the errand holds it until it is done (ours)
    if (updateAwareness(awareness_, sight_, heard_.interest, dt, w.tuning.alert))
        react(before, w);
    if (bodyCheck_ && awareness_.action != AiAction::Investigate)
        bodyCheck_ = false; // he turned up: that comes first
    if (awareness_.action < AiAction::Investigate)
        sawHide_ = false;
}

void Hunter::react(AiAction before, HunterWorld& w)
{
    route_.clear();
    routeIndex_ = 0;
    wait_ = 0.0f;
    searchNodes_.clear();
    const AiAction now = awareness_.action;
    if (before == AiAction::NotOnAlert && now > AiAction::Hunt && oneShot_ <= 0.0f)
        once(sight_ != kNoLevel ? "React_Surprise" : "React_Noise");
    else if (now == AiAction::NotOnAlert && oneShot_ <= 0.0f)
        once("Idle_Dismiss_Noise_01"); // gave up

    // What it says about it (the speech contexts of Communications.ini / the SFX bank).
    speechBusy_ = 0.0f;
    if (now >= AiAction::Chase && before < AiAction::Chase)
        say("DEFINITE_SIGHTING", w);
    else if (now == AiAction::Investigate && before < AiAction::Investigate)
        say(sight_ != kNoLevel ? "SURPRISE" : (w.tuning.alert.investigateRuns ? "RUN_TO_INVESTIGATE" : "WALK_TO_INVESTIGATE"), w);
    else if (now == AiAction::Hunt && before >= AiAction::Chase)
        say("NEGATIVE_CHASE_RESULT", w);
    else if (now == AiAction::Hunt && before == AiAction::Investigate)
        say("CURIOSITY_NO_RESULT", w);
    else if (now == AiAction::NotOnAlert)
        say("NEGATIVE_SEARCH_RESULT", w);
    chatterIn_ = std::uniform_real_distribution<float32>(kTauntMin, kTauntMax)(w.rng);
}

void Hunter::say(const char* context, HunterWorld& w)
{
    if (voice_.empty() || !speechOn_ || speechBusy_ > 0.0f || !w.sound)
        return;
    w.sound(voice_ + context, feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f}, 0.6f);
    speechBusy_ = kSpeechGap;
}

void Hunter::sayLine(int32 line, const HunterWorld& w)
{
    if (voice_.empty() || !speechOn_ || !w.sound)
        return;
    w.sound(voice_ + "@" + std::to_string(line), feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f}, 0.6f);
    speechBusy_ = kSpeechGap;
}

void Hunter::chatter(float32 dt, HunterWorld& w)
{
    speechBusy_ = std::max(0.0f, speechBusy_ - dt);
    if (!w.player.alive && awareness_.action >= AiAction::Chase && !tauntedDeath_)
    {
        tauntedDeath_ = true;
        speechBusy_ = 0.0f;
        say("TAUNT_PLAYER_DEATH", w);
        return;
    }
    if ((chatterIn_ -= dt) > 0.0f)
        return;
    switch (awareness_.action)
    {
    case AiAction::NotOnAlert:
        if (std::uniform_int_distribution<int>(0, 1)(w.rng) == 0)
            say("IDLE_MUMBLING", w);
        chatterIn_ = std::uniform_real_distribution<float32>(kMumbleMin, kMumbleMax)(w.rng);
        return;
    case AiAction::Hunt:
        // Searching where he is hiding: taunt the shadows.
        say(w.player.hidden && (w.player.feet - feet()).length() < w.tuning.alert.searchRadius ? "TAUNT_SAFEZONE" : "TAUNT_SEARCH", w);
        break;
    case AiAction::Chase: say("TAUNT_CHASE", w); break;
    case AiAction::Combat: say("TAUNT_SHORT", w); break;
    case AiAction::Investigate: break;
    }
    chatterIn_ = std::uniform_real_distribution<float32>(kTauntMin, kTauntMax)(w.rng);
}

void Hunter::hit(float32 damage, const Math::Vector3& from, HunterWorld& w, HitKind kind, const std::string& attackerStem)
{
    if (state_ == HunterState::Dead || state_ == HunterState::Executed)
        return;
    if (invulnerable_)
        damage = 0.0f; // SetEntityInvulnerable: it still reacts
    const bool wasStunned = state_ == HunterState::Stunned;
    const Math::Vector3 toAttacker{from.x - feet().x, 0.0f, from.z - feet().z};
    const bool fromFront = forwardOf(yaw_).dot(toAttacker) >= 0.0f;
    const std::string stem = melee_ ? melee_->clipStem : std::string("Fist");
    // Its guard: while it blocks (its melee traits' response), a quick or hard blow from in front
    // meets it and its BLOCK_DAMAGE share gets through (kicks and stomps go round it: ours).
    const bool guarded = blocking_ && (kind == HitKind::Quick || kind == HitKind::Hard) && fromFront &&
                         state_ == HunterState::Normal;
    if (!guarded)
        dropMelee(); // struck, it drops what it was about (0x567b20)
    if (kind == HitKind::Grapple)
    {
        // A grapple blow: the grapple plays its clips; this only takes its health.
        health_ -= damage;
        if (w.sound)
            w.sound("SFX_WEAPON_MELEE_COLLISION_FIST", feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f}, 0.8f);
        if (health_ <= 0.0f)
        {
            state_ = HunterState::Held; // (dieHeld takes a held hunter down)
            dieHeld(fromFront ? "Die_Bkw" : "Die_Fwd", w);
        }
        return;
    }
    if (state_ == HunterState::Down)
    {
        // On the floor (BT_FALL_DAMAGE / BT_FALL_DIE): no guard; a stomp plays Damage_Stomp_*,
        // anything else Damage_Lying_*, and it dies Die_Lying_*.
        health_ -= damage;
        const Math::Vector3 at = feet() + Math::Vector3{0.0f, 0.3f, 0.0f};
        const std::string dir = downFwd_ ? "Fwd" : "Bkw";
        if (w.sound)
            w.sound("SFX_WEAPON_MELEE_COLLISION_FIST", at, 0.8f);
        if (health_ <= 0.0f)
        {
            if (w.sound)
                w.sound("SFX_GENERIC_DEATH", at, 0.8f);
            state_ = HunterState::Dead;
            oneShot_ = 0.0f;
            once("Die_Lying_" + dir);
            oneShot_ = 1e9f; // hold the last pose
            if (w.scriptEvent)
                w.scriptEvent(name_, "OnDeath");
            return;
        }
        if (w.sound)
            w.sound("SFX_GENERIC_PAIN_LOW", at, 0.7f);
        if (downPhase_ == DownPhase::Lying)
        {
            oneShot_ = 0.0f;
            once((kind == HitKind::Stomp ? "Damage_Stomp_" : "Damage_Lying_") + dir);
            oneShot_ = 1e9f; // it ends lying: hold that
        }
        return;
    }
    if (aiming_ && !guarded)
    {
        aiming_ = false; // a blow that gets through drops its aim
        gunOverlay_ = 0.0f;
        model_->stopOverlay();
    }
    if (guarded)
    {
        damage *= traits_.blockDamage;
        const std::string react = stem + (kind == HitKind::Hard ? "_Block_Hit_Hard_T" : "_Block_Hit_Quick_T");
        model_->playOverlay(react, false);
        guardLeft_ = std::max(0.2f, model_->clipDuration(react));
    }
    health_ -= damage;
    struckAt_ = w.time;
    hurtPastHide(true, w);
    if (incomingFrom_)
    {
        // Struck by another hunter: it turns on that one, not on him (ours).
        foe_ = incomingFrom_;
        killedByHunter_ = health_ <= 0.0f;
    }
    else if (w.director.hostileToPlayer(name_))
    {
        // Struck: it knows exactly where the player is and fights. (Only a hunter whose leader has
        // him among its enemies: a buddy he catches with a blow - the journalist - takes the hurt
        // and stays on his side.)
        awareness_.action = AiAction::Combat;
        awareness_.alert = std::max(awareness_.alert, w.tuning.alert.setAlert[static_cast<std::size_t>(AiAction::Combat)]);
        awareness_.alertTime = 0.0f;
        awareness_.unsustained = 0.0f;
        lastKnown_ = from;
    }
    oneShot_ = 0.0f;
    route_.clear();
    const Math::Vector3 head = feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f};
    if (w.sound)
        w.sound("SFX_WEAPON_MELEE_COLLISION_FIST", head, guarded ? 0.4f : 0.7f);
    if (health_ <= 0.0f)
    {
        if (w.sound)
            w.sound("SFX_GENERIC_DEATH", head, 0.8f);
        state_ = HunterState::Dead;
        model_->stopOverlay();
        once(kind == HitKind::Kick ? "Die_Kick" : (fromFront ? "Die_Bkw" : "Die_Fwd"));
        oneShot_ = 1e9f; // hold the last pose
        stand(0.0f);
        if (w.scriptEvent)
            w.scriptEvent(name_, "OnDeath");
        return;
    }
    if (guarded)
        return; // it took the blow on its guard and fights on
    if (w.sound)
        w.sound(damage >= 10.0f ? "SFX_GENERIC_PAIN_HIGH" : "SFX_GENERIC_PAIN_LOW", head, 0.7f);
    stand(0.0f);
    if (kind == HitKind::Hard && wasStunned)
    {
        // A second hard blow while it reels fells it (ours: the original's rule is not traced).
        knockDown(fromFront, attackerStem, w);
        return;
    }
    if (kind == HitKind::Hard)
    {
        // Knocked reeling (Stun_Melee_*): defenceless until it recovers. The scripts hear
        // OnStartStunned / OnStopStunned.
        state_ = HunterState::Stunned;
        once(fromFront ? "Stun_Melee_Bkw" : "Stun_Melee_Fwd");
        stunLeft_ = std::max(oneShot_, 1.0f);
        if (w.scriptEvent)
            w.scriptEvent(name_, "OnStartStunned");
        return;
    }
    state_ = HunterState::Hurt;
    if (wasStunned && w.scriptEvent)
        w.scriptEvent(name_, "OnStopStunned");
    const bool kicked = kind == HitKind::Kick || kind == HitKind::Stomp;
    if (kind == HitKind::Obstruct && model_->clipDuration("Damage_Obstruct_Hit") > 0.0f)
        once("Damage_Obstruct_Hit");
    else
        once(kicked ? "Damage_Kick" : (fromFront ? "Damage_Melee_Quick1_Fist" : "Damage_Melee_Bkw_Fist"));
    turnTowards(toAttacker, 1.0f);
}

void Hunter::provoke(const Math::Vector3& at, const AlertData& alert)
{
    awareness_.action = AiAction::Combat;
    awareness_.alert = std::max(awareness_.alert, alert.setAlert[static_cast<std::size_t>(AiAction::Combat)]);
    awareness_.alertTime = 0.0f;
    awareness_.unsustained = 0.0f;
    lastKnown_ = at;
}

void Hunter::shot(float32 damage, const Math::Vector3& from, const BodyHit& where, HunterWorld& w)
{
    if (state_ == HunterState::Dead || state_ == HunterState::Executed || state_ == HunterState::Held)
        return;
    if (w.sound)
        w.sound("SFX_WEAPON_BULLET_HIT_PED", where.point, 0.8f);
    if (state_ == HunterState::Down)
    {
        hit(damage, from, w); // on the floor: Damage_Lying_*, Die_Lying_*
        return;
    }
    const bool wasStunned = state_ == HunterState::Stunned;
    health_ -= damage;
    struckAt_ = w.time;
    hurtPastHide(false, w);
    dropMelee();
    if (incomingFrom_)
    {
        foe_ = incomingFrom_; // shot by another hunter: that one (ours)
        killedByHunter_ = health_ <= 0.0f;
    }
    else if (w.director.hostileToPlayer(name_))
    {
        // Shot: it knows where he is and fights (as when struck; a buddy does not).
        awareness_.action = AiAction::Combat;
        awareness_.alert = std::max(awareness_.alert, w.tuning.alert.setAlert[static_cast<std::size_t>(AiAction::Combat)]);
        awareness_.alertTime = 0.0f;
        awareness_.unsustained = 0.0f;
        lastKnown_ = from;
    }
    route_.clear();
    const Math::Vector3 head = feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f};
    const std::string side = where.left ? "Left" : "Right";
    // A full-body reaction ends its aim (aimAndFire() raises the gun again).
    auto dropAim = [this] {
        aiming_ = false;
        gunOverlay_ = 0.0f;
        guardLeft_ = 0.0f;
        model_->stopOverlay();
    };
    if (health_ <= 0.0f)
    {
        if (w.sound)
            w.sound("SFX_GENERIC_DEATH", head, 0.8f);
        state_ = HunterState::Dead;
        dropAim();
        // Shot from in front it falls on its back (Bkw), from behind on its face (Fwd).
        std::string clip = where.zone == HitZone::Head ? (where.fromFront ? "Firearm_Head_Die_Bkw" : "Firearm_Head_Die_Fwd")
                         : where.zone == HitZone::Leg  ? "Firearm_Leg_Die_" + side
                                                       : (where.fromFront ? "Firearm_Torso_Die_Bkw" : "Firearm_Torso_Die_Fwd");
        if (model_->clipDuration(clip) <= 0.0f)
            clip = where.fromFront ? "Die_Bkw" : "Die_Fwd";
        oneShot_ = 0.0f;
        once(clip);
        oneShot_ = 1e9f; // hold the last pose
        stand(0.0f);
        if (w.scriptEvent)
            w.scriptEvent(name_, "OnDeath");
        return;
    }
    if (w.sound)
        w.sound(damage >= 10.0f ? "SFX_GENERIC_PAIN_HIGH" : "SFX_GENERIC_PAIN_LOW", head, 0.7f);
    if (wasStunned && w.scriptEvent)
        w.scriptEvent(name_, "OnStopStunned");
    if (where.zone == HitZone::Head)
    {
        // Its head snaps back over whatever it is doing.
        const std::string clip = where.fromFront ? "Firearm_Head_Damage_Fwd_T" : "Firearm_Head_Damage_Bkw_T";
        aiming_ = false;
        gunOverlay_ = 0.0f;
        if (model_->playOverlay(clip, false))
            guardLeft_ = std::max(0.2f, model_->clipDuration(clip));
        if (wasStunned)
            state_ = HunterState::Normal;
        return;
    }
    dropAim();
    stand(0.0f);
    state_ = HunterState::Hurt;
    oneShot_ = 0.0f;
    once(where.zone == HitZone::Leg ? "Firearm_Leg_Damage_" + side
                                    : (where.fromFront ? "Firearm_Torso_Damage_Fwd_" + side : std::string("Firearm_Torso_Damage_Bkw")));
}

void Hunter::knockDown(bool fromFront, const std::string& stem, HunterWorld& w)
{
    // BT_FALL_LAUNCH -> BT_FALL_LAND -> BT_FALL_STUN (lying KO_TIME) -> BT_FALL_GETUP. Struck
    // from in front it falls on its back (Bkw), from behind on its face (Fwd).
    const bool wasStunned = state_ == HunterState::Stunned;
    dropMelee();
    state_ = HunterState::Down;
    downFwd_ = !fromFront;
    downStem_ = stem;
    downPhase_ = DownPhase::Launch;
    aiming_ = false;
    gunOverlay_ = 0.0f;
    model_->stopOverlay();
    stand(0.0f);
    route_.clear();
    oneShot_ = 0.0f;
    once(fallClip(*model_, "Fall_Launch_Melee_", downFwd_, stem));
    downLeft_ = std::max(0.05f, oneShot_);
    if (!wasStunned && w.scriptEvent)
        w.scriptEvent(name_, "OnStartStunned");
}

void Hunter::updateDown(float32 dt, HunterWorld& w)
{
    stand(dt);
    if ((downLeft_ -= dt) > 0.0f)
        return;
    switch (downPhase_)
    {
    case DownPhase::Launch:
        downPhase_ = DownPhase::Land;
        oneShot_ = 0.0f;
        once(fallClip(*model_, "Fall_Land_Melee_", downFwd_, downStem_));
        downLeft_ = std::max(0.05f, oneShot_);
        break;
    case DownPhase::Land:
    {
        // Lying: the lying cycle where the level has it, else the landing's last pose.
        downPhase_ = DownPhase::Lying;
        const std::string cycle = downFwd_ ? "Firearm_Lying_Cycle_Fwd" : "Firearm_Lying_Cycle_Bkw";
        if (model_->clipDuration(cycle) > 0.0f)
            model_->crossfade(cycle, 0.2f, true);
        oneShot_ = 1e9f;
        // KO_TIME seconds (its type's +0x110, copied to ped+0x34c by 0x46fc80 and counted down by
        // 0x4b59f0). The player's own type is held to at least 2 s (0x4b5740). STUN_TIME (+0x114)
        // is parsed but never read: a stun lasts its Stun_Melee clip.
        downLeft_ = traits_.koTime;
        break;
    }
    case DownPhase::Lying:
        // Time up, it gets up unless someone stands over it (the entities its body overlaps,
        // 0x4e3c80; ours: within 1 m of its feet); then it waits rand(0, 1) s more and looks again.
        if (w.standingOver && w.standingOver(*this, feet(), 1.0f))
        {
            downLeft_ = std::uniform_real_distribution<float32>(0.0f, 1.0f)(w.rng);
            break;
        }
        downPhase_ = DownPhase::Getup;
        oneShot_ = 0.0f;
        once(downFwd_ ? "Getup_Fwd" : "Getup_Bkw");
        downLeft_ = std::max(0.05f, oneShot_);
        break;
    case DownPhase::Getup:
        state_ = HunterState::Normal;
        oneShot_ = 0.0f;
        swingPause_ = traits_.swingPause;
        if (w.scriptEvent)
            w.scriptEvent(name_, "OnStopStunned");
        break;
    }
}

void Hunter::carryIn(const std::string& clip, const Math::Matrix4x4& frame, const Math::Vector3& feet, bool loop)
{
    carry(clip, feet, yaw_, loop);
    inFrame_ = true;
    frame_ = frame;
}

void Hunter::carry(const std::string& clip, const Math::Vector3& feet, float32 yaw, bool loop)
{
    carried_ = true;
    inFrame_ = false;
    center_ = feet + Math::Vector3{0.0f, bodyHeight_, 0.0f};
    velocity_ = {0.0f, 0.0f, 0.0f};
    yaw_ = yaw;
    if (model_->currentClip() != clip)
        model_->crossfade(clip, 0.1f, loop);
}

bool Hunter::faceDown() const
{
    // (Put down off a shoulder it lies on its back: ours.)
    return !dropped_ && model_->currentClip().find("_Fwd") != std::string::npos;
}

void Hunter::hold(const std::string& clip, const Math::Vector3& feet, float32 yaw)
{
    if (state_ == HunterState::Dead || state_ == HunterState::Executed)
        return;
    dropMelee();
    state_ = HunterState::Held;
    center_ = feet + Math::Vector3{0.0f, bodyHeight_, 0.0f};
    velocity_ = {0.0f, 0.0f, 0.0f};
    yaw_ = yaw;
    speed_ = 0.0f;
    route_.clear();
    model_->stopOverlay();
    model_->crossfade(clip, 0.08f, false);
    loopClip_.clear();
    oneShot_ = 1e9f;
}

void Hunter::release() noexcept
{
    if (state_ != HunterState::Held)
        return;
    state_ = HunterState::Normal;
    oneShot_ = 0.0f;
    swingPause_ = traits_.swingPause;
}

void Hunter::killNow(HunterWorld& w)
{
    if (state_ == HunterState::Dead)
        return;
    health_ = 0.0f;
    state_ = HunterState::Dead;
    if (model_->clipDuration("Die_Bkw") > 0.0f)
        model_->crossfade("Die_Bkw", 0.05f, false);
    oneShot_ = 1e9f;
    if (w.scriptEvent)
        w.scriptEvent(name_, "OnDeath");
}

void Hunter::dieHeld(const std::string& dieClip, HunterWorld& w)
{
    if (state_ != HunterState::Held)
        return;
    health_ = 0.0f;
    state_ = HunterState::Dead;
    if (model_->clipDuration(dieClip) > 0.0f)
        model_->crossfade(dieClip, 0.05f, false);
    oneShot_ = 1e9f;
    if (w.sound)
        w.sound("SFX_GENERIC_DEATH", feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f}, 0.8f);
    if (w.scriptEvent)
        w.scriptEvent(name_, "OnDeath");
}

void Hunter::beginExecution(const Math::Vector3& feet, float32 yaw, const std::string& clip, HunterWorld& w)
{
    if (state_ == HunterState::Dead)
        return;
    state_ = HunterState::Executed;
    center_ = feet + Math::Vector3{0.0f, bodyHeight_, 0.0f};
    velocity_ = {0.0f, 0.0f, 0.0f};
    yaw_ = yaw;
    speed_ = 0.0f;
    route_.clear();
    model_->crossfade(clip, 0.1f, false);
    loopClip_.clear();
    oneShot_ = 1e9f;
    model_->setPlacement(this->feet(), yaw_);
    if (w.scriptEvent)
        w.scriptEvent(name_, "OnStartExecution");
}

void Hunter::finishExecution(const std::string& dieClip, HunterWorld& w)
{
    if (state_ != HunterState::Executed)
        return;
    health_ = 0.0f;
    state_ = HunterState::Dead;
    diedInExecution_ = true;
    if (!dieClip.empty() && model_->clipDuration(dieClip) > 0.0f)
        model_->crossfade(dieClip, 0.05f, false);
    oneShot_ = 1e9f; // hold the last pose
    if (w.scriptEvent)
        w.scriptEvent(name_, "OnDeath");
}

void Hunter::update(float32 dt, HunterWorld& w)
{
    if (gone_)
        return;
    moveClip_.clear();
    buddyOfPlayer_ = false;
    if (fall_ != FallPhase::None)
    {
        updateFall(dt, w); // the fall holds it
        return;
    }
    if (guardLeft_ > 0.0f && (guardLeft_ -= dt) <= 0.0f)
    {
        model_->stopOverlay();
        if (blocking_)
        {
            const std::string stem = melee_ ? melee_->clipStem : std::string("Fist");
            model_->playOverlay(model_->clipDuration(stem + "_Block_T") > 0.0f ? stem + "_Block_T" : std::string("Fist_Block_T"), true);
        }
    }
    if (state_ == HunterState::Stunned && (stunLeft_ -= dt) <= 0.0f)
    {
        state_ = HunterState::Normal;
        oneShot_ = 0.0f;
        if (w.scriptEvent)
            w.scriptEvent(name_, "OnStopStunned");
    }
    if (state_ == HunterState::Executed || state_ == HunterState::Held || carried_)
    {
        // The kill, grapple or carry plays where it was set up; nothing else moves it.
        if (inFrame_)
            model_->setPlacementMatrix(frame_);
        else
            model_->setPlacement(feet(), yaw_);
        model_->update(dt);
        return;
    }
    oneShot_ = std::max(0.0f, oneShot_ - dt);
    swingPause_ = std::max(0.0f, swingPause_ - dt);

    // A hostage (AISetHunterAsHostage, 0x51de70): tied, where it is, no AI.
    const HunterSetup* setupNow = w.director.hunter(name_);
    const bool hostage = setupNow && setupNow->hostage && state_ == HunterState::Normal;
    if (hostage)
    {
        awareness_ = Awareness{};
        sight_ = kNoLevel;
        route_.clear();
        stand(dt);
        poseClip_.clear();
        for (const char* c : {"Idle_Hostage_Slump", "IdleLoop_HandsTied_Stand"})
            if (poseClip_.empty() && model_->clipDuration(c) > 0.0f)
                poseClip_ = c;
    }
    else if (locked_ && state_ == HunterState::Normal)
    {
        // LockPed: no AI, it stands where it is.
        route_.clear();
        stand(dt);
    }
    else if (state_ != HunterState::Dead)
    {
        if (poseClip_ == "Idle_Hostage_Slump" || poseClip_ == "IdleLoop_HandsTied_Stand")
            poseClip_.clear();
        sense(dt, w);
        chatter(dt, w);
        // Sighting events for the level script.
        if (sight_ >= kMedium && !mediumSent_)
        {
            mediumSent_ = true;
            if (w.scriptEvent)
                w.scriptEvent(name_, "OnMediumSightingOrAbove");
        }
        if (sight_ >= kHigh && !highSent_)
        {
            highSent_ = true;
            if (w.scriptEvent)
                w.scriptEvent(name_, "OnHighSightingOrAbove");
        }
        if (awareness_.action == AiAction::NotOnAlert)
            mediumSent_ = highSent_ = false;

        if (state_ == HunterState::Hurt && oneShot_ <= 0.0f)
            state_ = HunterState::Normal;
        if (state_ == HunterState::Attacking)
            attacking(dt, w);
        else if (state_ == HunterState::Normal && oneShot_ <= 0.0f)
        {
            // A gun in its hand fights its own way (gunCombat); otherwise, or when that leaves it to
            // the action, by its action.
            // Another hunter to fight, while it is not after him (ours: the exe's enemy choice
            // between several targets is not traced).
            if (foe_ && (!foe_->alive() || foe_->gone()))
                foe_ = nullptr;
            // A buddy (BeBuddy) answers its enemies from its own states.
            const GoalDef* goal = awareness_.action < AiAction::Chase ? w.director.goalFor(name_, false) : nullptr;
            const bool buddy = goal && goal->type == GoalType::BeBuddy;
            if (!buddy && awareness_.action < AiAction::Chase && w.pickFoe && w.time - foeCheckAt_ > 0.5f)
            {
                foeCheckAt_ = w.time;
                if (!foe_ || flatDistance(foe_->feet(), feet()) > 40.0f)
                {
                    Hunter* picked = w.pickFoe(*this, true);
                    if (w.debugLog && picked && picked != foe_)
                        std::cerr << "[foe] " << name_ << " goes for " << picked->name() << " ("
                                  << flatDistance(picked->feet(), feet()) << " m)" << std::endl;
                    foe_ = picked;
                }
            }
            if (!buddy && awareness_.action < AiAction::Chase && foe_)
                fightFoe(dt, w);
            else if (awareness_.action >= AiAction::Chase && holdBoundary(dt, w))
                ;
            else if (!(gun_ && gunCombat(dt, w)))
                switch (awareness_.action)
                {
                case AiAction::NotOnAlert: idle(dt, w); break;
                case AiAction::Hunt: hunt(dt, w); break;
                case AiAction::Investigate: investigate(dt, w); break;
                case AiAction::Chase: chase(dt, w); break;
                case AiAction::Combat: combat(dt, w); break;
                }
        }
        else if (state_ == HunterState::Down)
            updateDown(dt, w);
        else
            stand(dt);
    }
    else
        stand(dt); // the dead do not walk on (killed mid-stride: AIKillEntity, REELHUNT_KILL)
    // Out of the hide action: up from its crouch, and the next one starts afresh.
    if (hiding_)
        hiding_ = false;
    else if (hidePhase_ != HidePhase::Choose)
    {
        hidePhase_ = HidePhase::Choose;
        hideNode_ = -1;
        if (poseClip_ == "Sneak_Stand" && gunState_ == kGunOff)
            poseClip_.clear();
    }
    // Its place in the squad's cover fight (and its cover point) outlasts a hit reaction, not
    // its death.
    if (gunState_ != kGunOff)
    {
        if (state_ == HunterState::Dead)
            leaveGunCombat();
        else if (state_ != HunterState::Normal)
        {
            const Subpack* sp = w.director.subpackOf(name_);
            rosterUpdate(sp ? sp->name : std::string(), w);
        }
    }

    // Body physics: gravity, then the same body + leg resolve as the player.
    velocity_.y -= kGravity * dt;
    center_ += velocity_ * dt;
    const bool grounded = w.field.resolveCharacter(center_, velocity_);
    // It starts to fall (0x4a0c10, every ped) going down, off the ground, with no floor within 1 m
    // below (0x728058).
    if (!grounded && velocity_.y < 0.0f && state_ != HunterState::Dead && state_ != HunterState::Held &&
        !w.field.raycast(feet(), feet() - Math::Vector3{0.0f, 1.0f, 0.0f}))
    {
        fall_ = FallPhase::Air;
        fallTop_ = feet().y;
        fallDeath_ = false;
        route_.clear();
        model_->crossfade("Fall_Flail_Upright", 0.15f, true);
        std::cerr << "[ai] " << name_ << " falls" << std::endl;
    }
    if (state_ == HunterState::Normal)
    {
        const bool calm = awareness_.action == AiAction::NotOnAlert;
        std::string clip = speed_ > 2.5f ? "Run_Fwd" : (speed_ > 0.05f ? walkClip() : (calm ? "Stand_Idle" : "Stand_Aware"));
        if (!moveClip_.empty() && speed_ > 0.05f && model_->clipDuration(moveClip_) > 0.0f)
            clip = moveClip_; // footwork: backing away
        bool repeat = true;
        if (!poseClip_.empty() && speed_ <= 0.05f)
        {
            clip = poseClip_; // crouched behind cover, or leaning out of it
            repeat = poseClip_.rfind("Peek_", 0) != 0; // a lean plays once and holds (0.27 s)
        }
        loop(clip, speed_, repeat);
    }
    model_->setPlacement(feet(), yaw_);
    model_->update(dt);
}

void Hunter::updateFall(float32 dt, HunterWorld& w)
{
    // Flailing (BT_FALL_FLAIL 0x28, Fall_Flail_Upright); 8 m down (0x728054) a deadly fall
    // (BT_FALL_FLAIL_DEATH 0x29, Fall_Flail_Die_Upright). On the ground it lands (BT_FALL_LAND
    // 0x2a, Fall_Land_Upright) and gets up (BT_FALL_GETUP 0x2f, Getup_Upright); a deadly one lands
    // with Fall_Land_Die_Upright and dies (0x4a0c10 case 4 -> 0x431480). It keeps its way across.
    velocity_.y -= kGravity * dt;
    center_ += velocity_ * dt;
    const bool grounded = w.field.resolveCharacter(center_, velocity_);
    switch (fall_)
    {
    case FallPhase::Air:
        if (!fallDeath_ && fallTop_ - feet().y >= 8.0f)
        {
            fallDeath_ = true;
            model_->crossfade("Fall_Flail_Die_Upright", 0.15f, true);
        }
        if (grounded)
        {
            velocity_ = Math::Vector3{0.0f, 0.0f, 0.0f};
            std::cerr << "[ai] " << name_ << " lands " << (fallTop_ - feet().y) << " m down" << (fallDeath_ ? ", dead" : "") << std::endl;
            if (fallDeath_)
            {
                killNow(w);
                model_->crossfade("Fall_Land_Die_Upright", 0.05f, false);
                fall_ = FallPhase::None;
                break;
            }
            fall_ = FallPhase::Land;
            model_->crossfade("Fall_Land_Upright", 0.05f, false);
            fallLeft_ = std::max(0.1f, model_->clipDuration("Fall_Land_Upright"));
        }
        break;
    case FallPhase::Land:
    case FallPhase::Getup:
        velocity_.x = velocity_.z = 0.0f;
        if ((fallLeft_ -= dt) > 0.0f)
            break;
        if (fall_ == FallPhase::Land)
        {
            fall_ = FallPhase::Getup;
            model_->crossfade("Getup_Upright", 0.1f, false);
            fallLeft_ = std::max(0.1f, model_->clipDuration("Getup_Upright"));
        }
        else
            fall_ = FallPhase::None;
        break;
    case FallPhase::None:
        break;
    }
    model_->setPlacement(feet(), yaw_);
    model_->update(dt);
}

// ---------------------------------------------------------------------------------------------
// Behaviours

void Hunter::idle(float32 dt, HunterWorld& w)
{
    const HunterSetup* setup = w.director.hunter(name_);
    if (!setup || !setup->added)
    {
        stand(dt);
        return;
    }
    if (aimTarget_ && gun_)
    {
        // SetHunterAimTarget: standing, its gun raised at the point (ours: no shot).
        route_.clear();
        stand(dt);
        turnTowards(Math::Vector3{aimTarget_->x - feet().x, 0.0f, aimTarget_->z - feet().z}, dt);
        if (!aiming_)
        {
            aiming_ = true;
            model_->playOverlay(gun_->aimClip(false));
        }
        return;
    }
    const float32 walk = walkSpeed();

    // Idling aiming (AISetEntityAim / AIModifyGoalAim): its gun up.
    const GoalDef* aimGoal = w.director.goalFor(name_, false);
    const bool idleAim = gun_ && (idleAim_ || (aimGoal && aimGoal->aim));
    if (idleAim && !aiming_)
    {
        aiming_ = idleAimRaised_ = true;
        model_->playOverlay(gun_->aimClip(false));
    }
    else if (!idleAim && idleAimRaised_)
    {
        idleAimRaised_ = false;
        lowerGun();
    }

    // In a squad: after its leader, not about its own goals and idle actions.
    if (squadLeader_ && (!squadLeader_->alive() || squadLeader_->gone()))
        squadLeader_ = nullptr;
    if (squadLeader_)
    {
        const Math::Vector3 to = squadLeader_->feet();
        if (flatDistance(feet(), to) > 2.5f)
        {
            if (route_.empty() || flatDistance(to, squadRouteTo_) > 1.0f)
            {
                planRoute(to, w);
                squadRouteTo_ = to;
            }
            followRoute(walk, dt, w);
        }
        else
        {
            route_.clear();
            stand(dt);
            turnTowards(Math::Vector3{std::sin(squadLeader_->yaw()), 0.0f, std::cos(squadLeader_->yaw())}, dt);
        }
        return;
    }

    // A goal from the subpack (AIDefineGoal*), else its idle actions.
    if (const GoalDef* g = w.director.goalFor(name_, false))
    {
        if (w.debugLog && g->name != goalName_)
            std::cerr << "[goal] " << name_ << " takes up " << g->name << "\n";
        goalName_ = g->name;
        if (pursueGoal(*g, dt, w))
            return;
    }

    // Idle actions, best priority first, each for its min..max time.
    std::vector<const IdleAction*> actions;
    for (const IdleAction& a : setup->idle)
        actions.push_back(&a);
    std::stable_sort(actions.begin(), actions.end(), [](const IdleAction* a, const IdleAction* b) { return a->priority < b->priority; });
    if (actions.empty())
    {
        // Nothing to do: stay at (or walk back to) the spot it started from.
        if (flatDistance(feet(), home_) > 0.6f)
        {
            if (route_.empty())
                planRoute(home_, w);
            followRoute(walk, dt, w);
        }
        else
        {
            route_.clear();
            stand(dt);
            // AILookAtEntity: it faces it; AISetHunterHomeNodeDirection / IdleDirection: that way.
            const auto look = setup->lookAt.empty() || !w.entityAt ? std::nullopt : w.entityAt(setup->lookAt);
            if (look)
                turnTowards(Math::Vector3{look->x - feet().x, 0.0f, look->z - feet().z}, dt);
            else
                turnTowards(setup->homeDirectionSet ? setup->homeDirection : forwardOf(homeYaw_), dt);
        }
        return;
    }
    if (idleActionIndex_ >= actions.size())
        idleActionIndex_ = 0;
    const IdleAction& act = *actions[idleActionIndex_];
    if (act.kind != IdleKind::Patrol)
    {
        if (idleActionLeft_ < 0.0f) // the first action: run it for its own time
            idleActionLeft_ = std::uniform_real_distribution<float32>(act.minTime, std::max(act.minTime, act.maxTime) + 1e-3f)(w.rng);
        idleActionLeft_ -= dt;
        if (idleActionLeft_ <= 0.0f)
        {
            idleActionIndex_ = (idleActionIndex_ + 1) % actions.size();
            const IdleAction& next = *actions[idleActionIndex_];
            idleActionLeft_ = std::uniform_real_distribution<float32>(next.minTime, std::max(next.minTime, next.maxTime) + 1e-3f)(w.rng);
            route_.clear();
            wait_ = 0.0f;
        }
    }

    switch (act.kind)
    {
    case IdleKind::Patrol:
    {
        const std::vector<int32> path = w.nav.path(act.path);
        if (path.empty())
        {
            stand(dt);
            return;
        }
        if (wait_ > 0.0f)
        {
            wait_ -= dt;
            stand(dt);
            return;
        }
        patrolIndex_ %= path.size();
        const int32 node = path[patrolIndex_];
        if (route_.empty())
            planRoute(w.nav.node(node).position, w);
        if (followRoute(walk, dt, w))
        {
            // A stop on this node: wait there (the script gives seconds).
            for (const PatrolStop& s : setup->stops)
                if (w.nav.find(s.node) == node)
                    wait_ = std::max(wait_, s.wait);
            if (wait_ <= 0.0f)
                wait_ = std::uniform_real_distribution<float32>(act.minTime, std::max(act.minTime, act.maxTime) + 1e-3f)(w.rng);
            patrolIndex_ = (patrolIndex_ + 1) % path.size();
            route_.clear();
        }
        return;
    }
    case IdleKind::WanderSearch:
    {
        if (wait_ > 0.0f)
        {
            wait_ -= dt;
            stand(dt);
            return;
        }
        if (route_.empty())
        {
            const int32 homeNode = w.nav.nearest(home_);
            std::vector<int32> near = w.nav.within(homeNode, std::max(3.0f, act.radius));
            if (near.empty())
            {
                stand(dt);
                return;
            }
            const int32 pick = near[std::uniform_int_distribution<std::size_t>(0, near.size() - 1)(w.rng)];
            planRoute(w.nav.node(pick).position, w);
        }
        if (followRoute(walk, dt, w))
        {
            wait_ = std::uniform_real_distribution<float32>(2.0f, 5.0f)(w.rng);
            route_.clear();
        }
        return;
    }
    default:
        stand(dt);
        return;
    }
}

void Hunter::hunt(float32 dt, HunterWorld& w)
{
    // Search: walk between graph nodes near where the player was last noticed, pausing at each.
    if (wait_ > 0.0f)
    {
        wait_ -= dt;
        lookAround(dt);
        return;
    }
    if (route_.empty())
    {
        if (searchNodes_.empty())
            planSearch(w);
        if (searchNodes_.empty())
        {
            lookAround(dt);
            return;
        }
        // The nearest node left next.
        std::size_t best = 0;
        for (std::size_t i = 1; i < searchNodes_.size(); ++i)
            if (flatDistance(feet(), w.nav.node(searchNodes_[i]).position) <
                flatDistance(feet(), w.nav.node(searchNodes_[best]).position))
                best = i;
        const int32 node = searchNodes_[best];
        searchNodes_.erase(searchNodes_.begin() + static_cast<std::ptrdiff_t>(best));
        if (w.searched)
            (*w.searched)[node] = w.time;
        planRoute(w.nav.node(node).position, w);
    }
    if (followRoute(walkSpeed(), dt, w))
    {
        wait_ = std::uniform_real_distribution<float32>(1.5f, 4.0f)(w.rng);
        route_.clear();
    }
}

void Hunter::planSearch(HunterWorld& w)
{
    searchNodes_ = planSearchNodes(w.nav, lastKnown_, w.tuning.alert, w.nodeLight, w.searched, w.time, w.rng);
}

SenseLevel Hunter::seeBody(const Math::Vector3& at, float32 light, const HunterWorld& w) const
{
    // 0x5192c0: its usual sight check (0x42b170) with the vision range set to 18 m (0x683600).
    VisionData vision = w.tuning.vision;
    vision.range = kBodySightRange;
    const Math::Vector3 eye = feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f};
    const Math::Vector3 to = at + Math::Vector3{0.0f, 0.3f, 0.0f} - eye;
    SightInput in;
    in.distance = to.length();
    const float32 flat = std::sqrt(to.x * to.x + to.z * to.z);
    const Math::Vector3 fwd = forwardOf(yaw_);
    in.horizontalAngle = flat > 1e-3f ? std::acos(std::clamp((fwd.x * to.x + fwd.z * to.z) / flat, -1.0f, 1.0f)) : 0.0f;
    in.verticalAngle = std::atan2(std::fabs(to.y), std::max(flat, 1e-3f));
    in.gait = Gait::Stand;
    in.hardDifficulty = w.hardDifficulty;
    in.light = in.faceLight = light;
    if (in.distance >= vision.range || !inViewFrustum(in, vision))
        return kNoLevel;
    // VISIBLE: the share of the lying body (ours: three points along its height off the floor) in
    // clear view.
    static constexpr float32 kLying[] = {0.15f, 0.3f, 0.45f};
    int32 clear = 0;
    for (float32 h : kLying)
        clear += w.field.lineOfSight(eye, at + Math::Vector3{0.0f, h, 0.0f}) ? 1 : 0;
    if (clear == 0)
        return kNoLevel;
    in.visible = static_cast<float32>(clear) / static_cast<float32>(std::size(kLying));
    return sightLevel(in, vision);
}

void Hunter::checkBody(const Math::Vector3& at, bool finder, HunterWorld& w, Errand kind)
{
    // Busy with him, or not on its feet: it leaves it (the coordinator does not take a squad off
    // its kill for a body, 0x50cb80).
    if (awareness_.action >= AiAction::Chase || state_ == HunterState::Dead || state_ == HunterState::Down ||
        state_ == HunterState::Held || state_ == HunterState::Executed)
        return;
    awareness_.action = AiAction::Investigate;
    const SenseLevel raise = w.tuning.alert.setAlert[static_cast<std::size_t>(AiAction::Investigate)];
    if (raise != kNoLevel && raise >= awareness_.alert)
    {
        awareness_.alert = raise;
        awareness_.alertTime = 0.0f;
    }
    awareness_.unsustained = 0.0f;
    lastKnown_ = at;
    bodyAt_ = at;
    bodyCheck_ = true;
    bodyLooked_ = false;
    errand_ = kind;
    bodyCheckLeft_ = kBodyCheckGiveUp;
    route_.clear();
    routeIndex_ = 0;
    searchNodes_.clear();
    wait_ = 0.0f;
    if (finder)
    {
        // Wait for Backup's first tick (0x50b8c0): line 0x29, ShoutForAssistance.
        speechBusy_ = 0.0f;
        sayLine(0x29, w);
    }
}

void Hunter::investigate(float32 dt, HunterWorld& w)
{
    if (bodyCheck_)
    {
        // Check Body (coordinator mode 5, 0x50d930): to within 1 m of it (0x793ae0), a look at it
        // (Idle_Body_Investigate01 / 02), then the search goes on from there (0x50b6b0 -> 0x50beb0).
        const Math::Vector3 to{bodyAt_.x - feet().x, 0.0f, bodyAt_.z - feet().z};
        if (errand_ == Errand::Backup)
        {
            // Answering a backup request (0x52db60 -> 0x52b310): a goto (action 0x11) to the
            // finder's node and place, walking (the request's kind 0 / 1: 0x52cec0), within 3 m
            // (0x7980ec); there it stands by until the squad is sent to the body.
            if (flatDistance(feet(), bodyAt_) > 3.0f)
            {
                if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
                {
                    planRoute(bodyAt_, w);
                    replanTimer_ = 2.0f;
                }
                followRoute(walkSpeed(), dt, w);
                return;
            }
            if (!bodyLooked_)
            {
                // There: its line 0x38 (0x52b310; most voices have none).
                bodyLooked_ = true;
                speechBusy_ = 0.0f;
                sayLine(0x38, w);
            }
            stand(dt);
            turnTowards(to, dt);
            return;
        }
        if (errand_ == Errand::Wait)
        {
            // Wait for Backup (coordinator mode 7): it stands by, facing the body, until the
            // squad is sent to check it (LevelRuntime::scanBodies).
            stand(dt);
            turnTowards(to, dt);
            return;
        }
        if (bodyLooked_ || (bodyCheckLeft_ -= dt) <= 0.0f)
        {
            bodyCheck_ = false;
            awareness_.action = AiAction::Hunt;
            lastKnown_ = bodyAt_;
            return;
        }
        const float32 reach = errand_ == Errand::Head ? kHeadCheckReach : kBodyCheckReach;
        if (flatDistance(feet(), bodyAt_) > reach)
        {
            if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
            {
                planRoute(bodyAt_, w);
                replanTimer_ = 2.0f;
            }
            followRoute(w.tuning.alert.investigateRuns ? runSpeed() : walkSpeed(), dt, w);
            return;
        }
        stand(dt);
        turnTowards(to, dt);
        bodyLooked_ = true;
        if (errand_ == Errand::Head)
        {
            // There (Check Head, 0x50ad00): its line 0x29, ShoutForAssistance (0x500960).
            speechBusy_ = 0.0f;
            sayLine(0x29, w);
            if (w.debugLog)
                std::cerr << "[body] " << name_ << " looks at the head" << std::endl;
            return;
        }
        once(std::uniform_int_distribution<int>(0, 1)(w.rng) ? "Idle_Body_Investigate02" : "Idle_Body_Investigate01");
        if (w.debugLog)
            std::cerr << "[body] " << name_ << " looks at the body (" << model_->currentClip() << ", " << oneShot_ << " s)" << std::endl;
        return;
    }

    // Go to what it noticed (investigate_action: run), then look around there.
    if (flatDistance(feet(), lastKnown_) > 1.0f)
    {
        if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
        {
            planRoute(lastKnown_, w);
            replanTimer_ = 2.0f;
        }
        followRoute(w.tuning.alert.investigateRuns ? runSpeed() : walkSpeed(), dt, w);
    }
    else
        lookAround(dt);
}

bool Hunter::pursueGoal(const GoalDef& g, float32 dt, HunterWorld& w)
{
    const float32 pace = g.run ? runSpeed() : walkSpeed();
    // To `target` (re-routed now and then: entities move); true once it is within `arrive`.
    auto goTo = [&](const Math::Vector3& target, float32 arrive, float32 speed) {
        if (flatDistance(feet(), target) <= arrive)
        {
            route_.clear();
            stand(dt);
            return true;
        }
        if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
        {
            planRoute(target, w);
            replanTimer_ = kReplan;
        }
        followRoute(speed, dt, w);
        return false;
    };
    auto fidget = [&] {
        if ((fidget_ -= dt) <= 0.0f)
        {
            std::uniform_int_distribution<int> pick(1, 5);
            once("Idle_Action_Idle_0" + std::to_string(pick(w.rng)));
            fidget_ = std::uniform_real_distribution<float32>(8.0f, 16.0f)(w.rng);
        }
    };
    const auto at = [&](const std::string& who) { return w.entityAt ? w.entityAt(who) : std::nullopt; };
    switch (g.type)
    {
    case GoalType::GotoNodeIdle:
    case GoalType::GuardDirection:
    case GoalType::GuardLookAtEntity:
    case GoalType::ShootVector:
    case GoalType::MeleeAttackVector:
    {
        // At its node: idle there, guard a direction, watch an entity, or fire along a direction
        // (covering fire: a shot that finds him only near where it lands).
        const int32 node = w.nav.find(g.node);
        if (node < 0)
            return false;
        if (!goTo(w.nav.node(node).position, std::max(0.5f, w.nav.node(node).radius + 0.2f), pace))
            return true;
        if (g.type == GoalType::GuardDirection)
            turnTowards(g.direction, dt);
        else if (g.type == GoalType::MeleeAttackVector)
            turnTowards(g.point - feet(), dt);
        else if (g.type == GoalType::GuardLookAtEntity)
        {
            if (const auto p = at(g.entity))
                turnTowards(*p - feet(), dt);
        }
        else if (g.type == GoalType::ShootVector)
        {
            turnTowards(g.direction, dt);
            if (gun_)
            {
                const Math::Vector3 dir = g.direction.length() > 1e-3f ? g.direction.normalized() : forwardOf(yaw_);
                const Math::Vector3 place = feet() + Math::Vector3{dir.x, 0.0f, dir.z} * 10.0f;
                aimAndFire(dt, w, flatDistance(feet(), w.player.feet), nullptr, &place);
            }
            return true;
        }
        fidget();
        return true;
    }
    case GoalType::GotoEntity:
    {
        const auto p = at(g.entity);
        if (!p)
            return false;
        if (goTo(*p, 1.5f, pace))
            turnTowards(*p - feet(), dt);
        return true;
    }
    case GoalType::GotoVector:
        goTo(g.point, 0.5f, pace);
        return true;
    case GoalType::BeBuddy:
        return beBuddy(g, dt, w);
    case GoalType::SeekCoverBackwards:
    case GoalType::Hide:
    {
        // Out of his sight: a hidden point near it (Game/ai/gun_combat.h) - the one farthest from
        // him for SeekCoverBackwards (CoverSeek mode 1), the nearest for Hide (mode 2) - crouched
        // there, facing him; looked for again every 2 s while the one it holds is seen (ours).
        if (goalCover_ < 0 || (w.time - goalCoverAt_ > 2.0f && !coverHidden(goalCover_, w)))
        {
            goalCoverAt_ = w.time;
            auto seen = [&](int32 n) { return !coverHidden(n, w); };
            auto none = [](int32) { return false; };
            Math::Vector3 away{feet().x - w.player.feet.x, 0.0f, feet().z - w.player.feet.z};
            const float32 len = away.length();
            away = len > 1e-3f ? away * (1.0f / len) : forwardOf(yaw_);
            const int32 n = g.type == GoalType::SeekCoverBackwards
                                ? gun::findCoverAlong(w.nav, feet(), w.player.feet, away, 0.0f, 0.0f, 1e9f, seen, none)
                                : gun::findCover(w.nav, feet(), w.player.feet, 0.0f, 1e9f, seen, none);
            if (n >= 0)
                goalCover_ = n;
        }
        if (goalCover_ < 0)
            return false;
        if (goTo(w.nav.node(goalCover_).position, gun::kCoverArrive, runSpeed()))
        {
            turnTowards(Math::Vector3{w.player.feet.x - feet().x, 0.0f, w.player.feet.z - feet().z}, dt);
            if (model_->clipDuration("Sneak_Stand") > 0.0f)
                poseClip_ = "Sneak_Stand";
        }
        return true;
    }
    case GoalType::OrbitEntity:
    {
        // CPedActOrbit (0x577de0): it keeps between inner + 0.5 and outer - 0.5 m of it
        // (0x79c878). Beyond: it walks at it at three quarters of its pace (state 2); inside:
        // it backs away to the inner radius, or with the flag aims at it (state 1, 0x562060);
        // between: it holds, facing it.
        const auto p = at(g.entity);
        if (!p)
            return false;
        const Math::Vector3 to{p->x - feet().x, 0.0f, p->z - feet().z};
        const float32 d = to.length();
        if (d > g.outer - 0.5f)
        {
            goTo(*p, g.outer - 0.5f, runSpeed() * 0.75f);
            return true;
        }
        route_.clear();
        turnTowards(to, dt);
        if (d < g.inner + 0.5f)
        {
            const bool player = w.entityAt && w.entityAt("player") && flatDistance(*w.entityAt("player"), *p) < 0.1f;
            if (g.flag && gun_ && player && sight_ != kNoLevel)
            {
                stand(dt);
                aimAndFire(dt, w, d);
                return true;
            }
            if (d > 1e-3f)
            {
                const float32 v = walkSpeed();
                velocity_.x = -to.x / d * v;
                velocity_.z = -to.z / d * v;
                speed_ = v;
                moveClip_ = "Walk_Bkw_Aware";
                return true;
            }
        }
        stand(dt);
        return true;
    }
    case GoalType::HuntEnemy:
        break;
    }
    return false;
}

void Hunter::chase(float32 dt, HunterWorld& w)
{
    const bool seen = sight_ != kNoLevel;
    const Math::Vector3 target = seen ? w.player.feet : lastKnown_;
    const float32 dist = flatDistance(feet(), target);
    // Close and in sight with nothing in the way at knee height: straight at him.
    const Math::Vector3 knee{0.0f, 0.5f, 0.0f};
    if (seen && dist < 7.0f && w.field.lineOfSight(feet() + knee, target + knee))
    {
        route_.clear();
        if (dist > 1.0f)
            steerTo(target, runSpeed(), 1.0f, dt);
        else
        {
            stand(dt);
            turnTowards(target - feet(), dt);
        }
        return;
    }
    if (!seen && dist < 1.0f)
    {
        lookAround(dt);
        return;
    }
    if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
    {
        planRoute(target, w);
        replanTimer_ = kReplan;
    }
    followRoute(runSpeed(), dt, w);
}

void Hunter::celebrate(float32 dt, HunterWorld& w)
{
    stand(dt);
    auto wait = [&](float32 lo, float32 hi, bool shortIsNone) {
        float32 r = randomIn(w.rng, lo, hi);
        if (shortIsNone && r < 3.0f)
            r = 0.0f;
        celebratePhase_ = Celebrate::Wait;
        celebrateUntil_ = w.time + r;
    };
    auto startRound = [&] {
        // 0x56e040: on rand(0, 10) >= 3 a round of rand(4, 7) s, else none.
        celebratePhase_ = Celebrate::Play;
        celebrateUntil_ = randomIn(w.rng, 0.0f, 10.0f) >= 3.0f ? w.time + randomIn(w.rng, 4.0f, 7.0f) : 0.0f;
    };
    switch (celebratePhase_)
    {
    case Celebrate::None:
        wait(0.0f, 7.0f, true); // 0x56e450
        return;
    case Celebrate::Wait:
        if (w.time >= celebrateUntil_)
            startRound();
        return;
    case Celebrate::Play:
        if (celebrateUntil_ != 0.0f)
        {
            if (w.time < celebrateUntil_)
            {
                const std::string clip = "Idle_Celebrate_0" + std::to_string(celebrateClip_ + 1);
                if (poseClip_ != clip)
                {
                    poseClip_ = clip;
                    if (w.debugLog)
                        std::cerr << "[melee] " << name_ << " celebrates (" << clip << ")" << std::endl;
                }
                return;
            }
            // The round is over: the next clip, rand(4, 7) s more, three rounds in all.
            ++celebrateRounds_;
            celebrateClip_ = std::uniform_int_distribution<int32>(0, 3)(w.rng);
            celebrateUntil_ = w.time + randomIn(w.rng, 4.0f, 7.0f);
            poseClip_.clear();
            if (celebrateRounds_ < 3)
                return;
            celebrateRounds_ = 0;
        }
        poseClip_.clear();
        wait(4.0f, 10.0f, false);
        return;
    }
}

void Hunter::combat(float32 dt, HunterWorld& w)
{
    const MeleeWeapon& weapon = melee_ ? *melee_ : w.tuning.fists;
    const float32 reach = weapon.quick.size + kPlayerRadius; // the shot sphere touches his body
    const float32 dist = flatDistance(feet(), w.player.feet);
    const Math::Vector3 to{w.player.feet.x - feet().x, 0.0f, w.player.feet.z - feet().z};
    const float32 facing =
        dist > 1e-3f ? Math::degrees(std::acos(std::clamp(forwardOf(yaw_).dot(to * (1.0f / dist)), -1.0f, 1.0f))) : 0.0f;
    if (!w.player.alive || w.player.down)
        meleeMove_ = MeleeMove::Close;
    if (w.time - combatAt_ > 1.0f)
    {
        // A fight begun afresh: the approach (state 2), its cycle from now.
        meleeMove_ = MeleeMove::Close;
        engageAt_ = -1.0f;
    }
    if (!w.player.alive)
    {
        dropMelee();
        celebrate(dt, w);
        return;
    }
    celebratePhase_ = Celebrate::None;
    if (w.player.down)
    {
        // He is on the floor: it strikes down at him.
        dropMelee();
        if (dist <= reach * 0.85f)
        {
            stand(dt);
            turnTowards(to, dt);
            if (swingPause_ <= 0.0f)
                startGroundAttack(w);
            return;
        }
        chase(dt, w);
        return;
    }
    // Badly hurt (SetHunterHideHealth): it breaks off and hides, fighting back only when cornered
    // with him in reach. It starts to (0x587f40) only with him within 60 degrees of its facing
    // (0x5885f0) or something between them (0x56aa00, the line from it to him), and only with a
    // hide health at all: at or under it, or under half its health while he holds a gun.
    const bool inHide = hidePhase_ != HidePhase::Choose || evadePhase_ != EvadePhase::None;
    const Math::Vector3 eyeUp{0.0f, 1.6f, 0.0f};
    const int32 healthPct = static_cast<int32>(health_ / std::max(fullHealth_, 1.0f) * 100.0f); // (0x41d120)
    if (hideHealth_ > 0 && (healthPct <= hideHealth_ || (w.player.gun && healthPct < 50)) &&
        !(evadePhase_ == EvadePhase::Cornered && dist <= reach) &&
        (inHide || facing < 60.0f || !w.field.lineOfSight(feet() + eyeUp, w.player.feet + eyeUp)))
    {
        if (evadePhase_ == EvadePhase::None && hidePhase_ == HidePhase::Choose && w.debugLog)
            std::cerr << "[hide] " << name_ << " hides at " << health_ << " hp\n";
        dropMelee();
        hide(dt, w);
        return;
    }
    if (w.player.hidden && sight_ == kNoLevel)
    {
        blindMelee(dt, w);
        return;
    }
    blindMelee_ = false;
    meleeThink(w, weapon, dist, facing);
    if (state_ != HunterState::Normal)
        return; // a blow started
    engagedDist_ = dist;
    combatAt_ = w.time;
    if (meleeAction_ == MeleeResponse::Wait && !blocking_ && positionMelee(dt, w, dist, to))
        return;
    if (dist <= reach * 0.85f || blocking_)
    {
        stand(dt);
        turnTowards(to, dt);
        return;
    }
    chase(dt, w);
}

bool Hunter::positionMelee(float32 dt, HunterWorld& w, float32 dist, const Math::Vector3& to)
{
    const float32 now = w.time;
    // 0x56b570 (every 0.06 s at most, 0x79c574): his blow is on its way and it is within 2.8 m
    // (3.5 m on the move; 3.0 / 3.7 once his blow is past its landing), and its record's row for
    // "his blow is coming" (situation 4) comes up block: it repositions. Of three ways (0x56bfd1):
    // hold back 0.5..0.7 s (0x79c5e0 / 0x79c5dc) and close in again (0x56f130); hold back
    // 0.3..1.5 s and back away (0x56e990); back away at once.
    if (meleeMove_ == MeleeMove::Close && w.player.swinging && now >= repositionAt_)
    {
        repositionAt_ = now + 0.06f;
        const bool moving = speed_ > 0.05f;
        const bool early = w.player.swingTime < w.player.swingLands;
        const float32 limit = early ? (moving ? 3.5f : 2.8f) : (moving ? 3.7f : 3.0f);
        if (dist < limit && pickMeleeResponse(Game::meleeTraits(meleeTraits_, w.hardDifficulty), 4, w.rng) == MeleeResponse::Block)
        {
            const int32 way = std::uniform_int_distribution<int32>(0, 2)(w.rng);
            auto range = [&](float32 a, float32 b) { return std::uniform_real_distribution<float32>(a, b)(w.rng); };
            if (way == 2)
                meleeMove_ = MeleeMove::Back;
            else
            {
                meleeMove_ = MeleeMove::Pause;
                pauseUntil_ = now + (way == 0 ? range(0.5f, 0.7f) : range(0.3f, 1.5f));
                afterPause_ = way == 0 ? MeleeMove::Close : MeleeMove::Back;
            }
            // Backing away: to 2.2..3.2 m (0x79c688 / 0x79c684), at three quarters of its pace 70
            // times in 100, else half (0x79c68c).
            backTo_ = range(2.2f, 3.2f);
            backSpeed_ = range(0.0f, 100.0f) <= 70.0f ? 0.75f : 0.5f;
            if (w.debugLog)
                std::cerr << "[melee] " << name_ << " repositions: " << (way == 0 ? "holds back, closes" : way == 1 ? "holds back, backs off" : "backs off")
                          << " (" << backTo_ << " m)\n";
        }
    }
    // At most two at him (state 1: 0x588420 against the count at 0x79c504): the rest wait on a ring
    // (state 0x13, 0x56ec60: 2.2 .. 3.5 m, 0x79c688 / 0x79c57c) until one of the two drops out.
    if (meleeMove_ == MeleeMove::Close && dist > 2.5f && w.meleeEngaged && w.meleeEngaged(*this) >= 2)
    {
        meleeMove_ = MeleeMove::Ring;
        if (w.debugLog)
            std::cerr << "[melee] " << name_ << " waits its turn (" << dist << " m)\n";
    }
    const bool running = w.player.gait >= 2 && !w.player.sneaking;
    const Math::Vector3 dir = dist > 1e-3f ? to * (1.0f / dist) : forwardOf(yaw_);
    const Math::Vector3 right{-dir.z, 0.0f, dir.x};
    auto moveAlong = [&](const Math::Vector3& v, float32 speed, const char* clip) {
        velocity_.x = v.x * speed;
        velocity_.z = v.z * speed;
        speed_ = speed;
        moveClip_ = clip;
    };
    auto range = [&](float32 a, float32 b) { return std::uniform_real_distribution<float32>(a, b)(w.rng); };
    // His bearing from its facing (ped +0x698 +0x184), in degrees.
    const float32 bearing = Math::degrees(std::acos(std::clamp(forwardOf(yaw_).dot(dir), -1.0f, 1.0f)));
    switch (meleeMove_)
    {
    case MeleeMove::Close:
    {
        // The engage cycle (state 2, 0x56ccb0; its timer rand(2, 5) s, 0x79c5c4 / 0x79c570): with
        // him within 60 degrees of its facing (0x5885f0) and the time up or him beyond 5 m, a
        // hunter whose melee fight may take cover (+0x24: the AI's own fights, 0x557110(.., 1))
        // seeks cover when he holds a gun aimed at it (0x588150: his gun mode, 0x587c90): the
        // hidden point farthest from him within 50 m (CoverSeek mode 1, cosine -1, 0x56e540). With
        // him off to the side the time waits (now + 3 s, 0x79c580); with no cover it tries again
        // in rand(5, 15) s (0x79c5ec). (Its hide test, 0x587f40, runs every frame here.)
        if (engageAt_ < 0.0f)
            engageAt_ = now + range(2.0f, 5.0f);
        if (bearing >= 60.0f)
        {
            if (engageAt_ < now)
                engageAt_ = now + 3.0f;
            return false;
        }
        if ((engageAt_ < now || dist > 5.0f) && w.player.gun && aimedAt(w))
        {
            Math::Vector3 away{feet().x - w.player.feet.x, 0.0f, feet().z - w.player.feet.z};
            away = away.length() > 1e-3f ? away.normalized() : forwardOf(yaw_);
            const int32 node = gun::findCoverAlong(
                w.nav, feet(), w.player.feet, away, -1.0f, 0.0f, 50.0f, [&](int32 n) { return !coverHidden(n, w); },
                [&](int32 n) { return gunRoster().taken(n, name_); });
            if (node >= 0)
            {
                meleeMove_ = MeleeMove::Cover;
                meleeCover_ = node;
                meleeCoverStuck_ = 0.0f;
                meleeCoverFrom_ = feet();
                planRoute(w.nav.node(node).position, w);
                if (w.debugLog)
                    std::cerr << "[melee] " << name_ << " takes cover from his gun at node " << node << std::endl;
                return true;
            }
            engageAt_ = now + range(5.0f, 15.0f);
            if (w.debugLog)
                std::cerr << "[melee] " << name_ << " finds no cover from his gun" << std::endl;
        }
        return false; // (closing in: the usual approach)
    }
    case MeleeMove::Cover:
    {
        // State 3 (0x56ce30): to the point, running while he is within 3.5 m or in sight (the
        // CoverSeek's pace, 0x5819f3); there, state 4. Getting nowhere: the approach again, its
        // cycle in rand(5, 15) s.
        const Math::Vector3 point = w.nav.node(meleeCover_).position;
        if (flatDistance(feet(), point) <= gun::kCoverArrive)
        {
            meleeMove_ = MeleeMove::InCover;
            meleeCoverFaced_ = false;
            stand(dt);
            return true;
        }
        if (route_.empty())
            planRoute(point, w);
        followRoute(dist < 3.5f || w.field.lineOfSight(feet() + Math::Vector3{0.0f, 1.6f, 0.0f}, w.player.feet + Math::Vector3{0.0f, 1.6f, 0.0f})
                        ? runSpeed()
                        : walkSpeed(),
                    dt, w);
        if (flatDistance(feet(), meleeCoverFrom_) > 0.5f)
        {
            meleeCoverFrom_ = feet();
            meleeCoverStuck_ = 0.0f;
        }
        else if ((meleeCoverStuck_ += dt) > 2.0f)
        {
            meleeMove_ = MeleeMove::Close;
            engageAt_ = now + range(5.0f, 15.0f);
        }
        return true;
    }
    case MeleeMove::InCover:
    {
        // State 4 (0x56cef0): turned to face him there (0x56ce30), still, until he comes round it
        // (0x5881a0): beyond 40 degrees of its facing within 4 m, 60 within 6 m, 80 within 10 m,
        // 100 anywhere (0x79efac..0x79efc0); then the approach again (rand(2, 5) s).
        stand(dt);
        turnTowards(to, dt);
        meleeCoverFaced_ = meleeCoverFaced_ || bearing < 10.0f;
        const bool flanked = meleeCoverFaced_ && ((dist < 4.0f && bearing > 40.0f) || (dist < 6.0f && bearing > 60.0f) ||
                                                  (dist < 10.0f && bearing > 80.0f) || bearing > 100.0f);
        if (flanked)
        {
            meleeMove_ = MeleeMove::Close;
            engageAt_ = now + range(2.0f, 5.0f);
        }
        return true;
    }
    case MeleeMove::Ring:
        turnTowards(to, dt);
        if (!w.meleeEngaged || w.meleeEngaged(*this) < 2)
        {
            meleeMove_ = MeleeMove::Close;
            return false;
        }
        // CPedActFindSpace (0x5731d0 / 0x573290, started by 0x56ec60 with 2.4 / 2.2 / 3.5 m): out
        // to 2.2 m, in to 3.5 m, and within that a spot no one else stands within 2.4 m of
        // (0x563120; ours: a sidestep along the ring away from the nearest).
        if (dist < 2.2f)
            moveAlong(dir * -1.0f, walkSpeed(), "Walk_Bkw_Aware");
        else if (dist > 3.5f)
            steerTo(w.player.feet, walkSpeed(), 3.5f, dt);
        else if (const auto other = w.crowdedBy ? w.crowdedBy(*this, 2.4f) : std::nullopt)
        {
            const float32 side = right.dot(*other - feet()) > 0.0f ? -1.0f : 1.0f;
            moveAlong(right * side, walkSpeed() * 0.5f, side > 0.0f ? "Walk_Right_Aware" : "Walk_Left_Aware");
        }
        else
            stand(dt);
        return true;
    case MeleeMove::Standoff:
        // State 0xe (0x56eac0 / 0x56d310): within 4 m of him (walking at him beyond), facing him,
        // for 1.5 .. 3.5 s; then it closes in (0x56f130). Beyond 7 m it gives up the stand-off.
        if (now >= moveUntil_ || dist > 7.0f)
        {
            meleeMove_ = MeleeMove::Close;
            return false;
        }
        turnTowards(to, dt);
        if (dist > 4.0f)
            steerTo(w.player.feet, walkSpeed(), 4.0f, dt);
        else
            stand(dt);
        return true;
    case MeleeMove::Circle:
        // State 0x10 (0x56f050 / 0x56db20): sideways round him at half pace, turning about every
        // 1.5 .. 2.5 s; inside 1.8 m it holds a second and closes in, beyond 4.5 m (or once he
        // runs) it closes in, and when its time is out it decides afresh (state 0xf).
        turnTowards(to, dt);
        if (dist < 1.8f)
        {
            meleeMove_ = MeleeMove::Pause;
            pauseUntil_ = now + 1.0f;
            afterPause_ = MeleeMove::Close;
            stand(dt);
            return true;
        }
        if (dist > 4.5f || running)
        {
            meleeMove_ = MeleeMove::Close;
            return false;
        }
        if (now >= moveUntil_)
        {
            decideFootwork(w, dist);
            if (meleeMove_ == MeleeMove::Close)
                return false;
            stand(dt);
            return true;
        }
        if (now >= flipAt_)
        {
            circleSide_ = -circleSide_;
            flipAt_ = now + std::uniform_real_distribution<float32>(1.5f, 2.5f)(w.rng);
        }
        moveAlong(right * circleSide_, walkSpeed(), circleSide_ > 0.0f ? "Walk_Right_Aware" : "Walk_Left_Aware");
        return true;
    case MeleeMove::Pause:
        stand(dt);
        turnTowards(to, dt);
        if (now >= pauseUntil_)
            meleeMove_ = afterPause_;
        return true;
    case MeleeMove::Back:
    {
        // Facing him, it walks backwards until it is out at the distance it chose (state 7),
        // then closes in again (0x56d1f0: 0x56f130 / a pause first; ours: straight away).
        turnTowards(to, dt);
        if (dist >= backTo_ || dist < 1e-3f)
        {
            // 0x56d1f0: beyond 1.8 m, its record's +0x1bc in 100 it decides afresh (state 0xf).
            meleeMove_ = MeleeMove::Close;
            const MeleeFootwork& f = meleeFootwork(meleeTraits_, w.hardDifficulty);
            if (dist > 1.8f && std::uniform_real_distribution<float32>(0.0f, 100.0f)(w.rng) < f.redecide)
                decideFootwork(w, dist);
            stand(dt);
            return true;
        }
        const Math::Vector3 away = to * (-1.0f / dist);
        const float32 v = walkSpeed() * backSpeed_ * 2.0f; // (0.75 / 0.5 of its run: ours as twice its walk)
        velocity_.x = away.x * v;
        velocity_.z = away.z * v;
        speed_ = v;
        moveClip_ = "Walk_Bkw_Aware";
        return true;
    }
    }
    return false;
}

void Hunter::decideFootwork(HunterWorld& w, float32 dist)
{
    // The free state 0xf (0x56d840): one of its record's four moves by weight (0x5a9ec0), each only
    // where it fits: a stand-off (0) beyond 1.8 m, a circle (1) at 1.8 .. 4.5 m, a hold (2), closing
    // in (3); neither stands off nor circles while he runs (0x588380 / 0x5884d0).
    const int32 pick = pickFootwork(meleeFootwork(meleeTraits_, w.hardDifficulty), w.rng);
    const bool running = w.player.gait >= 2 && !w.player.sneaking;
    auto range = [&](float32 a, float32 b) { return std::uniform_real_distribution<float32>(a, b)(w.rng); };
    const float32 now = w.time;
    meleeMove_ = MeleeMove::Close;
    if (pick == 0 && !running && dist > 1.8f)
    {
        meleeMove_ = MeleeMove::Standoff;
        moveUntil_ = now + range(1.5f, 3.5f);
    }
    else if (pick == 1 && !running && dist > 1.8f && dist < 4.5f)
    {
        meleeMove_ = MeleeMove::Circle;
        circleSide_ = std::uniform_int_distribution<int32>(0, 1)(w.rng) ? 1.0f : -1.0f; // (0x588590)
        moveUntil_ = now + range(1.5f, 4.0f);
        flipAt_ = now + range(1.5f, 2.5f);
    }
    else if (pick == 2)
    {
        meleeMove_ = MeleeMove::Pause;
        pauseUntil_ = now + (range(0.0f, 1.0f) <= 0.5f ? 0.0f : range(0.3f, 1.0f));
        afterPause_ = MeleeMove::Close;
    }
    if (w.debugLog && meleeMove_ != MeleeMove::Close)
        std::cerr << "[melee] " << name_
                  << (meleeMove_ == MeleeMove::Standoff ? " stands off" : meleeMove_ == MeleeMove::Circle ? " circles" : " holds")
                  << " (" << dist << " m)\n";
}

void Hunter::meleeThink(HunterWorld& w, const MeleeWeapon& weapon, float32 dist, float32 facing)
{
    const MeleeTraits& t = Game::meleeTraits(meleeTraits_, w.hardDifficulty);
    const float32 now = w.time;
    if (blocking_ && now >= blockUntil_)
        finishMeleeAction(w);
    if (meleeAction_ == MeleeResponse::Quick || meleeAction_ == MeleeResponse::Hard)
    {
        // Its blow waits for an opening; he got away (beyond 2.5 m) and it lets it go (ours).
        if (dist >= 2.5f)
            dropMelee();
        else
            tryBlow(w, weapon, dist, facing);
        return;
    }
    if (meleeAction_ != MeleeResponse::Wait)
        return; // blocking

    MeleeView v;
    v.distance = dist;
    v.facing = facing;
    v.reach = weapon.quick.size + 0.3f; // 0x56a1f0: its blow's SIZE + 0.3
    v.playerBlocking = w.player.blocking;
    v.playerGrabbing = w.player.grabbing;
    v.playerSwinging = w.player.swinging;
    v.swingHard = w.player.swingHard;
    v.swingTime = w.player.swingTime;
    v.swingLands = w.player.swingLands;
    v.hardLands = w.player.hardLands;
    v.selfBlocking = blocking_;
    v.selfBlockAge = w.time - blockFrom_;
    v.selfHurt = state_ == HunterState::Hurt || state_ == HunterState::Stunned;
    v.selfQuickLands = weapon.quick.strikeAt;
    v.selfGrabTime = weapon.grabTime;
    const int32 situation = classifyMeleeSituation(v);
    // The phase of his attack (0x566fa0): a change makes it decide at once.
    const int32 phase = w.player.blocking  ? 7
                        : w.player.grabbing ? 8
                        : !w.player.swinging ? 0
                                             : (w.player.swingHard ? 3 : 1) + (w.player.swingTime >= w.player.swingLands ? 1 : 0);
    // Else it thinks again 0.2 s after it last chose to wait (0x5a9a60).
    if (situation == lastSituation_ && phase == lastPhase_ && now <= waitedAt_ + 0.2f)
        return;
    lastSituation_ = situation;
    lastPhase_ = phase;
    const bool inReach = dist < v.reach;
    const bool noQuick = !((situation >= 8 && situation <= 10) || (inReach && now >= quickReady_));
    const bool noHard = !(inReach && now >= hardReady_);
    // A grab (0x569650): he stands, in front of it (within 30 deg), within reach of its arms.
    const bool canGrab = w.grabPlayer && dist < 1.6f && facing < 30.0f && std::fabs(w.player.feet.y - feet().y) < 0.5f;
    const bool noGrapple = now < grappleReady_ || !canGrab;
    MeleeRepeats scratch;
    MeleeRepeats& repeats =
        w.meleeRepeats ? (*w.meleeRepeats)[static_cast<std::size_t>(std::clamp(meleeTraits_, 0, kMeleeTraitCount - 1))] : scratch;
    const MeleeResponse response = chooseMeleeResponse(t, repeats, situation, noQuick, noHard, noGrapple, w.rng);
    if (w.debugLog && response != MeleeResponse::Wait)
    {
        static const char* kResponse[] = {"wait", "quick", "hard", "grapple", "block", "taunt"};
        std::cerr << "[melee] " << name_ << " (" << t.name << ") t " << now << " situation " << situation << " -> "
                  << kResponse[static_cast<int32>(response)] << "\n";
    }
    switch (response)
    {
    case MeleeResponse::Wait:
        waitedAt_ = now;
        break;
    case MeleeResponse::Quick:
        meleeAction_ = MeleeResponse::Quick;
        combo_ = chooseMeleeCombo(t, w.rng);
        quickStep_ = 0;
        tryBlow(w, weapon, dist, facing);
        break;
    case MeleeResponse::Hard:
        meleeAction_ = MeleeResponse::Hard;
        combo_ = {true};
        quickStep_ = 0;
        tryBlow(w, weapon, dist, facing);
        break;
    case MeleeResponse::Grapple:
        grappleReady_ = now + t.grapplePause;
        (void)w.grabPlayer(*this);
        break;
    case MeleeResponse::Block:
    {
        // Only against a blow he aims at it (0x569320): mid-swing, it within his reach + 0.5, he
        // facing it (within 45 deg).
        const Math::Vector3 toIt{feet().x - w.player.feet.x, 0.0f, feet().z - w.player.feet.z};
        const bool atIt = dist > 1e-3f && forwardOf(w.player.yaw).dot(toIt * (1.0f / dist)) > 0.707f;
        if (w.player.swinging && dist < w.player.reach + 0.5f && atIt)
            startBlock(t, weapon, w);
        break;
    }
    case MeleeResponse::Taunt:
        say("POSITIVE_TAUNT_COMBAT", w);
        waitedAt_ = now;
        break;
    }
}

bool Hunter::tryBlow(HunterWorld& w, const MeleeWeapon& weapon, float32 dist, float32 facing)
{
    // 0x569bc0: in reach, facing him (within 10 deg), and nobody else mid-swing at him.
    if (combo_.empty() || dist >= weapon.quick.size + 0.3f || facing >= 10.0f)
        return false;
    if (w.otherSwinging && w.otherSwinging(*this))
        return false;
    startSwing(w);
    return true;
}

void Hunter::startBlock(const MeleeTraits& t, const MeleeWeapon& weapon, HunterWorld& w)
{
    blocking_ = true;
    blockFrom_ = w.time;
    blockUntil_ = w.time + t.blockHold;
    meleeAction_ = MeleeResponse::Block;
    std::string clip = weapon.clipStem + "_Block_T";
    if (model_->clipDuration(clip) <= 0.0f)
        clip = "Fist_Block_T";
    if (guardLeft_ <= 0.0f)
        model_->playOverlay(clip, true);
}

void Hunter::finishMeleeAction(HunterWorld& w)
{
    // The action is over: its record's pause before the next of its kind.
    const MeleeTraits& t = Game::meleeTraits(meleeTraits_, w.hardDifficulty);
    if (meleeAction_ == MeleeResponse::Quick)
        quickReady_ = w.time + t.quickPause;
    else if (meleeAction_ == MeleeResponse::Hard)
        hardReady_ = w.time + t.hardPause;
    dropMelee();
}

void Hunter::dropMelee()
{
    meleeAction_ = MeleeResponse::Wait;
    combo_.clear();
    if (blocking_)
    {
        blocking_ = false;
        if (guardLeft_ <= 0.0f)
            model_->stopOverlay();
    }
}

void Hunter::startSwing(HunterWorld& w)
{
    const MeleeWeapon& weapon = melee_ ? *melee_ : w.tuning.fists;
    const bool hard = !combo_.empty() && combo_.front();
    if (!combo_.empty())
        combo_.erase(combo_.begin());
    swingHard_ = hard;
    swing_ = hard ? weapon.hard : weapon.quick;
    // Its weapon's swing (0x4c9ac0 posts 0xb: SFX_WEAPON_SWING_LO / _ / _HI by its type; fists
    // when it holds none).
    if (w.weaponSound)
        w.weaponSound(melee_ && weapon_ ? weapon_->ct : 0x10, 0xb, 0.0f, feet() + Math::Vector3{0.0f, kChestHeight, 0.0f});
    // Its weapon's clips (Fist_ / Blade_ / Beater_ / Beater2_Stance_Fire_...): the combo's quick
    // blows Quick1..3, a hard blow Hard1.
    const std::string stem = melee_ ? melee_->clipStem : std::string("Fist");
    const std::string suffix = hard ? std::string("_Stance_Fire_Hard1")
                                    : "_Stance_Fire_Quick" + std::to_string(std::min(quickStep_, 2) + 1);
    swingStep_ = hard ? 1 : std::min(quickStep_, 2) + 1;
    if (!hard)
        ++quickStep_;
    std::string clip = stem + suffix;
    if (model_->clipDuration(clip) <= 0.0f)
        clip = "Fist" + suffix;
    state_ = HunterState::Attacking;
    groundSwing_ = false;
    gunStrike_ = false;
    stand(0.0f);
    model_->crossfade(clip, 0.1f, false);
    swingClip_ = model_->clipDuration(clip);
    swingElapsed_ = 0.0f;
    swingLanded_ = false;
    loopClip_.clear();
}

void Hunter::attacking(float32 dt, HunterWorld& w)
{
    swingElapsed_ += dt;
    stand(dt);
    if (swingFoe_)
    {
        // A blow at another hunter: it lands on it within reach in front (the shot sphere at
        // SIZE, ours), one blow - no combo.
        turnTowards(swingFoe_->feet() - feet(), dt);
        if (!swingLanded_ && swingElapsed_ >= swing_.strikeAt)
        {
            swingLanded_ = true;
            const Math::Vector3 to{swingFoe_->feet().x - feet().x, 0.0f, swingFoe_->feet().z - feet().z};
            const float32 dist = to.length();
            const bool inFront = dist < 1e-3f || forwardOf(yaw_).dot(to * (1.0f / dist)) > 0.0f;
            if (swingFoe_->alive() && inFront && dist <= swing_.size + swingFoe_->radius())
                strikeFoe(*swingFoe_, swing_.damage, swingHard_ ? HitKind::Hard : HitKind::Quick, w);
        }
        if (swingElapsed_ >= swingClip_)
        {
            state_ = HunterState::Normal;
            swingFoe_ = nullptr;
            combo_.clear();
            swingPause_ = traits_.swingPause;
        }
        return;
    }
    turnTowards(w.player.feet - feet(), dt);
    if (!swingLanded_ && swingElapsed_ >= swing_.strikeAt)
    {
        // The blow: a sphere of radius SIZE at the chest, reaching his body in front.
        swingLanded_ = true;
        const Math::Vector3 to{w.player.feet.x - feet().x, 0.0f, w.player.feet.z - feet().z};
        const float32 dist = to.length();
        const bool inFront = dist < 1e-3f || forwardOf(yaw_).dot(to * (1.0f / dist)) > 0.0f;
        // Standing (or on a ladder above it), the sphere against his body sphere; on the floor his
        // body reaches further out (ours, 0.6 m, level with it).
        const Math::Vector3 body = w.player.feet + Math::Vector3{0.0f, kPlayerBodyAt, 0.0f};
        const bool reaches = groundSwing_ ? dist <= swing_.size + kPlayerRadius + 0.6f && std::fabs(w.player.feet.y - feet().y) < 1.2f
                                          : (body - (feet() + Math::Vector3{0.0f, kChestHeight, 0.0f})).length() <= swing_.size + kPlayerRadius;
        if (w.player.alive && inFront && reaches && w.hitPlayer)
        {
            const bool hard = !groundSwing_ && !gunStrike_ && swingHard_;
            w.hitPlayer(swing_.playerDamage, feet(), hard,
                        gunStrike_ ? std::string("Obstruct") : (melee_ ? melee_->clipStem : std::string("Fist")),
                        groundSwing_ || gunStrike_ ? 1 : swingStep_);
            if (std::uniform_int_distribution<int>(0, 2)(w.rng) == 0)
                say("POSITIVE_TAUNT_COMBAT", w);
        }
    }
    const bool fight = !groundSwing_ && !gunStrike_;
    // The combo's next blow follows at the swing's CHAIN_POINT, while he is still there.
    if (fight && !combo_.empty() && swingLanded_ && swingElapsed_ >= swing_.chainPoint)
    {
        if (w.player.alive && !w.player.down && flatDistance(feet(), w.player.feet) <= swing_.size + kPlayerRadius + 0.3f)
        {
            startSwing(w);
            return;
        }
        combo_.clear();
    }
    if (swingElapsed_ >= swingClip_)
    {
        state_ = HunterState::Normal;
        if (fight)
        {
            if (combo_.empty())
                finishMeleeAction(w);
        }
        else
            swingPause_ = traits_.swingPause;
    }
}


void Hunter::startGroundAttack(HunterWorld& w)
{
    // Its weapon's <stem>_Ground_Attack_1 with the weapon's hard blow, or with fists a stomp
    // (Stomp_01 at its KICK_WEAPON's stomp time, that record's shot).
    std::string clip = melee_ ? melee_->clipStem + "_Ground_Attack_1" : std::string();
    if (!clip.empty() && model_->clipDuration(clip) > 0.0f)
    {
        swing_ = melee_->hard;
        swing_.strikeAt = model_->clipDuration(clip) * 0.5f; // (ours: no hit time in the data)
    }
    else
    {
        const KickWeapon& k = w.tuning.kick(weapon_ ? weapon_->kickWeapon : std::string());
        clip = "Stomp_01";
        swing_ = k.shot;
        swing_.strikeAt = k.stompAt;
    }
    if (w.sound)
        w.sound("SFX_WEAPON_SWING", feet() + Math::Vector3{0.0f, 0.8f, 0.0f}, 0.5f);
    state_ = HunterState::Attacking;
    groundSwing_ = true;
    gunStrike_ = false;
    stand(0.0f);
    model_->crossfade(clip, 0.1f, false);
    swingClip_ = std::max(0.3f, model_->clipDuration(clip));
    swingElapsed_ = 0.0f;
    swingLanded_ = false;
    loopClip_.clear();
}

void Hunter::lowerGun()
{
    if (aiming_)
    {
        aiming_ = false;
        gunOverlay_ = 0.0f;
        model_->stopOverlay();
    }
}

void Hunter::leaveGunCombat()
{
    lowerGun();
    poseClip_.clear();
    if (gunState_ != kGunOff)
        route_.clear();
    gunState_ = kGunOff;
    coverNode_ = -1;
    gunfireSpot_ = -1;
    gunRoster().remove(name_);
}

void Hunter::enterGunState(int32 state, float32 wait, HunterWorld& w)
{
    if (w.debugLog && state != gunState_ && state >= kGunCloseIn && state <= kGunRush)
    {
        static const char* const kNames[] = {"closes to its range", "advances on his place", "follows him", "rushes him"};
        std::cerr << "[gun] " << name_ << " " << kNames[state - kGunCloseIn] << " ("
                  << flatDistance(feet(), w.player.feet) << " m)" << std::endl;
    }
    // The squad's role slots (gun::RoleSlots): seeking cover, re-seeking, advancing, in cover.
    if (state != gunState_)
    {
        gun::RoleSlots& roles = roleSlots();
        if (state == kGunSeek && reseeking_)
            roles.reseek(name_, w.time, std::uniform_int_distribution<int32>(0, 1)(w.rng) == 1, gunRoster().fightingCount());
        else if (state == kGunSeek)
            roles.seek(name_, w.time);
        else if (state == kGunAdvance)
            roles.advance(name_, w.time, gunRoster().countIn(7, name_) + 1);
        else if (state == kGunCover)
            roles.cover(name_, w.time, randomIn(w.rng, 1.5f, 4.5f), gunRoster().fightingCount());
    }
    reseeking_ = false;
    gunState_ = state;
    gunDecide_ = w.time + wait;
    route_.clear();
    poseClip_.clear();
    if (state != kGunSeek && state != kGunCover && state != kGunPeek && state != kGunExit)
        coverNode_ = -1; // it gives its cover point up
}

void Hunter::rosterUpdate(const std::string& subpack, const HunterWorld& w)
{
    gunRoster().update(name_, subpack, coverState(gunState_), coverNode_, w.time, sawHimAt_);
    gunRoster().setSub(name_, exeSub(gunState_));
    if (sawHimAt_ > -1e9f)
        gunRoster().spotted(name_, sawHimPos_, sawHimAt_);
    gunRoster().setGunfire(name_, gunfireOf(gunState_));
    // Its role coming due says its line (0x5138e8 -> 0x514590).
    gun::RoleSlots& roles = roleSlots();
    roles.expire(w.time);
    const int32 role = roles.roleOf(name_, w.time);
    if (role != role_)
    {
        role_ = role;
        if (role != gun::RoleSlots::None)
            sayLine(gun::RoleSlots::kLines[role], w);
    }
}

bool Hunter::coverHidden(int32 node, const HunterWorld& w) const
{
    // His eye (feet + 1.83 m, 0x79c394) cannot see a crouching hunter's head there (ours: 1 m).
    const Math::Vector3 eye = w.player.feet + Math::Vector3{0.0f, gun::kEnemyEye, 0.0f};
    const Math::Vector3 head = w.nav.node(node).position + Math::Vector3{0.0f, gun::kCoverHead, 0.0f};
    return !w.field.lineOfSight(eye, head);
}

bool Hunter::aimedAt(const HunterWorld& w) const
{
    if (!w.player.lockOn.empty())
        return w.player.lockOn == name_;
    const Math::Vector3 to{feet().x - w.player.feet.x, 0.0f, feet().z - w.player.feet.z};
    const float32 d = to.length();
    if (d < 1e-3f)
        return true;
    const float32 c = std::clamp(forwardOf(w.player.aimYaw).dot(to * (1.0f / d)), -1.0f, 1.0f);
    return Math::degrees(std::acos(c)) < gun::kUnderFireAngle;
}

bool Hunter::underFire(const HunterWorld& w) const
{
    // 0x587ee0: he fired within the last 0.5 s, aiming at it (0x587c90): locked on to it, else
    // within 8 degrees of the line to it.
    if (w.player.shotAgo >= gun::kUnderFireWindow)
        return false;
    if (!w.player.lockOn.empty())
        return w.player.lockOn == name_;
    const Math::Vector3 to{feet().x - w.player.feet.x, 0.0f, feet().z - w.player.feet.z};
    const float32 d = to.length();
    if (d < 1e-3f)
        return true;
    const float32 c = std::clamp(forwardOf(w.player.aimYaw).dot(to * (1.0f / d)), -1.0f, 1.0f);
    return Math::degrees(std::acos(c)) < gun::kUnderFireAngle;
}

bool Hunter::seekCover(float32 nearest, HunterWorld& w, bool away)
{
    // A free cover point `nearest` .. (max - 1) m from him (CPedActCoverSeek): the nearest (mode 2),
    // or under fire the one farthest from him (mode 1, 0x584c30: cosine 0).
    const float32 farthest = gun::fightBand().second - 1.0f;
    auto seen = [&](int32 n) { return !coverHidden(n, w); };
    auto taken = [&](int32 n) { return gunRoster().taken(n, name_); };
    Math::Vector3 awayDir{feet().x - w.player.feet.x, 0.0f, feet().z - w.player.feet.z};
    const float32 len = awayDir.length();
    awayDir = len > 1e-3f ? awayDir * (1.0f / len) : forwardOf(yaw_);
    const int32 node = away ? gun::findCoverAlong(w.nav, feet(), w.player.feet, awayDir, 0.0f, nearest, farthest, seen, taken)
                            : gun::findCover(w.nav, feet(), w.player.feet, nearest, farthest, seen, taken);
    if (node < 0)
    {
        if (w.debugLog)
        {
            const gun::CoverPoints p = gun::coverPoints(w.nav, feet(), [&](int32 n) { return !coverHidden(n, w); });
            std::cerr << "[cover] " << name_ << " finds none (" << p.hidden.size() << " hidden, " << p.visible.size()
                      << " seen of its nearby points)" << std::endl;
        }
        return false;
    }
    if (w.debugLog)
        std::cerr << "[cover] " << name_ << " takes cover at node " << node << " ("
                  << (w.nav.node(node).position - w.player.feet).length() << " m from him, "
                  << (w.nav.node(node).position - feet()).length() << " m away)" << std::endl;
    enterGunState(kGunSeek, gun::kSeekGiveUp, w);
    seekAway_ = away;
    coverNode_ = node;
    coverStuck_ = 0.0f;
    coverFrom_ = feet();
    planRoute(w.nav.node(node).position, w);
    // Taken from now on, so a squad mate deciding this frame looks elsewhere.
    const Subpack* sp = w.director.subpackOf(name_);
    rosterUpdate(sp ? sp->name : std::string(), w);
    return true;
}

bool Hunter::startPeek(float32 dist, HunterWorld& w)
{
    // CPedActPeek's point (0x571c20): from its chest (feet + 1.2 m) at his head (feet + 1.7 m),
    // straight over the cover, else 1.5 m to its right, else to its left.
    if (dist < gun::kPeekNear)
        return false;
    const Math::Vector3 chest = feet() + Math::Vector3{0.0f, gun::kPeekChest, 0.0f};
    const Math::Vector3 head = w.player.feet + Math::Vector3{0.0f, gun::kPeekHead, 0.0f};
    const Math::Vector3 right = rightOf(yaw_);
    const gun::PeekSide side =
        gun::peekSide(dist, [&](float32 offset) { return w.field.lineOfSight(chest + right * offset, head); });
    if (side == gun::PeekSide::None)
        return false;
    const int32 cover = coverNode_;
    enterGunState(kGunPeek, side == gun::PeekSide::Up ? gun::kPeekUpOut : gun::kPeekLeanOut, w);
    coverNode_ = cover;
    peekSide_ = side == gun::PeekSide::Right ? 1.0f : (side == gun::PeekSide::Left ? -1.0f : 0.0f);
    // The shot leaves the probe point (ours); over the top, its standing eye.
    peekFrom_ = peekSide_ != 0.0f ? chest + right * (peekSide_ * gun::kPeekSide) : feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f};
    peekStart_ = w.time;
    peekFired_ = false;
    return true;
}

bool Hunter::leaveCover(HunterWorld& w)
{
    // CPedActCoverExit: out of cover to the nearest of its nearby points his eye sees, not the
    // cover it leaves nor one a squad mate holds, (min + 1) .. (max - 1) m from him (0x586bf7,
    // 0x582e10; Game/ai/gun_combat.h).
    const auto [lo, hi] = gun::fightBand();
    const int32 from = coverNode_;
    const int32 node = gun::findExit(
        w.nav, feet(), w.player.feet, lo + 1.0f, hi - 1.0f, [&](int32 n) { return !coverHidden(n, w); },
        [&](int32 n) { return n == from || gunRoster().taken(n, name_); });
    if (node < 0)
        return false;
    if (w.debugLog)
        std::cerr << "[cover] " << name_ << " leaves cover for node " << node << " ("
                  << (w.nav.node(node).position - w.player.feet).length() << " m from him, "
                  << (w.nav.node(node).position - feet()).length() << " m away)" << std::endl;
    enterGunState(kGunExit, gun::kSeekGiveUp, w);
    coverNode_ = node;
    coverStuck_ = 0.0f;
    coverFrom_ = feet();
    planRoute(w.nav.node(node).position, w);
    return true;
}

bool Hunter::gunCombat(float32 dt, HunterWorld& w)
{
    // Chasing or fighting him with a gun, by its subpack's combat type (AISetSubpackCombatType):
    //   OPEN, COVER  the squad's order and the cover fight of Game/ai/gun_combat.h (both types get
    //                the same controller in the exe; none set: the same, ours); he in the shadows
    //                where it cannot see him: blind fire; he lost: to the gunfire, then the hunt
    //   MELEE, OPEN_MELEE  the melee controller: it closes in to strike with the gun (ours: how the
    //                melee action uses a gun is not traced)
    //   in reach     a blow with the gun (its OBSTRUCT_WEAPON: obstruct_1handedGun / _2handedGun,
    //                the blow at COMBAT_DELAYS 0.4 s, 4 damage): a gun in hand does not punch (ours)
    // In cover it cannot see him (that is the point of it), so while it is between cover and
    // peeking, or firing blind, it fights on as long as its squad saw him within its RUN_TIME
    // (SetHunterRunTime, default 10 s; ours: the exe's squad keeps its controller while the enemy
    // is known, how long that lasts is not traced).
    gunRoster().prune(w.time);
    const Subpack* sp = w.director.subpackOf(name_);
    const std::string subpack = sp ? sp->name : std::string();
    const bool seen = sight_ != kNoLevel;
    if (seen)
    {
        sawHimAt_ = w.time;
        sawHimPos_ = w.player.feet;
    }
    const CombatType type = w.director.combatTypeOf(name_);
    const bool gunSquad = type != CombatType::Melee && type != CombatType::OpenMelee;
    bool engaged = awareness_.action == AiAction::Chase || awareness_.action == AiAction::Combat;
    if (!engaged && awareness_.action == AiAction::Hunt &&
        (gunState_ == kGunSeek || gunState_ == kGunCover || gunState_ == kGunPeek || blindState(gunState_)))
    {
        const HunterSetup* setup = w.director.hunter(name_);
        engaged = w.time - std::max(sawHimAt_, gunRoster().lastSighting(subpack)) < (setup ? setup->runTime : 10.0f);
    }
    if (!w.player.alive)
    {
        leaveGunCombat();
        return false;
    }
    if (!engaged)
    {
        // He lost (cAI_CoordinateKillEnemy 0x506660): a gun squad that fought him goes to the
        // gunfire once (cAI_ControllerGotoGunfire); then, or for the rest, the hunt.
        if (gunSquad && awareness_.action == AiAction::Hunt && !gunfireDone_ && gunState_ != kGunOff &&
            gunState_ != kGunStayBack)
            return gotoGunfire(dt, w, subpack);
        leaveGunCombat();
        return false;
    }
    gunfireDone_ = false;
    if (gunfireState(gunState_))
        leaveGunCombat(); // found again: the fight takes over
    const float32 dist = flatDistance(feet(), w.player.feet);
    const Math::Vector3 to{w.player.feet.x - feet().x, 0.0f, w.player.feet.z - feet().z};
    const MeleeWeapon& strike = w.tuning.meleeWeapon(weapon_ && !weapon_->obstructWeapon.empty()
                                                         ? weapon_->obstructWeapon
                                                         : std::string(gun_->twoHanded() ? "obstruct_2handedGun" : "obstruct_1handedGun"));
    const float32 reach = strike.quick.size + kPlayerRadius;
    if (dist <= reach * 0.9f && std::fabs(w.player.feet.y - feet().y) < 1.2f)
    {
        // In its face: it strikes with the gun.
        leaveGunCombat();
        stand(dt);
        turnTowards(to, dt);
        if (swingPause_ <= 0.0f && dist > 1e-3f && forwardOf(yaw_).dot(to * (1.0f / dist)) > 0.7f)
            startGunStrike(w);
        return true;
    }
    if (!gunSquad)
    {
        leaveGunCombat();
        if (!seen)
            return false; // close in (chase)
        route_.clear();
        steerTo(w.player.feet, runSpeed(), reach * 0.8f, dt);
        return true;
    }
    if (w.player.hidden && !seen)
    {
        // In the shadows where it cannot see him, his place known (cAI_ControllerCombatBlind).
        blindFight(dt, w);
        rosterUpdate(subpack, w);
        return true;
    }
    if (blindState(gunState_))
        leaveGunCombat(); // out of the shadows: the cover fight again

    const bool fighting = coverState(gunState_);
    switch (gun::squadOrder(dist, fighting, gunState_ == kGunSeek || gunState_ == kGunExit, gunRoster().fighting(subpack, name_)))
    {
    case gun::SquadOrder::Chase:
    case gun::SquadOrder::Keep: // no order: it goes on as it was (chasing)
        leaveGunCombat();
        return false;
    case gun::SquadOrder::StayBack:
        // Held back: it stands where it is (ours: facing him, firing when it can).
        if (gunState_ != kGunStayBack)
        {
            leaveGunCombat();
            gunState_ = kGunStayBack;
        }
        route_.clear();
        stand(dt);
        turnTowards(to, dt);
        if (seen && dist <= gun_->maxRange)
            aimAndFire(dt, w, dist);
        else
            lowerGun();
        rosterUpdate(subpack, w);
        return true;
    case gun::SquadOrder::Fight:
        break;
    }
    if (!fighting)
        enterGunState(kGunAssess, gun::kFirstDecision, w);
    coverFight(dt, w, dist);
    rosterUpdate(subpack, w);
    return true;
}

void Hunter::blindFight(float32 dt, HunterWorld& w)
{
    // cAI_ControllerCombatBlind / CPedActBlindGunCombat (Game/ai/gun_combat.h). Where he was: taken
    // again when he moved more than 0.2 squared m (0x59faa5); ours: its own last fix on him.
    const Math::Vector3 moved = lastKnown_ - blindPlace_;
    if (!blindState(gunState_) || moved.dot(moved) > gun::kBlindMoved)
        blindPlace_ = lastKnown_;
    const float32 dist = flatDistance(feet(), blindPlace_);
    const Math::Vector3 to{blindPlace_.x - feet().x, 0.0f, blindPlace_.z - feet().z};
    if (gun::blindOrder(dist) == gun::BlindOrder::GetCloser)
    {
        // "Get Closer": run at the place, ordered again at most every second (0x5a0160).
        lowerGun();
        if (gunState_ != kGunCloser)
        {
            if (!blindState(gunState_))
                leaveGunCombat();
            enterGunState(kGunCloser, 0.0f, w);
            blindOrderAt_ = -1e9f;
        }
        if (w.time - blindOrderAt_ >= gun::kBlindReorder)
        {
            planRoute(blindPlace_, w);
            blindOrderAt_ = w.time;
        }
        followRoute(runSpeed(), dt, w);
        return;
    }
    // "Blind Gun[Open]" / "[Cover]": close in until the line from its gun to the place is clear,
    // then stand and fire at it, facing it within acos 0.9 (0x5873d0); each order lasts 5 s
    // (0x5876d1), here a fresh approach then.
    if (gunState_ != kGunBlind)
    {
        if (!blindState(gunState_))
            leaveGunCombat();
        enterGunState(kGunBlind, gun::kBlindOrder, w);
    }
    if (w.time >= gunDecide_)
        enterGunState(kGunBlind, gun::kBlindOrder, w);
    const Math::Vector3 gunAt = feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f};
    const Math::Vector3 aimAt = blindPlace_ + Math::Vector3{0.0f, kChestHeight, 0.0f};
    if (dist > 1.0f && !w.field.lineOfSight(gunAt, aimAt))
    {
        lowerGun();
        if (route_.empty())
            planRoute(blindPlace_, w);
        followRoute(walkSpeed(), dt, w); // ours: the pace
        return;
    }
    route_.clear();
    stand(dt);
    turnTowards(to, dt);
    aimAndFire(dt, w, dist, nullptr, &blindPlace_);
}

bool Hunter::gotoGunfire(float32 dt, HunterWorld& w, const std::string& subpack)
{
    // cAI_ControllerGotoGunfire (Game/ai/gun_combat.h): the squad goes to his last fix: wait short of
    // it, all on to spots that see it, quit, back to the hunt.
    gun::SquadRoster& roster = gunRoster();
    gun::GunfireSquad& g = roster.gunfire(subpack, w.time);
    if (!gunfireState(gunState_) && g.releaseAt >= 0.0f && w.time >= g.releaseAt)
    {
        g = gun::GunfireSquad{}; // the last search is over: a new one
        g.touched = w.time;
    }
    if (g.node < 0)
    {
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
        if (!roster.lastSpot(subpack, at))
            at = lastKnown_;
        g.place = at + Math::Vector3{0.0f, gun::kGunfireRise, 0.0f};
        g.node = w.nav.nearest(at);
        if (g.node >= 0)
            g.spots = gun::gunfireSpots(w.nav, g.node, [&](int32 n) {
                return w.field.lineOfSight(w.nav.node(n).position + Math::Vector3{0.0f, gun::kGunfireSpotEye, 0.0f}, g.place);
            });
        g.given.assign(g.spots.size(), 0);
    }
    auto done = [&] {
        gunfireDone_ = true;
        leaveGunCombat();
        return false;
    };
    if (g.node < 0)
        return done();
    lowerGun();
    const Math::Vector3 toPlace{g.place.x - feet().x, 0.0f, g.place.z - feet().z};
    auto goTo = [&](int32 state, int32 node) {
        enterGunState(state, 0.0f, w);
        gunfireSpot_ = node;
        coverStuck_ = 0.0f;
        coverFrom_ = feet();
        planRoute(w.nav.node(node).position, w);
    };
    // Getting nowhere for 2 s (ours, as on the way to cover).
    auto stuck = [&] {
        if (flatDistance(feet(), coverFrom_) > 0.5f)
        {
            coverFrom_ = feet();
            coverStuck_ = 0.0f;
        }
        return (coverStuck_ += dt) > 2.0f;
    };
    if (!gunfireState(gunState_))
    {
        // "Goto Wait": the route node 20 short of the place (0x5cab10); a shorter route: wait here.
        leaveGunCombat();
        const int32 from = w.nav.nearest(feet());
        const std::vector<int32> path = from >= 0 ? w.nav.route(from, g.node) : std::vector<int32>{};
        const int32 i = gun::gunfireWaitIndex(path.size());
        if (i >= 0)
            goTo(kGunfireGotoWait, path[static_cast<std::size_t>(i)]);
        else
            enterGunState(kGunfireWait, 0.0f, w);
        rosterUpdate(subpack, w);
    }
    // On to the spots once 3 s ran out since the first waited and two wait (or the only one).
    if (!g.ready && g.waitEnds >= 0.0f && w.time >= g.waitEnds)
        g.ready = gun::gunfireReady(roster.gunfireMembers(subpack), roster.gunfireMembers(subpack, gun::Gunfire::Wait));
    const bool released = g.releaseAt >= 0.0f && w.time >= g.releaseAt;
    switch (gunState_)
    {
    case kGunfireGotoWait:
        if (released)
            return done();
        if (followRoute(runSpeed(), dt, w) || stuck()) // ours: the pace
            enterGunState(kGunfireWait, 0.0f, w);
        break;
    case kGunfireWait:
        // Facing the place (AI state 59); the first to wait starts the squad's 3 s (0x5ca838).
        if (released)
            return done();
        stand(dt);
        turnTowards(toPlace, dt);
        if (g.waitEnds < 0.0f)
            g.waitEnds = w.time + gun::kGunfireWait;
        if (g.ready)
            goTo(kGunfireGotoShoot, g.deal(static_cast<uint32>(w.rng())));
        break;
    case kGunfireGotoShoot:
    {
        // To its spot facing the place (AI state 60). 3 s after the first got there (ours: the
        // exe's test at 0x5cacb0 is not traced) they all quit (0x5cae10).
        if (g.quitAt >= 0.0f && w.time >= g.quitAt)
        {
            if (g.releaseAt < 0.0f)
                g.releaseAt = w.time + gun::kGunfireQuit;
            enterGunState(kGunfireQuit, 0.0f, w);
            break;
        }
        const bool there = flatDistance(feet(), w.nav.node(gunfireSpot_).position) <= gun::kCoverArrive ||
                           followRoute(walkSpeed(), dt, w) || stuck(); // ours: the pace
        if (there)
        {
            route_.clear();
            stand(dt);
            turnTowards(toPlace, dt);
            if (g.quitAt < 0.0f)
                g.quitAt = w.time + gun::kGunfireLook;
        }
        break;
    }
    case kGunfireQuit:
        // Standing, facing the place (AI state 61), until 4 s after the first quit (0x5cad70).
        stand(dt);
        turnTowards(toPlace, dt);
        if (released)
            return done();
        break;
    default:
        break;
    }
    rosterUpdate(subpack, w);
    return true;
}

void Hunter::coverFight(float32 dt, HunterWorld& w, float32 dist)
{
    // CPedActGunCoverCombat as far as traced (Game/ai/gun_combat.h): assess, take the nearest
    // cover from him, crouch there, lean out (CPedActPeek) or leave it (CPedActCoverExit) to fire,
    // crouch again; no cover: stand and fire.
    const Math::Vector3 to{w.player.feet.x - feet().x, 0.0f, w.player.feet.z - feet().z};
    const bool seen = sight_ != kNoLevel;
    const float32 reseek = gun::fightBand().first + 1.0f; // (min + 1) .. (max - 1), 0x586630
    const bool decide = w.time >= gunDecide_;
    auto fire = [&] {
        if (seen && dist <= gun_->maxRange)
            aimAndFire(dt, w, dist);
        else
            lowerGun();
    };
    auto holdGround = [&] {
        route_.clear();
        stand(dt);
        turnTowards(to, dt);
    };
    auto backInCover = [&] {
        const int32 cover = coverNode_;
        enterGunState(kGunCover, randomIn(w.rng, gun::kHideMin, gun::kHideMax), w);
        coverNode_ = cover;
    };

    // Too near: inside the band's near end, standing or moving at him (0x586fb0); going off: past
    // its far end, standing or moving away (0x5870d0). Lost: not seen for half a second (ours; the
    // exe's ped+0x758).
    const auto [bandNear, bandFar] = gun::fightBand();
    const bool still = speed_ < 0.05f;
    const bool atHim = velocity_.x * to.x + velocity_.z * to.z > 0.0f;
    const bool tooNear = dist < bandNear && (still || atHim);
    const bool goingOff = dist > bandFar && (still || !atHim);
    const bool lost = !seen && w.time - sawHimAt_ > 0.5f;
    gun::SquadRoster& roster = gunRoster();
    auto advanceAllowed = [&] {
        if (flatDistance(w.player.feet, sawHimPos_) <= gun::kAdvanceNear && w.time <= roster.advancePace)
            return false;
        return !(roster.countIn(7, name_) > 1 && w.time < roster.advancePace + gun::kAdvanceHold);
    };
    auto routeTo = [&](const Math::Vector3& p, float32 speed) {
        if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
        {
            planRoute(p, w);
            replanTimer_ = 1.0f;
        }
        followRoute(speed, dt, w);
    };
    auto assess = [&] { enterGunState(kGunAssess, randomIn(w.rng, gun::kAssessMin, gun::kAssessMax), w); };

    // He within 3 m of its cover point spoils it (0x583c38): it looks again.
    if (coverNode_ >= 0 && flatDistance(w.nav.node(coverNode_).position, w.player.feet) < gun::kCoverSpoiled)
    {
        coverNode_ = -1;
        if (!seekCover(reseek, w))
            enterGunState(kGunStand, gun::kStandTime, w);
    }

    switch (gunState_)
    {
    case kGunAssess:
        holdGround();
        fire();
        if (!decide)
            break;
        if (lost)
        {
            // Lost (0x585610 -> 0x586630): it seeks again, setting the squad's advance time; or it
            // advances on his place when it may (state 6 -> 7, 0x585310).
            if (w.time > roster.advancePace)
                roster.advancePace = w.time + (!w.player.gun                  ? gun::kAdvancePace
                                               : kind_ == 1 || kind_ == 2 ? randomIn(w.rng, gun::kAdvancePaceTroopMin, gun::kAdvancePaceTroopMax)
                                                                          : randomIn(w.rng, gun::kAdvancePaceMin, gun::kAdvancePaceMax));
            reseeking_ = true;
            if (advanceAllowed())
                enterGunState(kGunAdvance, 0.0f, w);
            else if (!seekCover(reseek, w))
                enterGunState(kGunStand, gun::kStandTime, w);
            break;
        }
        if (!seekCover(gun::kCoverNear, w))
        {
            // No cover: no hidden point near it and he beyond the gun's far range - to its range.
            const auto [nearR, farR] = gun::weaponRange(weapon_ ? weapon_->ct : 0);
            const bool noHidden = gun::coverPoints(w.nav, feet(), [&](int32 n) { return !coverHidden(n, w); }).hidden.empty();
            if (noHidden && dist > farR)
            {
                closeTo_ = (roster.countIn(5) & 1) ? nearR : farR; // (itself counted, as the exe)
                enterGunState(kGunCloseIn, 0.0f, w);
            }
            else
                enterGunState(kGunStand, gun::kStandTime, w);
        }
        break;
    case kGunCloseIn:
        // State 8 (0x585d90): at him at half pace (ours: its walk) to the range; too near or him
        // going off: it stands; there: it decides.
        lowerGun();
        if (tooNear || goingOff)
            enterGunState(kGunStand, gun::kStandTime, w);
        else if (dist <= closeTo_)
            assess();
        else
            routeTo(w.player.feet, walkSpeed());
        break;
    case kGunAdvance:
        // State 7 (0x585b30): to where he was lost, running while fewer than three advance; found
        // again or there: it decides.
        lowerGun();
        if (seen || flatDistance(feet(), sawHimPos_) <= gun::kCoverArrive)
            assess();
        else if (tooNear || goingOff)
            enterGunState(kGunStand, gun::kStandTime, w);
        else
            routeTo(sawHimPos_, roster.countIn(7, name_) < 2 ? runSpeed() : walkSpeed());
        break;
    case kGunFollow:
        // State 9 (0x585de0): after him, running while at most two follow, until within (max - 1);
        // then cover again; lost: it decides.
        lowerGun();
        if (lost)
            assess();
        else if (dist <= bandFar - 1.0f)
        {
            if (!seekCover(reseek, w))
                enterGunState(kGunStand, gun::kStandTime, w);
        }
        else
            routeTo(w.player.feet, roster.countIn(9, name_) < 2 ? runSpeed() : walkSpeed());
        break;
    case kGunRush:
        // State 13 (0x585ef0): to within 7 m of him, firing when it sees him; there it decides.
        if (dist <= gun::kRushTo)
            assess();
        else
        {
            if (seen && dist <= gun_->maxRange)
                aimAndFire(dt, w, dist);
            else
                lowerGun();
            routeTo(w.player.feet, runSpeed());
        }
        break;
    case kGunStand:
        holdGround();
        fire();
        // Under fire in the open: cover away from him (mode 1; ours: the exe's state 1, 0x585120).
        if (underFire(w) && seekCover(gun::kCoverNear, w, true))
            break;
        // State 12 (0x5859a0): lost, it decides; him going off: after him (9); him too near where
        // it cannot see him: at him (13); its time out: it decides.
        if (lost)
            assess();
        else if (goingOff)
            enterGunState(kGunFollow, 0.0f, w);
        else if (tooNear && !seen)
            enterGunState(kGunRush, 0.0f, w);
        else if (decide)
            assess();
        break;
    case kGunSeek:
    case kGunExit:
    {
        // To the point, gun down: running while it has him in sight or he is within 3.5 m (the exe
        // picks its faster pace on those two, 0x5819f3), else walking (ours: the pace mapping).
        // Into cover there; out of it (CoverExit) it stands and fires.
        lowerGun();
        // On the way under fire (0x584c30): it seeks again, away from him (mode 1, cosine 0).
        if (gunState_ == kGunSeek && !seekAway_ && underFire(w) && seekCover(gun::kCoverNear, w, true))
            break;
        // Seeking again with him lost (state 6, 0x585310): it advances on his place when it may.
        if (gunState_ == kGunSeek && lost && advanceAllowed())
        {
            enterGunState(kGunAdvance, 0.0f, w);
            break;
        }
        const Math::Vector3 point = w.nav.node(coverNode_).position;
        if (flatDistance(feet(), point) <= gun::kCoverArrive)
        {
            if (gunState_ == kGunSeek)
                backInCover();
            else
                enterGunState(kGunStand, gun::kStandTime, w);
            break;
        }
        if (route_.empty())
            planRoute(point, w);
        followRoute(seen || dist < 3.5f ? runSpeed() : walkSpeed(), dt, w);
        // Getting nowhere (a wall, a squad mate) or not there in kSeekGiveUp s (ours): it stands its
        // ground instead.
        if (flatDistance(feet(), coverFrom_) > 0.5f)
        {
            coverFrom_ = feet();
            coverStuck_ = 0.0f;
        }
        else if ((coverStuck_ += dt) > 2.0f || decide)
            enterGunState(kGunStand, gun::kStandTime, w);
        break;
    }
    case kGunCover:
        // Crouched behind it, gun down, facing him (sub-state 2); after rand(1.5, 3) s it leans out
        // (or rises over it) where it can see his head, else leaves the cover; he come round it:
        // other cover. No new peek within 4 s of the last.
        holdGround();
        lowerGun();
        if (model_->clipDuration("Sneak_Stand") > 0.0f)
            poseClip_ = "Sneak_Stand"; // ours: the player's crouch, hunters have none
        if (decide)
        {
            if (!coverHidden(coverNode_, w))
            {
                coverNode_ = -1;
                if (!seekCover(reseek, w))
                    enterGunState(kGunStand, gun::kStandTime, w);
            }
            else if (w.time < peekReady_)
                gunDecide_ = peekReady_;
            else if (!startPeek(dist, w) && !leaveCover(w))
                enterGunState(kGunStand, gun::kStandTime, w);
        }
        break;
    case kGunPeek:
    {
        // Leaning out (Peek_L_ / Peek_R_Sneak) or up over the cover, firing from there. In again when
        // its time is out (2 s leaning, 3 s up; a first shot gives it rand(1.5, 4) s, our reading),
        // when it cannot see him from there once settled (2 s), or he is beyond 20 m / within 1 m.
        holdGround();
        const std::string lean = peekSide_ < 0.0f ? "Peek_L_Sneak" : (peekSide_ > 0.0f ? "Peek_R_Sneak" : std::string());
        if (!lean.empty() && model_->clipDuration(lean) > 0.0f)
            poseClip_ = lean;
        const Math::Vector3 chest = w.player.feet + Math::Vector3{0.0f, kChestHeight, 0.0f};
        const bool view = w.field.lineOfSight(peekFrom_, chest);
        if (view && dist <= gun_->maxRange)
        {
            if (aimAndFire(dt, w, dist, &peekFrom_) && !peekFired_)
            {
                peekFired_ = true;
                gunDecide_ = w.time + randomIn(w.rng, gun::kPeekMin, gun::kPeekMax);
            }
        }
        else
            lowerGun();
        if (decide || dist > gun::kPeekReach || dist < gun::kPeekNear || (!view && w.time - peekStart_ > gun::kPeekSettle))
        {
            peekReady_ = w.time + gun::kPeekCooldown;
            backInCover();
        }
        break;
    }
    default:
        break;
    }
}

bool Hunter::beBuddy(const GoalDef& g, float32 dt, HunterWorld& w)
{
    // cAI_ObjectiveBeBuddy (vtable 0x79b060, update 0x54bd80). Each frame: the radius events
    // (0x54c060: OnFriendEnteringBuddyRadius / OnFriendLeavingBuddyRadius, events 22 / 23, the
    // radius squared, 0x40c8b0), the player's attack window (0x54c0d0: 4 s after his ped word has
    // 0x800000, attacking), then its state (0x54c190):
    //   0 none -> follow (follow mode 1, AISetBuddyFollow) or wait (0, AISetBuddyWait).
    //   1 follow (0x54d1a0: a follow order on him). Following with him within 5 m (0x79afe0, 25
    //     squared) and crouched (0x54e700: his ped word 0x40; ours: sneaking): 2.
    //   2 / 3 to the node linked to its own nearest him (0x405c10), twice (0x54d200 / 0x54d260),
    //     crouched (order flag 0x414f30), then 4.
    //   4 hold there facing him while he stays crouched, else follow again.
    //   5 wait facing him (0x54d3d0); on entering, and every 4 s (0x54ead0), to the graph node
    //     within 3 m (0x4060c0, 0x79afc8: 9 squared) when it is not on it (6, 0x54d450; 7 steps
    //     on from there, 0x54d540 - ours stops at 6's node).
    //   8 / 9 its nearest enemy (0x54e160, over its leader's enemy list) is near (0x54dd30): within
    //     2 m (4 squared) for kinds 0 / 3 - fought hand to hand (order 0xb at 2 m, 0x54d660) while
    //     he attacked in the last 4 s or, with him over 10 m away, while that enemy is after it -
    //     or within 10 m (100) for kinds 1 / 2, shot (order 0xc at 10 m, 0x54d6c0) while he
    //     attacked in the last 4 s. It ends when the window closes, the enemy is gone or beyond
    //     4 / 15 m (0x79afd8 / dc: 16 / 225), or another is the nearest (0x54dfa0).
    //   10 hurt (0x54e980: its ped word 0x10000 newly set): it stands facing the enemy (order mode
    //     7, 0x54d7e0); once over and a second on (0x54e880), with an enemy still after it and him
    //     over 4 m away it switches to following (0x40c140(1)), else back to follow / wait.
    // States 1-7 test 8 / 9 first, then 10. Ours: the kind is whether it carries a gun (the exe's
    // +0x58 +0x20 is not traced); the two flags it reads through the ped's AI interface
    // (+0x14 slots 0x38 / 0x3c, 0x40bf50 / 70) are: holds a melee weapon / a gun. On a change of mode the player says
    // a line half the time (ids 0x1bd0 / 0x1bc4 + 0..5, 0x54ef80 / 0x54eea0), else the buddy does
    // (speech order 0x22, 0x38 / 0x39): see below.
    const std::string friendName = g.target.empty() ? std::string("player") : g.target;
    const auto p = w.entityAt ? w.entityAt(friendName) : std::nullopt;
    if (!p)
        return false;
    const Math::Vector3 him = *p;
    const float32 d = (him - feet()).length();
    const bool inside = d <= g.radius;
    if (buddyInside_ != (inside ? 1 : 0))
    {
        buddyInside_ = inside ? 1 : 0;
        if (w.scriptEvent)
            w.scriptEvent(name_, inside ? "OnFriendEnteringBuddyRadius" : "OnFriendLeavingBuddyRadius");
    }
    const HunterSetup* hs = w.director.hunter(name_);
    const bool scripted = !hs || hs->buddyFollow;
    if (scripted != buddyScripted_)
    {
        buddyScripted_ = scripted;
        buddyFollowSet_ = -1; // the script set it again
    }
    const bool follow = buddyFollowSet_ >= 0 ? buddyFollowSet_ == 1 : scripted;
    const bool isPlayer = friendName.size() == 6 && std::equal(friendName.begin(), friendName.end(), "player", [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
    buddyOfPlayer_ = isPlayer;
    if (isPlayer && (w.player.swinging || w.player.shotAgo < 0.1f))
        friendFoughtAt_ = w.time;
    const bool fought = w.time - friendFoughtAt_ < 4.0f;
    const bool crouched = isPlayer && w.player.sneaking;
    if (buddyHealth_ < 0.0f)
        buddyHealth_ = health_;
    const bool hurtNow = health_ < buddyHealth_;
    buddyHealth_ = health_;
    if (hurtNow)
        buddyHurtAt_ = w.time;
    Hunter* enemy = w.pickFoe ? w.pickFoe(*this, false) : nullptr;
    const float32 enemyDist = enemy ? (enemy->feet() - feet()).length() : 1e9f;

    // A change of mode (0x54ec80): with him the friend, on a coin (0x54ef80 / 0x54eea0) Cash says
    // one of six lines - waiting SFX_E1V1_SCRIPTED_TRAMP_7..12 (0x1bd0), following _1..6
    // (0x1bc4); the journalist's (+0x248; ours: the Jou_Bod model) SCRIPTED_JOURNO_7..12 /
    // _1..6 (0x1b42 / 0x1b36) - one not among the last five picked (0x54f060); else the buddy
    // says line 0x38 (waiting) or 0x39 (following).
    if (buddyMode_ != (follow ? 1 : 0))
    {
        buddyMode_ = follow ? 1 : 0;
        bool cash = false;
        if (isPlayer && (w.rng() & 1u) == 0u && w.sound)
        {
            static std::array<int32, 5> lastPicks{-1, -1, -1, -1, -1};
            static std::size_t nextPick = 0;
            const bool journo = traits_.model.rfind("Jou_Bod", 0) == 0;
            const int32 base = follow ? (journo ? 0x1b36 : 0x1bc4) : (journo ? 0x1b42 : 0x1bd0);
            int32 pick = -1;
            for (int32 tries = 0; tries < 64; ++tries)
            {
                const int32 c = base + static_cast<int32>(w.rng() % 6u);
                if (std::find(lastPicks.begin(), lastPicks.end(), c) == lastPicks.end())
                {
                    pick = c;
                    break;
                }
            }
            if (pick >= 0)
            {
                lastPicks[nextPick] = pick;
                nextPick = (nextPick + 1) % lastPicks.size();
                w.sound("#" + std::to_string(pick), *p + Math::Vector3{0.0f, kEyeHeight, 0.0f}, 0.8f);
                cash = true;
            }
        }
        if (!cash)
        {
            speechBusy_ = 0.0f;
            sayLine(follow ? 0x39 : 0x38, w);
        }
    }
    const Buddy was = buddy_;
    auto stepToward = [&](const Math::Vector3& to) {
        // 0x405c10: of the nodes linked to its own, the one nearest `to`.
        const int32 from = w.nav.nearest(feet());
        int32 best = -1;
        float32 bestDist = 1e9f;
        if (from < 0)
            return best;
        const auto& node = w.nav.node(from);
        for (std::size_t i = 0; i < node.links.size(); ++i)
            if (i >= node.linkOpen.size() || node.linkOpen[i])
                if (const float32 dd = (w.nav.node(node.links[i]).position - to).length(); dd < bestDist)
                {
                    best = node.links[i];
                    bestDist = dd;
                }
        return best;
    };
    auto waitNode = [&] {
        // 0x4060c0: the graph node within 3 m of it (its nearest), when it is not standing on it.
        const int32 n = w.nav.nearest(feet());
        if (n < 0)
            return -1;
        const float32 dd = flatDistance(w.nav.node(n).position, feet());
        return dd <= 3.0f && dd > 0.3f ? n : -1;
    };
    auto enterWait = [&] {
        buddyCheckAt_ = w.time + 4.0f;
        buddyNode_ = waitNode();
        buddy_ = buddyNode_ >= 0 ? Buddy::WaitTo : Buddy::Wait;
    };
    auto settle = [&] {
        if (follow)
            buddy_ = Buddy::Follow;
        else
            enterWait();
    };
    auto react = [&] {
        if (!enemy)
            return false;
        // Hand to hand only holding a melee weapon (the AI interface's slot 0x38, 0x4134c0: weapon
        // class 0 / 1), shooting only holding a gun (slot 0x3c, 0x413500: class 3); empty-handed
        // it does neither.
        Buddy s = Buddy::None;
        if (!gun_)
        {
            if (weapon_ && melee_ && enemyDist <= 2.0f && (d <= 10.0f ? fought : enemy->foe() == this))
                s = Buddy::Fight;
        }
        else if (enemyDist <= 10.0f && fought)
            s = Buddy::Shoot;
        if (s == Buddy::None)
            return false;
        buddy_ = s;
        foe_ = enemy;
        return true;
    };
    auto arrive = [&](int32 node, float32 speed) {
        const Math::Vector3 at = w.nav.node(node).position;
        if (flatDistance(feet(), at) <= 0.5f)
        {
            route_.clear();
            stand(dt);
            return true;
        }
        if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
        {
            planRoute(at, w);
            replanTimer_ = kReplan;
        }
        followRoute(speed, dt, w);
        return false;
    };
    auto faceHim = [&] {
        route_.clear();
        stand(dt);
        turnTowards(Math::Vector3{him.x - feet().x, 0.0f, him.z - feet().z}, dt);
    };

    if (buddy_ == Buddy::None)
        settle();
    if (buddy_ <= Buddy::WaitOn && !react() && hurtNow)
        buddy_ = Buddy::Hurt;

    switch (buddy_)
    {
    case Buddy::None:
    case Buddy::Follow:
        if (!follow)
        {
            enterWait();
            break;
        }
        if (crouched && d <= 5.0f && (buddyNode_ = stepToward(him)) >= 0)
        {
            buddy_ = Buddy::SneakTo;
            break;
        }
        // The follow order itself is not traced; ours keeps within 5 m, running beyond 15 m.
        if (d > 5.0f)
        {
            if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
            {
                planRoute(him, w);
                replanTimer_ = kReplan;
            }
            followRoute(d > 15.0f ? runSpeed() : walkSpeed(), dt, w);
        }
        else
            faceHim();
        break;
    case Buddy::SneakTo:
    case Buddy::SneakOn:
        moveClip_ = "Sneak_Walk_Fwd";
        if (arrive(buddyNode_, walkSpeed() * 0.6f))
        {
            if (buddy_ == Buddy::SneakTo && (buddyNode_ = stepToward(him)) >= 0)
                buddy_ = Buddy::SneakOn;
            else
                buddy_ = Buddy::Hold;
        }
        else if (!follow)
            enterWait();
        break;
    case Buddy::Hold:
        if (!follow)
            enterWait();
        else if (!crouched)
            buddy_ = Buddy::Follow;
        else
        {
            faceHim();
            if (model_->clipDuration("Sneak_Stand") > 0.0f)
                poseClip_ = "Sneak_Stand"; // ours: crouched by him
        }
        break;
    case Buddy::Wait:
        if (follow)
            buddy_ = Buddy::Follow;
        else if (w.time >= buddyCheckAt_ && (buddyCheckAt_ = w.time + 4.0f, buddyNode_ = waitNode()) >= 0)
            buddy_ = Buddy::WaitTo;
        else
            faceHim();
        break;
    case Buddy::WaitTo:
    case Buddy::WaitOn:
        if (arrive(buddyNode_, walkSpeed()))
            buddy_ = Buddy::Wait;
        else if (follow)
            buddy_ = Buddy::Follow;
        break;
    case Buddy::Fight:
    case Buddy::Shoot:
    {
        const float32 quit = buddy_ == Buddy::Fight ? 4.0f : 15.0f;
        if (!fought || !foe_ || !foe_->alive() || foe_->gone() || (foe_->feet() - feet()).length() > quit || enemy != foe_)
        {
            foe_ = nullptr;
            if (!react())
                settle();
            break;
        }
        fightFoe(dt, w);
        break;
    }
    case Buddy::Hurt:
        if (w.time - buddyHurtAt_ < 1.0f)
        {
            route_.clear();
            stand(dt);
            const Math::Vector3 at = enemy ? enemy->feet() : him;
            turnTowards(Math::Vector3{at.x - feet().x, 0.0f, at.z - feet().z}, dt);
        }
        else if (enemy && enemy->foe() == this)
        {
            if (d > 4.0f)
            {
                buddyFollowSet_ = 1;
                buddy_ = Buddy::Follow;
            }
            else
                faceHim();
        }
        else
            settle();
        break;
    }
    if (w.debugLog && buddy_ != was)
    {
        static const char* const kNames[] = {"none", "follow", "sneak-to", "sneak-on", "hold", "wait", "wait-to", "wait-on", "fight", "shoot", "hurt"};
        std::cerr << "[buddy] " << name_ << " " << kNames[static_cast<int>(was)] << " -> "
                  << kNames[static_cast<int>(buddy_)] << " (him " << d << " m)" << std::endl;
    }
    return true;
}

void Hunter::strikeFoe(Hunter& foe, float32 damage, HitKind kind, HunterWorld& w)
{
    foe.incomingFrom_ = this;
    foe.hit(damage, feet(), w, kind, melee_ ? melee_->clipStem : std::string("Fist"));
    foe.incomingFrom_ = nullptr;
}

void Hunter::fightFoe(float32 dt, HunterWorld& w)
{
    // Another hunter its leader has as an enemy (AIAddLeaderEnemy with a hunter's name,
    // AIAddAllHuntersInPackAsLeaderEnemies): Piggsy and the LEADER_NOENEMY pack, the hunters and
    // the journalist / tramp, Bonus3's gangs. Ours: with a gun it fires at it from where it stands
    // when it sees it within its range, else closes in; without one it runs to it and swings, one
    // blow at a time (SWING_PAUSE between).
    Hunter& f = *foe_;
    const Math::Vector3 to{f.feet().x - feet().x, 0.0f, f.feet().z - feet().z};
    const float32 dist = to.length();
    const Math::Vector3 eye = feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f};
    const Math::Vector3 chest = f.feet() + Math::Vector3{0.0f, kChestHeight, 0.0f};
    if (gun_)
    {
        if (dist <= gun_->maxRange && w.field.lineOfSight(eye, chest))
        {
            route_.clear();
            stand(dt);
            turnTowards(to, dt);
            const Math::Vector3 at = f.feet();
            if (aimAndFire(dt, w, dist, nullptr, &at))
            {
                const float32 chance = gun_->hitChance(dist) * std::clamp(traits_.hitAccuracy / 100.0f, 0.0f, 1.0f);
                if (std::uniform_real_distribution<float32>(0.0f, 1.0f)(w.rng) < chance)
                {
                    BodyHit b;
                    b.zone = HitZone::Torso;
                    b.point = chest;
                    b.distance = dist;
                    b.fromFront = forwardOf(f.yaw()).dot(to * (-1.0f / std::max(dist, 1e-3f))) >= 0.0f;
                    f.incomingFrom_ = this;
                    f.shot(zoneDamage(*gun_, HitZone::Torso, f.fullHealth(), f.hardHead()), feet(), b, w);
                    f.incomingFrom_ = nullptr;
                }
            }
            return;
        }
        lowerGun();
    }
    const MeleeWeapon& weapon = melee_ ? *melee_ : w.tuning.fists;
    const float32 reach = weapon.quick.size + f.radius();
    if (dist > reach * 0.85f)
    {
        if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
        {
            planRoute(f.feet(), w);
            replanTimer_ = kReplan;
        }
        followRoute(runSpeed(), dt, w);
        return;
    }
    route_.clear();
    stand(dt);
    turnTowards(to, dt);
    if (swingPause_ <= 0.0f && dist > 1e-3f && forwardOf(yaw_).dot(to * (1.0f / dist)) > 0.7f)
    {
        combo_.clear();
        swingFoe_ = &f;
        startSwing(w);
    }
}

void Hunter::blindMelee(float32 dt, HunterWorld& w)
{
    // cAI_ControllerCombatBlind for a hunter without a gun: within 16 m of where he was "Blind
    // Melee" (CPedActBlindMeleeCombat, update 0x56f570: to the place facing it, one blow there,
    // done), beyond it "Get Closer" (run at it, ordered again at most every second). An order lasts
    // 5 s (as the blind gun's, 0x5876d1); then a fresh one swings again.
    const Math::Vector3 moved = lastKnown_ - blindPlace_;
    if (!blindMelee_ || moved.dot(moved) > gun::kBlindMoved || w.time - blindOrderAt_ >= gun::kBlindOrder)
    {
        blindMelee_ = true;
        blindSwung_ = false;
        blindPlace_ = lastKnown_;
        blindOrderAt_ = w.time;
        route_.clear();
    }
    const float32 dist = flatDistance(feet(), blindPlace_);
    const Math::Vector3 to{blindPlace_.x - feet().x, 0.0f, blindPlace_.z - feet().z};
    const MeleeWeapon& weapon = melee_ ? *melee_ : w.tuning.fists;
    const float32 reach = weapon.quick.size + kPlayerRadius;
    if (dist > reach * 0.85f)
    {
        if (route_.empty() || (replanTimer_ -= dt) <= 0.0f)
        {
            planRoute(blindPlace_, w);
            replanTimer_ = gun::kBlindReorder;
        }
        followRoute(gun::blindOrder(dist) == gun::BlindOrder::GetCloser ? runSpeed() : walkSpeed(), dt, w);
        return;
    }
    route_.clear();
    stand(dt);
    turnTowards(to, dt);
    if (!blindSwung_ && swingPause_ <= 0.0f && dist > 1e-3f && forwardOf(yaw_).dot(to * (1.0f / dist)) > gun::kBlindFacing)
    {
        combo_.clear();
        startSwing(w);
        blindSwung_ = true;
        if (w.debugLog)
            std::cerr << "[melee] " << name_ << " swings blind at where he was (" << flatDistance(blindPlace_, w.player.feet)
                      << " m off him)" << std::endl;
    }
}

bool Hunter::holdBoundary(float32 dt, HunterWorld& w)
{
    // A squad that keeps to its territory does not follow him out of it (0x5017a0 -> 0x4fd6b0): it
    // goes to the territory's node nearest him (a goto order, running) and holds there facing him,
    // firing when it can; with no such node it evades (0x4fd830, "Boundary Evade").
    const int32 node = w.boundaryNode ? w.boundaryNode(*this) : -1;
    if (node == -1)
    {
        boundaryNode_ = -1;
        evadePhase_ = EvadePhase::None;
        return false;
    }
    if (gunState_ != kGunOff)
        leaveGunCombat();
    if (node == -2)
    {
        boundaryNode_ = -1;
        evade(dt, w);
        return true;
    }
    evadePhase_ = EvadePhase::None;
    const float32 dist = flatDistance(feet(), w.player.feet);
    const Math::Vector3 to{w.player.feet.x - feet().x, 0.0f, w.player.feet.z - feet().z};
    const Math::Vector3 p = w.nav.node(node).position;
    if (node != boundaryNode_)
    {
        boundaryNode_ = node;
        route_.clear();
        if (w.debugLog)
            std::cerr << "[boundary] " << name_ << " holds its territory at node " << node << " ("
                      << flatDistance(w.nav.node(node).position, w.player.feet) << " m from him)" << std::endl;
    }
    if (flatDistance(feet(), p) > gun::kCoverArrive)
    {
        if (gun_)
            lowerGun();
        if (route_.empty())
            planRoute(p, w);
        followRoute(runSpeed(), dt, w);
        return true;
    }
    route_.clear();
    stand(dt);
    turnTowards(to, dt);
    if (gun_ && sight_ != kNoLevel && dist <= gun_->maxRange)
        aimAndFire(dt, w, dist);
    else if (gun_)
        lowerGun();
    return true;
}

void Hunter::startFlee(HunterWorld& w)
{
    // Away from him to about 20 m of him (the search CPedActEvade starts, 0x5778e0 -> 0x5767d0 with
    // 20 m, 0x6848e8; ours: the reachable node within 30 m whose distance from him is nearest 20 m,
    // farther from him than it stands now).
    evadePhase_ = EvadePhase::Flee;
    route_.clear();
    const int32 here = w.nav.nearest(feet());
    if (here < 0)
        return;
    const float32 now = flatDistance(feet(), w.player.feet);
    int32 best = -1;
    float32 bestScore = 1e9f;
    for (int32 n : w.nav.within(here, 30.0f))
    {
        const float32 d = flatDistance(w.nav.node(n).position, w.player.feet);
        if (d <= now)
            continue;
        const float32 score = std::fabs(d - 20.0f);
        if (score < bestScore)
        {
            best = n;
            bestScore = score;
        }
    }
    if (best >= 0)
        planRoute(w.nav.node(best).position, w);
}

int32 Hunter::findHideNode(HunterWorld& w)
{
    // Not with him within sqrt 3 m. The search (0x559700) runs at most every 0.5 s, else the last
    // pick stands while it is unused: the dark nodes (0x403080: floor light under 0.06, 0x69a5b4),
    // the nearest 151 by distance, from a random one of the nearest quarter on, the first not used
    // within the last 60 s and within 20 m. It is taken if he cannot see it (0x580a40; ours: his
    // eye to a crouched head there, as the gun fight's cover).
    if (flatDistance(feet(), w.player.feet) < std::sqrt(3.0f) || !w.nodeLight)
        return -1;
    const auto used = [this](int32 n) {
        return std::any_of(hideUsed_.begin(), hideUsed_.end(), [n](const auto& u) { return u.first == n; });
    };
    if (w.time - hideSearchAt_ >= 0.5f)
    {
        hideSearchAt_ = w.time;
        hidePick_ = -1;
        std::vector<std::pair<float32, int32>> dark;
        for (int32 n = 0; n < static_cast<int32>(std::min(w.nav.size(), w.nodeLight->size())); ++n)
            if ((*w.nodeLight)[static_cast<std::size_t>(n)] < 0.06f)
            {
                const Math::Vector3 d = w.nav.node(n).position - feet();
                dark.emplace_back(d.dot(d), n);
            }
        std::sort(dark.begin(), dark.end());
        if (dark.size() > 151)
            dark.resize(151);
        if (!dark.empty())
        {
            const std::size_t from = std::uniform_int_distribution<std::size_t>(0, dark.size() / 4)(w.rng);
            for (std::size_t i = from; i < dark.size(); ++i)
                if (!used(dark[i].second) && dark[i].first < 20.0f * 20.0f)
                {
                    hidePick_ = dark[i].second;
                    break;
                }
        }
    }
    else if (hidePick_ >= 0 && used(hidePick_))
        hidePick_ = -1;
    return hidePick_ >= 0 && coverHidden(hidePick_, w) ? hidePick_ : -1;
}

void Hunter::hurtPastHide(bool blow, HunterWorld& w)
{
    // 0x410c60: at or under its hide health now and not before (+0x1b4 keeps the last answer), a
    // blow (damage kinds 1, 2, 4, 0x10) makes its reaction 0x11 - Communications.ini's
    // ShoutForHelp (0x523245), the speech context CALLING_FOR_HELP. A shot only updates it.
    const bool past = hideHealth_ > 0 && health_ > 0.0f &&
                      static_cast<int32>(health_ / std::max(fullHealth_, 1.0f) * 100.0f) <= hideHealth_;
    const bool crossed = past && !pastHide_;
    pastHide_ = past;
    if (!crossed || !blow)
        return;
    if (w.debugLog)
        std::cerr << "[hide] " << name_ << " shouts for help at " << health_ << " hp" << std::endl;
    speechBusy_ = 0.0f;
    say("CALLING_FOR_HELP", w);
}

bool Hunter::hideDisturbed(const HunterWorld& w) const
{
    // Struck within 0.2 s; or he within 6 m (8 m with his ped flag 0x200, not traced), within 45
    // degrees of its facing, and in its line of sight.
    if (w.time - struckAt_ < 0.2f)
        return true;
    const Math::Vector3 to{w.player.feet.x - feet().x, 0.0f, w.player.feet.z - feet().z};
    const float32 d = to.length();
    if (d >= 6.0f)
        return false;
    if (d > 1e-3f && forwardOf(yaw_).dot(to * (1.0f / d)) < std::cos(Math::radians(45.0f)))
        return false;
    return w.field.lineOfSight(feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f},
                               w.player.feet + Math::Vector3{0.0f, kChestHeight, 0.0f});
}

void Hunter::hide(float32 dt, HunterWorld& w)
{
    // 0x57f950. Choose (0): a dark node out of his sight (0x580180, state 4), else flee (0x580100,
    // CPedActEvade to 50 m, state 1; cornered there it fights back). Fleeing it keeps looking for
    // one. To the node (4): it stops for 0.2 s, then runs there; he within 6 m and the node in his
    // sight, it chooses again. Within 1 m (0x580340, 5) it crouches and turns to him, then
    // watches (0x580740, 6), crouched; disturbed (0x580b50) it stands and looks at him (0x5807d0,
    // 7): no longer, it crouches again; still after 0.2 s, it chooses again. A node it went for is
    // not taken again for 60 s (0x580e20); watching from it keeps it so.
    hiding_ = true;
    lowerGun();
    hideUsed_.erase(std::remove_if(hideUsed_.begin(), hideUsed_.end(), [&w](const auto& u) { return u.second <= w.time; }),
                    hideUsed_.end());
    const Math::Vector3 to{w.player.feet.x - feet().x, 0.0f, w.player.feet.z - feet().z};
    const float32 dist = to.length();
    auto toCover = [&](int32 node) {
        hideNode_ = node;
        hidePhase_ = HidePhase::ToCover;
        hideAt_ = w.time;
        hideUsed_.emplace_back(node, w.time + 60.0f);
        evadePhase_ = EvadePhase::None;
        poseClip_.clear();
        planRoute(w.nav.node(node).position, w);
        hideFrom_ = feet();
        hideStuck_ = 0.0f;
        if (w.debugLog)
            std::cerr << "[hide] " << name_ << " goes to cover at node " << node << " ("
                      << flatDistance(feet(), w.nav.node(node).position) << " m)" << std::endl;
    };
    auto choose = [&] {
        hidePhase_ = HidePhase::Choose;
        hideNode_ = -1;
        poseClip_.clear();
    };
    switch (hidePhase_)
    {
    case HidePhase::Choose:
        if (const int32 node = findHideNode(w); node >= 0)
            toCover(node);
        else
        {
            hidePhase_ = HidePhase::Flee;
            evadePhase_ = EvadePhase::None;
        }
        stand(dt);
        return;
    case HidePhase::Flee:
        if (const int32 node = findHideNode(w); node >= 0 && evadePhase_ != EvadePhase::Cornered)
        {
            toCover(node);
            stand(dt);
            return;
        }
        evade(dt, w);
        return;
    case HidePhase::ToCover:
    {
        if (w.time - hideAt_ < 0.2f)
        {
            stand(dt);
            return;
        }
        if (dist < 6.0f && !coverHidden(hideNode_, w))
        {
            choose();
            stand(dt);
            return;
        }
        const bool there = flatDistance(feet(), w.nav.node(hideNode_).position) <= 1.0f;
        // Getting nowhere for 2 s (a wall, a squad mate; ours, as the gun fight's cover): another.
        if (flatDistance(feet(), hideFrom_) > 0.5f)
        {
            hideFrom_ = feet();
            hideStuck_ = 0.0f;
        }
        else if (!there && (hideStuck_ += dt) > 2.0f)
        {
            choose();
            stand(dt);
            return;
        }
        if (!there && !route_.empty() && !followRoute(runSpeed(), dt, w))
            return;
        if (!there && flatDistance(feet(), w.nav.node(hideNode_).position) > 1.0f)
        {
            choose(); // no way there
            stand(dt);
            return;
        }
        route_.clear();
        hidePhase_ = HidePhase::Settle;
        hideAt_ = w.time;
        if (w.debugLog)
            std::cerr << "[hide] " << name_ << " crouches in cover" << std::endl;
        break;
    }
    case HidePhase::Settle:
        stand(dt);
        turnTowards(to, dt);
        if (dist < 1e-3f || forwardOf(yaw_).dot(to * (1.0f / dist)) > 0.95f || w.time - hideAt_ > 1.0f)
        {
            hidePhase_ = HidePhase::Watch;
            hideAt_ = w.time;
        }
        break;
    case HidePhase::Watch:
        stand(dt);
        for (auto& u : hideUsed_)
            if (u.first == hideNode_)
                u.second = w.time + 60.0f;
        if (hideDisturbed(w))
        {
            hidePhase_ = HidePhase::Look;
            hideAt_ = w.time;
            poseClip_.clear();
            if (w.debugLog)
                std::cerr << "[hide] " << name_ << " stands to look at him (" << dist << " m)" << std::endl;
        }
        break;
    case HidePhase::Look:
        stand(dt);
        turnTowards(to, dt);
        if (!hideDisturbed(w))
            hidePhase_ = HidePhase::Watch;
        else if (w.time - hideAt_ > 0.2f)
            choose();
        break;
    }
    if (hidePhase_ == HidePhase::Settle || hidePhase_ == HidePhase::Watch)
        if (model_->clipDuration("Sneak_Stand") > 0.0f)
            poseClip_ = "Sneak_Stand"; // crouched (ours: the player's crouch, hunters have none)
}

void Hunter::evade(float32 dt, HunterWorld& w)
{
    // CPedActEvade (update 0x577510): fleeing (0), and once it stops it holds on the ring of the
    // distance it got to: beyond 25 m 25, beyond 15 m 15, beyond 5 m 5 (state 1: he inside it, it
    // flees again); nearer it stands (state 2) while he is within 10 m (0x79c894), then flees.
    lowerGun();
    const float32 dist = flatDistance(feet(), w.player.feet);
    const Math::Vector3 to{w.player.feet.x - feet().x, 0.0f, w.player.feet.z - feet().z};
    if (evadePhase_ == EvadePhase::None)
        startFlee(w);
    switch (evadePhase_)
    {
    case EvadePhase::Flee:
        if (!route_.empty() && !followRoute(runSpeed(), dt, w))
            return;
        evadeRing_ = dist > 25.0f ? 25.0f : dist > 15.0f ? 15.0f : dist > 5.0f ? 5.0f : 0.0f;
        evadePhase_ = evadeRing_ > 0.0f ? EvadePhase::Hold : EvadePhase::Cornered;
        if (w.debugLog)
            std::cerr << "[evade] " << name_ << " holds at " << dist << " m (ring " << evadeRing_ << ")\n";
        break;
    case EvadePhase::Hold:
        if (dist < evadeRing_)
            startFlee(w);
        break;
    case EvadePhase::Cornered:
        if (dist > 10.0f)
            startFlee(w);
        break;
    case EvadePhase::None:
        break;
    }
    if (evadePhase_ != EvadePhase::Flee)
    {
        stand(dt);
        turnTowards(to, dt);
    }
}

void Hunter::startGunStrike(HunterWorld& w)
{
    // The blow with the gun: <grip>_Obstruct_Hit, landing at its OBSTRUCT_WEAPON's COMBAT_DELAYS.
    const MeleeWeapon& strike = w.tuning.meleeWeapon(weapon_ && !weapon_->obstructWeapon.empty()
                                                         ? weapon_->obstructWeapon
                                                         : std::string(gun_->twoHanded() ? "obstruct_2handedGun" : "obstruct_1handedGun"));
    std::string clip = gun_->twoHanded() ? "2Handed_Obstruct_Hit" : "1Handed_Obstruct_Hit";
    for (const char* c : {"1Handed_Obstruct_Hit", "Melee_Obstruct_Hit"})
        if (model_->clipDuration(clip) <= 0.0f)
            clip = c;
    lowerGun();
    if (w.sound)
        w.sound("SFX_WEAPON_SWING", feet() + Math::Vector3{0.0f, kChestHeight, 0.0f}, 0.5f);
    swing_ = strike.quick;
    state_ = HunterState::Attacking;
    groundSwing_ = false;
    gunStrike_ = true;
    stand(0.0f);
    model_->crossfade(clip, 0.1f, false);
    swingClip_ = std::max(0.3f, model_->clipDuration(clip));
    swingElapsed_ = 0.0f;
    swingLanded_ = false;
    loopClip_.clear();
}

bool Hunter::aimAndFire(float32 dt, HunterWorld& w, float32 dist, const Math::Vector3* from, const Math::Vector3* at)
{
    // The gun up (1Handed_ / 2Handed_ / Sniper_Aim_T) and fired (<stem>_Fire_T), CLIP_AMMO shots
    // between reloads (<stem>_Reload_T; a hunter's reload costs it no rounds: the exe 0x4f9bd9
    // takes them only from the player's). `from`: where the shot leaves (a lean); `at`: a blind shot
    // at that place, fired facing it within acos 0.9 (0x587672), finding him only near it.
    // True when it fired this frame.
    const Math::Vector3 target = at ? *at : w.player.feet;
    const Math::Vector3 to{target.x - feet().x, 0.0f, target.z - feet().z};
    const float32 toLength = to.length();
    const std::string aim = gun_->aimClip(false);
    if (!aiming_)
    {
        aiming_ = true;
        model_->playOverlay(aim);
        fireIn_ = std::max(fireIn_, 0.6f); // it raises the gun first (ours)
    }
    if (gunOverlay_ > 0.0f && (gunOverlay_ -= dt) <= 0.0f)
        model_->playOverlay(aim);
    if ((fireIn_ -= dt) > 0.0f)
        return false;
    if (ammo_ <= 0)
    {
        const std::string reload = gun_->clipStem + "_Reload_T";
        const float32 d = model_->playOverlay(reload, false) ? model_->clipDuration(reload) : 1.5f;
        gunOverlay_ = d;
        fireIn_ = d + 0.2f;
        ammo_ = gun_->clipAmmo;
        if (w.reloadGun)
            w.reloadGun(*this);
        return false;
    }
    if (toLength > 1e-3f && forwardOf(yaw_).dot(to * (1.0f / toLength)) < (at ? gun::kBlindFacing : 0.97f))
        return false; // still turning to him
    --ammo_;
    const std::string fire = gun_->clipStem + "_Fire_T";
    gunOverlay_ = model_->playOverlay(fire, false) ? std::max(0.1f, model_->clipDuration(fire)) : 0.0f;
    const float32 crouch = w.player.sneaking ? kCrouchScale : 1.0f;
    const Math::Vector3 eye = from ? *from : feet() + Math::Vector3{0.0f, kEyeHeight, 0.0f};
    const Math::Vector3 chest = w.player.feet + Math::Vector3{0.0f, kChestHeight * crouch, 0.0f};
    const float32 chance = gun_->hitChance(dist) * std::clamp(traits_.hitAccuracy / 100.0f, 0.0f, 1.0f);
    const bool near = !at || flatDistance(w.player.feet, *at) <= gun::kBlindHit;
    const bool hit = near && w.field.lineOfSight(eye, chest) && std::uniform_real_distribution<float32>(0.0f, 1.0f)(w.rng) < chance;
    if (w.shootPlayer)
        w.shootPlayer(*this, hit ? gun_->playerDamage : 0.0f, *gun_, hit);
    // The next shot as its fire clip ends (exe 0x4b6e50: a ped in the combat stance fires
    // again at the end of <Gun>_Stance_Fire while its trigger is held, and the aim component holds
    // it through each firing window: a peek's 2..3 s out, 3 s in the open). The data gives the
    // single-shot guns no rate of their own (FIRE_CYCLE_TIME is only on the uzi and commando).
    fireIn_ = std::max(gunOverlay_, 0.1f);
    return true;
}

} // namespace Game
