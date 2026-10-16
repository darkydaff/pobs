cmake_minimum_required(VERSION 3.20)
project(pobs VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
add_definitions(-D_GNU_SOURCE)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# OBS SDK & Flatpak include and library paths
set(OBS_FLATPAK_ROOT "/var/lib/flatpak/app/com.obsproject.Studio/current/active/files")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OBS_FLATPAK_ROOT}/include
    ${OBS_FLATPAK_ROOT}/include/obs
    ${OBS_FLATPAK_ROOT}/include/simde
)

set(POBS_SOURCES
    src/pobs_plugin.cpp
    src/pobs_service.cpp
    src/source/pobs_source.c
    src/decoder/pobs_decoder.c
    src/net/pobs_ws_server.cpp
    src/adb/adb_manager.cpp
    src/ui/pobs_dock.cpp
    src/ui/pobs_icons.cpp
    src/ui/qr_code_widget.cpp
    src/ui/qrcodegen.cpp
)

set(POBS_HEADERS
    src/pobs_service.hpp
    src/source/pobs_source.h
    src/decoder/pobs_decoder.h
    src/net/pobs_ws_server.hpp
    src/adb/adb_manager.hpp
    src/ui/pobs_dock.hpp
    src/ui/pobs_icons.hpp
    src/ui/qr_code_widget.hpp
    src/ui/qrcodegen.hpp
)

add_library(pobs MODULE
    ${POBS_SOURCES}
    ${POBS_HEADERS}
)

set_target_properties(pobs PROPERTIES
    PREFIX ""
)

target_link_libraries(pobs PRIVATE
    "${OBS_FLATPAK_ROOT}/lib/libobs.so"
    "${OBS_FLATPAK_ROOT}/lib/libobs-frontend-api.so"
    "${OBS_FLATPAK_ROOT}/lib/libavcodec.so"
    "${OBS_FLATPAK_ROOT}/lib/libavutil.so"
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    pthread
)

# Installation target
set(USER_OBS_PLUGIN_DIR "$ENV{HOME}/.var/app/com.obsproject.Studio/config/obs-studio/plugins/pobs")

install(TARGETS pobs
    LIBRARY DESTINATION "${USER_OBS_PLUGIN_DIR}/bin/64bit"
)
