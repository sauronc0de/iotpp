cmake_minimum_required(VERSION 3.22)
set(APP_NAME "analyzer" CACHE STRING "Application to build")

# Register selected application
set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/app/${APP_NAME}")

# ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(${APP_NAME} VERSION 0.0.0)
