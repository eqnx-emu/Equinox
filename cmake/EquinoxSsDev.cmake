# ScreenScraper developer credentials, from the environment only. The cache
# variables are read on the first configure; clear the CMake cache to change them.
# The values are written, escaped as C string literals, into a header in the
# build directory, so any character is safe and nothing lands in the source tree.
set(EQUINOX_SS_DEVID "$ENV{EQUINOX_SS_DEVID}" CACHE STRING "")
set(EQUINOX_SS_DEVPASSWORD "$ENV{EQUINOX_SS_DEVPASSWORD}" CACHE STRING "")

foreach(_name EQUINOX_SS_DEVID EQUINOX_SS_DEVPASSWORD)
    set(_value "${${_name}}")
    string(REPLACE "\\" "\\\\" _value "${_value}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    string(REPLACE "\n" "\n" _value "${_value}")
    string(REPLACE "\r" "\r" _value "${_value}")
    set(${_name}_C "${_value}")
endforeach()

configure_file("${CMAKE_CURRENT_LIST_DIR}/ss_dev.h.in"
    "${EQUINOX_SS_DEV_OUT_DIR}/ss_dev.h" @ONLY)
