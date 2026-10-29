add_library(wifi STATIC wifi.cpp)
target_include_directories(wifi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
