#include "trackheader/tracker_core.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct TestSink final : trackheader::PoseSink
{
    int count = 0;
    trackheader::Pose last{};

    void send(const trackheader::Pose& pose, std::int64_t) override
    {
        ++count;
        last = pose;
    }
};

}  // namespace

int main()
{
    using namespace trackheader;

    CoreConfig config;
    config.filter.enabled = false;
    TrackerCore core(config);
    TestSink sink;
    core.add_sink(sink);

    PoseSample first;
    first.valid = true;
    first.timestamp_ns = 1'000'000'000;
    first.pose.rotation_deg = {10.0, 0.0, 0.0};
    first.pose.translation_m = {0.1, 0.2, 0.3};
    require(core.submit(first), "first valid sample should be accepted");
    require(sink.count == 1, "accepted sample should be sent immediately");
    require(std::abs(sink.last.rotation_deg[0]) < 1e-12,
            "first sample should establish the center");

    PoseSample second = first;
    second.timestamp_ns += 4'000'000;
    second.pose.rotation_deg[0] = 20.0;
    second.pose.translation_m[0] = 0.2;
    require(core.submit(second), "second valid sample should be accepted");
    require(std::abs(sink.last.rotation_deg[0] - 69.3203883495) < 1e-9,
            "rotation should be mapped using the configured bounds");
    require(std::abs(sink.last.translation_m[0] - (0.1 / 0.307 * 0.769)) < 1e-12,
            "translation should be mapped using the configured bounds");

    core.center();
    PoseSample third = second;
    third.timestamp_ns += 4'000'000;
    require(core.submit(third), "sample after recenter should be accepted");
    require(std::abs(sink.last.rotation_deg[0]) < 1e-12,
            "recenter should take effect on the next sample");

    std::cout << "trackheader_core self-test passed\n";
    return 0;
}
