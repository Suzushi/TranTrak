#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "trackheader/pose_sink.h"

namespace trackheader {

// Sends the six-double UDP pose format used by OpenTrack and the legacy
// TrackHeader/FOXTracker implementations.
class UdpPoseSink final : public PoseSink
{
public:
    UdpPoseSink(std::string host, std::uint16_t port);
    ~UdpPoseSink() override;

    UdpPoseSink(const UdpPoseSink&) = delete;
    UdpPoseSink& operator=(const UdpPoseSink&) = delete;

    bool open();
    void close();
    bool is_open() const;

    void send(const Pose& pose, std::int64_t timestamp_ns) override;

    // Packet order: translation X/Y/Z in centimeters, then yaw/pitch/roll
    // in degrees. This is public so protocol conversion can be tested without
    // requiring a live receiver.
    static std::array<double, 6> encode_packet(const Pose& pose);

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace trackheader
