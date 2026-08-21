#include "trackheader/udp_pose_sink.h"

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

}  // namespace

int main()
{
    using namespace trackheader;

    Pose pose;
    pose.rotation_deg = {10.0, -20.0, 30.0};
    pose.translation_m = {0.1, -0.2, 0.3};

    const auto packet = UdpPoseSink::encode_packet(pose);
    require(std::abs(packet[0] - 20.0) < 1e-12,
            "UDP X translation should use legacy axis conversion");
    require(std::abs(packet[1] + 30.0) < 1e-12,
            "UDP Y translation should use legacy axis conversion");
    require(std::abs(packet[2] + 10.0) < 1e-12,
            "UDP Z translation should use legacy axis conversion");
    require(std::abs(packet[3] - 10.0) < 1e-12,
            "UDP yaw should be preserved");
    require(std::abs(packet[4] + 20.0) < 1e-12,
            "UDP pitch should be preserved");
    require(std::abs(packet[5] + 30.0) < 1e-12,
            "UDP roll should be inverted for compatibility");

    UdpPoseSink sink("127.0.0.1", 9);
    require(sink.open(), "UDP sink should open a local datagram socket");
    require(sink.is_open(), "UDP sink should report an open socket");
    sink.send(pose, 123);
    sink.close();
    require(!sink.is_open(), "UDP sink should close its socket");

    std::cout << "trackheader UDP sink self-test passed\n";
    return 0;
}
