add_library(utc STATIC utc.cpp)
target_include_directories(utc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
