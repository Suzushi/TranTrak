#pragma once

#include <cstdint>
#include <vector>

#include "trackheader/pose_filter.h"
#include "trackheader/pose_mapper.h"
#include "trackheader/pose_sink.h"

namespace trackheader {

struct CoreConfig
{
    MappingConfig mapping{};
    FilterConfig filter{};
};

class TrackerCore
{
public:
    explicit TrackerCore(CoreConfig config = {});

    void set_config(CoreConfig config);
    void add_sink(PoseSink& sink);
    void clear_sinks();

    void center();
    void reset();

    // Called by the host's capture/inference loop. No internal queue is used:
    // the caller decides how stale frames are dropped before this boundary.
    bool submit(const PoseSample& sample);

    const Pose& latest_pose() const { return latest_pose_; }
    bool has_pose() const { return has_pose_; }

private:
    CoreConfig config_;
    PoseMapper mapper_;
    PoseFilter filter_;
    std::vector<PoseSink*> sinks_;
    Pose latest_pose_{};
    std::int64_t last_timestamp_ns_ = 0;
    bool has_pose_ = false;
};

}  // namespace trackheader

