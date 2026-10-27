// Renderer/camera.cpp - perspective camera (projection / view / clip).
#include "Renderer/camera.h"
#include "Math/matrix.h"
#include <cmath>

namespace Renderer {

Camera::Camera()
    : Engine::Rw::Entity(static_cast<uint32>(ObjectType::Camera)) {}

void Camera::setProjection(float32 aspect, float32 fov, float32 near, float32 far) noexcept {
    aspect_ = aspect;
    fov_ = fov;
    near_ = near;
    far_ = far;
    updateProjection();
}

void Camera::setView(const Math::Vector3& pos, float32 yaw, float32 pitch, float32 roll) noexcept {
    pos_ = pos;
    yaw_ = yaw;
    pitch_ = pitch;
    roll_ = roll;
    updateView();
}

void Camera::update() noexcept {
    updateProjection();
    updateView();
}

void Camera::updateProjection() noexcept {
    const float32 f = 1.0f / std::tan(Math::radians(fov_) * 0.5f);
    Math::Matrix4x4 m = Math::Matrix4x4::identity();
    m.m00 = f / aspect_;
    m.m11 = -f; // Vulkan framebuffers are top-left origin: NDC +Y renders at the bottom. Flip Y so
                // world +Y (up) lands at screen top. Culling is VK_CULL_MODE_NONE, so the winding
                // inversion this causes is harmless.
    // Vulkan clip space has depth in [0, 1] (not OpenGL's [-1, 1]): near -> 0, far -> 1.
    // The GL form put the near half of the frustum at negative depth, where it was clipped.
    m.m22 = far_ / (near_ - far_);
    m.m23 = (near_ * far_) / (near_ - far_);
    m.m32 = -1.0f;
    m.m33 = 0.0f;
    projection_ = m;
    clip_ = projection_ * view_;
}

void Camera::updateView() noexcept {
    const Math::Vector3 negP = Math::Vector3{-pos_.x, -pos_.y, -pos_.z};
    const Math::Matrix4x4 rotX = Math::Matrix4x4::rotationX(-pitch_);
    const Math::Matrix4x4 rotY = Math::Matrix4x4::rotationY(-yaw_);
    const Math::Matrix4x4 trans = Math::Matrix4x4::translation(negP);
    view_ = Math::Matrix4x4::rotationZ(-roll_) * rotX * rotY * trans;
    clip_ = projection_ * view_; // keep clip() current even if update() is not called
}

} // namespace Renderer
