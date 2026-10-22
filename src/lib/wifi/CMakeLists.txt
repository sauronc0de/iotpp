idf_component_register(
    SRCS "wifi.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_event esp_netif esp_wifi nvs_flash freertos
)
