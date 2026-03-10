cmake_minimum_required(VERSION 3.14)
project(backend_notes)

set(CMAKE_CXX_STANDARD 17)

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)

# Принудительно указываем пути к Linux библиотекам
set(GLEW_INCLUDE_DIR "/usr/include")
set(GLEW_LIBRARY "/usr/lib/x86_64-linux-gnu/libGLEW.so")
set(SDL2_INCLUDE_DIR "/usr/include/SDL2")
set(SDL2_LIBRARY "/usr/lib/x86_64-linux-gnu/libSDL2.so")
set(OPENGL_LIBRARY "/usr/lib/x86_64-linux-gnu/libOpenGL.so")

# ZeroMQ
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# ImGui
add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC 
    ${THIRD_PARTY_DIR}/imgui
    ${SDL2_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIR}
)

# ImPlot
add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)

target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PRIVATE imgui)

# Main executable
add_executable(main ${SRC_DIR}/main.cpp)
target_include_directories(main PRIVATE
    ${THIRD_PARTY_DIR}/imgui
    ${THIRD_PARTY_DIR}/implot
    ${SDL2_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIRS}
)

target_link_libraries(main PRIVATE
    imgui
    implot
    ${SDL2_LIBRARY}
    ${OPENGL_LIBRARY}
    ${GLEW_LIBRARY}
    ${ZMQ_LIBRARIES}
    pthread  # Добавляем pthread для ZeroMQ
)