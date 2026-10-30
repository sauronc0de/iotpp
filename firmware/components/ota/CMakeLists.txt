idf_component_register(
    SRCS "ota.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES esp_common
    PRIV_REQUIRES config_store wifi utc app_update esp_http_client esp_timer esp_system
        esp_partition espressif__cjson mbedtls freertos nvs_flash esp_app_format esp_hw_support log
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
set(OTA_CA_CERT "" CACHE FILEPATH "PEM CA for the local OTA HTTPS server (empty uses ESP certificate bundle)")
set(OTA_CA_PEM "")
if(OTA_CA_CERT)
    file(READ "${OTA_CA_CERT}" OTA_CA_PEM)
    if(NOT OTA_CA_PEM MATCHES "-----BEGIN CERTIFICATE-----")
        message(FATAL_ERROR "OTA_CA_CERT must contain a PEM certificate")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OTA_CA_CERT}")
endif()
configure_file(ota_build.hpp.in ota_build.hpp @ONLY)
