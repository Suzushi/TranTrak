#pragma once

#include <array>

#include "trackheader/pose.h"

namespace trackheader {

struct FilterConfig
{
    bool enabled = true;
    double rotation_smoothing = 0.085;
    double rotation_deadzone = 2.97;
    double translation_smoothing = 0.029;
    double translation_deadzone = 0.03;
};

class PoseFilter
{
public:
    explicit PoseFilter(FilterConfig config = {});

    void set_config(FilterConfig config);
    void reset();
    Pose filter(const Pose& input, double dt_seconds);

private:
    FilterConfig config_;
    std::array<double, 6> last_{};
    bool first_ = true;
};

}  // namespace trackheader

