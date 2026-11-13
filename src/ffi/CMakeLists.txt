# ---------------------------------------------------------------------------
# Engine core + C-ABI façade for Flutter and other frontends
# ---------------------------------------------------------------------------

add_library(gs3d_engine_core STATIC
    EngineHost.cpp
    TaskBoard.cpp
    FrameChannel.cpp
    HeadlessRenderKernel.cpp
    LodBuilder.cpp
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
    # TilePointCache（细尺度 tile 的 CPU LRU）在 app_support 中；
    # 依赖方向 app_support → preprocess → data，无回环。
    gs3d_app_support
    Vulkan::Vulkan
)

# FFI only contains ABI adapters and the legacy metadata/control group.  The
# rendering implementation is kept in gs3d_engine_core so a future JNI, Rust,
# Python or native desktop frontend can reuse it without depending on Flutter.
set(GS3D_FFI_SOURCES
    geoscatter3d_ffi.cpp
    Gs3dEngineApi.cpp
)

if(ANDROID)
    # 平台 surface 适配层（JNI）：ANativeWindow → surface 句柄，形如 C ABI。
    list(APPEND GS3D_FFI_SOURCES AndroidSurfaceJni.cpp)
endif()

add_library(gs3d_ffi SHARED ${GS3D_FFI_SOURCES})

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

if(ANDROID)
    # log   : util/Log.hpp 在 Android 上走 __android_log_write（app 的
    #         stdout/stderr 不进 logcat），该符号由 NDK 的 liblog 提供；
    # android: ANativeWindow_fromSurface / ANativeWindow_release。
    target_link_libraries(gs3d_ffi PRIVATE log android)
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
