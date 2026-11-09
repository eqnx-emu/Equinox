cmake_minimum_required(VERSION 3.10)

# Equinox builds for two targets:
#   -DPLATFORM_SWITCH=ON   the Nintendo Switch homebrew (what ships)
#   -DPLATFORM_DESKTOP=ON  a Windows/MSYS2 build used only for local testing

set(EXTERN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/extern)
set(BOREALIS_LIBRARY ${EXTERN_PATH}/borealis/library)

# build options
include(${EXTERN_PATH}/cmake/commonOption.cmake)

# Dependencies
option(USE_SHARED_LIB "Whether to use shared libs provided by system" OFF)
option(MOONLIGHT_BUILD_TESTS "Build unit tests (desktop only)" OFF)
cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)

# toolchain
include(${EXTERN_PATH}/cmake/toolchain.cmake)

# Renderers
if (PLATFORM_SWITCH)
    set(USE_DEKO3D_RENDERER ON)
else ()
    set(USE_D3D11_RENDERER ON)
endif ()

set(SUPPORT_UPSCALING OFF)
if (ENABLE_UPSCALING)
    message(STATUS "Use ENABLE_UPSCALING")
    if (PLATFORM_SWITCH AND USE_DEKO3D_RENDERER)
        set(SUPPORT_UPSCALING ON)
    else ()
        message(FATAL_ERROR "ENABLE_UPSCALING=ON is only supported with the Switch deko3d renderer")
    endif ()
endif ()

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "${EXTERN_PATH}/cmake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${EXTERN_PATH}/cmake")

# project info
project(Moonlight)
include(GNUInstallDirs)

set(VERSION_MAJOR "1")
set(VERSION_MINOR "9")
set(VERSION_ALTER "2")
set(VERSION_BUILD "7")
set(PACKAGE_NAME "ru.xitrix.Moonlight")
set(PROJECT_AUTHOR "luizfbalves")
# Name shown in the Switch homebrew menu. The build target stays "Moonlight".
set(APP_DISPLAY_NAME "Equinox")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/moonlight_icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

