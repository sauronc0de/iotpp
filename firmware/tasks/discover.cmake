# Discover task modules before ESP-IDF runs Kconfig. CONFIGURE_DEPENDS also
# reconfigures an existing build when a task directory is added or removed.
file(GLOB task_modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*/CMakeLists.txt")
# Track optional settings files too, including ones added to existing modules.
file(GLOB task_settings CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*/Kconfig")
set(available_tasks)
set(task_kconfig "# Generated from firmware/tasks; do not edit.\nmenu \"Device tasks\"\n")
foreach(module IN LISTS task_modules)
    get_filename_component(task_dir "${module}" DIRECTORY)
    get_filename_component(task "${task_dir}" NAME)
    if(NOT task MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR "Invalid task directory name: ${task}")
    endif()
    list(APPEND available_tasks "${task}")
    string(TOUPPER "${task}" symbol)
    string(APPEND task_kconfig
        "\nconfig TASK_${symbol}\n    bool \"${task}\"\n    default n\n")
    # A future task can keep its settings alongside its implementation.
    if(EXISTS "${task_dir}/Kconfig")
        string(APPEND task_kconfig
            "\nif TASK_${symbol}\nsource \"${task_dir}/Kconfig\"\nendif\n")
    endif()
endforeach()
string(APPEND task_kconfig "\nendmenu\nsource \"${CMAKE_CURRENT_LIST_DIR}/../config/Kconfig.projbuild\"\n")
set(task_kconfig_path "${CMAKE_BINARY_DIR}/config/Kconfig.tasks")
file(CONFIGURE OUTPUT "${task_kconfig_path}" CONTENT "${task_kconfig}" @ONLY)
