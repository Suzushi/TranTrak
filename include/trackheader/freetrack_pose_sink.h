#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "trackheader/pose_sink.h"

namespace trackheader {

struct FreetrackPoseSinkConfig
{
    // Optional path to "facetracknoir supported games.csv" for the game
    // handshake table. Empty is allowed for V160 clients.
    std::string games_csv_path;
};

// Windows-only FreeTrack/TrackIR shared-memory output. The implementation is
// kept out of trackheader_core and is only built on Windows.
class FreetrackPoseSink final : public PoseSink
{
public:
    explicit FreetrackPoseSink(FreetrackPoseSinkConfig config = {});
    ~FreetrackPoseSink() override;

    FreetrackPoseSink(const FreetrackPoseSink&) = delete;
    FreetrackPoseSink& operator=(const FreetrackPoseSink&) = delete;

    bool open();
    void close();
    bool is_open() const;
    std::string connected_game() const;

    void send(const Pose& pose, std::int64_t timestamp_ns) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace trackheader
