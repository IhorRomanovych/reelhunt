// tests/player_test.cpp - G1 PlayerController: verify the movement + third-person camera math.
// Self-contained (only Game/player_controller), no SDL/Vulkan/librw.
#include "Game/locomotion.h"
#include "Game/player_controller.h"
#include "Game/stamina.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{
    bool near(float a, float b, float eps)
    {
        return std::fabs(a - b) <= eps;
    }

    // Simulates one frame and, when grounded, applies the ground clamp the physics pass would
    // (hold the player at the ground plane and feed the clamped position back in).
    struct Sim
    {
        Game::PlayerController pc;
        float groundY = 0.0f;

        Game::PlayerState step(float dt, const Game::PlayerInput& in, bool onGround)
        {
            const Game::PlayerState s = pc.update(dt, in, onGround);
            if (onGround)
            {
                const Math::Vector3 p = pc.position();
                if (p.y < groundY)
                {
                    Math::Vector3 clamped = p;
                    clamped.y = groundY;
                    pc.setPosition(clamped);
                }
            }
            return s;
        }
    };

    int g_failures = 0;
    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            ++g_failures;
            std::printf("  CHECK FAIL: %s\n", what);
        }
    }

    void runMovementTests()
    {
        const float dt = 1.0f / 60.0f;

        // W (forward) at yaw 0 moves the player toward -Z.
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
            Game::PlayerInput in{};
            in.moveZ = 1.0f;
            for (int i = 0; i < 30; ++i)
                s.step(dt, in, true);
            check(s.pc.position().z < -1.0f, "W at yaw 0 moves the player toward -Z");
            check(s.pc.state() == Game::PlayerState::Walk, "moving on ground == Walk");
        }

        // D (right) at yaw 0 moves the player toward +X.
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
            Game::PlayerInput in{};
            in.moveX = 1.0f;
            for (int i = 0; i < 30; ++i)
                s.step(dt, in, true);
            check(s.pc.position().x > 1.0f, "D at yaw 0 moves the player toward +X");
        }

        // Facing +90 deg (turn right), W now moves toward -X.
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 1.5707963f, 0.0f); // yaw = +90 deg
            Game::PlayerInput in{};
            in.moveZ = 1.0f;
            for (int i = 0; i < 30; ++i)
                s.step(dt, in, true);
            check(s.pc.position().x < -1.0f, "W after turning +90 deg moves toward -X");
        }

        // No input on the ground: velocity decays to (near) zero (snappy stop).
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
            Game::PlayerInput fwd{};
            fwd.moveZ = 1.0f;
            for (int i = 0; i < 20; ++i)
                s.step(dt, fwd, true);
            const float speedAfterMove = s.pc.velocity().length();
            Game::PlayerInput none{};
            for (int i = 0; i < 30; ++i)
                s.step(dt, none, true);
            check(s.pc.velocity().length() < 1e-2f, "velocity decays to ~0 after release");
            check(s.pc.state() == Game::PlayerState::Idle, "stationary on ground == Idle");
            check(speedAfterMove > 1.0f, "reached forward speed while W was held");
        }

        // Crouch: held + moving == CrouchMove, eye height drops; held + still == Crouch.
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
            Game::PlayerInput in{};
            in.crouch = true;
            for (int i = 0; i < 5; ++i)
                s.step(dt, in, true);
            check(s.pc.state() == Game::PlayerState::Crouch, "crouched + stationary == Crouch");
            check(s.pc.eyeHeight() < s.pc.tuning().standEye, "crouch lowers the eye height");
            for (int i = 0; i < 30; ++i)
                s.step(dt, in, true);
            check(s.pc.eyeHeight() == s.pc.tuning().crouchEye, "... to the crouch height");
            in.moveZ = 1.0f;
            for (int i = 0; i < 60; ++i)
                s.step(dt, in, true);
            check(s.pc.state() == Game::PlayerState::CrouchMove, "crouched + moving == CrouchMove");
            check(s.pc.position().z < -0.5f, "crouch still moves forward");
        }

        // Run (held) == Run and is faster than walk.
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
            Game::PlayerInput in{};
            in.moveZ = 1.0f;
            in.run = true;
            for (int i = 0; i < 20; ++i)
                s.step(dt, in, true);
            check(s.pc.state() == Game::PlayerState::Run, "run held + moving == Run");
            check(s.pc.velocity().length() > s.pc.tuning().walkSpeed, "run is faster than walk");
        }
    }

    void runGravityJumpTests()
    {
        const float dt = 1.0f / 60.0f;

        // Jump launches upward, then gravity brings the player down (Jump -> Fall).
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
            Game::PlayerInput jump{};
            jump.jump = true;
            s.step(dt, jump, true); // launch frame (still grounded this frame)
            check(s.pc.velocity().y > 0.0f, "jump sets upward velocity");
            check(near(s.pc.velocity().y, s.pc.tuning().jumpSpeed, 1e-3f), "launch speed == jumpSpeed");

            float maxY = s.pc.position().y;
            bool sawJump = false, sawFall = false;
            for (int i = 0; i < 60; ++i)
            {
                s.step(dt, {}, false); // airborne
                if (s.pc.position().y > maxY)
                    maxY = s.pc.position().y;
                if (s.pc.state() == Game::PlayerState::Jump)
                    sawJump = true;
                if (s.pc.state() == Game::PlayerState::Fall)
                    sawFall = true;
            }
            check(maxY > 0.5f, "player rose well above the launch point");
            check(sawJump, "ascending airborne state == Jump");
            check(sawFall, "descending airborne state == Fall");
            check(s.pc.velocity().y < 0.0f, "player is falling after the arc");
        }

        // In the air with no jump: pure fall (gravity integrates velocity down).
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 10.0f, 0.0f}, 0.0f, 0.0f);
            for (int i = 0; i < 30; ++i)
                s.step(dt, {}, false);
            check(s.pc.velocity().y < 0.0f, "gravity pulls the airborne player down");
            check(s.pc.position().y < 10.0f, "airborne player descends");
            check(s.pc.state() == Game::PlayerState::Fall, "airborne + descending == Fall");
        }
    }

    void runCameraTests()
    {
        // At yaw 0 the camera sits behind (+Z) the pivot (0.3 m to his right, 2 m up), camDist
        // away, level with it (exe 0x58c020).
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
            const Math::Vector3 c = s.pc.cameraPosition();
            check(near(c.x, s.pc.tuning().camSide, 1e-3f), "camera is behind the pivot, to his right, at yaw 0");
            check(c.z > 0.0f, "camera is behind the player (forward is -Z)");
            check(near(c.z, s.pc.tuning().camDist, 1e-2f), "camera distance == camDist");
            check(near(c.y, s.pc.tuning().standEye, 1e-2f), "camera level with the pivot");
            // Looking down 30 degrees raises it over the pivot along the view.
            s.pc.setLook(0.0f, -0.5236f);
            s.step(1.0f / 60.0f, {}, true);
            const Math::Vector3 d = s.pc.cameraPosition() - s.pc.cameraPivot();
            check(near(d.length(), s.pc.tuning().camDist, 1e-2f) && d.y > 1.9f, "the pitch swings it round the pivot");
        }

        // The camera follows the player: after moving, the camera has tracked the new position.
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
            const float dt = 1.0f / 60.0f;
            Game::PlayerInput in{};
            in.moveZ = 1.0f;
            for (int i = 0; i < 40; ++i)
                s.step(dt, in, true);
            const Math::Vector3 p = s.pc.position();
            const Math::Vector3 c = s.pc.cameraPosition();
            // The camera stays on the player's forward axis, behind and above.
            check(c.z - p.z > 0.0f, "camera stays behind the (moved) player");
            check(c.y > p.y, "camera stays above the player");
            // It rides him: the pivot moves with him at once.
            const float dx = c.x - p.x, dz = c.z - p.z;
            const float side = s.pc.tuning().camSide, dist = s.pc.tuning().camDist;
            check(near(std::sqrt(dx * dx + dz * dz), std::sqrt(side * side + dist * dist), 0.01f),
                  "camera keeps its place by him while following");
        }

        // Crouching drops the camera to the crouch eye height.
        {
            Sim s;
            s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
            const float dt = 1.0f / 60.0f;
            Game::PlayerInput in{};
            in.crouch = true;
            for (int i = 0; i < 40; ++i)
                s.step(dt, in, true);
            const Math::Vector3 c = s.pc.cameraPosition();
            check(near(c.y, s.pc.tuning().crouchEye, 0.1f), "crouching lowers the follow camera");
        }
    }
}

