cmake_minimum_required(VERSION 3.22)
project(iotpp-host LANGUAGES C CXX)

option(IOTPP_BUILD_DEX_CLI "Build the DEX command-line client" ON)
option(IOTPP_BUILD_HUB "Build the hub" ON)
option(IOTPP_BUILD_SIM "Build the ESP32 device simulator" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

add_library(host_options INTERFACE)
target_compile_features(host_options INTERFACE cxx_std_17)
target_compile_options(host_options INTERFACE -Wall -Wextra -Wpedantic)

add_subdirectory("../libs/dex" "${CMAKE_CURRENT_BINARY_DIR}/libs/dex")

if(IOTPP_BUILD_DEX_CLI)
    add_subdirectory(dex-cli)
endif()

if(IOTPP_BUILD_HUB)
    add_subdirectory(hub)
endif()

if(IOTPP_BUILD_SIM)
    add_subdirectory(sim)
endif()
