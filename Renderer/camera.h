// Renderer/camera.h - perspective camera (projection / view / clip) as a scene entity.
#pragma once
#include "engine_types.h"
#include "renderware_api_stub.h"
#include "Math/math.h"
#include "Math/vector.h"
#include "Math/matrix.h"
#include "Math/transform.h"
#include "Renderer/smartref.h"

namespace Renderer {

// Camera entity. Builds a Vulkan perspective projection (depth 0..1, +Y up on screen), a view
// matrix from position/yaw/pitch, and the clip matrix (projection * view). Call update() after
// changing the projection or view so clip() is current.
class Camera : public Engine::Rw::Entity {
public:
    Camera();

    [[nodiscard]] uint32 type() const noexcept override { return static_cast<uint32>(ObjectType::Camera); }

    // --- projection ---
    void setProjection(float32 aspect, float32 fov, float32 near, float32 far) noexcept;
    [[nodiscard]] Math::Matrix4x4 projection() const noexcept { return projection_; }
    [[nodiscard]] float32 fov() const noexcept { return fov_; }       // vertical, degrees
    [[nodiscard]] float32 aspect() const noexcept { return aspect_; }
    [[nodiscard]] float32 nearPlane() const noexcept { return near_; }
    [[nodiscard]] float32 farPlane() const noexcept { return far_; }

    // --- view ---
    // `roll` turns the view about its line of sight (radians, clockwise on screen).
    void setView(const Math::Vector3& pos, float32 yaw, float32 pitch, float32 roll = 0.0f) noexcept;
    [[nodiscard]] Math::Matrix4x4 view() const noexcept { return view_; }
    [[nodiscard]] Math::Vector3 position() const noexcept { return pos_; }
    [[nodiscard]] float32 yaw() const noexcept { return yaw_; }
    [[nodiscard]] float32 pitch() const noexcept { return pitch_; }

    // --- clip (projection * view) ---
    [[nodiscard]] Math::Matrix4x4 clip() const noexcept { return clip_; }

    void update() noexcept;

private:
    Math::Vector3 pos_ = Math::Vector3{0.0f, 0.0f, 0.0f};
    float32 yaw_ = 0.0f;
    float32 pitch_ = 0.0f;
    float32 roll_ = 0.0f;
    float32 aspect_ = 1.0f;
    float32 fov_ = 45.0f;
    float32 near_ = 0.1f;
    float32 far_ = 100.0f;

    Math::Matrix4x4 projection_ = Math::Matrix4x4::identity();
    Math::Matrix4x4 view_ = Math::Matrix4x4::identity();
    Math::Matrix4x4 clip_ = Math::Matrix4x4::identity();

    void updateProjection() noexcept;
    void updateView() noexcept;
};

} // namespace Renderer
