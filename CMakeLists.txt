cmake_minimum_required(VERSION 3.20)
project(Y8960Player VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(Y8960_BUILD_CLI   "Build the y8960player command" ON)
option(Y8960_BUILD_GUI   "Build the y8960gui application" ON)
option(Y8960_BUILD_TESTS "Build the tests" ON)

# 開発用。ビルド済みのエミュレータのライブラリがあるフォルダを渡すと、実行ファイルの
# フォルダへ写す。配布物の組み立てはこれに頼らない。
set(Y8960_EMULATOR_DIR "" CACHE PATH "Folder holding the prebuilt emulator libraries")

# エミュレータのライブラリは実行ファイルと同じフォルダから探すので、実行ファイルを
# 1箇所に集める。
# 生成式を使うと、マルチ構成のジェネレータが構成名のフォルダを勝手に足さなくなる。
set(Y8960_BIN_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${Y8960_BIN_DIR}")

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(src/core)

if(Y8960_BUILD_CLI OR Y8960_BUILD_GUI)
    include(FetchContent)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.16
        GIT_SHALLOW    TRUE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
    add_subdirectory(src/app)
endif()

if(Y8960_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(Y8960_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

if(Y8960_EMULATOR_DIR)
    set(_emu_files "")
    foreach(_name Y8960emuEngine EPSGemuEngine DSAemuEngine)
        file(GLOB _found
            "${Y8960_EMULATOR_DIR}/${_name}.dll"
            "${Y8960_EMULATOR_DIR}/lib${_name}.so"
            "${Y8960_EMULATOR_DIR}/lib${_name}.dylib")
        list(APPEND _emu_files ${_found})
    endforeach()
    if(NOT _emu_files)
        message(FATAL_ERROR "Y8960_EMULATOR_DIR にエミュレータのライブラリがありません: ${Y8960_EMULATOR_DIR}")
    endif()
    add_custom_target(copy_emulators ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${Y8960_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_emu_files} "${Y8960_BIN_DIR}"
        COMMENT "Copying emulator libraries")
endif()

if(Y8960_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
