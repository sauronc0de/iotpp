cmake_minimum_required(VERSION 3.22)
set(APP_NAME "sentinel" CACHE STRING "Application to build")
set(DEVICE_PROFILE "sentinel" CACHE STRING "Device profile from devices/")
if(NOT DEVICE_PROFILE MATCHES "^[a-zA-Z0-9_]+$" OR
   NOT EXISTS "${CMAKE_SOURCE_DIR}/devices/${DEVICE_PROFILE}.cmake")
    message(FATAL_ERROR "Unknown device profile: ${DEVICE_PROFILE}")
endif()
include("${CMAKE_SOURCE_DIR}/devices/${DEVICE_PROFILE}.cmake")
set(DEVICE_TASKS "AUTO" CACHE STRING "Enabled device tasks: AUTO or semicolon-separated names")
if(DEVICE_TASKS STREQUAL "AUTO")
    set(DEVICE_TASKS "${DEVICE_DEFAULT_TASKS}")
endif()
list(REMOVE_DUPLICATES DEVICE_TASKS)
message(STATUS "Device profile: ${DEVICE_PROFILE}; tasks: ${DEVICE_TASKS}")
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
idf_build_set_property(IOTPP_DEVICE_TASKS "${DEVICE_TASKS}")

project(${APP_NAME} VERSION 0.0.0)

string(REPLACE "." "," firmware_version "${PROJECT_VERSION}")
idf_component_get_property(app_component ${APP_NAME} COMPONENT_LIB)
target_compile_definitions(${app_component} PRIVATE "FIRMWARE_VERSION=${firmware_version}")
