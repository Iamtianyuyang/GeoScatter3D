# ---------------------------------------------------------------------------
# Source Directory Root: Only gs3d_ffi shared library (single-process engine)
# ---------------------------------------------------------------------------

add_subdirectory(platform)
add_subdirectory(data)
add_subdirectory(${PROJECT_SOURCE_DIR}/engine/pipeline ${CMAKE_CURRENT_BINARY_DIR}/pipeline)

add_library(gs3d_pipeline_ffi SHARED ${PROJECT_SOURCE_DIR}/engine/abi/gs3d_pipeline.cpp)
target_compile_definitions(gs3d_pipeline_ffi PRIVATE GS3D_PIPELINE_EXPORTS)
target_link_libraries(gs3d_pipeline_ffi PRIVATE gs3d_preprocess)
if(ANDROID)
    target_link_libraries(gs3d_pipeline_ffi PRIVATE log)
endif()
add_subdirectory(render)
add_subdirectory(app)
add_subdirectory(camera)
add_subdirectory(control)
add_subdirectory(ui)
add_subdirectory(ffi)

# ---------------------------------------------------------------------------
# 独立命令行工具（CSV/DAT → GS3D v2 bundle、格式校验）
#
# 5e884c8 重写本文件时把这两个目标漏掉了，但源码（src/preprocess_main.cpp、
# tools/gs3d_groundtruth.cpp）、文档（docs/architecture.md、
# docs/spec/gs3d-format.md）、打包清单（cmake/Packaging.cmake）以及
# tests/FreshClonePreprocess 测试都还在引用它们。缺了目标 → 测试靠构建树里
# 遗留的旧 exe 假绿，干净克隆直接 Not Run。
# ---------------------------------------------------------------------------
add_executable(GeoScatter3DPreprocess
    preprocess_main.cpp
)
target_include_directories(GeoScatter3DPreprocess PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DPreprocess PRIVATE
    gs3d_preprocess
    gs3d_platform
    gs3d_app_support
)
copy_runtime_assets(GeoScatter3DPreprocess)

add_executable(gs3d_pack
    ${PROJECT_SOURCE_DIR}/engine/cli/gs3d_pack.cpp
)
target_include_directories(gs3d_pack PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/engine
)
target_link_libraries(gs3d_pack PRIVATE
    gs3d_preprocess
    gs3d_data
)

add_executable(gs3d_groundtruth
    ${PROJECT_SOURCE_DIR}/tools/gs3d_groundtruth.cpp
)
target_include_directories(gs3d_groundtruth PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(gs3d_groundtruth PRIVATE
    gs3d_data
    gs3d_platform
)
copy_runtime_assets(gs3d_groundtruth)
