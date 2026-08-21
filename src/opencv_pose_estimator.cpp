#include "trackheader/opencv_pose_estimator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

#include <onnxruntime_cxx_api.h>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/face.hpp>

namespace trackheader {

namespace {

using Clock = std::chrono::steady_clock;

struct Timer
{
    Clock::time_point started = Clock::now();

    double elapsed_ms() const
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }
};

double clamp(double value, double lo, double hi)
{
    return std::max(lo, std::min(value, hi));
}

cv::Rect expand_box(const cv::Rect2f& box, double rate, const cv::Size& bounds)
{
    const float side = std::max(box.width, box.height);
    if (side <= 0.0f)
        return {};

    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    const float size = static_cast<float>(side * (1.0 + 2.0 * rate));
    const cv::Rect roi(static_cast<int>(cx - size * 0.5f),
                       static_cast<int>(cy - size * 0.5f),
                       static_cast<int>(size), static_cast<int>(size));
    return roi & cv::Rect(0, 0, bounds.width, bounds.height);
}

struct Intrinsics
{
    cv::Matx33d K = cv::Matx33d::eye();
    cv::Vec<double, 5> distortion = cv::Vec<double, 5>::zeros();
};

// The calibration format is deliberately tiny: one "key: value" per line.
// It is enough for fx/fy/cx/cy/k1/k2/p1/p2/k3 without bringing yaml-cpp into
// the diagnostic build.
Intrinsics load_intrinsics(const std::string& path, int width, int height)
{
    Intrinsics intr;
    const double focal = static_cast<double>(width);
    intr.K = cv::Matx33d(focal, 0, width / 2.0,
                         0, focal, height / 2.0,
                         0, 0, 1);

    if (path.empty())
        return intr;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::fprintf(stderr, "calibration: can't open '%s', using defaults\n", path.c_str());
        return intr;
    }

    double fx = focal, fy = focal, cx = width / 2.0, cy = height / 2.0;
    std::array<double, 5> distortion{};
    std::string line;
    while (std::getline(file, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        try {
            const double parsed = std::stod(value);
            if (key == "fx") fx = parsed;
            else if (key == "fy") fy = parsed;
            else if (key == "cx") cx = parsed;
            else if (key == "cy") cy = parsed;
            else if (key == "k1") distortion[0] = parsed;
            else if (key == "k2") distortion[1] = parsed;
            else if (key == "p1") distortion[2] = parsed;
            else if (key == "p2") distortion[3] = parsed;
            else if (key == "k3") distortion[4] = parsed;
        } catch (const std::exception&) {
            // Ignore comments and malformed lines; defaults remain valid.
        }
    }

    intr.K = cv::Matx33d(fx, 0, cx, 0, fy, cy, 0, 0, 1);
    intr.distortion = cv::Vec<double, 5>(distortion[0], distortion[1], distortion[2],
                                        distortion[3], distortion[4]);
    return intr;
}

struct Detection
{
    cv::Rect2f box{};
    bool valid = false;
};

struct LandmarkResult
{
    std::vector<cv::Point2f> points;
    double confidence = 0.0;
    bool valid = false;

    cv::Rect2f bounding_box() const
    {
        if (points.empty())
            return {};
        float x0 = points[0].x, y0 = points[0].y;
        float x1 = x0, y1 = y0;
        for (const auto& point : points) {
            x0 = std::min(x0, point.x);
            y0 = std::min(y0, point.y);
            x1 = std::max(x1, point.x);
            y1 = std::max(y1, point.y);
        }
        return {x0, y0, x1 - x0, y1 - y0};
    }
};

struct YuNet
{
    cv::Ptr<cv::FaceDetectorYN> detector;

    bool init(const OpenCvPoseEstimatorConfig& config)
    {
        try {
            detector = cv::FaceDetectorYN::create(
                config.yunet_model_path, "", cv::Size(0, 0),
                config.face_score_threshold, config.face_nms_threshold);
        } catch (const cv::Exception& error) {
            std::fprintf(stderr, "yunet: failed to load '%s': %s\n",
                         config.yunet_model_path.c_str(), error.what());
            return false;
        }
        return !detector.empty();
    }

