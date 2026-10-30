idf_component_register(
    SRCS "rbamp_async.cpp"
    INCLUDE_DIRS "include"
    REQUIRES rbamp esp_driver_i2c
    PRIV_REQUIRES esp_timer
)
target_link_libraries(${COMPONENT_LIB} PUBLIC rbamp_reading)
idf_component_get_property(rbamp_dir rbamp COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${rbamp_dir}/src")
