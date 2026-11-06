# disable in-source build
if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed")
endif ()

include(CMakeDependentOption)

# Platform. Equinox ships for the Switch; the Windows desktop build exists
# only to test the app locally without a console.
option(PLATFORM_SWITCH "build for switch" OFF)
option(PLATFORM_DESKTOP "build the Windows desktop development target" OFF)

option(ENABLE_UPSCALING "Enable video upscaling (Switch deko3d renderer)" ${PLATFORM_SWITCH})

set(MOONLIGHT_DESKTOP_SYSTEM_PREFIX "" CACHE PATH "MSYS2 prefix used to resolve desktop packages, e.g. C:/msys64/ucrt64")

# Windows Only
cmake_dependent_option(WIN32_TERMINAL "Show terminal when run on Windows" ON "WIN32" OFF)
cmake_dependent_option(USE_D3D11 "Using directx 11 instead of OpenGL." ON "WIN32" OFF)

# Nintendo Switch Only
cmake_dependent_option(USE_DEKO3D "Using deko3d instead of OpenGL." OFF "PLATFORM_SWITCH" OFF)

option(USE_BOOST_FILESYSTEM "Whether to use the boost::filesystem instead of std::filesystem" OFF)

# Do not use it until you know what you are doing.
option(CUSTOM_RESOURCES_DIR "Custom resources dir" "")

option(USE_GLFW "using glfw for input and create window" OFF)
option(USE_SDL2 "using sdl2 for input and create window" OFF)
option(USE_SDL3 "using sdl3 for input and create window" OFF)

# Borealis reads this; neither Equinox target bundles resources with libromfs.
option(USE_LIBROMFS "using libromfs to bundle resources" OFF)

# Disable highlight border animation
option(SIMPLE_HIGHLIGHT "Simple highlight" OFF)

# Enable unity build, using -DCMAKE_UNITY_BUILD_BATCH_SIZE=8 to set the batch size
# https://cmake.org/cmake/help/latest/prop_tgt/UNITY_BUILD.html
option(BRLS_UNITY_BUILD "Unity build" OFF)


if (NOT DEFINED APP_PLATFORM_INCLUDE)
    set(APP_PLATFORM_INCLUDE)
endif ()

if (NOT DEFINED APP_PLATFORM_LIB)
    set(APP_PLATFORM_LIB)
endif ()

if (NOT DEFINED APP_PLATFORM_OPTION)
    set(APP_PLATFORM_OPTION)
endif ()

if (NOT DEFINED APP_PLATFORM_LINK_OPTION)
    set(APP_PLATFORM_LINK_OPTION)
    if (PLATFORM_SWITCH)
        list(APPEND APP_PLATFORM_LINK_OPTION "-Wl,-Map,borealis.map")
    endif ()
endif ()
