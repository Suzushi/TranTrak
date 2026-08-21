#include "trackheader/opencv_camera_source.h"

#include <chrono>
#include <cstdio>

namespace trackheader {

namespace {

using Clock = std::chrono::steady_clock;

int default_backend()
{
#ifdef _WIN32
    return cv::CAP_DSHOW;
#elif defined(__APPLE__)
    return cv::CAP_AVFOUNDATION;
#else
    return cv::CAP_ANY;
#endif
}

}  // namespace

OpenCvCameraSource::~OpenCvCameraSource()
{
    stop();
}

bool OpenCvCameraSource::start(const OpenCvCameraConfig& config)
{
    stop();

    const int backend = config.backend >= 0 ? config.backend : default_backend();
    try {
        if (!camera_.open(config.camera, backend)) {
            std::fprintf(stderr, "camera: can't open device %d\n", config.camera);
            return false;
        }

        camera_.set(cv::CAP_PROP_FRAME_WIDTH, config.width);
        camera_.set(cv::CAP_PROP_FRAME_HEIGHT, config.height);
        if (config.fps > 0.0)
            camera_.set(cv::CAP_PROP_FPS, config.fps);
        camera_.set(cv::CAP_PROP_BUFFERSIZE, 1);
    } catch (const cv::Exception& error) {
        std::fprintf(stderr, "camera: failed to open device %d: %s\n",
                     config.camera, error.what());
        camera_.release();
        return false;
    }

    width_ = static_cast<int>(camera_.get(cv::CAP_PROP_FRAME_WIDTH));
    height_ = static_cast<int>(camera_.get(cv::CAP_PROP_FRAME_HEIGHT));
    if (width_ <= 0)
        width_ = config.width;
    if (height_ <= 0)
        height_ = config.height;

    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_frame_.release();
        latest_timestamp_ns_ = 0;
        frame_sequence_ = 0;
        delivered_sequence_ = 0;
        has_frame_ = false;
    }

    running_ = true;
    opened_ = true;
    capture_thread_ = std::thread(&OpenCvCameraSource::capture_loop, this);
    return true;
}

void OpenCvCameraSource::stop()
{
    running_ = false;
    frame_available_.notify_all();
    if (capture_thread_.joinable())
        capture_thread_.join();
    if (camera_.isOpened())
        camera_.release();
    opened_ = false;

    std::lock_guard<std::mutex> lock(frame_mutex_);
    has_frame_ = false;
    latest_frame_.release();
    frame_available_.notify_all();
}

bool OpenCvCameraSource::grab_latest(cv::Mat& frame, std::int64_t& timestamp_ns,
                                     int timeout_ms)
{
    std::unique_lock<std::mutex> lock(frame_mutex_);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    frame_available_.wait_until(lock, deadline, [&] {
        return !running_ || (has_frame_ && frame_sequence_ > delivered_sequence_);
    });

    if (!has_frame_ || frame_sequence_ <= delivered_sequence_)
        return false;

    delivered_sequence_ = frame_sequence_;
    frame = latest_frame_.clone();
    timestamp_ns = latest_timestamp_ns_;
    return !frame.empty();
}

void OpenCvCameraSource::capture_loop()
{
    cv::Mat frame;
    while (running_) {
        bool captured = false;
        try {
            captured = camera_.grab() && camera_.retrieve(frame) && !frame.empty();
        } catch (const cv::Exception&) {
            captured = false;
        }

        if (!captured) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        const auto now = Clock::now().time_since_epoch();
        const auto timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            latest_frame_ = frame.clone();
            latest_timestamp_ns_ = timestamp_ns;
            has_frame_ = true;
            ++frame_sequence_;
        }
        frame_available_.notify_all();
    }
}

}  // namespace trackheader
