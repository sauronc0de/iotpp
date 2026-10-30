idf_component_register(
    SRCS "ota_peripheral.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ota
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_link_libraries(${COMPONENT_LIB} PUBLIC dex_server)
