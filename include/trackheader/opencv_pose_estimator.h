#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "trackheader/pose_estimator.h"

namespace trackheader {

struct OpenCvPoseEstimatorConfig
{
    std::string yunet_model_path;
    std::string landmark_model_path;
    std::string face_model_path;
    std::string calibration_path;

    int camera_width = 640;
    int camera_height = 480;
    // Used as a fallback/documentation value. init() reads the actual ONNX
    // shape, so models with 112/14 and 224/28 layouts are both supported.
    int landmark_nn_size = 112;
    int landmark_nn_output = 14;
    float face_score_threshold = 0.6f;
    float face_nms_threshold = 0.3f;
    double redetect_confidence = 0.35;
    double cervical_face_model = -0.1;
    double max_reprojection_error_px = 12.0;
    double max_rotation_speed_deg_s = 1200.0;
    double max_translation_speed_m_s = 2.0;
};

struct VisionDebugInfo
{
    cv::Rect face_roi{};
    std::vector<cv::Point2f> landmarks;
    double detect_ms = 0.0;
    double landmark_ms = 0.0;
    double pnp_ms = 0.0;
    double pnp_reprojection_error_px = 0.0;
    int pnp_inliers = 0;
    double confidence = 0.0;
    bool face_found = false;
};

// OpenCV/ONNX Runtime implementation of the old VisionPipeline. The public
// core remains free of both dependencies; only this adapter knows image APIs
// and model formats.
class OpenCvPoseEstimator final : public PoseEstimator
{
public:
    OpenCvPoseEstimator();
    ~OpenCvPoseEstimator() override;

    OpenCvPoseEstimator(OpenCvPoseEstimator&&) noexcept;
    OpenCvPoseEstimator& operator=(OpenCvPoseEstimator&&) noexcept;
    OpenCvPoseEstimator(const OpenCvPoseEstimator&) = delete;
    OpenCvPoseEstimator& operator=(const OpenCvPoseEstimator&) = delete;

    bool init(const OpenCvPoseEstimatorConfig& config);
    bool ready() const;
    void reset();

    PoseSample estimate(const ImageView& frame) override;
    const VisionDebugInfo& debug() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace trackheader
