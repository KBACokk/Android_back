cmake_minimum_required(VERSION 3.10)
set(OpenGL_GL_PREFERENCE GLVND)

project(NetworkAnalyzer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_IGNORE_PATH "/mnt/c/msys64;/mnt/c/msys64/mingw64;/mnt/c/msys64/mingw64/lib/cmake")

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(CURL REQUIRED libcurl)

find_package(PostgreSQL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

set(STB_IMAGE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/stb")

set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)
set(IMGUI_DIR ${THIRD_PARTY_DIR}/imgui)
set(IMPLOT_DIR ${THIRD_PARTY_DIR}/implot)

file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/heatmap" HEATMAP_CACHE_ROOT)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" PROJECT_SOURCE_ROOT)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/paths_config.h.in"
    "${CMAKE_BINARY_DIR}/generated/paths_config.h"
    @ONLY
)

include_directories(
    ${STB_IMAGE_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMPLOT_DIR}
    ${ZMQ_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIRS}
)

set(PROJECT_SOURCES
    src/main.cpp
    src/db/db.cpp
    src/server/server.cpp
    src/gui/gui.cpp
    src/osm/curl_func.cpp
    src/osm/tile_cache.cpp
    src/osm/tile_paths.cpp
    src/osm/tile_texture.cpp
    src/osm/tile_loader.cpp
    src/osm/osm_map.cpp
    src/osm/heatmap.cpp
)

set(UI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)

add_executable(network_analyzer ${PROJECT_SOURCES} ${UI_SOURCES})

target_link_libraries(network_analyzer
    ${ZMQ_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${CURL_LIBRARIES}
    ${PostgreSQL_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    pthread
    dl
)