#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include <opencv2/videoio.hpp>

#include "trackheader/opencv_pose_estimator.h"
#include "trackheader/tracker_core.h"

namespace {

struct Options
{
    std::string video;
    std::string output = "poses.csv";
    std::string yunet = "models/face_detection_yunet_2023mar.onnx";
    std::string landmark = "models/lm_model1_opt.onnx";
    std::string face_model = "models/model_66.txt";
    std::string calibration;
};

bool next_arg(int& index, int argc, char** argv, std::string& value)
{
    if (index + 1 >= argc)
        return false;
    value = argv[++index];
    return true;
}

bool parse_options(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out")) {
            if (!next_arg(i, argc, argv, options.output)) return false;
        } else if (!std::strcmp(argv[i], "--yunet")) {
            if (!next_arg(i, argc, argv, options.yunet)) return false;
        } else if (!std::strcmp(argv[i], "--landmark")) {
            if (!next_arg(i, argc, argv, options.landmark)) return false;
        } else if (!std::strcmp(argv[i], "--face-model")) {
            if (!next_arg(i, argc, argv, options.face_model)) return false;
        } else if (!std::strcmp(argv[i], "--calibration")) {
            if (!next_arg(i, argc, argv, options.calibration)) return false;
        } else if (!std::strcmp(argv[i], "--help")) {
            return false;
        } else if (argv[i][0] != '-') {
            if (!options.video.empty())
                return false;
            options.video = argv[i];
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return false;
        }
    }
    return !options.video.empty();
}

void print_usage(const char* program)
{
    std::fprintf(stderr,
        "usage: %s VIDEO [--out poses.csv]\n"
        "       [--yunet path] [--landmark path] [--face-model path]\n"
        "       [--calibration path]\n\n"
        "Writes one CSV row per video frame with pose and pipeline timing.\n",
        program);
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 1;
    }

    cv::VideoCapture video(options.video);
    if (!video.isOpened()) {
        std::fprintf(stderr, "replay: can't open '%s'\n", options.video.c_str());
        return 3;
    }

    const double video_fps = video.get(cv::CAP_PROP_FPS);
    const int video_width = static_cast<int>(video.get(cv::CAP_PROP_FRAME_WIDTH));
    const int video_height = static_cast<int>(video.get(cv::CAP_PROP_FRAME_HEIGHT));

    trackheader::OpenCvPoseEstimatorConfig estimator_config;
    estimator_config.yunet_model_path = options.yunet;
    estimator_config.landmark_model_path = options.landmark;
    estimator_config.face_model_path = options.face_model;
    estimator_config.calibration_path = options.calibration;
    estimator_config.camera_width = video_width > 0 ? video_width : 640;
    estimator_config.camera_height = video_height > 0 ? video_height : 480;

    trackheader::OpenCvPoseEstimator estimator;
    if (!estimator.init(estimator_config)) {
        std::fprintf(stderr, "replay: estimator initialization failed\n");
        return 2;
    }

    std::ofstream output(options.output);
    if (!output.is_open()) {
        std::fprintf(stderr, "replay: can't open output '%s'\n", options.output.c_str());
        return 4;
    }
    output << "frame,timestamp_ns,valid,accepted,confidence,yaw,pitch,roll,x,y,z,"
              "detect_ms,landmark_ms,pnp_ms,reprojection_error_px,inliers\n";

    trackheader::TrackerCore core;
    cv::Mat frame;
    std::int64_t frame_index = 0;
    std::int64_t valid_frames = 0;
    std::int64_t accepted_frames = 0;
    double detect_total_ms = 0.0;
    double landmark_total_ms = 0.0;
    double pnp_total_ms = 0.0;
    const auto started = std::chrono::steady_clock::now();

    while (video.read(frame)) {
        std::int64_t timestamp_ns = 0;
        if (video_fps > 0.0)
            timestamp_ns = static_cast<std::int64_t>(frame_index * 1e9 / video_fps);
        else
            timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count();

        trackheader::ImageView image{frame.data, frame.cols, frame.rows,
                                     static_cast<int>(frame.step),
                                     trackheader::PixelFormat::bgr8, timestamp_ns};
        const trackheader::PoseSample sample = estimator.estimate(image);
        const bool accepted = sample.valid && core.submit(sample);
        if (sample.valid)
            ++valid_frames;
        if (accepted)
            ++accepted_frames;

        const auto& debug = estimator.debug();
        detect_total_ms += debug.detect_ms;
        landmark_total_ms += debug.landmark_ms;
        pnp_total_ms += debug.pnp_ms;

        trackheader::Pose pose{};
        if (accepted)
            pose = core.latest_pose();
        output << frame_index << ',' << timestamp_ns << ','
               << (sample.valid ? 1 : 0) << ',' << (accepted ? 1 : 0) << ','
               << sample.confidence << ','
               << pose.rotation_deg[0] << ',' << pose.rotation_deg[1] << ','
               << pose.rotation_deg[2] << ',' << pose.translation_m[0] << ','
               << pose.translation_m[1] << ',' << pose.translation_m[2] << ','
               << debug.detect_ms << ',' << debug.landmark_ms << ','
               << debug.pnp_ms << ',' << debug.pnp_reprojection_error_px << ','
               << debug.pnp_inliers << '\n';
        ++frame_index;
    }

    const double wall_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const double total_frames = static_cast<double>(frame_index);
    std::printf("replay: %lld frames, %lld valid, %lld accepted\n",
                static_cast<long long>(frame_index),
                static_cast<long long>(valid_frames),
                static_cast<long long>(accepted_frames));
    if (valid_frames > 0) {
        std::printf("replay: average valid stage cost detect %.1f ms  lm %.1f ms  pnp %.1f ms\n",
                    detect_total_ms / valid_frames,
                    landmark_total_ms / valid_frames,
                    pnp_total_ms / valid_frames);
    }
    std::printf("replay: wall %.2f s, %.1f frames/s, output '%s'\n",
                wall_seconds,
                wall_seconds > 0.0 ? total_frames / wall_seconds : 0.0,
                options.output.c_str());
    return 0;
}