    Detection detect(const cv::Mat& frame)
    {
        Detection result;
        if (frame.empty() || detector.empty())
            return result;

        detector->setInputSize(frame.size());
        cv::Mat faces;
        detector->detect(frame, faces);
        float best_score = -1.0f;
        for (int row = 0; row < faces.rows; ++row) {
            const float score = faces.at<float>(row, 14);
            if (score > best_score) {
                best_score = score;
                result.valid = true;
                result.box = cv::Rect2f(faces.at<float>(row, 0), faces.at<float>(row, 1),
                                        faces.at<float>(row, 2), faces.at<float>(row, 3));
            }
        }
        return result;
    }
};

float logit(float probability, int heatmap_size)
{
    probability = clamp(probability, 0.0000001f, 0.99999f);
    probability = probability / (1.0f - probability);
    return static_cast<float>(std::log(probability) / (heatmap_size <= 7 ? 8.0 : 16.0));
}

struct LandmarkModel
{
    static constexpr int kFeatureCount = 66;
    static constexpr int kOutputChannels = 198;

    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "trackheader"};
    std::unique_ptr<Ort::Session> session;
    std::vector<float> input_buffer;
    std::string input_name = "input";
    std::string output_name = "output";
    int nn_size = 112;
    int nn_output = 14;

    bool init(const OpenCvPoseEstimatorConfig& config)
    {
        nn_size = config.landmark_nn_size;
        nn_output = config.landmark_nn_output;

        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        try {
            session = std::make_unique<Ort::Session>(env, config.landmark_model_path.c_str(), options);
        } catch (const Ort::Exception& error) {
            std::fprintf(stderr, "landmark: failed to load '%s': %s\n",
                         config.landmark_model_path.c_str(), error.what());
            return false;
        }

        try {
            Ort::AllocatorWithDefaultOptions allocator;
            auto input_name_allocated = session->GetInputNameAllocated(0, allocator);
            auto output_name_allocated = session->GetOutputNameAllocated(0, allocator);
            input_name = input_name_allocated.get();
            output_name = output_name_allocated.get();

            const auto input_shape = session->GetInputTypeInfo(0)
                                         .GetTensorTypeAndShapeInfo().GetShape();
            if (input_shape.size() != 4 || input_shape[1] != 3 ||
                input_shape[2] <= 0 || input_shape[2] != input_shape[3]) {
                std::fprintf(stderr,
                             "landmark: unsupported input shape for '%s'\n",
                             config.landmark_model_path.c_str());
                return false;
            }
            nn_size = static_cast<int>(input_shape[2]);

            const auto output_shape = session->GetOutputTypeInfo(0)
                                          .GetTensorTypeAndShapeInfo().GetShape();
            if (output_shape.size() != 4 || output_shape[1] != kOutputChannels ||
                output_shape[2] <= 0 || output_shape[2] != output_shape[3]) {
                std::fprintf(stderr,
                             "landmark: unsupported output shape for '%s'\n",
                             config.landmark_model_path.c_str());
                return false;
            }
            nn_output = static_cast<int>(output_shape[2]);
        } catch (const Ort::Exception& error) {
            std::fprintf(stderr, "landmark: failed to inspect model '%s': %s\n",
                         config.landmark_model_path.c_str(), error.what());
            return false;
        }

        if (nn_size != config.landmark_nn_size || nn_output != config.landmark_nn_output) {
            std::fprintf(stderr, "landmark: using model shape input=%dx%d heatmap=%dx%d\n",
                         nn_size, nn_size, nn_output, nn_output);
        }

        input_buffer.resize(static_cast<size_t>(nn_size) * nn_size * 3);
        return true;
    }

