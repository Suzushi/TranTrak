cmake_minimum_required(VERSION 3.21)

project(TrackHeader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRACKHEADER_BUILD_TESTS "Build core self-tests" ON)
option(TRACKHEADER_BUILD_VISION "Build OpenCV/ONNX Runtime vision adapter" OFF)
option(TRACKHEADER_BUILD_MACOS_DIAGNOSTIC "Build the macOS camera diagnostic" OFF)
option(TRACKHEADER_BUILD_REPLAY "Build the offline video replay tool" OFF)
option(TRACKHEADER_BUILD_WINDOWS_APP "Build the Windows head-tracking host" OFF)

add_library(trackheader_core STATIC
    src/pose_mapper.cpp
    src/pose_filter.cpp
    src/tracker_core.cpp
)

target_include_directories(trackheader_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(trackheader_core PUBLIC cxx_std_17)

add_library(trackheader_network STATIC
    src/udp_pose_sink.cpp
)
target_include_directories(trackheader_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(trackheader_network PUBLIC trackheader_core)
if(WIN32)
    target_link_libraries(trackheader_network PRIVATE ws2_32)
endif()
target_compile_features(trackheader_network PUBLIC cxx_std_17)

if(TRACKHEADER_BUILD_TESTS)
    enable_testing()
    add_executable(trackheader_core_tests tests/core_self_test.cpp)
    target_link_libraries(trackheader_core_tests PRIVATE trackheader_core)
    add_test(NAME trackheader_core_tests COMMAND trackheader_core_tests)

    add_executable(trackheader_udp_sink_tests tests/udp_sink_self_test.cpp)
    target_link_libraries(trackheader_udp_sink_tests PRIVATE trackheader_network)
    add_test(NAME trackheader_udp_sink_tests COMMAND trackheader_udp_sink_tests)
endif()

if(TRACKHEADER_BUILD_VISION OR TRACKHEADER_BUILD_MACOS_DIAGNOSTIC OR
   TRACKHEADER_BUILD_REPLAY OR TRACKHEADER_BUILD_WINDOWS_APP)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio highgui objdetect)
    find_package(onnxruntime CONFIG REQUIRED)

    # OpenCV 5 renamed the calib3d CMake target to calib while retaining the
    # public calib3d.hpp header. Keep the adapter compatible with OpenCV 4 too.
    if(TARGET opencv_calib)
        set(TRACKHEADER_OPENCV_CALIB_TARGET opencv_calib)
    elseif(TARGET opencv_calib3d)
        set(TRACKHEADER_OPENCV_CALIB_TARGET opencv_calib3d)
    else()
        message(FATAL_ERROR "TrackHeader requires OpenCV's calib3d module")
    endif()

    add_library(trackheader_vision STATIC
        src/opencv_camera_source.cpp
        src/opencv_pose_estimator.cpp
    )
    target_include_directories(trackheader_vision PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(trackheader_vision PUBLIC
        trackheader_core
        ${OpenCV_LIBS}
        ${TRACKHEADER_OPENCV_CALIB_TARGET}
        onnxruntime::onnxruntime
    )
    target_compile_features(trackheader_vision PUBLIC cxx_std_17)
endif()

if(TRACKHEADER_BUILD_MACOS_DIAGNOSTIC)
    if(NOT APPLE)
        message(FATAL_ERROR "TRACKHEADER_BUILD_MACOS_DIAGNOSTIC requires an Apple platform")
    endif()
    add_executable(trackheader_macos_diagnostic
        apps/macos_camera_diagnostic.cpp
    )
    target_link_libraries(trackheader_macos_diagnostic PRIVATE
        trackheader_vision
        trackheader_network
    )
    target_compile_features(trackheader_macos_diagnostic PRIVATE cxx_std_17)
endif()

if(TRACKHEADER_BUILD_REPLAY)
    add_executable(trackheader_video_replay
        apps/video_replay.cpp
    )
    target_link_libraries(trackheader_video_replay PRIVATE trackheader_vision)
    target_compile_features(trackheader_video_replay PRIVATE cxx_std_17)
endif()

if(WIN32)
    add_library(trackheader_windows STATIC
        src/windows/freetrack_pose_sink.cpp
    )
    target_include_directories(trackheader_windows PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(trackheader_windows PUBLIC trackheader_core)
    target_compile_features(trackheader_windows PUBLIC cxx_std_17)

    if(TRACKHEADER_BUILD_WINDOWS_APP)
        add_executable(trackheader_windows_app
            apps/windows_tracker.cpp
        )
        target_link_libraries(trackheader_windows_app PRIVATE
            trackheader_vision
            trackheader_network
            trackheader_windows
            user32
        )
        target_compile_features(trackheader_windows_app PRIVATE cxx_std_17)
    endif()
elseif(TRACKHEADER_BUILD_WINDOWS_APP)
    message(FATAL_ERROR "TRACKHEADER_BUILD_WINDOWS_APP requires a Windows platform")
endif()
