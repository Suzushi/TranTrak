#pragma once

#include <cstdint>

#include "trackheader/pose.h"

namespace trackheader {

enum class PixelFormat
{
    bgr8,
    gray8,
};

// A non-owning image view. The capture adapter owns the memory and controls
// whether old frames are dropped; the estimator only reads it during estimate.
struct ImageView
{
    const std::uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride_bytes = 0;
    PixelFormat format = PixelFormat::bgr8;
    std::int64_t timestamp_ns = 0;

    bool valid() const
    {
        return data != nullptr && width > 0 && height > 0 && stride_bytes > 0;
    }
};

class PoseEstimator
{
public:
    virtual ~PoseEstimator() = default;
    virtual PoseSample estimate(const ImageView& frame) = 0;
};

}  // namespace trackheader

