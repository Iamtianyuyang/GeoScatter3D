#include "app/ViewerApp.hpp"
#include "app/UiActions.hpp"
#include "util/Log.hpp"
#include "platform/NativeFileDialog.hpp"
#include "platform/Window.hpp"

#include <filesystem>

namespace gs3d::app {

void ViewerApp::apply_project_open_commands(
    const UiActions& gui_cmds,
    gs3d::platform::Window& window
) {
    // 窗口级命令与打开命令一起消费（悬浮 Dock 布局的全屏切换按钮）。
    if (gui_cmds.toggle_fullscreen_requested) {
        window.toggle_fullscreen();
    }
    if (gui_cmds.show_welcome_requested) {
        open_request_ = ViewerOpenRequest{
            .kind = ViewerOpenRequestKind::Welcome
        };
        window.request_close();
    } else if (!gui_cmds.open_project_path.empty()) {
        open_request_ = ViewerOpenRequest{
            .kind = ViewerOpenRequestKind::Project,
            .path = gui_cmds.open_project_path
        };
        window.request_close();
    } else if (!gui_cmds.open_raw_data_path.empty()) {
        open_request_ = ViewerOpenRequest{
            .kind = ViewerOpenRequestKind::RawData,
            .path = gui_cmds.open_raw_data_path
        };
        window.request_close();
    } else if (gui_cmds.open_bundle_requested) {
        const auto result = gs3d::platform::choose_project_directory();
        if (!result.error.empty()) {
            gs3d::util::log::error() << "[OPEN] " << result.error << '\n';
        } else if (result.path.has_value()) {
            std::error_code ec;
            const auto manifest_path = *result.path / "manifest.toml";
            if (!std::filesystem::is_regular_file(manifest_path, ec)) {
                gs3d::util::log::error()
                    << "[OPEN] 请选择包含 manifest.toml 的 "
                    << ".gs3d.bundle 项目目录。\n";
            } else {
                open_request_ = ViewerOpenRequest{
                    .kind = ViewerOpenRequestKind::Project,
                    .path = *result.path
                };
                window.request_close();
            }
        }
    } else if (gui_cmds.open_requested) {
        const auto result = gs3d::platform::choose_raw_data_file();
        if (!result.error.empty()) {
            gs3d::util::log::error() << "[OPEN] " << result.error << '\n';
        } else if (result.path.has_value()) {
            open_request_ = ViewerOpenRequest{
                .kind = ViewerOpenRequestKind::RawData,
                .path = *result.path
            };
            window.request_close();
        }
    }
}

} // namespace gs3d::app
