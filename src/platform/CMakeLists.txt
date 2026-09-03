# ---------------------------------------------------------------------------
# Platform Subsystem: OS, CPU, Windowing, and Native Dialogs
# ---------------------------------------------------------------------------

add_library(gs3d_platform STATIC
    CpuInfo.cpp
    Window.cpp
    NativeFileDialog.cpp
)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)

    target_sources(gs3d_platform PRIVATE
        NativeFileDialogMac.mm
    )
    set_source_files_properties(
        NativeFileDialogMac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )

    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(
        UNIFORM_TYPE_IDENTIFIERS_FRAMEWORK
        UniformTypeIdentifiers
        REQUIRED
    )
    target_link_libraries(gs3d_platform PUBLIC
        ${COCOA_FRAMEWORK}
        ${UNIFORM_TYPE_IDENTIFIERS_FRAMEWORK}
    )
endif()

target_include_directories(gs3d_platform PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_platform PUBLIC
    glfw
    Threads::Threads
)
