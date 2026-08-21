# TrackHeader 交接文档

更新时间：2026-08-21

## 1. 项目定位

TrackHeader 是一个 Windows-first 的低延迟摄像头头部瞄准程序。

当前仓库已经从旧项目中独立出来：

```text
/Users/ryou/Documents/code/TrackHeader
```

旧项目仍位于：

```text
/Users/ryou/Documents/code/HEADTracker
```

旧目录只作为算法和资源参考，不应继续作为 TrackHeader 的工作区。后续开发、构建、提交和推送都应在 `TrackHeader` 目录进行。

GitHub 仓库：

```text
git@github.com:Suzushi/TranTrak.git
```

当前分支和提交：

```text
main
15fbf57 过滤异常头部姿态并匹配实际相机分辨率
```

当前 `main` 已与 `origin/main` 同步。

## 2. 当前架构

数据流为：

```text
摄像头 ImageView
    -> PoseEstimator
    -> PoseSample
    -> TrackerCore
    -> PoseSink
```

### `trackheader_core`

位置：

```text
include/trackheader/pose.h
include/trackheader/pose_estimator.h
include/trackheader/pose_sink.h
include/trackheader/pose_mapper.h
include/trackheader/pose_filter.h
include/trackheader/tracker_core.h
src/pose_mapper.cpp
src/pose_filter.cpp
src/tracker_core.cpp
```

核心库只负责：

- 居中和重新居中
- 旋转/平移映射
- Accela 风格滤波
- 时间戳处理
- 姿态 sink 分发

核心库不包含 OpenCV、ONNX Runtime、Qt、Win32、Winsock、SDL 或 FreeTrack 头文件，可以在没有第三方依赖的环境中单独测试。

### `trackheader_vision`

位置：

```text
include/trackheader/opencv_pose_estimator.h
src/opencv_pose_estimator.cpp
```

这是旧版 `VisionPipeline` 的迁移实现，包含：

- YuNet 人脸检测
- ONNX Runtime 66 点关键点推理
- ROI 传播和低置信度重检测
- 66 点模型加载
- RANSAC solvePnP
- 重投影误差和正深度检查
- 连续帧旋转/平移跳变检查

关键点模型的输入/输出 shape 在启动时从 ONNX 模型动态读取。目前已验证：

```text
lm_model1_opt.onnx
input:  [1, 3, 224, 224]
output: [1, 198, 28, 28]
```

因此不再依赖错误的固定 `112x112` 配置。

### macOS 诊断程序

位置：

```text
apps/macos_camera_diagnostic.cpp
```

它使用 OpenCV 的 AVFoundation backend 打开摄像头，采用 `grab/retrieve` 处理采集帧，然后调用 `OpenCvPoseEstimator` 和 `TrackerCore`。预览窗口显示：

- 摄像头 FPS
- 检测耗时
- 关键点耗时
- PnP 耗时
- 重投影误差
- RANSAC 内点数量
- 当前是否检测到有效人脸

按 `C` 重新居中，按 `Esc` 退出。

## 3. 构建和运行

### 核心测试

```sh
cd /Users/ryou/Documents/code/TrackHeader
cmake -S . -B build -DTRACKHEADER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### macOS 视觉诊断程序

Homebrew 依赖：

```sh
brew install cmake opencv onnxruntime
```

配置和编译：

```sh
cd /Users/ryou/Documents/code/TrackHeader
cmake -S . -B build \
  -DTRACKHEADER_BUILD_TESTS=ON \
  -DTRACKHEADER_BUILD_VISION=ON \
  -DTRACKHEADER_BUILD_MACOS_DIAGNOSTIC=ON \
  -DOpenCV_DIR="$(brew --prefix opencv)/lib/cmake/opencv5" \
  -Donnxruntime_DIR="$(brew --prefix onnxruntime)/lib/cmake/onnxruntime"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

运行：

```sh
./build/trackheader_macos_diagnostic
```

如果摄像头分辨率或设备编号不同：

```sh
./build/trackheader_macos_diagnostic --camera 1 --width 640 --height 480
```

