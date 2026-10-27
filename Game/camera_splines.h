// Game/camera_splines.h - a level's scripted camera paths (PlaySplineFileDefault).
//
// levels/<level>/spl/splines.spl is the binary concatenation of every spline the level uses,
// in the order of the RECORDs after "#STARTCODE" in the tuning pak's levels/<level>/spl/splines.ini
// (50 on Jury_Turf). A block: u32 12, u32 (block size - 12), u8 ff ff 00 08, 32 bytes of zeros,
// u32 point count at +0x2c, u32 1, then the points (3 floats each, the level script's frame).
// The record says what the spline is for: ISFOLLOW 1 = the path the camera moves along,
// 0 = the point it looks at; ISSMOOTH, ISALIGNEDFOLLOWPATH and DURATION (seconds).
// The scripts start a FOLLOW and a LOOKAT spline together and wait on IsSplinePlaying.
#pragma once
#include "engine_types.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Math/vector.h"

namespace Game {

struct CameraSpline {
    std::vector<Math::Vector3> points; // script frame
    float32 duration = 1.0f;           // seconds
    bool follow = true;                // camera path (true) or look-at path (false)
    bool smooth = true;                // Catmull-Rom through the points, else straight segments

    // The point at fraction t (0..1) of the path; each segment between two points takes an
    // equal share of the time.
    [[nodiscard]] Math::Vector3 at(float32 t) const;
};

class CameraSplines {
public:
    // `ini` is the text of splines.ini, `splPath` the level's spl/splines.spl.
    bool load(const std::string& ini, const std::string& splPath);
    [[nodiscard]] const CameraSpline* find(const std::string& name) const;
    [[nodiscard]] std::size_t size() const noexcept { return splines_.size(); }

private:
    std::unordered_map<std::string, CameraSpline> splines_; // upper-case name
};

} // namespace Game
