idf_component_register(
    SRCS "udp.cpp" "udp_async.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common lwip
    PRIV_REQUIRES log
)
