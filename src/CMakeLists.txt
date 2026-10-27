cmake_minimum_required(VERSION 3.22)
set(APP_NAME "analyzer" CACHE STRING "Application to build")
set(SDKCONFIG "${CMAKE_SOURCE_DIR}/config/build/sdkconfig" CACHE FILEPATH "Local project configuration")
set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/config/sdkconfig.defaults" CACHE STRING "Shared project defaults")

# Register the selected application and ESP-IDF components.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/app/${APP_NAME}"
    "${CMAKE_SOURCE_DIR}/lib/wifi"
    "${CMAKE_SOURCE_DIR}/lib/udp"
)

# ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(${APP_NAME} VERSION 0.0.0)
