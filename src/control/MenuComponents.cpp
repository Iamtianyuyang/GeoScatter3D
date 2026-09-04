#include "control/MenuComponents.hpp"

#include "ui/layouts/LayoutRegistry.hpp"
#include "ui/PanelRegistry.hpp"

#include <stdexcept>
#include <string>

namespace gs3d::control {

namespace {

// ── 菜单项基类 ─────────────────────────────────────────────────────
class MenuComponentBase : public Component {
public:
    MenuComponentBase(
        gs3d::app::AppState& app_state,
        ComponentInfo info,
        std::vector<CommandSpec> caps
    )
        : app_state_(app_state),
          info_(std::move(info)),
          capabilities_(std::move(caps)) {}

    const ComponentInfo& info() const noexcept override { return info_; }
    const std::vector<CommandSpec>& capabilities() const noexcept override { return capabilities_; }

    nlohmann::json get_state() override {
        return {
            {"id", info_.id},
            {"name", info_.name},
            {"type", component_type_name(info_.type)},
            {"debug", info_.debug}
        };
    }

    nlohmann::json execute(const std::string& command, const nlohmann::json& params) override {
        if (command == "get_state") return get_state();
        if (command == "click") { do_click(params); return {{"id", info_.id}, {"clicked", true}}; }
        throw ComponentError(-32601, "unknown command: " + command);
    }

protected:
    virtual void do_click(const nlohmann::json& /*params*/) = 0;
    gs3d::app::AppState& app_state_;

private:
    ComponentInfo info_;
    std::vector<CommandSpec> capabilities_;
};

// ── 文件菜单 ───────────────────────────────────────────────────────

class FileOpenComponent final : public MenuComponentBase {
public:
    FileOpenComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.file.open", "打开数据文件", "Ctrl+O 打开数据文件",
             ComponentType::kMenu, false},
            {{"click", "触发打开文件对话框", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        app_state_.control_actions.open_requested = true;
    }
};

class FileOpenBundleComponent final : public MenuComponentBase {
public:
    FileOpenBundleComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.file.open_bundle", "打开 GS3D Bundle", "打开 GS3D Bundle 项目",
             ComponentType::kMenu, false},
            {{"click", "触发打开 Bundle 对话框", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        app_state_.control_actions.open_bundle_requested = true;
    }
};

class FileScreenshotComponent final : public MenuComponentBase {
public:
    FileScreenshotComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.file.screenshot", "截图", "保存当前视口截图",
             ComponentType::kMenu, false},
            {{"click", "触发截图保存", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        app_state_.control_actions.screenshot_requested = true;
    }
};

// ── 视图菜单 ───────────────────────────────────────────────────────

class ViewNewViewComponent final : public MenuComponentBase {
public:
    ViewNewViewComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.view.new_view", "新建视图", "Ctrl+N 新建视图",
             ComponentType::kMenu, false},
            {{"click", "创建新视口", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        // 通过 control_actions 传递，由 ViewerApp 处理
        app_state_.control_actions.camera_view_axis = -2;  // 特殊值：新建视图
    }
};

class ViewRestoreWorkspaceComponent final : public MenuComponentBase {
public:
    ViewRestoreWorkspaceComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.view.restore_workspace", "恢复默认工作区", "重置窗口布局",
             ComponentType::kMenu, false},
            {{"click", "恢复默认工作区", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        app_state_.control_actions.restore_default_workspace_requested = true;
    }
};

class ViewThemeComponent final : public MenuComponentBase {
public:
    ViewThemeComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.view.theme", "主题切换", "切换 UI 主题",
             ComponentType::kMenu, false},
            {{"click", "切换到下一个主题", false},
             {"set_value", "设置主题 (value: 主题索引 0-3)", true},
             {"get_state", "获取当前主题", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        // 循环切换：0→1→2→3→0
        app_state_.control_actions.theme_change_requested = true;
        app_state_.control_actions.theme_id =
            (app_state_.control_actions.theme_id + 1) % 4;
    }

    nlohmann::json execute(const std::string& command, const nlohmann::json& params) override {
        if (command == "set_value") {
            const int theme_id = params.value("value", 0);
            app_state_.control_actions.theme_change_requested = true;
            app_state_.control_actions.theme_id = theme_id % 4;
            return {{"id", info().id}, {"theme_id", app_state_.control_actions.theme_id}};
        }
        return MenuComponentBase::execute(command, params);
    }
};

class ViewLayoutComponent final : public MenuComponentBase {
public:
    ViewLayoutComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.view.layout", "布局切换", "切换 UI 布局模式",
             ComponentType::kMenu, false},
            {{"click", "切换到下一个布局", false},
             {"set_value", "设置布局 (value: 布局 ID 或索引)", true},
             {"get_state", "获取当前布局", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        auto& reg = ui::LayoutRegistry::instance();
        const std::size_t count = reg.layout_count();
        if (count == 0) return;
        std::size_t cur_idx = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (reg.layout_at(i)->id == reg.active_layout_id()) {
                cur_idx = i;
                break;
            }
        }
        const std::size_t next_idx = (cur_idx + 1) % count;
        reg.set_active_layout_id(reg.layout_at(next_idx)->id);
    }

