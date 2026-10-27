// Game/level_frame.h - the one mapping from level-file coordinates to the renderer's scene frame.
//
// Everything placed in a level - world (.bsp) geometry, .inst props and their .col collision
// spheres - goes through this struct so visuals and collision can never disagree.
#pragma once
#include "engine_types.h"

#include <cmath>

#include "Math/matrix.h"
#include "Math/quaternion.h"
#include "Math/vector.h"

namespace Game {

struct LevelFrame {
    // Recentre (world bounding-box centre) + uniform scale (1 = RenderWare metres), see LevelContent.
    Math::Vector3 center{0.0f, 0.0f, 0.0f};
    float32 scale = 1.0f;

    // The scene frame is Y-up and the camera projection already maps world +Y to screen-up,
    // so level geometry is used as authored (no Y reflection).
    // --- RenderWare world (.bsp, Y-up) ---
    [[nodiscard]] Math::Vector3 worldPoint(const Math::Vector3& p) const noexcept {
        return (p - center) * scale;
    }
    [[nodiscard]] static Math::Vector3 worldNormal(const Math::Vector3& n) noexcept { return n; }

    // --- .inst placements (Z-up level coordinates: x, y = ground plane, z = height) ---
    // Scene position of a placement's model origin.
    [[nodiscard]] Math::Vector3 placementOrigin(const Math::Vector3& instPos) const noexcept {
        return worldPoint(Math::Vector3{instPos.x, instPos.z, -instPos.y});
    }

    // Rotation taking a model-local direction (DFF / COL local frame, Y-up) to the scene for a
    // placement whose .inst rotation is the quaternion (x, y, z, w):
    //   toScene * R(q*) * modelToLevel, where modelToLevel maps local +Y (up) to level +Z (up)
    //   and toScene maps level (x, y, z) to scene (x, z, -y).
    // The .inst stores the inverse rotation (RenderWare frames keep the conjugate): with q itself
    // Jury_Turf's double doors Gen_Admin_(D) / (D)01 (yaw -91 / +91 degrees, hinges 2.98 m apart,
    // 1.5 m leaves) point away from each other into the wall; with q* they close their frame.
    // A yaw about level +Z becomes a yaw about scene +Y (heading preserved); identity stays upright.
    [[nodiscard]] static Math::Matrix4x4 placementRotation(const Math::Vector4& quatXYZW) noexcept {
        Math::Matrix4x4 modelToLevel;
        modelToLevel.m11 = 0.0f; modelToLevel.m12 = -1.0f;
        modelToLevel.m21 = 1.0f; modelToLevel.m22 = 0.0f;
        Math::Matrix4x4 toScene;
        toScene.m11 = 0.0f; toScene.m12 = 1.0f;
        toScene.m21 = -1.0f; toScene.m22 = 0.0f;
        const Math::Quaternion q =
            Math::Quaternion{quatXYZW.w, -quatXYZW.x, -quatXYZW.y, -quatXYZW.z}.normalized();
        return toScene * q.toMatrix().make4x4() * modelToLevel;
    }

    // Scene position of a model-local point (in model units) of a placed object.
    [[nodiscard]] Math::Vector3 placedPoint(const Math::Matrix4x4& rotation, const Math::Vector3& origin,
                                            const Math::Vector3& local) const noexcept {
        return rotation.multiplyPoint(local * scale) + origin;
    }
    [[nodiscard]] static Math::Vector3 placedNormal(const Math::Matrix4x4& rotation, const Math::Vector3& n) noexcept {
        return rotation.multiplyPoint(n).normalized();
    }
    // Heading (yaw about scene +Y, 0 = facing scene -Z) of a placement: the yaw of the inverse of
    // its quaternion (see placementRotation).
    [[nodiscard]] static float32 placementYaw(const Math::Vector4& quatXYZW) noexcept {
        return -2.0f * std::atan2(quatXYZW.z, quatXYZW.w);
    }
    // The facing of a placed character (Player_Inst, Hunter_Inst): an unrotated character faces
    // level -Y, half a turn from placementYaw. Jury_Turf shows it: the player walks from his
    // start (Player_Inst ~7 degrees) along scene +Z to hDummy (~184 degrees), and trigger_07's
    // first-execution scene has him reach hDummy from behind, coming from +Z.
    [[nodiscard]] static float32 characterYaw(const Math::Vector4& quatXYZW) noexcept {
        return placementYaw(quatXYZW) + 3.14159265358979f;
    }
};

} // namespace Game
