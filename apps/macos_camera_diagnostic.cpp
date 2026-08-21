#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include "trackheader/opencv_camera_source.h"
#include "trackheader/opencv_pose_estimator.h"
#include "trackheader/pose_sink.h"
#include "trackheader/tracker_core.h"
#include "trackheader/udp_pose_sink.h"

namespace {

struct Options
{
    int camera = 0;
    int width = 640;
    int height = 480;
    std::string yunet = "models/face_detection_yunet_2023mar.onnx";
    std::string landmark = "models/lm_model1_opt.onnx";
    std::string face_model = "models/model_66.txt";
    std::string calibration;
    std::string udp_host;
    int udp_port = 0;
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
        std::string value;
        if (!std::strcmp(argv[i], "--camera")) {
            if (!next_arg(i, argc, argv, value)) return false;
            options.camera = std::stoi(value);
        } else if (!std::strcmp(argv[i], "--width")) {
            if (!next_arg(i, argc, argv, value)) return false;
            options.width = std::stoi(value);
        } else if (!std::strcmp(argv[i], "--height")) {
            if (!next_arg(i, argc, argv, value)) return false;
            options.height = std::stoi(value);
        } else if (!std::strcmp(argv[i], "--yunet")) {
            if (!next_arg(i, argc, argv, options.yunet)) return false;
        } else if (!std::strcmp(argv[i], "--landmark")) {
            if (!next_arg(i, argc, argv, options.landmark)) return false;
        } else if (!std::strcmp(argv[i], "--face-model")) {
            if (!next_arg(i, argc, argv, options.face_model)) return false;
        } else if (!std::strcmp(argv[i], "--calibration")) {
            if (!next_arg(i, argc, argv, options.calibration)) return false;
        } else if (!std::strcmp(argv[i], "--udp-host")) {
            if (!next_arg(i, argc, argv, options.udp_host)) return false;
        } else if (!std::strcmp(argv[i], "--udp-port")) {
            if (!next_arg(i, argc, argv, value)) return false;
            options.udp_port = std::stoi(value);
        } else if (!std::strcmp(argv[i], "--help")) {
            return false;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

void print_usage(const char* program)
{
    std::fprintf(stderr,
        "usage: %s [--camera N] [--width N] [--height N]\n"
        "       [--yunet path] [--landmark path] [--face-model path]\n"
        "       [--calibration path] [--udp-host HOST --udp-port N]\n\n"
        "Press ESC in the preview window to quit, C to recenter.\n", program);
}

class ConsoleSink final : public trackheader::PoseSink
{
public:
    void send(const trackheader::Pose& pose, std::int64_t timestamp_ns) override
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_print < std::chrono::milliseconds(500))
            return;
        last_print = now;
        std::printf("pose t=%lld yaw=%7.2f pitch=%7.2f roll=%7.2f xyz=(%+.3f,%+.3f,%+.3f)\n",
                    static_cast<long long>(timestamp_ns),
                    pose.rotation_deg[0], pose.rotation_deg[1], pose.rotation_deg[2],
                    pose.translation_m[0], pose.translation_m[1], pose.translation_m[2]);
    }

private:
    std::chrono::steady_clock::time_point last_print{};
};

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 1;
    }

    trackheader::OpenCvPoseEstimatorConfig estimator_config;
    estimator_config.yunet_model_path = options.yunet;
    estimator_config.landmark_model_path = options.landmark;
    estimator_config.face_model_path = options.face_model;
    estimator_config.calibration_path = options.calibration;
    estimator_config.camera_width = options.width;
    estimator_config.camera_height = options.height;

    trackheader::OpenCvCameraConfig camera_config;
    camera_config.camera = options.camera;
    camera_config.width = options.width;
    camera_config.height = options.height;
    trackheader::OpenCvCameraSource camera;
    if (!camera.start(camera_config)) {
        std::fprintf(stderr, "diagnostic: can't open camera %d\n", options.camera);
        return 3;
    }

    estimator_config.camera_width = camera.width();
    estimator_config.camera_height = camera.height();
    std::printf("diagnostic: camera=%dx%d\n", estimator_config.camera_width,
                estimator_config.camera_height);

    trackheader::OpenCvPoseEstimator estimator;
    if (!estimator.init(estimator_config)) {
        std::fprintf(stderr, "diagnostic: estimator initialization failed\n");
        return 2;
    }

    trackheader::CoreConfig core_config;
    trackheader::TrackerCore core(core_config);
    ConsoleSink sink;
    core.add_sink(sink);
    trackheader::UdpPoseSink udp_sink(options.udp_host,
                                      static_cast<std::uint16_t>(options.udp_port));
    if (!options.udp_host.empty() || options.udp_port != 0) {
        if (!udp_sink.open()) {
            std::fprintf(stderr, "diagnostic: UDP sink initialization failed\n");
            return 2;
        }
        core.add_sink(udp_sink);
        std::printf("diagnostic: UDP output=%s:%d\n",
                    options.udp_host.c_str(), options.udp_port);
    }

    cv::namedWindow("TrackHeader diagnostic", cv::WINDOW_AUTOSIZE);
    cv::Mat frame;
    std::int64_t frames = 0;
    auto fps_started = std::chrono::steady_clock::now();
    double fps = 0.0;

    for (;;) {
        // The source keeps only the newest captured frame, so a slow model
        // cannot make the diagnostic loop consume stale frames.
        std::int64_t timestamp_ns = 0;
        if (!camera.grab_latest(frame, timestamp_ns) || frame.empty())
            continue;

        const auto now = std::chrono::steady_clock::now();
        trackheader::ImageView image{frame.data, frame.cols, frame.rows,
                                     static_cast<int>(frame.step),
                                     trackheader::PixelFormat::bgr8, timestamp_ns};
        const trackheader::PoseSample sample = estimator.estimate(image);
        if (sample.valid)
            core.submit(sample);

        ++frames;
        const double elapsed = std::chrono::duration<double>(now - fps_started).count();
        if (elapsed >= 1.0) {
            fps = frames / elapsed;
            frames = 0;
            fps_started = now;
        }

        const auto& debug = estimator.debug();
        if (debug.face_found) {
            cv::rectangle(frame, debug.face_roi, cv::Scalar(0, 255, 0), 1);
            for (const auto& point : debug.landmarks)
                cv::circle(frame, point, 1, cv::Scalar(0, 0, 255), -1);
        }
        char status[256];
        std::snprintf(status, sizeof(status),
                      "%.1f fps  d %.1f ms  lm %.1f ms  pnp %.1f ms  err %.1fpx/%d  %s",
                      fps, debug.detect_ms, debug.landmark_ms, debug.pnp_ms,
                      debug.pnp_reprojection_error_px, debug.pnp_inliers,
                      debug.face_found ? "face" : "searching");
        cv::putText(frame, status, cv::Point(8, 20), cv::FONT_HERSHEY_SIMPLEX,
                    0.48, cv::Scalar(255, 255, 0), 1, cv::LINE_AA);
        cv::imshow("TrackHeader diagnostic", frame);

        const int key = cv::waitKey(1) & 0xff;
        if (key == 27)
            break;
        if (key == 'c' || key == 'C')
            core.center();
    }

    camera.stop();
    cv::destroyAllWindows();
    return 0;
}
