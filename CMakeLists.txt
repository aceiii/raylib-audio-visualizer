cmake_minimum_required(VERSION 3.15)

project(raylib-cpp-template VERSION 0.0.1 LANGUAGES CXX)

set(EXE_NAME main)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(TOML_EXCEPTIONS=0)

set(SOURCE_FILES
    src/main.cpp
    src/audiovisualizer.h
    src/audiovisualizer.cpp
)

set(ARGPARSE_BUILD_TESTS OFF)
set(KISSFFT_STATIC ON)
set(KISSFFT_TEST OFF)
set(KISSFFT_TOOLS OFF)

include(cmake/raylib.cmake)
include(cmake/rlimgui.cmake)
include(cmake/imgui_club.cmake)

add_subdirectory(external/spdlog)
add_subdirectory(external/argparse)
add_subdirectory(external/magic_enum)
add_subdirectory(external/nativefiledialog-extended)
add_subdirectory(external/tomlplusplus)
add_subdirectory(external/kissfft)

add_executable(${EXE_NAME} ${SOURCE_FILES})

target_link_libraries(${EXE_NAME} spdlog)
target_link_libraries(${EXE_NAME} argparse)
target_link_libraries(${EXE_NAME} magic_enum)
target_link_libraries(${EXE_NAME} rlimgui)
target_link_libraries(${EXE_NAME} nfd)
target_link_libraries(${EXE_NAME} tomlplusplus::tomlplusplus)
target_link_libraries(${EXE_NAME} kissfft)

target_include_directories(${EXE_NAME} PUBLIC external/rlimgui)

