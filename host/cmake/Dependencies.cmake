# Third-party dependencies shared by the host tools. System packages are used
# when available; otherwise pinned versions are fetched at configure time.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

function(iotpp_require_magic_enum)
    if(TARGET magic_enum::magic_enum)
        return()
    endif()
    find_package(magic_enum 0.9.7 CONFIG QUIET)
    if(NOT magic_enum_FOUND)
        FetchContent_Declare(magic_enum
            GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
            GIT_TAG v0.9.7
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(magic_enum)
    endif()
endfunction()

function(iotpp_require_hub_deps)
    iotpp_require_magic_enum()

    # JSON
    find_package(nlohmann_json 3.11 CONFIG QUIET)
    if(NOT nlohmann_json_FOUND)
        FetchContent_Declare(nlohmann_json
            URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
            URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(nlohmann_json)
    endif()

    # HTTP server (header-only; plain HTTP on the LAN)
    if(NOT TARGET httplib::httplib)
        FetchContent_Declare(httplib
            URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR do-not-configure)
        FetchContent_MakeAvailable(httplib)
        add_library(httplib_header INTERFACE)
        target_include_directories(httplib_header SYSTEM INTERFACE "${httplib_SOURCE_DIR}")
        find_package(Threads REQUIRED)
        target_link_libraries(httplib_header INTERFACE Threads::Threads)
        add_library(httplib::httplib ALIAS httplib_header)
    endif()

    # SQLite
    find_package(SQLite3 QUIET)
    if(NOT SQLite3_FOUND AND NOT TARGET SQLite::SQLite3)
        FetchContent_Declare(sqlite_amalgamation
            URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(sqlite_amalgamation)
        add_library(sqlite3_bundled STATIC "${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c")
        target_include_directories(sqlite3_bundled SYSTEM PUBLIC "${sqlite_amalgamation_SOURCE_DIR}")
        target_compile_definitions(sqlite3_bundled PRIVATE SQLITE_DQS=0 SQLITE_THREADSAFE=1
            SQLITE_DEFAULT_WAL_SYNCHRONOUS=1 SQLITE_OMIT_LOAD_EXTENSION)
        set_target_properties(sqlite3_bundled PROPERTIES POSITION_INDEPENDENT_CODE ON)
        find_package(Threads REQUIRED)
        target_link_libraries(sqlite3_bundled PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
        add_library(SQLite::SQLite3 ALIAS sqlite3_bundled)
    endif()

    # Browser libraries (vendored into the binary; no CDN needed on the LAN)
    FetchContent_Declare(web_htm
        URL https://registry.npmjs.org/htm/-/htm-3.1.1.tgz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR do-not-configure)
    FetchContent_Declare(web_uplot
        URL https://registry.npmjs.org/uplot/-/uplot-1.6.31.tgz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR do-not-configure)
    FetchContent_MakeAvailable(web_htm web_uplot)
    set(IOTPP_WEB_VENDOR_FILES
        "vendor/preact.js=${web_htm_SOURCE_DIR}/preact/standalone.module.js"
        "vendor/uPlot.js=${web_uplot_SOURCE_DIR}/dist/uPlot.esm.js"
        "vendor/uPlot.css=${web_uplot_SOURCE_DIR}/dist/uPlot.min.css"
        PARENT_SCOPE)
endfunction()