void runLocomotionTests()
{
    using S = Game::PlayerState;
    auto clip = [](S st, float x, float z, bool run) { return Game::locomotionClip({.state = st, .moveX = x, .moveZ = z, .run = run}); };
    check(clip(S::Idle, 0, 0, false) == "Stand", "standing plays Stand");
    check(clip(S::Walk, 0, 1, false) == "Run_Fwd", "moving forward plays Run_Fwd");
    check(clip(S::Walk, 0, -1, false) == "Run_Bkw", "moving back plays Run_Bkw");
    check(clip(S::Walk, -1, 0, false) == "Run_Left", "strafing left plays Run_Left");
    check(clip(S::Walk, 0.6f, 1, false) == "Run_Fwd", "the dominant axis picks the direction");
    check(clip(S::Run, 1, 0, true) == "Sprint_Right", "RUN strafing right plays Sprint_Right");
    check(clip(S::Crouch, 0, 0, false) == "Sneak_Stand", "sneaking still plays Sneak_Stand");
    check(clip(S::CrouchMove, 0, 1, false) == "Sneak_Walk_Fwd", "sneaking forward plays Sneak_Walk_Fwd");
    check(clip(S::CrouchMove, 0, -1, true) == "Sneak_Run_Bkw", "sneak + RUN back plays Sneak_Run_Bkw");
    auto mix = [](S st, float x, float z, bool run) { return Game::locomotionMix({.state = st, .moveX = x, .moveZ = z, .run = run}); };
    const Game::LocomotionMix diag = mix(S::Walk, -1, 1, false);
    check(diag.clip == "Run_Left" && std::fabs(diag.weight - 0.5f) < 1e-5f,
          "forward + left: Run_Fwd with half of Run_Left blended in");
    const Game::LocomotionMix sneakDiag = mix(S::CrouchMove, 1, -1, false);
    check(sneakDiag.clip == "Sneak_Walk_Right" && std::fabs(sneakDiag.weight - 0.5f) < 1e-5f,
          "sneaking back + right blends Sneak_Walk_Right into Sneak_Walk_Bkw");
    check(mix(S::Walk, 0, 1, false).clip.empty() && mix(S::Idle, 0, 0, false).clip.empty() &&
              mix(S::Crouch, 0, 0, false).clip.empty(),
          "straight movement and standing blend nothing");

    // Sneak + RUN moves at the sneak-run speed, faster than a plain sneak.
    const float dt = 1.0f / 60.0f;
    auto settle = [&](bool run) {
        Sim s;
        s.pc.reset(Math::Vector3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
        Game::PlayerInput in{};
        in.moveZ = 1.0f;
        in.crouch = true;
        in.run = run;
        for (int i = 0; i < 60; ++i)
            s.step(dt, in, true);
        return s.pc.velocity().length();
    };
    check(std::fabs(settle(true) - Game::PlayerTuning{}.crouchRunSpeed) < 1e-2f, "sneak + RUN settles at crouchRunSpeed");
    check(settle(true) > settle(false), "sneak + RUN is faster than sneaking");
}

// Game/stamina.h: sprint drain, out of breath until 0.4, refill by pace (exe 0x4626c0 / 0x466e10).
void runStaminaTests()
{
    Game::Stamina st;
    st.setTuning({}); // the player record's: 20 s of sprint, refills over 80 / 54 / 22 s
    for (int i = 0; i < 300; ++i)
        st.update(1.0f / 30.0f, true, Game::Stamina::Pace::Running); // 10 s
    check(near(st.level(), 0.5f, 0.01f) && st.canSprint(), "10 s of sprinting uses half the stamina");
    int ticks = 0;
    while (st.canSprint() && ticks < 1000)
    {
        st.update(1.0f / 30.0f, true, Game::Stamina::Pace::Running);
        ++ticks;
    }
    check(st.level() == 0.0f && !st.canSprint() && ticks > 290 && ticks < 300, "about 10 s more empties it: no more sprinting");
    for (int i = 0; i < 30 * 8; ++i)
        st.update(1.0f / 30.0f, false, Game::Stamina::Pace::Still); // 8 s still: 8 / 22
    check(!st.canSprint() && near(st.level(), 8.0f / 22.0f, 0.01f), "standing refills over 22 s; still out of breath below 0.4");
    for (int i = 0; i < 30; ++i)
        st.update(1.0f / 30.0f, false, Game::Stamina::Pace::Still);
    check(st.canSprint(), "back to 0.4: sprinting again");
    Game::Stamina run;
    run.setTuning({});
    for (int i = 0; i < 300; ++i)
        run.update(1.0f / 30.0f, true, Game::Stamina::Pace::Running);
    for (int i = 0; i < 30 * 8; ++i)
        run.update(1.0f / 30.0f, false, Game::Stamina::Pace::Running);
    check(near(run.level(), 0.5f + 8.0f / 80.0f, 0.01f), "running refills over 80 s");
}

int main()
{
    std::printf("G1 PlayerController tests\n");
    runMovementTests();
    runGravityJumpTests();
    runCameraTests();
    runLocomotionTests();
    runStaminaTests();
    std::printf("%s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
