# ---------------------------------------------------------------------------
# Engine core + C-ABI façade for Flutter and other frontends
# ---------------------------------------------------------------------------

add_library(gs3d_engine_core STATIC
    HeadlessRenderKernel.cpp
)

set_target_properties(gs3d_engine_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(gs3d_engine_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/stb
)

target_link_libraries(gs3d_engine_core PUBLIC
    gs3d_data
    gs3d_render
    gs3d_camera_core
    Vulkan::Vulkan
)

# FFI only contains ABI adapters and the legacy metadata/control group.  The
# rendering implementation is kept in gs3d_engine_core so a future JNI, Rust,
# Python or native desktop frontend can reuse it without depending on Flutter.
add_library(gs3d_ffi SHARED
    geoscatter3d_ffi.cpp
    Gs3dEngineApi.cpp
)

target_include_directories(gs3d_ffi PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
    ${PROJECT_SOURCE_DIR}/third_party/stb
)

target_link_libraries(gs3d_ffi PRIVATE
    gs3d_engine_core
    gs3d_data
    gs3d_platform
    gs3d_app_support
    gs3d_control
    Vulkan::Vulkan
)

if(WIN32)
    target_link_libraries(gs3d_ffi PRIVATE comdlg32 ole32 shell32 ws2_32)
endif()

target_compile_definitions(gs3d_ffi PRIVATE GS3D_FFI_EXPORTS)

# Copy dll to root build output directory
if(WIN32)
    add_custom_command(TARGET gs3d_ffi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:gs3d_ffi>
                ${PROJECT_BINARY_DIR}/$<CONFIG>/$<TARGET_FILE_NAME:gs3d_ffi>
    )
endif()
