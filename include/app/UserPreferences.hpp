#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gs3d::app {

struct AppState;

// Per-user settings must never be written into the repository's template
// configuration. On Linux this is XDG_CONFIG_HOME (or ~/.config); on Windows
// it is APPDATA. GS3D_USER_CONFIG_DIR is available for tests and portable
// deployments that need an explicit location.
[[nodiscard]]
std::filesystem::path user_preferences_path();

[[nodiscard]]
std::optional<std::string> load_preferred_gpu_preference();

// Writes only the machine-specific GPU choice. The update is atomic so an
// interrupted shutdown cannot leave a truncated preferences file behind.
// Other sections (e.g. [render_settings]) are preserved.
bool save_preferred_gpu_preference(std::string_view preferred_gpu);

// 属性面板中用户可修改的关键渲染设置子集（点大小/形状/着色/色标/值域/
// 高度缩放）。写入用户偏好文件而非项目 viewer.toml，保证项目模板可提交。
struct RenderSettingsPreferences {
    float point_size = 1.5f;
    int point_shape = 0;   // 0=方形, 1=圆形, 2=菱形, 3=三角形
    int height_attr_index = 1;
    int color_attr_index = 0;
    float height_exaggeration = 1.0f;
    int colormap_index = 8;
    bool value_clip_enabled = false;
    float value_clip_min = 0.0f;
    float value_clip_max = 1.0f;
};

[[nodiscard]]
std::optional<RenderSettingsPreferences> load_render_settings_preferences();

// 将渲染设置写入用户偏好文件的 [render_settings] 段。原子更新；
// 其他段（如 [graphics]）保持不变。
bool save_render_settings_preferences(
    const RenderSettingsPreferences& preferences
);

// 将偏好应用到 AppState：覆盖全局默认 RenderSettingsState（不读取文件，
// 纯函数）；属性索引按当前数据集属性表钳制。由 make_initial_viewer_app_state
// 在播种各视图副本前调用，保证新开视图也继承偏好。
void apply_render_settings_preferences(
    AppState& state,
    const RenderSettingsPreferences& preferences
);

} // namespace gs3d::app
