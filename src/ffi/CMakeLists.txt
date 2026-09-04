# ---------------------------------------------------------------------------
# FFI Subsystem: C-ABI Shared Library for Flutter & External Bindings
# ---------------------------------------------------------------------------

add_library(gs3d_ffi SHARED
    geoscatter3d_ffi.cpp
)

target_include_directories(gs3d_ffi PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
)

target_link_libraries(gs3d_ffi PRIVATE
    gs3d_data
    gs3d_platform
    gs3d_app_support
    gs3d_control
)

if(WIN32)
    target_link_libraries(gs3d_ffi PRIVATE comdlg32)
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