    nlohmann::json execute(const std::string& command, const nlohmann::json& params) override {
        auto& reg = ui::LayoutRegistry::instance();
        if (command == "set_value") {
            if (params.contains("value") && params["value"].is_string()) {
                reg.set_active_layout_id(params["value"].get<std::string>());
            } else if (params.contains("value") && params["value"].is_number_integer()) {
                const int idx = params["value"].get<int>();
                if (idx >= 0 && static_cast<std::size_t>(idx) < reg.layout_count()) {
                    reg.set_active_layout_id(reg.layout_at(static_cast<std::size_t>(idx))->id);
                }
            }
            return {{"id", info().id}, {"layout_id", std::string(reg.active_layout_id())}};
        }
        if (command == "get_state") {
            return {
                {"id", info().id},
                {"layout_id", std::string(reg.active_layout_id())},
                {"layout_name", std::string(reg.active_layout_name())}
            };
        }
        return MenuComponentBase::execute(command, params);
    }
};

// ── 窗口菜单 ───────────────────────────────────────────────────────

class WindowNewWorkspaceComponent final : public MenuComponentBase {
public:
    WindowNewWorkspaceComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.window.new_workspace", "新建工作窗口", "创建新工作窗口",
             ComponentType::kMenu, false},
            {{"click", "创建新工作窗口", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        // 通过 control_actions 传递，由 ViewerApp 处理
        app_state_.control_actions.camera_view_axis = -3;  // 特殊值：新建工作窗口
    }
};

// ── 帮助菜单 ───────────────────────────────────────────────────────

class HelpWelcomeComponent final : public MenuComponentBase {
public:
    HelpWelcomeComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.help.welcome", "欢迎页", "显示欢迎页",
             ComponentType::kMenu, false},
            {{"click", "打开欢迎页", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        app_state_.control_actions.show_welcome_requested = true;
    }
};

class HelpShortcutsComponent final : public MenuComponentBase {
public:
    HelpShortcutsComponent(gs3d::app::AppState& s)
        : MenuComponentBase(s,
            {"menu.help.shortcuts", "快捷键总览", "显示快捷键总览 overlay",
             ComponentType::kMenu, false},
            {{"click", "打开快捷键总览", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        app_state_.ui_chrome.shortcut_overlay_open = true;
    }
};

} // namespace

std::vector<std::unique_ptr<Component>> make_menu_components(
    gs3d::app::AppState& app_state,
    gs3d::app::UiActions& /*actions*/
) {
    std::vector<std::unique_ptr<Component>> result;
    result.push_back(std::make_unique<FileOpenComponent>(app_state));
    result.push_back(std::make_unique<FileOpenBundleComponent>(app_state));
    result.push_back(std::make_unique<FileScreenshotComponent>(app_state));
    result.push_back(std::make_unique<ViewNewViewComponent>(app_state));
    result.push_back(std::make_unique<ViewRestoreWorkspaceComponent>(app_state));
    result.push_back(std::make_unique<ViewLayoutComponent>(app_state));
    result.push_back(std::make_unique<ViewThemeComponent>(app_state));
    result.push_back(std::make_unique<WindowNewWorkspaceComponent>(app_state));
    result.push_back(std::make_unique<HelpWelcomeComponent>(app_state));
    result.push_back(std::make_unique<HelpShortcutsComponent>(app_state));
    return result;
}

} // namespace gs3d::control