    LandmarkResult infer(const cv::Mat& frame, const cv::Rect& roi_input)
    {
        LandmarkResult result;
        if (!session || frame.empty())
            return result;

        const cv::Rect roi = roi_input & cv::Rect(0, 0, frame.cols, frame.rows);
        if (roi.area() < 10)
            return result;

        // input_buffer is mutable workspace, while the estimator itself is a
        // single-consumer object as documented by PoseEstimator.
        auto* self = this;
        cv::Mat crop = frame(roi).clone();
        cv::resize(crop, crop, cv::Size(nn_size, nn_size), 0, 0, cv::INTER_LINEAR);
        crop.convertTo(crop, CV_32F);
        cv::cvtColor(crop, crop, cv::COLOR_BGR2RGB);

        static const cv::Scalar mean(0.485 / 0.229, 0.456 / 0.224, 0.406 / 0.225);
        static const cv::Scalar std255(0.229 * 255.0, 0.224 * 255.0, 0.225 * 255.0);
        cv::divide(crop, std255, crop);
        cv::subtract(crop, mean, crop);

        const int plane = nn_size * nn_size;
        const float* source = crop.ptr<float>();
        for (int i = 0; i < plane; ++i) {
            self->input_buffer[i] = source[i * 3 + 0];
            self->input_buffer[plane + i] = source[i * 3 + 1];
            self->input_buffer[2 * plane + i] = source[i * 3 + 2];
        }

        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 4> shape{1, 3, nn_size, nn_size};
        Ort::Value input = Ort::Value::CreateTensor<float>(
            memory, self->input_buffer.data(), self->input_buffer.size(), shape.data(), shape.size());
        const char* input_names[] = {self->input_name.c_str()};
        const char* output_names[] = {self->output_name.c_str()};

        try {
            auto outputs = session->Run(Ort::RunOptions{nullptr}, input_names, &input, 1,
                                        output_names, 1);
            const float* heatmaps = outputs[0].GetTensorData<float>();
            const int heatmap_area = nn_output * nn_output;
            const float coordinate_scale = static_cast<float>(nn_size - 1);
            result.points.reserve(kFeatureCount);
            double confidence_sum = 0.0;

            for (int landmark = 0; landmark < kFeatureCount; ++landmark) {
                const int offset = heatmap_area * landmark;
                int argmax = 0;
                float max_value = -1e30f;
                for (int i = 0; i < heatmap_area; ++i) {
                    if (heatmaps[offset + i] > max_value) {
                        argmax = i;
                        max_value = heatmaps[offset + i];
                    }
                }

                const int x = argmax / nn_output;
                const int y = argmax % nn_output;
                const float offset_x = coordinate_scale *
                    logit(heatmaps[kFeatureCount * heatmap_area + offset + argmax], nn_output);
                const float offset_y = coordinate_scale *
                    logit(heatmaps[2 * kFeatureCount * heatmap_area + offset + argmax], nn_output);

                // Preserve the legacy model's x/y decoding convention.
                const float point_y = static_cast<float>(roi.y) +
                    static_cast<float>(roi.height) / nn_size *
                    (coordinate_scale * x / (nn_output - 1) + offset_x);
                const float point_x = static_cast<float>(roi.x) +
                    static_cast<float>(roi.width) / nn_size *
                    (coordinate_scale * y / (nn_output - 1) + offset_y);
                result.points.emplace_back(point_x, point_y);
                confidence_sum += max_value;
            }
            result.confidence = result.points.empty() ? 0.0 : confidence_sum / result.points.size();
            result.valid = !result.points.empty();
        } catch (const Ort::Exception& error) {
            std::fprintf(stderr, "landmark: inference failed: %s\n", error.what());
        }
        return result;
    }
};

struct PnpSolver
{
    cv::Matx33d K = cv::Matx33d::eye();
    cv::Mat distortion;
    std::vector<cv::Point3f> model_points;

    bool init(const OpenCvPoseEstimatorConfig& config)
    {
        const Intrinsics intr = load_intrinsics(config.calibration_path,
                                                config.camera_width, config.camera_height);
        K = intr.K;
        distortion = cv::Mat(1, 5, CV_64F);
        for (int i = 0; i < 5; ++i)
            distortion.at<double>(0, i) = intr.distortion[i];

        std::ifstream file(config.face_model_path);
        if (!file.is_open()) {
            std::fprintf(stderr, "pnp: can't open face model '%s'\n",
                         config.face_model_path.c_str());
            return false;
        }

        model_points.clear();
        double px, py, pz;
        while (file >> px >> py >> pz)
            model_points.emplace_back(static_cast<float>(px), static_cast<float>(-py),
                                      static_cast<float>(-(pz + config.cervical_face_model)));
        if (model_points.size() < 46) {
            std::fprintf(stderr, "pnp: face model '%s' has too few points\n",
                         config.face_model_path.c_str());
            return false;
        }
        return true;
    }

    PoseSample solve(const LandmarkResult& landmarks, std::int64_t timestamp_ns) const
    {
        // Stable subset from the legacy PnpSolver.
        static constexpr std::array<int, 17> stable_indices{
            0, 1, 15, 16, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 39, 42, 45};

        PoseSample sample;
        sample.timestamp_ns = timestamp_ns;
        sample.confidence = landmarks.confidence;
        if (!landmarks.valid || model_points.empty())
            return sample;

        std::vector<cv::Point3f> points_3d;
        std::vector<cv::Point2f> points_2d;
        for (const int index : stable_indices) {
            if (static_cast<size_t>(index) >= landmarks.points.size() ||
                static_cast<size_t>(index) >= model_points.size())
                continue;
            points_3d.push_back(model_points[index]);
            points_2d.push_back(landmarks.points[index]);
        }
        if (points_3d.size() < 4)
            return sample;

        cv::Mat rotation_vector, translation_vector;
        if (!cv::solvePnP(points_3d, points_2d, K, distortion,
                          rotation_vector, translation_vector, false,
                          cv::SOLVEPNP_ITERATIVE))
            return sample;

        cv::Mat rotation;
        cv::Rodrigues(rotation_vector, rotation);
        const double r00 = rotation.at<double>(0, 0);
        const double r10 = rotation.at<double>(1, 0);
        const double r20 = rotation.at<double>(2, 0);
        const double r21 = rotation.at<double>(2, 1);
        const double r22 = rotation.at<double>(2, 2);
        constexpr double rad_to_deg = 180.0 / 3.14159265358979323846;
        sample.pose.rotation_deg = {
            std::atan2(r10, r00) * rad_to_deg,
            std::asin(clamp(-r20, -1.0, 1.0)) * rad_to_deg,
            std::atan2(r21, r22) * rad_to_deg};

        sample.pose.translation_m = {
            -translation_vector.at<double>(0, 0),
            -translation_vector.at<double>(1, 0),
            -translation_vector.at<double>(2, 0)};
        sample.valid = true;
        return sample;
    }
};

}  // namespace

