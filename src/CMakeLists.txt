cmake_minimum_required(VERSION 3.22)
set(APP_NAME "sentinel" CACHE STRING "Application to build")
# One library registry for firmware and native builds. Executables only link.
set(IOTPP_LIBRARIES dex dex_server rbamp_async monitoring udp wifi)
if(APP_NAME STREQUAL "dex-client")
    project(iotpp LANGUAGES CXX)
else()
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

    # Project libraries are ordinary CMake targets; ESP-IDF registers the app.
    set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/app/${APP_NAME}")

    # ESP-IDF build system
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    idf_build_set_property(IOTPP_DEVICE_TASKS "${DEVICE_TASKS}")

    project(${APP_NAME} VERSION 0.0.0)
endif()

foreach(library IN LISTS IOTPP_LIBRARIES)
    add_subdirectory("lib/${library}" EXCLUDE_FROM_ALL)
endforeach()

if(APP_NAME STREQUAL "dex-client")
    add_subdirectory("app/dex-client")
else()
    target_link_libraries(rbamp_async PUBLIC idf::rbamp idf::esp_driver_i2c PRIVATE idf::esp_timer)
    idf_component_get_property(rbamp_dir rbamp COMPONENT_DIR)
    target_include_directories(rbamp_async PRIVATE "${rbamp_dir}/src")
    target_link_libraries(udp PUBLIC idf::esp_common idf::lwip)
    target_link_libraries(wifi PUBLIC idf::esp_common
        PRIVATE idf::esp_event idf::esp_netif idf::esp_wifi idf::nvs_flash idf::freertos)

    string(REPLACE "." "," firmware_version "${PROJECT_VERSION}")
    idf_component_get_property(app_component ${APP_NAME} COMPONENT_LIB)
    target_compile_definitions(${app_component} PRIVATE "FIRMWARE_VERSION=${firmware_version}")
endif()
