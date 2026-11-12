iotpp_require_magic_enum()

add_executable(dex-client main.cpp)
target_link_libraries(dex-client PRIVATE host_options dex magic_enum::magic_enum)
