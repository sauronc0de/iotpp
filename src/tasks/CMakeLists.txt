# Task modules contribute sources, dependencies and startup calls to the app.
set(device_task_sources)
set(device_task_include_dirs)
set(device_task_requires)
set(device_task_libraries)
set(device_task_headers "")
set(device_task_startup "")
set(device_task_wifi OFF)

foreach(task IN LISTS DEVICE_TASKS)
    if(NOT task MATCHES "^[a-zA-Z0-9_]+$" OR
       NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${task}/CMakeLists.txt")
        message(FATAL_ERROR "Unknown device task: ${task}")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/${task}/CMakeLists.txt")
endforeach()


if(device_task_wifi)
    list(APPEND device_task_libraries wifi)
    string(APPEND device_task_headers "#include \"wifi.h\"\n#include \"sdkconfig.h\"\n")
    # Initialize shared networking once, before starting any task.
    set(device_task_startup
        "  ESP_ERROR_CHECK(wifi::initialize(CONFIG_SENTINEL_WIFI_SSID, CONFIG_SENTINEL_WIFI_PASSWORD));\n${device_task_startup}")
endif()