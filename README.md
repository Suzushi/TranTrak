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

The core test build has no third-party dependency. To build the macOS camera
diagnostic, install CMake, OpenCV, and ONNX Runtime, then copy the compatible
models described in [`models/README.md`](models/README.md):

```sh
brew install cmake opencv onnxruntime
```

Configure the optional targets:

```sh
cmake -S . -B build \
  -DTRACKHEADER_BUILD_VISION=ON \
  -DTRACKHEADER_BUILD_MACOS_DIAGNOSTIC=ON \
  -DOpenCV_DIR="$(brew --prefix opencv)/lib/cmake/opencv5" \
  -Donnxruntime_DIR="$(brew --prefix onnxruntime)/lib/cmake/onnxruntime"
cmake --build build --target trackheader_macos_diagnostic
./build/trackheader_macos_diagnostic
```

The diagnostic expects these model files under `models/` by default:

- `face_detection_yunet_2023mar.onnx`
- `lm_model1_opt.onnx`
- `model_66.txt`

Use `--yunet`, `--landmark`, and `--face-model` to point at another model
directory. The landmark adapter reads the ONNX input/output shape at startup,
so compatible 112/14 and 224/28 model variants do not need separate flags.
Press `C` to recenter and `Esc` to quit.

The preview status reports PnP reprojection error and RANSAC inlier count.
Frames with excessive reprojection error, impossible camera depth, or an
unphysical frame-to-frame pose jump are rejected and trigger a fresh face
detection.

The diagnostic camera uses a dedicated capture thread with a single latest
frame slot, so a slow inference pass does not build a stale frame queue. UDP
output can be enabled for OpenTrack-compatible consumers:

```sh
./build/trackheader_macos_diagnostic \
  --udp-host 127.0.0.1 --udp-port 4242
```

The packet contains six native-endian doubles: X/Y/Z translation in
centimeters followed by yaw/pitch/roll in degrees. The axis conversion matches
the legacy FOXTracker sender.

For camera-independent testing, build the offline replay tool:

```sh
cmake -S . -B build \
  -DTRACKHEADER_BUILD_VISION=ON \
  -DTRACKHEADER_BUILD_REPLAY=ON \
  -DOpenCV_DIR="$(brew --prefix opencv)/lib/cmake/opencv5" \
  -Donnxruntime_DIR="$(brew --prefix onnxruntime)/lib/cmake/onnxruntime"
cmake --build build --target trackheader_video_replay
./build/trackheader_video_replay recording.mov --out poses.csv
```

The CSV records validity, mapped pose, confidence, timing for each stage,
reprojection error, and RANSAC inlier count.

On Windows, the `trackheader_windows` target also provides
`FreetrackPoseSink` for the legacy `FT_SharedMem` / `FT_Mutext` protocol. It
is intentionally separate from the platform-neutral core and requires a
Windows application host to construct and attach it.

The first Windows host is a headless diagnostic executable. Configure it with
the same model paths used by the macOS diagnostic:

```sh
cmake -S . -B build \
  -DTRACKHEADER_BUILD_VISION=ON \
  -DTRACKHEADER_BUILD_WINDOWS_APP=ON \
  -DOpenCV_DIR="C:/dev/opencv/opencv/build" \
  -DONNXRUNTIME_ROOT="C:/dev/onnxruntime-x64"
cmake --build build --target trackheader_windows_app
./build/trackheader_windows_app --no-freetrack --frames 300
```

It uses DirectShow through OpenCV, sends FreeTrack by default, optionally
sends UDP, and binds `Alt+C` for recentering. `Ctrl+C` stops the process.
`ONNXRUNTIME_ROOT` must contain `include/onnxruntime_cxx_api.h`,
`lib/onnxruntime.lib`, and `lib/onnxruntime.dll` (or `bin/onnxruntime.dll`).
The DLL is copied next to the executable after a successful build.
