#include "trackheader/tracker_core.h"

#include <algorithm>

namespace trackheader {

TrackerCore::TrackerCore(CoreConfig config)
    : config_(config), mapper_(config.mapping), filter_(config.filter)
{
}

void TrackerCore::set_config(CoreConfig config)
{
    config_ = config;
    mapper_.set_config(config.mapping);
    filter_.set_config(config.filter);
}

void TrackerCore::add_sink(PoseSink& sink)
{
    sinks_.push_back(&sink);
}

void TrackerCore::clear_sinks()
{
    sinks_.clear();
}

void TrackerCore::center()
{
    mapper_.request_center();
    filter_.reset();
}

void TrackerCore::reset()
{
    mapper_.reset();
    filter_.reset();
    latest_pose_ = {};
    last_timestamp_ns_ = 0;
    has_pose_ = false;
}

bool TrackerCore::submit(const PoseSample& sample)
{
    if (!sample.valid)
        return false;

    double dt = 0.004;
    if (last_timestamp_ns_ > 0 && sample.timestamp_ns > last_timestamp_ns_)
        dt = static_cast<double>(sample.timestamp_ns - last_timestamp_ns_) * 1e-9;
    last_timestamp_ns_ = sample.timestamp_ns;

    latest_pose_ = filter_.filter(mapper_.map(sample.pose), dt);
    has_pose_ = true;
    for (PoseSink* sink : sinks_)
        if (sink)
            sink->send(latest_pose_, sample.timestamp_ns);
    return true;
}

}  // namespace trackheader

