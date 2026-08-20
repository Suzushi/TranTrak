#include "trackheader/pose_filter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace trackheader {

namespace {

double clamp(double value, double lo, double hi)
{
    return std::max(lo, std::min(value, hi));
}

double wrap_delta(double delta)
{
    while (delta > 180.0)
        delta -= 360.0;
    while (delta < -180.0)
        delta += 360.0;
    return delta;
}

}  // namespace

PoseFilter::PoseFilter(FilterConfig config) : config_(config) {}

void PoseFilter::set_config(FilterConfig config)
{
    config_ = config;
}

void PoseFilter::reset()
{
    last_ = {};
    first_ = true;
}

Pose PoseFilter::filter(const Pose& input, double dt_seconds)
{
    if (!config_.enabled)
        return input;

    std::array<double, 6> values{
        input.translation_m[0], input.translation_m[1], input.translation_m[2],
        input.rotation_deg[0], input.rotation_deg[1], input.rotation_deg[2]};

    if (first_) {
        last_ = values;
        first_ = false;
        return input;
    }

    const double dt = clamp(dt_seconds, 0.0001, 0.1);
    for (int i = 0; i < 6; ++i) {
        const bool rotation = i >= 3;
        double delta = values[i] - last_[i];
        if (rotation)
            delta = wrap_delta(delta);

        const double deadzone = rotation ? config_.rotation_deadzone
                                         : config_.translation_deadzone;
        const double smoothing = rotation ? config_.rotation_smoothing
                                          : config_.translation_smoothing;
        if (std::abs(delta) <= deadzone)
            delta = 0.0;
        else
            delta -= std::copysign(deadzone, delta);

        const double scale = std::max(smoothing, 1e-6);
        last_[i] += delta * dt / scale;
        if (rotation)
            last_[i] = wrap_delta(last_[i]);
    }

    return Pose{
        {last_[3], last_[4], last_[5]},
        {last_[0], last_[1], last_[2]}};
}

}  // namespace trackheader

