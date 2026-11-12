iotpp_require_hub_deps()

# Static web UI: every file under web/ plus vendored browser libraries is
# compiled into the executable (single-binary deployment). Use --web-root
# to serve the source tree instead while developing the frontend.
set(web_dir "${CMAKE_CURRENT_SOURCE_DIR}/web")
file(GLOB_RECURSE web_files CONFIGURE_DEPENDS "${web_dir}/*")
set(web_entries "")
set(web_sources "")
foreach(file IN LISTS web_files)
    file(RELATIVE_PATH url "${web_dir}" "${file}")
    list(APPEND web_entries "${url}=${file}")
    list(APPEND web_sources "${file}")
endforeach()
foreach(entry IN LISTS IOTPP_WEB_VENDOR_FILES)
    list(APPEND web_entries "${entry}")
    string(REGEX REPLACE "^[^=]*=" "" file "${entry}")
    list(APPEND web_sources "${file}")
endforeach()
set(assets_cpp "${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp")
set(entries_file "${CMAKE_CURRENT_BINARY_DIR}/web_assets.list")
file(WRITE "${entries_file}.tmp" "${web_entries}")
file(COPY_FILE "${entries_file}.tmp" "${entries_file}" ONLY_IF_DIFFERENT)
add_custom_command(
    OUTPUT "${assets_cpp}"
    COMMAND ${CMAKE_COMMAND} "-DOUTPUT=${assets_cpp}" "-DENTRIES_FILE=${entries_file}"
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    DEPENDS ${web_sources} "${entries_file}" "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    COMMENT "Embedding web UI"
    VERBATIM)

add_executable(iotpp-hub
    main.cpp
    src/assets.cpp
    src/dex_link.cpp
    src/devices.cpp
    src/http_api.cpp
    src/registry.cpp
    src/storage.cpp
    "${assets_cpp}")
target_include_directories(iotpp-hub PRIVATE src)
target_compile_features(iotpp-hub PRIVATE cxx_std_20)
target_link_libraries(iotpp-hub PRIVATE host_options dex magic_enum::magic_enum
    nlohmann_json::nlohmann_json httplib::httplib SQLite::SQLite3)
