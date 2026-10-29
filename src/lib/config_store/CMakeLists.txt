add_library(config_store STATIC config_store.cpp)
target_include_directories(config_store PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(config_store PUBLIC cxx_std_17)
