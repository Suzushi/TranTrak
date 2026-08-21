#ifndef _WIN32
#error "windows_tracker.cpp is Windows-only"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "trackheader/freetrack_pose_sink.h"
#include "trackheader/opencv_camera_source.h"
#include "trackheader/opencv_pose_estimator.h"
#include "trackheader/tracker_core.h"
#include "trackheader/udp_pose_sink.h"

namespace {

constexpr int kCenterHotkeyId = 1;
std::atomic<bool> stop_requested{false};

BOOL WINAPI console_handler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
        signal == CTRL_CLOSE_EVENT || signal == CTRL_LOGOFF_EVENT ||
        signal == CTRL_SHUTDOWN_EVENT) {
        stop_requested = true;
        return TRUE;
    }
    return FALSE;
}

struct Options
{
    int camera = 0;
    int width = 640;
    int height = 480;
    int frames = 0;
    bool use_freetrack = true;
    std::string yunet = "models/face_detection_yunet_2023mar.onnx";
    std::string landmark = "models/lm_model1_opt.onnx";
    std::string face_model = "models/model_66.txt";
    std::string calibration;
    std::string games_csv;
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
        } else if (!std::strcmp(argv[i], "--frames")) {
            if (!next_arg(i, argc, argv, value)) return false;
            options.frames = std::stoi(value);
        } else if (!std::strcmp(argv[i], "--yunet")) {
            if (!next_arg(i, argc, argv, options.yunet)) return false;
        } else if (!std::strcmp(argv[i], "--landmark")) {
            if (!next_arg(i, argc, argv, options.landmark)) return false;
        } else if (!std::strcmp(argv[i], "--face-model")) {
            if (!next_arg(i, argc, argv, options.face_model)) return false;
        } else if (!std::strcmp(argv[i], "--calibration")) {
            if (!next_arg(i, argc, argv, options.calibration)) return false;
        } else if (!std::strcmp(argv[i], "--games-csv")) {
            if (!next_arg(i, argc, argv, options.games_csv)) return false;
        } else if (!std::strcmp(argv[i], "--udp-host")) {
            if (!next_arg(i, argc, argv, options.udp_host)) return false;
        } else if (!std::strcmp(argv[i], "--udp-port")) {
            if (!next_arg(i, argc, argv, value)) return false;
            options.udp_port = std::stoi(value);
        } else if (!std::strcmp(argv[i], "--no-freetrack")) {
            options.use_freetrack = false;
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
        "usage: %s [--camera N] [--width N] [--height N] [--frames N]\n"
        "       [--yunet path] [--landmark path] [--face-model path]\n"
        "       [--calibration path] [--games-csv path]\n"
        "       [--udp-host HOST --udp-port N] [--no-freetrack]\n\n"
        "Runs head tracking without a GUI. Press Alt+C to recenter and Ctrl+C\n"
        "to stop.\n", program);
}

void pump_messages(trackheader::TrackerCore& core)
{
    MSG message{};
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_HOTKEY && message.wParam == kCenterHotkeyId)
            core.center();
    }
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 1;
    }

    SetConsoleCtrlHandler(console_handler, TRUE);
    if (!RegisterHotKey(nullptr, kCenterHotkeyId, MOD_ALT | MOD_NOREPEAT, 'C'))
        std::fprintf(stderr, "windows: unable to register Alt+C (error %lu)\n",
                     static_cast<unsigned long>(GetLastError()));

    trackheader::OpenCvCameraConfig camera_config;
    camera_config.camera = options.camera;
    camera_config.width = options.width;
    camera_config.height = options.height;
    trackheader::OpenCvCameraSource camera;
    if (!camera.start(camera_config)) {
        std::fprintf(stderr, "windows: camera initialization failed\n");
        UnregisterHotKey(nullptr, kCenterHotkeyId);
        return 3;
    }

    trackheader::OpenCvPoseEstimatorConfig estimator_config;
    estimator_config.yunet_model_path = options.yunet;
    estimator_config.landmark_model_path = options.landmark;
    estimator_config.face_model_path = options.face_model;
    estimator_config.calibration_path = options.calibration;
    estimator_config.camera_width = camera.width();
    estimator_config.camera_height = camera.height();

    trackheader::OpenCvPoseEstimator estimator;
    if (!estimator.init(estimator_config)) {
        std::fprintf(stderr, "windows: estimator initialization failed\n");
        camera.stop();
        UnregisterHotKey(nullptr, kCenterHotkeyId);
        return 2;
    }

    trackheader::FreetrackPoseSinkConfig freetrack_config;
    freetrack_config.games_csv_path = options.games_csv;
    trackheader::FreetrackPoseSink freetrack(freetrack_config);
    if (options.use_freetrack && !freetrack.open()) {
        std::fprintf(stderr, "windows: FreeTrack initialization failed\n");
        camera.stop();
        UnregisterHotKey(nullptr, kCenterHotkeyId);
        return 4;
    }

    trackheader::UdpPoseSink udp(options.udp_host,
                                 static_cast<std::uint16_t>(options.udp_port));
    const bool use_udp = !options.udp_host.empty() || options.udp_port != 0;
    if (use_udp && !udp.open()) {
        std::fprintf(stderr, "windows: UDP initialization failed\n");
        camera.stop();
        UnregisterHotKey(nullptr, kCenterHotkeyId);
        return 5;
    }

    trackheader::TrackerCore core;
    if (options.use_freetrack)
        core.add_sink(freetrack);
    if (use_udp)
        core.add_sink(udp);

    std::printf("windows: camera=%dx%d freetrack=%s udp=%s\n",
                camera.width(), camera.height(),
                options.use_freetrack ? "on" : "off",
                use_udp ? "on" : "off");

    std::int64_t processed = 0;
    std::int64_t valid = 0;
    auto stats_started = std::chrono::steady_clock::now();
    std::int64_t stats_frames = 0;
    while (!stop_requested && (options.frames <= 0 || processed < options.frames)) {
        cv::Mat frame;
        std::int64_t timestamp_ns = 0;
        if (!camera.grab_latest(frame, timestamp_ns, 200)) {
            pump_messages(core);
            continue;
        }

        trackheader::ImageView image{frame.data, frame.cols, frame.rows,
                                     static_cast<int>(frame.step),
                                     trackheader::PixelFormat::bgr8, timestamp_ns};
        const trackheader::PoseSample sample = estimator.estimate(image);
        if (sample.valid) {
            ++valid;
            core.submit(sample);
        }
        ++processed;
        ++stats_frames;
        pump_messages(core);

        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - stats_started).count();
        if (elapsed >= 1.0) {
            const auto& debug = estimator.debug();
            std::printf("windows: %.1f fps valid=%lld err=%.1fpx/%d sink=%s\n",
                        stats_frames / elapsed,
                        static_cast<long long>(valid),
                        debug.pnp_reprojection_error_px,
                        debug.pnp_inliers,
                        freetrack.connected_game().c_str());
            stats_frames = 0;
            stats_started = now;
        }
    }

    udp.close();
    freetrack.close();
    camera.stop();
    UnregisterHotKey(nullptr, kCenterHotkeyId);
    SetConsoleCtrlHandler(console_handler, FALSE);
    std::printf("windows: stopped after %lld frames (%lld valid)\n",
                static_cast<long long>(processed), static_cast<long long>(valid));
    return 0;
}
