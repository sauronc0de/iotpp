idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES wifi udp
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../config/Kconfig.projbuild"
)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../lib/dex" "${CMAKE_CURRENT_BINARY_DIR}/dex")
target_link_libraries(${COMPONENT_LIB} PRIVATE dex)
