# Task modules contribute sources, dependencies and startup calls to the app.
set(device_task_sources)
set(device_task_include_dirs)
set(device_task_requires)
set(device_task_libraries config_store)
set(device_task_compile_definitions)
set(device_task_headers "")
set(device_task_startup "")
set(device_task_early_startup "")
set(device_task_prepare "")
set(device_task_wifi OFF)
set(device_task_utc OFF)

foreach(task IN LISTS DEVICE_TASKS)
    if(NOT task MATCHES "^[a-zA-Z0-9_]+$" OR
       NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${task}/CMakeLists.txt")
        message(FATAL_ERROR "Unknown device task: ${task}")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/${task}/CMakeLists.txt")
endforeach()


if(device_task_utc)
    list(APPEND device_task_libraries utc)
    list(APPEND device_task_requires esp_netif)
    string(APPEND device_task_headers "#include \"utc.hpp\"\n")
    set(device_task_startup "  ESP_ERROR_CHECK(utc::initialize());\n${device_task_startup}")
endif()

set(device_task_startup "${device_task_early_startup}${device_task_startup}")

if(device_task_wifi)
    list(APPEND device_task_libraries wifi)
    string(APPEND device_task_headers "#include \"wifi.h\"\n#include \"sdkconfig.h\"\n")
    # Initialize shared networking once, before starting any task.
    set(device_task_startup
        "  ESP_ERROR_CHECK(wifi::initialize(CONFIG_SENTINEL_WIFI_SSID, CONFIG_SENTINEL_WIFI_PASSWORD));\n${device_task_startup}")
endif()
