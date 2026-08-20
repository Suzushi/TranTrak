cmake_minimum_required(VERSION 3.21)

project(TrackHeader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRACKHEADER_BUILD_TESTS "Build core self-tests" ON)

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

