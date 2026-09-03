cmake_minimum_required(VERSION 3.28.3)

project(GeoScatter3D VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Sources contain UTF-8 (Chinese) string literals; without /utf-8 MSVC
    # interprets them in the system code page and mangles or rejects them.
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# Modular Subsystems
# ---------------------------------------------------------------------------

# External dependencies (Vulkan, GLFW, Threads, Python, nlohmann_json, glslang)
include(cmake/Dependencies.cmake)

# Shader compilation pipeline (GLSL -> SPIR-V)
include(cmake/Shaders.cmake)

# Source domain modules & executables (platform, data, render, ui, app, etc.)
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Testing Subsystem
# ---------------------------------------------------------------------------

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Packaging & Distribution (install & CPack)
# ---------------------------------------------------------------------------

include(cmake/Packaging.cmake)