macOS 首次运行需要在“系统设置 -> 隐私与安全性 -> 摄像头”中允许 Terminal 或 IDE 访问摄像头。

## 4. 模型文件

诊断程序默认读取：

```text
models/face_detection_yunet_2023mar.onnx
models/lm_model1_opt.onnx
models/model_66.txt
```

模型文件已被 `.gitignore` 忽略，当前机器本地存在，但不会随 Git 提交同步。模型来源和复制方式见：

```text
models/README.md
```

如果切换到另一台机器，需要重新复制模型，或通过参数指定路径：

```sh
./build/trackheader_macos_diagnostic \
  --yunet /path/to/face_detection_yunet_2023mar.onnx \
  --landmark /path/to/lm_model1_opt.onnx \
  --face-model /path/to/model_66.txt
```

## 5. 已验证内容

已完成并推送的提交：

```text
80d2711 建立独立的无平台头部追踪核心
a859253 迁移视觉管线并添加 macOS 摄像头诊断
d95e79b 兼容 OpenCV 5 并完善 macOS 构建说明
2f575d1 根据 ONNX shape 自动适配关键点模型
15fbf57 过滤异常头部姿态并匹配实际相机分辨率
```

目前已验证：

- Apple Clang 可以编译核心库
- 核心自测通过
- CMake 可以找到 OpenCV 5 和 ONNX Runtime
- `trackheader_vision` 可以编译
- `trackheader_macos_diagnostic` 可以链接
- 三个模型可以完成初始化
- ONNX Runtime 不再报告 112/224 输入维度错误
- RANSAC、重投影误差和帧间跳变过滤代码可以编译

## 6. 当前限制

以下内容还没有完成：

1. 没有在真实摄像头权限可用的环境中完成长时间姿态稳定性验证。
2. 当前诊断程序的默认输出是控制台，不是 UDP 或 FreeTrack。
3. Windows 原生摄像头采集、Win32 计时、FreeTrack/NPClient 和全局快捷键尚未迁移。
4. 当前 `OpenCvPoseEstimator` 仍然以 OpenCV 图像为输入，尚未接入 Windows Media Foundation 或 DirectShow 原生采集。
5. PnP 阈值目前是固定默认值：重投影误差 `12 px`、旋转速度 `1200 deg/s`、平移速度 `2 m/s`，需要基于真实运行记录调参。
6. 当前诊断程序是同步推理循环，还没有独立的采集线程、推理线程和输出线程。

## 7. 下一步建议

建议按以下顺序推进：

### 第一阶段：确认视觉质量

- 在摄像头权限正常的环境运行诊断程序。
- 记录 `err px / inliers` 和实际 FPS。
- 确认静止头部时 yaw、pitch、roll 不再跳变。
- 根据真实数据调整重投影误差、置信度和跳变阈值。
- 增加视频录制/回放工具，避免每次调试都依赖摄像头。

### 第二阶段：改善低延迟数据流

- 采集线程只保留最新帧。
- 推理线程处理最新帧，旧帧直接丢弃。
- 输出线程使用最新有效姿态快照。
- 预览、日志和统计从推理热路径中降频或异步处理。

### 第三阶段：Windows 平台接入

- 添加 `platform/win32` 摄像头适配器。
- 添加 Win32 高精度计时和线程优先级控制。
- 添加 FreeTrack/NPClient sink。
- 添加 UDP sink。
- 添加 Win32 全局快捷键和托盘功能。
- 保持这些模块不进入 `trackheader_core`。

## 8. 开发约定

- 后续命令默认在 `TrackHeader` 目录执行。
- 不要把旧 `HEADTracker` 目录重新作为工作区。
- 不要提交 `build/`、`.DS_Store` 或模型二进制文件。
- 核心接口变更后必须运行核心 CTest。
- 视觉代码变更后至少运行完整 CMake build，并用诊断程序验证模型初始化。
- 每个可独立验证的阶段创建一个清晰的 Git commit，并推送到 `origin/main`。

