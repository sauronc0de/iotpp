find_package(magic_enum 0.9.7 CONFIG QUIET)
if(NOT magic_enum_FOUND)
    include(FetchContent)
    FetchContent_Declare(magic_enum
        GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
        GIT_TAG v0.9.7
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(magic_enum)
endif()

add_executable(dex-client main.cpp)
target_link_libraries(dex-client PRIVATE host_options dex magic_enum::magic_enum)
