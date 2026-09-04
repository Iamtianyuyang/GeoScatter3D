# ---------------------------------------------------------------------------
# Source Directory Root: Orchestrate Modules and Top-Level Executables
# ---------------------------------------------------------------------------

add_subdirectory(platform)
add_subdirectory(data)
add_subdirectory(preprocess)
add_subdirectory(render)
add_subdirectory(app)
add_subdirectory(camera)
add_subdirectory(control)
add_subdirectory(ui)
add_subdirectory(ffi)

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------

# Main Interactive Viewer Executable
add_executable(GeoScatter3D
    main.cpp
)

target_include_directories(GeoScatter3D PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
    ${PROJECT_SOURCE_DIR}/third_party/stb
    ${PROJECT_SOURCE_DIR}/third_party/nanosvg
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(GeoScatter3D PRIVATE
    gs3d_app
)

copy_runtime_assets(GeoScatter3D)

add_custom_command(TARGET GeoScatter3D POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:GeoScatter3D>
            ${PROJECT_BINARY_DIR}/$<CONFIG>/$<TARGET_FILE_NAME:GeoScatter3D>
)

# Standalone Preprocessor Executable
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

add_custom_command(TARGET GeoScatter3DPreprocess POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:GeoScatter3DPreprocess>
            ${PROJECT_BINARY_DIR}/$<CONFIG>/$<TARGET_FILE_NAME:GeoScatter3DPreprocess>
)

# Ground-truth validator utility
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
