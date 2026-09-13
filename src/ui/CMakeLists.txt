# ---------------------------------------------------------------------------
# UI Subsystem: 纯逻辑组件（面板注册表、工作区、主题、布局度量）。
# Flutter 为唯一前端；本库仅保留控制面/应用依赖的
# 无 GUI 依赖组件。
# ---------------------------------------------------------------------------

# Core UI component library
add_library(gs3d_ui STATIC
    WorkspaceManager.cpp
    color/Theme.cpp
    color/ThemeRegistry.cpp
    layouts/LayoutMetrics.cpp
    layouts/LayoutRegistry.cpp
)

target_include_directories(gs3d_ui PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_ui PUBLIC
    gs3d_app_support
    gs3d_render
    gs3d_platform
)
