cmake_minimum_required(VERSION 3.25)

if(APPLE)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path 
        OUTPUT_VARIABLE MACOS_SDK_PATH 
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CMAKE_OSX_SYSROOT ${MACOS_SDK_PATH} CACHE PATH "macOS SDK Root Path" FORCE)
endif()

project(MpscBufferSuite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT MSVC)
    add_compile_options(-O3 -march=native -flto -fno-plt -Wno-interference-size)
    add_link_options(-flto)
endif()

# Define the standalone header-only library target
add_library(mpsc_buffer INTERFACE)

# Expose the EXACT 'include' directory root on disk
target_include_directories(mpsc_buffer INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

add_subdirectory(apps/mpsc_benchmark)