add_library(ota_dex STATIC ota_peripheral.cpp)
target_include_directories(ota_dex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ota_dex PUBLIC cxx_std_17)
target_link_libraries(ota_dex PUBLIC ota dex_server)
