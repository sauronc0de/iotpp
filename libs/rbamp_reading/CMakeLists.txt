add_library(rbamp_reading INTERFACE)
target_include_directories(rbamp_reading INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(rbamp_reading INTERFACE cxx_std_17)
