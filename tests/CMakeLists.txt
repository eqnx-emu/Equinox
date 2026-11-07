cmake_minimum_required(VERSION 3.13)
project(moonlight_tests CXX)

set(CMAKE_CXX_STANDARD 20)
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

add_executable(test_emu_roms
    test_emu_roms.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/RomScan.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/CoverName.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_roms PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")

add_executable(test_emu_launch
    test_emu_launch.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/LaunchLine.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_launch PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")

# RunnerInstaller with a fake transport defined in the test (no curl).
add_executable(test_emu_installer
    test_emu_installer.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/RunnerInstaller.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/RunnerManifest.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_installer PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_installer PRIVATE ${JANSSON_LIB} ${MBEDCRYPTO_LIB})

add_executable(test_emu_shelf
    test_emu_shelf.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/PlayedAgo.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/CoverStore.cpp")
target_include_directories(test_emu_shelf PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")

add_executable(test_emu_favorites
    test_emu_favorites.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/LocalFavorites.cpp")
target_include_directories(test_emu_favorites PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_favorites PRIVATE ${JANSSON_LIB})
