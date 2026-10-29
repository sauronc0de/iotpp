add_library(monitoring INTERFACE)
target_include_directories(monitoring INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(monitoring INTERFACE cxx_std_17)
target_link_libraries(monitoring INTERFACE dex rbamp_async)
