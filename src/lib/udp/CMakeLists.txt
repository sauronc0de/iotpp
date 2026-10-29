add_library(udp STATIC udp.cpp udp_async.cpp)
target_include_directories(udp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
