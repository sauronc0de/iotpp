add_library(ota STATIC ota.cpp)
target_include_directories(ota PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(ota PUBLIC cxx_std_17)
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
target_link_libraries(ota PUBLIC idf::esp_common PRIVATE config_store wifi utc
    idf::app_update idf::esp_http_client idf::esp_timer idf::esp_system
    idf::esp_partition idf::espressif__cjson idf::mbedtls idf::freertos idf::nvs_flash)
