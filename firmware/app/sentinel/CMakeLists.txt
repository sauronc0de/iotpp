idf_build_get_property(available_tasks IOTPP_AVAILABLE_TASKS)
idf_build_get_property(task_kconfig_path IOTPP_TASK_KCONFIG)
set(DEVICE_TASKS "")
foreach(task IN LISTS available_tasks)
    string(TOUPPER "${task}" symbol)
    # Kconfig is not loaded during IDF's dependency-discovery pass. Declare
    # dependencies for all tasks there; compile only selected tasks afterwards.
    if(CMAKE_BUILD_EARLY_EXPANSION OR CONFIG_TASK_${symbol})
        list(APPEND DEVICE_TASKS "${task}")
    endif()
endforeach()
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    message(STATUS "Enabled device tasks: ${DEVICE_TASKS}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../tasks/CMakeLists.txt")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/../../tasks/device_tasks.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/device_tasks.cpp"
    @ONLY
)

idf_component_register(
    SRCS "main.cpp" "${CMAKE_CURRENT_BINARY_DIR}/device_tasks.cpp" ${device_task_sources}
    INCLUDE_DIRS "." "../../tasks" ${device_task_include_dirs}
    PRIV_REQUIRES ${device_task_requires}
    KCONFIG_PROJBUILD "${task_kconfig_path}"
)
target_link_libraries(${COMPONENT_LIB} PRIVATE ${device_task_libraries})
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${device_task_compile_definitions})
