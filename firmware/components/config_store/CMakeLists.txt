idf_component_register(
    SRCS "config_store.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash freertos log
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
