#pragma once

#include <array>
#include <cstdint>

namespace trackheader {

// Rotation order is yaw, pitch, roll in degrees. Translation is x, y, z in
// meters. This is the only pose representation crossing the core boundary.
struct Pose
{
    std::array<double, 3> rotation_deg{0.0, 0.0, 0.0};
    std::array<double, 3> translation_m{0.0, 0.0, 0.0};
};

struct PoseSample
{
    Pose pose{};
    std::int64_t timestamp_ns = 0;
    double confidence = 0.0;
    bool valid = false;
};

}  // namespace trackheader

