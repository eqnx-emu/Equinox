cmake_minimum_required(VERSION 3.13)
project(moonlight_tests C CXX)

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
    "${MOONLIGHT_ROOT}/app/src/utils/emu/ReplaceFile.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/ResumeState.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/RunnerManifest.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_installer PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_installer PRIVATE ${JANSSON_LIB} ${MBEDCRYPTO_LIB})

add_executable(test_emu_shelf
    test_emu_shelf.cpp
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

find_library(Z_LIB z)
if(NOT Z_LIB)
    message(FATAL_ERROR "zlib not found (needed by test_emu_crc)")
endif()
file(GLOB LZMA_SRC CONFIGURE_DEPENDS "${MOONLIGHT_ROOT}/extern/lzma/*.c")
add_executable(test_emu_crc
    test_emu_crc.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/RomCrc.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/SevenZip.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp"
    ${LZMA_SRC})
target_include_directories(test_emu_crc PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern"
    "${MOONLIGHT_ROOT}/extern/lzma")
target_link_libraries(test_emu_crc PRIVATE ${Z_LIB})

add_executable(test_emu_gamedb
    test_emu_gamedb.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/GameDb.cpp")
target_include_directories(test_emu_gamedb PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_compile_definitions(test_emu_gamedb PRIVATE MOONLIGHT_ROOT="${MOONLIGHT_ROOT}")

add_executable(test_emu_meta
    test_emu_meta.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/GameMeta.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/CoverStore.cpp")
target_include_directories(test_emu_meta PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_meta PRIVATE ${JANSSON_LIB})

add_executable(test_emu_library_index
    test_emu_library_index.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/LibraryIndex.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/GameMeta.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/RomScan.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/CoverStore.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/CoverName.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_library_index PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_library_index PRIVATE ${JANSSON_LIB})

add_executable(test_emu_screenscraper
    test_emu_screenscraper.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/ScreenScraper.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/CoverName.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_screenscraper PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_screenscraper PRIVATE ${JANSSON_LIB})

add_executable(test_emu_library
    test_emu_library.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Library.cpp")
target_include_directories(test_emu_library PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")

add_executable(test_emu_gamedata
    test_emu_gamedata.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/GameDataJob.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/RomCrc.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/GameDb.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/CoverName.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/CoverStore.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/GameMeta.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/ScreenScraper.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_gamedata PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_gamedata PRIVATE ${JANSSON_LIB} ${Z_LIB})

add_executable(test_emu_video
    test_emu_video.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/VideoDefaults.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/ReplaceFile.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/Console.cpp")
target_include_directories(test_emu_video PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_video PRIVATE ${JANSSON_LIB})
target_compile_definitions(test_emu_video PRIVATE
    EQX_I18N_DIR="${MOONLIGHT_ROOT}/resources/i18n")

add_executable(test_emu_downloads
    test_emu_downloads.cpp
    "${MOONLIGHT_ROOT}/app/src/utils/emu/DownloadQueue.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/DownloadStore.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/ResumeState.cpp"
    "${MOONLIGHT_ROOT}/app/src/utils/emu/ReplaceFile.cpp")
target_include_directories(test_emu_downloads PRIVATE
    "${MOONLIGHT_ROOT}/app/src/utils"
    "${MOONLIGHT_ROOT}/extern")
target_link_libraries(test_emu_downloads PRIVATE ${JANSSON_LIB})

find_library(CURL_LIB curl)
if(CURL_LIB)
    add_executable(test_http_resumable
        test_http_resumable.cpp
        "${MOONLIGHT_ROOT}/app/src/utils/HttpDownload.cpp"
        "${MOONLIGHT_ROOT}/app/src/utils/emu/ResumeState.cpp")
    target_include_directories(test_http_resumable PRIVATE
        "${MOONLIGHT_ROOT}/app/src/utils"
        "${MOONLIGHT_ROOT}/extern")
    target_compile_definitions(test_http_resumable PRIVATE APP_VERSION="test")
    target_link_libraries(test_http_resumable PRIVATE ${CURL_LIB} ${JANSSON_LIB})
    if(WIN32)
        target_link_libraries(test_http_resumable PRIVATE ws2_32)
    endif()
endif()
