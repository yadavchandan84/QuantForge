cmake_minimum_required(VERSION 3.20)

project(QuantForge
    VERSION 0.1.0
    DESCRIPTION "Event-driven backtesting & trading simulator"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Standard & global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Place runtime artifacts in predictable locations
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(QF_BUILD_TESTS      "Build unit tests (GoogleTest)" ON)
option(QF_BUILD_BENCHMARKS "Build benchmarks"              ON)
option(QF_BUILD_PYTHON     "Build pybind11 Python module"  OFF)
option(QF_ENABLE_SANITIZERS "Enable ASan/UBSan (Debug)"    OFF)
option(QF_ENABLE_WARNINGS  "Enable high warning level"     ON)

# ---------------------------------------------------------------------------
# Interface target carrying common compile settings
# ---------------------------------------------------------------------------
add_library(qf_project_options INTERFACE)
target_compile_features(qf_project_options INTERFACE cxx_std_20)

if(QF_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(qf_project_options INTERFACE /W4)
    else()
        target_compile_options(qf_project_options INTERFACE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    endif()
endif()

if(QF_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(qf_project_options INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(qf_project_options INTERFACE
        -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(QF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(QF_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Python bindings
# ---------------------------------------------------------------------------
if(QF_BUILD_PYTHON)
    add_subdirectory(python)
endif()

message(STATUS "QuantForge ${PROJECT_VERSION}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests        : ${QF_BUILD_TESTS}")
message(STATUS "  Benchmarks   : ${QF_BUILD_BENCHMARKS}")
message(STATUS "  Python       : ${QF_BUILD_PYTHON}")
message(STATUS "  Sanitizers   : ${QF_ENABLE_SANITIZERS}")
