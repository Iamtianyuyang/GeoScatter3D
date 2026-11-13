cmake_minimum_required(VERSION 3.28.3)

# The version has one home, engine/foundation/Gs3dVersion.hpp, shared with the FFI.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/engine/foundation/Gs3dVersion.hpp" _gs3d_version_header)
string(REGEX MATCH "GS3D_PRODUCT_VERSION \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _gs3d_version_match
    "${_gs3d_version_header}")
if(NOT _gs3d_version_match)
    message(FATAL_ERROR "engine/foundation/Gs3dVersion.hpp: GS3D_PRODUCT_VERSION literal not found")
endif()

project(GeoScatter3D VERSION ${CMAKE_MATCH_1} LANGUAGES CXX)

# gs3d_ffi is a shared library built from static libs; Linux toolchains need
# those objects to be position independent (Windows/Android are unaffected).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# New engine tree: headers are included as "<layer>/<module>/<file>.hpp".
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/engine)

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

# 合成基准 bundle 生成器（粗尺度 LOD + 细尺度 tile 的真机/桌面验证资产）。
option(GS3D_BUILD_BENCH_BUNDLE_TOOL
    "Build the synthetic bundle generator used for large-dataset verification"
    ON
)
if(GS3D_BUILD_BENCH_BUNDLE_TOOL AND NOT ANDROID)
    add_executable(gs3d_bench_bundle tools/gs3d_bench_bundle.cpp)
    target_include_directories(gs3d_bench_bundle PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(gs3d_bench_bundle PRIVATE
        gs3d_data
        gs3d_preprocess
    )
else()
    # 保证 gs3d_shaders 等依赖目标在配置阶段即存在时行为一致。
    set(GS3D_BUILD_BENCH_BUNDLE_TOOL OFF)
endif()

# ---------------------------------------------------------------------------
# Testing Subsystem
# ---------------------------------------------------------------------------

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Packaging & Distribution (install & CPack) — desktop only
# ---------------------------------------------------------------------------

if(NOT ANDROID)
    include(cmake/Packaging.cmake)
endif()
