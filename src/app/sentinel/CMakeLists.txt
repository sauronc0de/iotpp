idf_build_get_property(DEVICE_TASKS IOTPP_DEVICE_TASKS)
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
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../config/Kconfig.projbuild"
)
target_link_libraries(${COMPONENT_LIB} PRIVATE ${device_task_libraries})
