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