struct OpenCvPoseEstimator::Impl
{
    OpenCvPoseEstimatorConfig config;
    YuNet detector;
    LandmarkModel landmarks;
    PnpSolver pnp;
    cv::Rect current_roi{};
    VisionDebugInfo debug;
    bool tracking = false;
    bool ready = false;

    bool init(const OpenCvPoseEstimatorConfig& config_in)
    {
        config = config_in;
        ready = detector.init(config) && landmarks.init(config) && pnp.init(config);
        tracking = false;
        return ready;
    }

    void reset()
    {
        tracking = false;
        current_roi = {};
    }
};

OpenCvPoseEstimator::OpenCvPoseEstimator() : impl_(std::make_unique<Impl>()) {}
OpenCvPoseEstimator::~OpenCvPoseEstimator() = default;
OpenCvPoseEstimator::OpenCvPoseEstimator(OpenCvPoseEstimator&&) noexcept = default;
OpenCvPoseEstimator& OpenCvPoseEstimator::operator=(OpenCvPoseEstimator&&) noexcept = default;

bool OpenCvPoseEstimator::init(const OpenCvPoseEstimatorConfig& config)
{
    return impl_->init(config);
}

bool OpenCvPoseEstimator::ready() const
{
    return impl_->ready;
}

void OpenCvPoseEstimator::reset()
{
    impl_->reset();
}

PoseSample OpenCvPoseEstimator::estimate(const ImageView& input)
{
    impl_->debug = {};
    PoseSample invalid;
    invalid.timestamp_ns = input.timestamp_ns;
    if (!impl_->ready || !input.valid())
        return invalid;

    cv::Mat frame;
    if (input.format == PixelFormat::bgr8) {
        frame = cv::Mat(input.height, input.width, CV_8UC3,
                        const_cast<std::uint8_t*>(input.data), input.stride_bytes);
    } else if (input.format == PixelFormat::gray8) {
        cv::Mat gray(input.height, input.width, CV_8UC1,
                     const_cast<std::uint8_t*>(input.data), input.stride_bytes);
        cv::cvtColor(gray, frame, cv::COLOR_GRAY2BGR);
    } else {
        return invalid;
    }

    cv::Rect roi;
    if (!impl_->tracking) {
        const Timer timer;
        const Detection detection = impl_->detector.detect(frame);
        impl_->debug.detect_ms = timer.elapsed_ms();
        if (!detection.valid)
            return invalid;
        roi = expand_box(detection.box, 0.35, frame.size());
    } else {
        roi = impl_->current_roi;
        if (roi.area() < 10)
            roi = cv::Rect(0, 0, frame.cols, frame.rows);
    }

    Timer timer;
    LandmarkResult landmark = impl_->landmarks.infer(frame, roi);
    impl_->debug.landmark_ms = timer.elapsed_ms();

    if (!landmark.valid)
        return invalid;

    if (landmark.confidence < impl_->config.redetect_confidence) {
        impl_->tracking = false;
        const Timer detect_timer;
        const Detection detection = impl_->detector.detect(frame);
        impl_->debug.detect_ms += detect_timer.elapsed_ms();
        if (!detection.valid)
            return invalid;

        roi = expand_box(detection.box, 0.35, frame.size());
        const Timer landmark_timer;
        landmark = impl_->landmarks.infer(frame, roi);
        impl_->debug.landmark_ms += landmark_timer.elapsed_ms();
        if (!landmark.valid)
            return invalid;
    }

    const Timer pnp_timer;
    PoseSample result = impl_->pnp.solve(landmark, input.timestamp_ns);
    impl_->debug.pnp_ms = pnp_timer.elapsed_ms();
    impl_->debug.face_roi = roi;
    impl_->debug.landmarks = landmark.points;
    impl_->debug.confidence = landmark.confidence;
    impl_->debug.face_found = result.valid;

    if (result.valid) {
        impl_->tracking = true;
        impl_->current_roi = expand_box(landmark.bounding_box(), 0.35, frame.size());
    }
    return result;
}

const VisionDebugInfo& OpenCvPoseEstimator::debug() const
{
    return impl_->debug;
}

}  // namespace trackheader
