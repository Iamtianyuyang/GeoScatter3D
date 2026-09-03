# ---------------------------------------------------------------------------
# Distribution. `cmake --install <build-dir> --config Release --prefix <dir>`
# produces the self-contained release layout that ResourcePath documents
# (executables at the root, config/ and assets/ beside them, data/ created
# at runtime). CPack wraps the same rules into a versioned archive.
# ---------------------------------------------------------------------------

install(TARGETS
    GeoScatter3D
    GeoScatter3DPreprocess
    gs3d_groundtruth
    RUNTIME DESTINATION .
)

if(WIN32)
    # vcpkg's applocal deployment already places every dependent DLL
    # (vulkan-1.dll, glfw3.dll, ...) next to the built executable. Ship
    # exactly that set instead of re-resolving dependencies at install time.
    install(
        DIRECTORY $<TARGET_FILE_DIR:GeoScatter3D>/
        DESTINATION .
        FILES_MATCHING PATTERN "*.dll"
    )
endif()

if(MSVC)
    # The executables link the dynamic CRT (/MD): msvcp140.dll and
    # vcruntime140*.dll are NOT part of Windows, so a machine without the
    # VC++ Redistributable would fail to launch. Ship them app-locally.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    include(InstallRequiredSystemLibraries)
endif()

# Source assets plus the SPIR-V generated into the build tree, mirroring
# copy_runtime_assets. Stale .spv in the source tree must not shadow the
# build outputs, so only the generated directory contributes shaders.
install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/assets/
    DESTINATION assets
    PATTERN "*.spv" EXCLUDE
)
install(DIRECTORY ${GS3D_GENERATED_SHADER_DIR}/ DESTINATION assets/shaders)

# Committed configuration templates only — local UI state (imgui_layout.ini)
# and debug configs stay out of the package.
install(FILES
    ${PROJECT_SOURCE_DIR}/config/viewer.toml
    ${PROJECT_SOURCE_DIR}/config/sample-viewer.toml
    DESTINATION config
)

install(FILES ${PROJECT_SOURCE_DIR}/examples/sample-points.csv DESTINATION examples)

install(FILES ${PROJECT_SOURCE_DIR}/README.md DESTINATION .)

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME
    "${PROJECT_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
