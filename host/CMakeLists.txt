cmake_minimum_required(VERSION 3.22)
project(iotpp-host LANGUAGES CXX)

option(IOTPP_BUILD_DEX_CLI "Build the DEX command-line client" ON)
option(IOTPP_BUILD_HUB "Build the hub" ON)

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
