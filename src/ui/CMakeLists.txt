# ---------------------------------------------------------------------------
# UI Subsystem: Dear ImGui, Panels, Styling, Canvas, and Chrome
# ---------------------------------------------------------------------------

# ImGui backend library
add_library(gs3d_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(gs3d_imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(gs3d_imgui PUBLIC
    Vulkan::Vulkan
    glfw
)

# Core UI component library
add_library(gs3d_ui STATIC
    ImGuiLayer.cpp
    AppChrome.cpp
    color/Theme.cpp
    color/ThemeRegistry.cpp
    layouts/LayoutMetrics.cpp
    Widgets.cpp
    UiRoot.cpp
    layouts/standard_workbench/StandardWorkbenchLayout.cpp
    UiOverlays.cpp
    layouts/standard_workbench/WorkbenchViewControls.cpp
    layouts/floating_dock/FloatingDockLayout.cpp
    layouts/floating_dock/FloatingDockUi.cpp
    layouts/analysis_rail/AnalysisRailLayout.cpp
    layouts/analysis_rail/AnalysisRailUi.cpp
    layouts/standard_workbench/DockLayoutBuilder.cpp
    SvgLogoTexture.cpp
    canvas/ViewportAxisTicks.cpp
    canvas/ViewportCanvas.cpp
    panels/RegionStatsPanel.cpp
    panels/DatasetPanel.cpp
    panels/NavigationMapPanel.cpp
    panels/MeasurementPanel.cpp
    panels/AuxiliaryPanels.cpp
    panels/RenderSettingsPanel.cpp
)

target_include_directories(gs3d_ui PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/stb
    ${PROJECT_SOURCE_DIR}/third_party/nanosvg
)

target_link_libraries(gs3d_ui PUBLIC
    gs3d_app_support
    gs3d_render
    gs3d_imgui
    gs3d_platform
    glfw
)
