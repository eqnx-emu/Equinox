cmake_minimum_required(VERSION 3.13)
project(moonlight_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MOONLIGHT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

add_executable(test_stream_capacity
    test_stream_capacity.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/StreamCapacity.cpp")
target_include_directories(test_stream_capacity PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
