# Trackmania map password tool: Dear ImGui + GLFW + OpenGL3, Windows / macOS / Linux.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#   ctest --test-dir build -C Release
#
# Linux needs the usual GLFW/GL dev packages, e.g. on Debian/Ubuntu:
#   sudo apt install build-essential cmake pkg-config libgl-dev libx11-dev libxrandr-dev \
#        libxinerama-dev libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev
# and zenity or kdialog at runtime for the file dialogs (drag & drop always works).
cmake_minimum_required(VERSION 3.20)
project(tm-map-password-reset CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # self-contained .exe
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(TM_BUILD_GUI "Build the GUI (fetches Dear ImGui, GLFW, portable-file-dialogs)" ON)

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_include_directories(test_core PRIVATE src)
add_test(NAME core COMMAND test_core)

if(NOT TM_BUILD_GUI)
    return()
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
FetchContent_Declare(pfd
    GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
    GIT_TAG c12ea8c9a727f5320a2b4570aee863bbede2a204
    SOURCE_SUBDIR header-only)  # nonexistent dir: fetch only, don't add its CMakeLists

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(imgui glfw pfd)

find_package(OpenGL REQUIRED)

add_executable(tm-map-password-reset WIN32 MACOSX_BUNDLE
    src/main.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(tm-map-password-reset PRIVATE
    src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${pfd_SOURCE_DIR})
target_link_libraries(tm-map-password-reset PRIVATE glfw OpenGL::GL)

if(WIN32)
    target_compile_definitions(tm-map-password-reset PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_link_options(tm-map-password-reset PRIVATE /ENTRY:mainCRTStartup)  # keep int main() with no console
    endif()
endif()
