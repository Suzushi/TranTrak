#pragma once

#include <array>

#include "trackheader/pose.h"

namespace trackheader {

struct MappingConfig
{
    std::array<double, 3> input_translation{0.307, 0.118, 0.307};
    std::array<double, 3> output_translation{0.769, 0.734, 0.755};
    std::array<double, 3> translation_expo{0.0, 0.0, 0.0};
    std::array<double, 3> input_rotation{25.75, 15.95, 44.65};
    std::array<double, 3> output_rotation{178.5, 103.5, 43.5};
    std::array<double, 3> rotation_expo{0.0, 0.0, 0.0};
};

class PoseMapper
{
public:
    explicit PoseMapper(MappingConfig config = {});

    void set_config(MappingConfig config);
    void request_center();
    void reset();
    Pose map(const Pose& input);

    static double remap_axis(double value, double input_bound,
                             double output_bound, double expo);

private:
    MappingConfig config_;
    Pose home_{};
    bool has_home_ = false;
    bool pending_center_ = false;
};

}  // namespace trackheader

