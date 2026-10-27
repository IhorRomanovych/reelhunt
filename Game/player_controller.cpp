// Game/player_controller.cpp - G1 player movement + third-person follow camera (pure math).
#include "Game/player_controller.h"

#include <algorithm>
#include <cmath>

namespace Game
{
void PlayerController::reset(const Math::Vector3& spawn, float32 yaw, float32 pitch) noexcept
{
    pos_ = spawn;
    vel_ = Math::Vector3{};
    yaw_ = yaw;
    pitch_ = pitch;
    crouching_ = false;
    state_ = PlayerState::Idle;
    pivotHeight_ = tuning_.standEye;
    camPos_ = idealCameraPos();
}

void PlayerController::groundBasis(float32 yaw, Math::Vector3& fwd, Math::Vector3& rgt) noexcept
{
    const Math::Matrix4x4 m = Math::Matrix4x4::rotationY(-yaw);
    fwd = Math::Vector3{-m.m20, 0.0f, -m.m22}; // forward on the ground plane (-Z at yaw 0)
    rgt = Math::Vector3{m.m00, 0.0f, m.m02};   // right on the ground plane (+X at yaw 0)
}

Math::Vector3 PlayerController::cameraPivot() const noexcept
{
    Math::Vector3 fwd{}, rgt{};
    groundBasis(yaw_, fwd, rgt);
    return pos_ + rgt * tuning_.camSide + Math::Vector3{0.0f, pivotHeight_ - tuning_.feetBelow, 0.0f};
}

Math::Vector3 PlayerController::idealCameraPos() const noexcept
{
    // Back from the pivot along the view: (0, 0, -3.9) turned by the pitch, then the heading.
    Math::Vector3 fwd{}, rgt{};
    groundBasis(yaw_, fwd, rgt);
    const Math::Vector3 view = fwd * std::cos(pitch_) + Math::Vector3{0.0f, std::sin(pitch_), 0.0f};
    return cameraPivot() - view * tuning_.camDist;
}

Math::Quaternion PlayerController::orientation() const noexcept
{
    return Math::Quaternion::fromAxisAngle(Math::Vector3{0.0f, 1.0f, 0.0f}, yaw_);
}

PlayerState PlayerController::update(float32 dt, const PlayerInput& in, bool onGround)
{
    // --- look (mouse) ---
    yaw_ -= in.mouseDX * tuning_.lookSens;
    pitch_ -= in.mouseDY * tuning_.lookSens;
    if (pitch_ > tuning_.maxPitch)
        pitch_ = tuning_.maxPitch;
    if (pitch_ < -tuning_.maxPitch)
        pitch_ = -tuning_.maxPitch;

    // --- crouch (held, ground only) ---
    crouching_ = in.crouch && onGround;

    // --- desired horizontal velocity (camera-relative) ---
    Math::Vector3 fwd{}, rgt{};
    groundBasis(yaw_, fwd, rgt);
    Math::Vector3 wish = fwd * in.moveZ + rgt * in.moveX;
    const float32 wishLen = wish.length();
    const bool moving = wishLen > 1e-4f;
    if (moving)
        wish = wish * (1.0f / wishLen);
    float32 targetSpeed = crouching_ ? (in.run ? tuning_.crouchRunSpeed : tuning_.crouchSpeed)
                        : (in.run ? tuning_.runSpeed : tuning_.walkSpeed);
    if (in.maxSpeed > 0.0f && targetSpeed > in.maxSpeed)
        targetSpeed = in.maxSpeed;
    // (A pad's stick tilted part way moves him slower: ours.)
    const Math::Vector3 targetVel = wish * (moving ? targetSpeed * std::min(1.0f, wishLen) : 0.0f);

    // --- horizontal acceleration (approach target; snappy stop toward 0 when idle) ---
    {
        const Math::Vector3 cur{vel_.x, 0.0f, vel_.z};
        Math::Vector3 delta = targetVel - cur;
        const float32 dLen = delta.length();
        if (dLen > 1e-6f)
        {
            const float32 rate = onGround ? tuning_.accel : tuning_.accel * tuning_.airControl;
            const float32 step = rate * dt;
            const float32 k = dLen > step ? step / dLen : 1.0f;
            vel_.x += delta.x * k;
            vel_.z += delta.z * k;
        }
    }

    // --- vertical: gravity always, jump on the edge while grounded ---
    vel_.y -= tuning_.gravity * dt;
    if (in.jump && onGround)
        vel_.y = tuning_.jumpSpeed;
    // The ground supports the player: no downward velocity while grounded (prevents the
    // falling velocity from accumulating and leaking into horizontal speed / camera).
    if (onGround && vel_.y < 0.0f)
        vel_.y = 0.0f;

    // --- integrate position ---
    pos_ = pos_ + vel_ * dt;

    // --- the follow camera: it rides him; the pivot's height moves toward the stance's ---
    const float32 targetHeight = crouching_ ? tuning_.crouchEye : tuning_.standEye;
    const float32 step = tuning_.pivotStep * dt;
    pivotHeight_ += std::clamp(targetHeight - pivotHeight_, -step, step);
    camPos_ = idealCameraPos();

    // --- state machine ---
    if (state_ != PlayerState::Dead)
    {
        if (!onGround)
        {
            state_ = vel_.y > 0.0f ? PlayerState::Jump : PlayerState::Fall;
        }
        else if (crouching_)
        {
            state_ = moving ? PlayerState::CrouchMove : PlayerState::Crouch;
        }
        else
        {
            state_ = !moving ? PlayerState::Idle
                    : (in.run ? PlayerState::Run : PlayerState::Walk);
        }
    }
    return state_;
}
} // namespace Game
