idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES wifi udp
    KCONFIG_PROJBUILD "${CMAKE_CURRENT_LIST_DIR}/../../config/Kconfig.projbuild"
)
