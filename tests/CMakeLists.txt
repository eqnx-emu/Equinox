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

add_executable(test_host_probe test_host_probe.cpp)
target_include_directories(test_host_probe PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")

add_executable(test_stage_retry test_stage_retry.cpp)
target_include_directories(test_stage_retry PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")

add_executable(test_emu_console
    test_emu_console.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_console PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")

find_library(JANSSON_LIB jansson)
find_library(MBEDCRYPTO_LIB mbedcrypto)
if(NOT JANSSON_LIB)
    message(FATAL_ERROR "jansson not found (needed by test_emu_manifest)")
endif()
if(NOT MBEDCRYPTO_LIB)
    message(FATAL_ERROR "mbedcrypto not found (needed by test_emu_manifest)")
endif()
add_executable(test_emu_manifest
    test_emu_manifest.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/RunnerManifest.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_manifest PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_manifest PRIVATE ${JANSSON_LIB} ${MBEDCRYPTO_LIB})
