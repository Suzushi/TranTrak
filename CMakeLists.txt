cmake_minimum_required(VERSION 3.21)

project(TrackHeader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRACKHEADER_BUILD_TESTS "Build core self-tests" ON)
option(TRACKHEADER_BUILD_VISION "Build OpenCV/ONNX Runtime vision adapter" OFF)
option(TRACKHEADER_BUILD_MACOS_DIAGNOSTIC "Build the macOS camera diagnostic" OFF)

add_library(trackheader_core STATIC
    src/pose_mapper.cpp
    src/pose_filter.cpp
    src/tracker_core.cpp
)

target_include_directories(trackheader_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(trackheader_core PUBLIC cxx_std_17)

if(TRACKHEADER_BUILD_TESTS)
    enable_testing()
    add_executable(trackheader_core_tests tests/core_self_test.cpp)
    target_link_libraries(trackheader_core_tests PRIVATE trackheader_core)
    add_test(NAME trackheader_core_tests COMMAND trackheader_core_tests)
endif()

if(TRACKHEADER_BUILD_VISION OR TRACKHEADER_BUILD_MACOS_DIAGNOSTIC)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio highgui calib3d objdetect)
    find_package(onnxruntime CONFIG REQUIRED)

    add_library(trackheader_vision STATIC
        src/opencv_pose_estimator.cpp
    )
    target_include_directories(trackheader_vision PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(trackheader_vision PUBLIC
        trackheader_core
        ${OpenCV_LIBS}
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
    target_link_libraries(trackheader_macos_diagnostic PRIVATE trackheader_vision)
    target_compile_features(trackheader_macos_diagnostic PRIVATE cxx_std_17)
endif()
