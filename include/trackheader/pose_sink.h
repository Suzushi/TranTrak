#pragma once

#include "trackheader/pose.h"

namespace trackheader {

// Implementations may write UDP, FreeTrack shared memory, a CSV log, or a UI
// diagnostic stream. None of those platform choices belong in the core.
class PoseSink
{
public:
    virtual ~PoseSink() = default;
    virtual void send(const Pose& pose, std::int64_t timestamp_ns) = 0;
};

}  // namespace trackheader

