#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include <opencv2/videoio.hpp>

namespace trackheader {

struct OpenCvCameraConfig
{
    int camera = 0;
    int width = 640;
    int height = 480;
    double fps = 0.0;
    // -1 selects the platform default: AVFoundation on macOS, DirectShow on
    // Windows, and OpenCV's default backend elsewhere.
    int backend = -1;
};

// OpenCV camera source with a dedicated capture thread and a single latest
// frame slot. Consumers never receive a frame older than the newest captured
// frame at the time of the handoff.
class OpenCvCameraSource final
{
public:
    OpenCvCameraSource() = default;
    ~OpenCvCameraSource();

    OpenCvCameraSource(const OpenCvCameraSource&) = delete;
    OpenCvCameraSource& operator=(const OpenCvCameraSource&) = delete;

    bool start(const OpenCvCameraConfig& config);
    void stop();
    bool is_open() const { return opened_.load(); }

    bool grab_latest(cv::Mat& frame, std::int64_t& timestamp_ns,
                     int timeout_ms = 1000);

    int width() const { return width_; }
    int height() const { return height_; }

private:
    void capture_loop();

    cv::VideoCapture camera_;
    std::thread capture_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> opened_{false};

    mutable std::mutex frame_mutex_;
    std::condition_variable frame_available_;
    cv::Mat latest_frame_;
    std::int64_t latest_timestamp_ns_ = 0;
    std::uint64_t frame_sequence_ = 0;
    std::uint64_t delivered_sequence_ = 0;
    bool has_frame_ = false;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace trackheader
