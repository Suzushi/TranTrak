#include "trackheader/pose_mapper.h"

#include <algorithm>
#include <cmath>

namespace trackheader {

namespace {

double clamp(double value, double lo, double hi)
{
    return std::max(lo, std::min(value, hi));
}

double remap_value(double value, double expo)
{
    const double x = clamp(value, -1.0, 1.0);
    const double e = clamp(expo, 0.0, 1.0);
    return (1.0 - e) * x + e * x * x * x;
}

}  // namespace

PoseMapper::PoseMapper(MappingConfig config) : config_(config) {}

void PoseMapper::set_config(MappingConfig config)
{
    config_ = config;
}

void PoseMapper::request_center()
{
    pending_center_ = true;
}

void PoseMapper::reset()
{
    home_ = {};
    has_home_ = false;
    pending_center_ = false;
}

double PoseMapper::remap_axis(double value, double input_bound,
                              double output_bound, double expo)
{
    if (std::abs(input_bound) < 1e-12)
        return 0.0;
    return remap_value(value / input_bound, expo) * output_bound;
}

Pose PoseMapper::map(const Pose& input)
{
    if (!has_home_ || pending_center_) {
        home_ = input;
        has_home_ = true;
        pending_center_ = false;
        return {};
    }

    Pose relative;
    for (int i = 0; i < 3; ++i) {
        relative.rotation_deg[i] = input.rotation_deg[i] - home_.rotation_deg[i];
        relative.translation_m[i] = input.translation_m[i] - home_.translation_m[i];
        relative.rotation_deg[i] = remap_axis(relative.rotation_deg[i],
                                              config_.input_rotation[i],
                                              config_.output_rotation[i],
                                              config_.rotation_expo[i]);
        relative.translation_m[i] = remap_axis(relative.translation_m[i],
                                               config_.input_translation[i],
                                               config_.output_translation[i],
                                               config_.translation_expo[i]);
    }
    return relative;
}

}  // namespace trackheader

