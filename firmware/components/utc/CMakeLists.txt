idf_component_register(
    SRCS "utc.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES wifi esp_netif freertos log
)
