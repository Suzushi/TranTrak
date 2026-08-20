# TrackHeader

Windows-first, low-latency webcam head tracking.

The repository is intentionally split into two boundaries:

```text
camera + vision adapter -> PoseSample -> trackheader_core -> PoseSink
```

`trackheader_core` does not include UI, OpenCV, Win32, Winsock, SDL, Qt, or
FreeTrack headers. It consumes a timestamped pose estimate and immediately
performs centering, mapping, filtering, and sink dispatch.

The `PoseEstimator` interface accepts a non-owning image view and returns a
`PoseSample`; it does not require OpenCV or a specific camera API. The host
application owns the real-time loop. On Windows it can use native
camera capture, Win32 timing, and FreeTrack shared memory. On macOS it can use
OpenCV or AVFoundation and a diagnostic sink to validate the same core.

## Build

```sh
cmake -S . -B build -DTRACKHEADER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

This first slice intentionally has no third-party dependency. The visual
estimator will be added behind an interface after the core behavior is stable.
