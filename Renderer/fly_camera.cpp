// Renderer/fly_camera.cpp - WASD/Space/C/Q + hold-LMB free-fly camera controller.
#include "Renderer/fly_camera.h"

#include <cmath>
#include <iostream>

#include "Math/math.h"
#include "Math/matrix.h"

namespace Renderer {

void FlyCamera::handleEvent(const SDL_Event &evt)
{
    if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt.button.button == SDL_BUTTON_LEFT)
    {
        mouseLook_ = true; // hold LMB to look (click-drag)
    }
    else if (evt.type == SDL_EVENT_MOUSE_BUTTON_UP && evt.button.button == SDL_BUTTON_LEFT)
    {
        mouseLook_ = false;
    }
    else if (evt.type == SDL_EVENT_MOUSE_MOTION && mouseLook_)
    {
        const float32 sens = 0.0022f;
        yaw_ -= static_cast<float32>(evt.motion.xrel) * sens;
        pitch_ -= static_cast<float32>(evt.motion.yrel) * sens;
        const float32 lim = Math::radians(89.0f);
        if (pitch_ > lim)
            pitch_ = lim;
        if (pitch_ < -lim)
            pitch_ = -lim;
    }
}

void FlyCamera::update(float32 dt, Camera *cam)
{
    const bool *keys = SDL_GetKeyboardState(nullptr); // indexed by SDL_Scancode in SDL3
    // World-space camera axes from the same rotation Camera::updateView uses (Rx(-pitch)*Ry(-yaw)).
    const Math::Matrix4x4 m = Math::Matrix4x4::rotationX(-pitch_) * Math::Matrix4x4::rotationY(-yaw_);
    const Math::Vector3 fwd{-m.m20, -m.m21, -m.m22}; // camera forward in world
    const Math::Vector3 rgt{m.m00, m.m01, m.m02};    // camera right in world
    const Math::Vector3 upv{m.m10, m.m11, m.m12};    // camera up in world
    Math::Vector3 mv{};
    if (keys[SDL_SCANCODE_W])
        mv += fwd;
    if (keys[SDL_SCANCODE_S])
        mv -= fwd;
    if (keys[SDL_SCANCODE_D])
        mv += rgt;
    if (keys[SDL_SCANCODE_A])
        mv -= rgt;
    if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_E])
        mv += upv;
    if (keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_Q])
        mv -= upv;
    const float32 mlen = mv.length();
    if (mlen > 1e-6f)
    {
        const float32 speed = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) ? 45.0f : 14.0f;
        pos_ += mv * ((speed * dt) / mlen);
    }
    cam->setView(pos_, yaw_, pitch_);
    cam->update();
    { // Log the camera transform so a known view can be reproduced in automated screenshots.
        const bool changed = std::abs(pos_.x - lastPos_.x) > 1e-4f || std::abs(pos_.y - lastPos_.y) > 1e-4f || std::abs(pos_.z - lastPos_.z) > 1e-4f || std::abs(yaw_ - lastYaw_) > 1e-5f || std::abs(pitch_ - lastPitch_) > 1e-5f;
        if (changed)
        {
            lastPos_ = pos_;
            lastYaw_ = yaw_;
            lastPitch_ = pitch_;
            std::cerr << "main: [cam] pos=" << pos_.x << "," << pos_.y << "," << pos_.z
                      << " yawDeg=" << Math::degrees(yaw_)
                      << " pitchDeg=" << Math::degrees(pitch_) << "\n";
        }
    }
}

} // namespace Renderer