add_definitions(
        -DAPP_VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
        -Dcimg_display=0
        -Dcimg_use_png
        -Dcimg_no_system_calls
        -DBRLS_I18N_PREFIX=\"main/\")

find_package(MbedTLS REQUIRED COMPONENTS crypto tls x509)
add_compile_definitions(USE_MBEDTLS_CRYPTO)
set(USE_MBEDTLS_CRYPTO ON)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(extern)

# setting src and include
file(GLOB MAIN_SRC CONFIGURE_DEPENDS
    app/src/*.cpp
    app/src/crypto/*.cpp
    app/src/gestures/*.cpp
    app/src/keyboards/*.cpp
    app/src/libgamestream/*.cpp
    app/src/streaming/*.cpp
    app/src/streaming/audio/*.cpp
    app/src/streaming/ffmpeg/*.cpp
    app/src/streaming/switch/*.cpp
    app/src/views/*.cpp
    app/src/utils/*.cpp
    app/src/utils/emu/*.cpp
    extern/lzma/*.c)

if (PLATFORM_SWITCH)
    list(REMOVE_ITEM MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/app/src/streaming/audio/SDLAudioRenderer.cpp)
    set(PLATFORM_LIBS
        # base lib
        nx m
    )
    list(APPEND MAIN_SRC
            ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c
            app/src/switch/wrapper.c
            app/src/streaming/video/deko3d/DKVideoRenderer.cpp)
else ()
    configure_file("${CMAKE_SOURCE_DIR}/app/platforms/windows/windows.manifest.in" "${CMAKE_BINARY_DIR}/app.manifest")
    configure_file("${CMAKE_SOURCE_DIR}/app/platforms/windows/resource.rc.in" "${CMAKE_BINARY_DIR}/resource.rc")
    set_source_files_properties(${CMAKE_BINARY_DIR}/resource.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/app.manifest")
    list(APPEND MAIN_SRC
            ${CMAKE_BINARY_DIR}/resource.rc
            app/src/streaming/video/D3D11/D3D11VideoRenderer.cpp)
endif ()

# building target
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set(EQUINOX_SS_DEV_OUT_DIR "${CMAKE_BINARY_DIR}/generated")
include(cmake/EquinoxSsDev.cmake)
target_include_directories(${PROJECT_NAME} PRIVATE "${EQUINOX_SS_DEV_OUT_DIR}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
if (USE_SDL3)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_ENABLE_OLD_NAMES)
endif ()

# building release file
if (PLATFORM_SWITCH)
    if (USE_DEKO3D)
        gen_dksh("${PROJECT_RESOURCES}/shaders")
    endif ()
    # The console provides the system fonts, so only the bundled Onest font
    # is kept from resources/font.
    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "${APP_DISPLAY_NAME}" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp --titleid=${PROJECT_TITLEID}
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/resources/font
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/resources/font
        COMMAND ${CMAKE_COMMAND} -E copy
            ${PROJECT_RESOURCES}/font/onest_regular.ttf
            ${PROJECT_RESOURCES}/font/onest_semibold.ttf
            ${PROJECT_RESOURCES}/font/onest_OFL.txt
            ${CMAKE_BINARY_DIR}/resources/font/
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
    )
    list(APPEND APP_PLATFORM_LIB dav1d z)
else ()
    add_custom_target(${PROJECT_NAME}.data
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_CURRENT_BINARY_DIR}/resources
    )
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)
endif ()

target_include_directories(${PROJECT_NAME} PUBLIC
        app/include
        app/include/gestures
        app/src/crypto
        app/src/gestures
        app/src/keyboards
        app/src/libgamestream
        app/src/streaming
        app/src/streaming/audio
        app/src/streaming/ffmpeg
        app/src/streaming/switch
        app/src/streaming/video
        app/src/streaming/video/D3D11
        app/src/streaming/video/deko3d
        app/src/utils
        extern/CImg
        extern/lzma
        ${MBEDTLS_INCLUDE_DIRS}
        ${APP_PLATFORM_INCLUDE})
target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-ffunction-sections>
        $<$<COMPILE_LANGUAGE:C,CXX>:-fdata-sections>
        ${APP_PLATFORM_OPTION})

if (PLATFORM_SWITCH)
    target_include_directories(${PROJECT_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/switch/include
    )
endif ()

set(MOONLIGHT_APP_SDL_LIBS)
if (USE_SDL3)
    list(APPEND MOONLIGHT_APP_SDL_LIBS SDL3::SDL3)
elseif (USE_SDL2)
    find_package(SDL2 REQUIRED)
    if (TARGET SDL2::SDL2)
        list(APPEND MOONLIGHT_APP_SDL_LIBS SDL2::SDL2)
    elseif (TARGET SDL2::SDL2-static)
        list(APPEND MOONLIGHT_APP_SDL_LIBS SDL2::SDL2-static)
    else ()
        list(APPEND MOONLIGHT_APP_SDL_LIBS ${SDL2_LIBRARIES})
    endif ()
endif ()

find_package(CURL REQUIRED)
find_package(Jansson REQUIRED)
find_package(EXPAT REQUIRED)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)

find_library(OPUS_LIBRARY opus)
message("opus: ${OPUS_LIBRARY}")

set(MOONLIGHT_APP_FFMPEG_LIBS)
if (PLATFORM_SWITCH)
    list(APPEND MOONLIGHT_APP_FFMPEG_LIBS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/switch/libavcodec.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/switch/libavformat.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/switch/libavutil.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/switch/libswresample.a)
else ()
    find_library(AVCODEC_LIBRARY avcodec)
    find_library(AVFORMAT_LIBRARY avformat)
    find_library(AVUTIL_LIBRARY avutil)
    find_library(SWRESAMPLE_LIBRARY swresample)
    message("avcodec: ${AVCODEC_LIBRARY}")

    get_filename_component(_moonlight_ffmpeg_library_dir "${AVCODEC_LIBRARY}" DIRECTORY)
    get_filename_component(_moonlight_ffmpeg_prefix "${_moonlight_ffmpeg_library_dir}/../.." ABSOLUTE)
    find_path(MOONLIGHT_FFMPEG_INCLUDE_DIR
        NAMES libavcodec/avcodec.h
        HINTS "${_moonlight_ffmpeg_prefix}/include")
    if (NOT MOONLIGHT_FFMPEG_INCLUDE_DIR)
        message(FATAL_ERROR "Could not locate the FFmpeg development headers")
    endif ()
    target_include_directories(${PROJECT_NAME} PRIVATE "${MOONLIGHT_FFMPEG_INCLUDE_DIR}")

    list(APPEND MOONLIGHT_APP_FFMPEG_LIBS
        ${AVCODEC_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${AVFORMAT_LIBRARY}
        ${SWRESAMPLE_LIBRARY})
    unset(_moonlight_ffmpeg_library_dir)
    unset(_moonlight_ffmpeg_prefix)
endif ()

find_library(ZSTD_LIBRARY zstd)
message("zstd: ${ZSTD_LIBRARY}")

# Recompiled CURL with Mbedtls support for Switch
if (PLATFORM_SWITCH)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/switch/libcurl.a)
else ()
    target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
endif ()

target_link_libraries(${PROJECT_NAME} PRIVATE
        borealis
        moonlight-common-c
        mdns
        Jansson::Jansson
        EXPAT::EXPAT
        ${ZSTD_LIBRARY}
        ${OPUS_LIBRARY}
        PNG::PNG
        ZLIB::ZLIB
        ${MOONLIGHT_APP_FFMPEG_LIBS}
        ${MOONLIGHT_APP_SDL_LIBS}
        ${APP_PLATFORM_LIB}
        MbedTLS::mbedtls
        MbedTLS::mbedx509
        MbedTLS::mbedcrypto)

if (USE_D3D11_RENDERER)
    add_definitions(-DUSE_D3D11_RENDERER)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3dcompiler)
elseif (USE_DEKO3D_RENDERER)
    add_definitions(-DUSE_DEKO3D_RENDERER)
endif ()

if (SUPPORT_UPSCALING)
    add_definitions(-DSUPPORT_UPSCALING)
endif ()

# Copy the MSYS2 DLLs the executable needs next to it.
if (PLATFORM_DESKTOP AND NOT MOONLIGHT_DESKTOP_SYSTEM_PREFIX STREQUAL "")
    set(_moonlight_runtime_dependency_dir "${MOONLIGHT_DESKTOP_SYSTEM_PREFIX}/bin")
    add_custom_target(${PROJECT_NAME}.runtime ALL
        COMMAND "${CMAKE_COMMAND}"
            -DMOONLIGHT_RUNTIME_EXECUTABLE=$<TARGET_FILE:${PROJECT_NAME}>
            -DMOONLIGHT_RUNTIME_SEARCH_DIR=${_moonlight_runtime_dependency_dir}
            -DMOONLIGHT_OBJDUMP_COMMAND=${CMAKE_OBJDUMP}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/extern/cmake/stage_runtime_deps.cmake"
        DEPENDS ${PROJECT_NAME}
        VERBATIM)
endif ()

if (PLATFORM_DESKTOP AND MOONLIGHT_BUILD_TESTS)
    add_subdirectory(tests)
endif ()
