# ---------------------------------------------------------------------------
# App Subsystem: State Models and Orchestration (app_support only)
# ---------------------------------------------------------------------------

# App Support & State Model Library（FFI 与控制面依赖；不含旧 Viewer）
add_library(gs3d_app_support STATIC
    AppConfig.cpp
    config/AppConfigCommandLine.cpp
    config/AppConfigValidation.cpp
    config/AppConfigViewerToml.cpp
    session/BenchmarkSession.cpp
    session/PreprocessedBundle.cpp
    config/RecentProjects.cpp
    config/ResourcePath.cpp
    config/UserPreferences.cpp
    session/TileLoadCommit.cpp
    session/TilePointCache.cpp
    systems/ViewportResizeScheduler.cpp
    state/ViewportInteractionState.cpp
    state/ViewportPresentationState.cpp
    systems/ViewerFrameClock.cpp
    state/ViewerWorkbenchLayout.cpp
    systems/MeasurementManager.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/layouts/LayoutRegistry.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/WorkspaceManager.cpp
)

target_include_directories(gs3d_app_support PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
    ${NLOHMANN_JSON_INCLUDE_DIR}
)

target_link_libraries(gs3d_app_support PUBLIC
    gs3d_preprocess
    Vulkan::Vulkan
)
