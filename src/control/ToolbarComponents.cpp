#include "control/ToolbarComponents.hpp"

#include "ui/PanelRegistry.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace gs3d::control {

namespace {

// ── 基类 ───────────────────────────────────────────────────────────

class ToolbarComponentBase : public Component {
public:
    ToolbarComponentBase(
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
    virtual void do_click(const nlohmann::json&) = 0;
    gs3d::app::AppState& app_state_;

private:
    ComponentInfo info_;
    std::vector<CommandSpec> capabilities_;
};

// ── 只读组件基类（状态栏等） ─────────────────────────────────────

class ReadOnlyComponentBase : public Component {
public:
    ReadOnlyComponentBase(
        const gs3d::app::AppState& app_state,
        ComponentInfo info,
        std::vector<CommandSpec> caps
    )
        : app_state_(app_state),
          info_(std::move(info)),
          capabilities_(std::move(caps)) {}

    const ComponentInfo& info() const noexcept override { return info_; }
    const std::vector<CommandSpec>& capabilities() const noexcept override { return capabilities_; }

    nlohmann::json execute(const std::string& command, const nlohmann::json& params) override {
        if (command == "get_state") return get_state();
        throw ComponentError(-32601, "unknown command: " + command);
    }

protected:
    const gs3d::app::AppState& app_state_;

private:
    ComponentInfo info_;
    std::vector<CommandSpec> capabilities_;
};

// ── 工具栏组件 ─────────────────────────────────────────────────────

class ToolbarOpenComponent final : public ToolbarComponentBase {
public:
    ToolbarOpenComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"toolbar.open", "打开文件", "打开数据文件",
             ComponentType::kToolbar, false},
            {{"click", "触发打开文件", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override { app_state_.control_actions.open_requested = true; }
};

class ToolbarScreenshotComponent final : public ToolbarComponentBase {
public:
    ToolbarScreenshotComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"toolbar.screenshot", "截图", "保存视口截图",
             ComponentType::kToolbar, false},
            {{"click", "触发截图", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override { app_state_.control_actions.screenshot_requested = true; }
};

class ToolbarAddViewComponent final : public ToolbarComponentBase {
public:
    ToolbarAddViewComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"toolbar.add_view", "新建视图", "创建新视口",
             ComponentType::kToolbar, false},
            {{"click", "新建视口", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        // 通过 control_actions 传递，由 ViewerApp 处理
        app_state_.control_actions.camera_view_axis = -2;  // 特殊值：新建视图
    }
};

class ToolbarMeasureComponent final : public ToolbarComponentBase {
public:
    ToolbarMeasureComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"toolbar.measure", "测量", "切换测量模式",
             ComponentType::kToolbar, false},
            {{"click", "切换测量模式", false},
             {"get_state", "获取当前测量状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        auto& meas = gs3d::app::measurement_for_view(
            app_state_, app_state_.active_viewport_index);
        meas.toggle_measure_mode();
        if (!meas.measure_mode_active()) meas.clear_pending();
    }

    nlohmann::json get_state() override {
        auto& meas = gs3d::app::measurement_for_view(
            app_state_, app_state_.active_viewport_index);
        return {
            {"id", info().id},
            {"measure_mode_active", meas.measure_mode_active()}
        };
    }
};

class ToolbarLinkCameraComponent final : public ToolbarComponentBase {
public:
    ToolbarLinkCameraComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"toolbar.link_camera", "联动相机", "切换相机联动模式",
             ComponentType::kToolbar, false},
            {{"click", "切换相机联动", false},
             {"get_state", "获取联动状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        bool any_linked = false;
        for (auto& v : app_state_.render_views) {
            if (v.camera_linked) { any_linked = true; break; }
        }
        const bool new_state = !any_linked;
        for (auto& v : app_state_.render_views) v.camera_linked = new_state;
    }

    nlohmann::json get_state() override {
        bool any_linked = false;
        for (const auto& v : app_state_.render_views) {
            if (v.camera_linked) { any_linked = true; break; }
        }
        return {{"id", info().id}, {"linked", any_linked}};
    }
};

class ToolbarPanelPaletteComponent final : public ToolbarComponentBase {
public:
    ToolbarPanelPaletteComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"toolbar.panel_palette", "面板命令面板", "打开面板命令面板 (Ctrl+P)",
             ComponentType::kToolbar, false},
            {{"click", "打开面板命令面板", false},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        app_state_.ui_chrome.panel_palette_open = true;
        app_state_.ui_chrome.panel_palette_query[0] = '\0';
    }
};

// ── 原生运行时渲染参数组件 ─────────────────────────────────────────

class RuntimeRenderSettingsComponent final : public Component {
public:
    explicit RuntimeRenderSettingsComponent(gs3d::app::AppState& app_state)
        : app_state_(app_state) {}

    [[nodiscard]] const ComponentInfo& info() const noexcept override {
        return info_;
    }

    [[nodiscard]] const std::vector<CommandSpec>& capabilities() const noexcept override {
        return capabilities_;
    }

    [[nodiscard]] nlohmann::json get_state() override {
        const auto& settings = gs3d::app::render_settings_for_view(
            app_state_, app_state_.active_viewport_index
        );
        return {
            {"id", info_.id},
            {"point_size", settings.point_size},
            {"point_shape", settings.point_shape},
            {"height_attribute_index", settings.height_attr_index},
            {"height_attributes", settings.height_by_options},
            {"height_exaggeration", settings.height_exaggeration},
            {"color_attribute_index", settings.color_attr_index},
            {"color_attributes", settings.color_by_options},
            {"colormap_index", settings.colormap_index},
            {"value_clip_enabled", settings.value_clip_enabled},
            {"value_clip_min", settings.value_clip_min},
            {"value_clip_max", settings.value_clip_max}
            ,{"cache_hit_rate", settings.cache_hit_rate}
            ,{"loaded_tiles", settings.loaded_tiles}
            ,{"pending_tiles", settings.pending_tiles}
        };
    }

    [[nodiscard]] nlohmann::json execute(
        const std::string& command,
        const nlohmann::json& params
    ) override {
        if (command == "get_state") {
            return get_state();
        }
        if (command != "set") {
            throw ComponentError(-32601, "unknown command: " + command);
        }
        if (!params.is_object()) {
            throw ComponentError(-32602, "render settings params must be an object");
        }

        gs3d::app::RenderSettingsCommand update;
        if (params.contains("viewport_indices")) {
            const auto& indices = params.at("viewport_indices");
            if (!indices.is_array()) {
                throw ComponentError(-32602, "viewport_indices must be an array");
            }
            update.has_viewport_scope = true;
            for (const auto& index : indices) {
                if (!index.is_number_integer()) {
                    throw ComponentError(-32602, "viewport index must be an integer");
                }
                update.viewport_indices.push_back(index.get<int>());
            }
        }

        bool changed = false;
        if (params.contains("point_size")) {
            update.point_size_changed = true;
            update.point_size = require_number(params, "point_size");
            changed = true;
        }
        if (params.contains("height_attribute")) {
            update.height_by_changed = true;
            update.height_by_index = resolve_attribute_index(
                params.at("height_attribute"),
                gs3d::app::render_settings_for_view(
                    app_state_, app_state_.active_viewport_index
                ).height_by_options,
                "height_attribute"
            );
            changed = true;
        }
        if (params.contains("height_exaggeration")) {
            update.height_exag_changed = true;
            update.height_exag = require_number(params, "height_exaggeration");
            changed = true;
        }
        if (params.contains("color_attribute")) {
            update.color_by_changed = true;
            update.color_by_index = resolve_attribute_index(
                params.at("color_attribute"),
                gs3d::app::render_settings_for_view(
                    app_state_, app_state_.active_viewport_index
                ).color_by_options,
                "color_attribute"
            );
            changed = true;
        }
        if (params.contains("colormap_index")) {
            update.colormap_changed = true;
            update.colormap_index = require_integer(params, "colormap_index");
            changed = true;
        }
        if (params.contains("point_shape")) {
            update.point_shape_changed = true;
            update.point_shape = require_integer(params, "point_shape");
            changed = true;
        }

        const bool has_clip_field =
            params.contains("value_clip_enabled") ||
            params.contains("value_clip_min") ||
            params.contains("value_clip_max");
        if (has_clip_field) {
            const auto& settings = gs3d::app::render_settings_for_view(
                app_state_, app_state_.active_viewport_index
            );
            update.value_clip_changed = true;
            update.value_clip_enabled = params.value(
                "value_clip_enabled", settings.value_clip_enabled
            );
            update.value_clip_min = params.contains("value_clip_min")
                ? require_number(params, "value_clip_min")
                : settings.value_clip_min;
            update.value_clip_max = params.contains("value_clip_max")
                ? require_number(params, "value_clip_max")
                : settings.value_clip_max;
            changed = true;
        }

        if (!changed) {
            throw ComponentError(-32602, "no supported render setting was supplied");
        }
        app_state_.control_actions.render_settings_commands.push_back(
            std::move(update)
        );
        return {{"id", info_.id}, {"queued", true}};
    }

private:
    static float require_number(
        const nlohmann::json& params,
        const char* name
    ) {
        const auto& value = params.at(name);
        if (!value.is_number()) {
            throw ComponentError(-32602, std::string(name) + " must be numeric");
        }
        return value.get<float>();
    }

    static int require_integer(
        const nlohmann::json& params,
        const char* name
    ) {
        const auto& value = params.at(name);
        if (!value.is_number_integer()) {
            throw ComponentError(-32602, std::string(name) + " must be an integer");
        }
        return value.get<int>();
    }

    static int resolve_attribute_index(
        const nlohmann::json& value,
        const std::vector<std::string>& options,
        const char* name
    ) {
        if (value.is_number_integer()) {
            return value.get<int>();
        }
        if (!value.is_string()) {
            throw ComponentError(
                -32602,
                std::string(name) + " must be an attribute name or index"
            );
        }
        std::string attribute = value.get<std::string>();
        if (attribute == "elevation") {
            attribute = "z";
        }
        const auto found = std::find(options.begin(), options.end(), attribute);
        if (found == options.end()) {
            throw ComponentError(-32602, "unknown attribute: " + attribute);
        }
        return static_cast<int>(std::distance(options.begin(), found));
    }

    gs3d::app::AppState& app_state_;
    ComponentInfo info_{
        "runtime.render_settings",
        "原生渲染参数",
        "通过 ViewerApp 同帧队列修改真实 Vulkan 渲染参数",
        ComponentType::kToolbar,
        false
    };
    std::vector<CommandSpec> capabilities_{
        {"set", "设置点样式、属性映射和裁切范围", true},
        {"get_state", "查询当前原生渲染参数", false}
    };
};

class RuntimeCameraComponent final : public Component {
public:
    explicit RuntimeCameraComponent(gs3d::app::AppState& app_state)
        : app_state_(app_state) {}

    [[nodiscard]] const ComponentInfo& info() const noexcept override {
        return info_;
    }

    [[nodiscard]] const std::vector<CommandSpec>& capabilities() const noexcept override {
        return capabilities_;
    }

    [[nodiscard]] nlohmann::json get_state() override {
        const int active = gs3d::app::resolve_viewport_index(
            app_state_, app_state_.active_viewport_index
        );
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        if (active >= 0 && active < static_cast<int>(app_state_.render_views.size())) {
            const auto& view = app_state_.render_views[static_cast<std::size_t>(active)];
            width = std::max(view.image_width, 1u);
            height = std::max(view.image_height, 1u);
        }
        return {
            {"id", info_.id},
            {"active_viewport_index", active},
            {"width", width},
            {"height", height}
        };
    }

    [[nodiscard]] nlohmann::json execute(
        const std::string& command,
        const nlohmann::json& params
    ) override {
        if (command == "get_state") {
            return get_state();
        }
        if (command != "input") {
            throw ComponentError(-32601, "unknown command: " + command);
        }
        if (!params.is_object()) {
            throw ComponentError(-32602, "camera input params must be an object");
        }

        const int active = gs3d::app::resolve_viewport_index(
            app_state_, app_state_.active_viewport_index
        );
        gs3d::app::ViewportFrameCmd frame;
        frame.index = active;
        frame.active = true;
        frame.hovered = true;
        if (active >= 0 && active < static_cast<int>(app_state_.render_views.size())) {
            const auto& view = app_state_.render_views[static_cast<std::size_t>(active)];
            frame.width = std::max(view.image_width, 1u);
            frame.height = std::max(view.image_height, 1u);
        } else {
            frame.width = 1;
            frame.height = 1;
        }

        const bool has_rotate = params.contains("rotate_delta_x") ||
            params.contains("rotate_delta_y");
        const bool has_pan = params.contains("pan_delta_x") ||
            params.contains("pan_delta_y");
        if (has_rotate && has_pan) {
            throw ComponentError(-32602, "camera input cannot rotate and pan together");
        }
        if (has_rotate) {
            frame.rotate = true;
            frame.mouse_delta_x = optional_number(params, "rotate_delta_x");
            frame.mouse_delta_y = optional_number(params, "rotate_delta_y");
        }
        if (has_pan) {
            frame.pan = true;
            frame.mouse_delta_x = optional_number(params, "pan_delta_x");
            frame.mouse_delta_y = optional_number(params, "pan_delta_y");
        }
        if (params.contains("scroll_y")) {
            frame.mouse_wheel = optional_number(params, "scroll_y");
        }
        if (!frame.interacting()) {
            throw ComponentError(-32602, "camera input contains no movement");
        }
        app_state_.control_actions.viewport_frames.push_back(frame);
        return {{"id", info_.id}, {"queued", true}};
    }

private:
    static float optional_number(
        const nlohmann::json& params,
        const char* name
    ) {
        if (!params.contains(name)) {
            return 0.0f;
        }
        const auto& value = params.at(name);
        if (!value.is_number()) {
            throw ComponentError(-32602, std::string(name) + " must be numeric");
        }
        return value.get<float>();
    }

    gs3d::app::AppState& app_state_;
    ComponentInfo info_{
        "runtime.camera",
        "原生相机输入",
        "通过 ViewerApp 相机系统应用 Flutter 轨道、平移和缩放输入",
        ComponentType::kCanvas,
        false
    };
    std::vector<CommandSpec> capabilities_{
        {"input", "提交相机交互增量", true},
        {"get_state", "查询活动原生视口", false}
    };
};

class RuntimePickComponent final : public Component {
public:
    explicit RuntimePickComponent(gs3d::app::AppState& app_state)
        : app_state_(app_state) {}

    [[nodiscard]] const ComponentInfo& info() const noexcept override { return info_; }
    [[nodiscard]] const std::vector<CommandSpec>& capabilities() const noexcept override { return capabilities_; }

    [[nodiscard]] nlohmann::json get_state() override {
        const int active = gs3d::app::resolve_viewport_index(app_state_, app_state_.active_viewport_index);
        if (active < 0 || active >= static_cast<int>(app_state_.render_views.size())) {
            return {{"id", info_.id}, {"ready", false}};
        }
        const auto& view = app_state_.render_views[static_cast<std::size_t>(active)];
        const auto& stats = gs3d::app::region_stats_for_view(app_state_, active);
        return {{"id", info_.id}, {"ready", true}, {"active_viewport_index", active},
                {"canvas_left", view.canvas_rect_min_x}, {"canvas_top", view.canvas_rect_min_y},
                {"canvas_right", view.canvas_rect_max_x}, {"canvas_bottom", view.canvas_rect_max_y},
                {"width", view.image_width}, {"height", view.image_height},
                {"region_computing", stats.computing}, {"region_valid", stats.valid},
                {"region_point_count", stats.point_count}};
    }

    [[nodiscard]] nlohmann::json execute(const std::string& command, const nlohmann::json& params) override {
        if (command == "get_state") return get_state();
        if (command != "input" || !params.is_object()) {
            throw ComponentError(-32602, "pick input must be an object");
        }
        const int active = gs3d::app::resolve_viewport_index(app_state_, app_state_.active_viewport_index);
        if (active < 0 || active >= static_cast<int>(app_state_.render_views.size())) {
            throw ComponentError(-32602, "no active native viewport");
        }
        const auto& view = app_state_.render_views[static_cast<std::size_t>(active)];
        const auto input_kind = params.value("kind", std::string{});
        gs3d::app::ViewportFrameCmd frame;
        frame.index = active;
        frame.active = true;
        frame.width = std::max(view.image_width, 1u);
        frame.height = std::max(view.image_height, 1u);
        frame.mouse_on_image = true;

        const auto to_local_x = [&](const char* name) {
            return require_number(params, name) - view.canvas_rect_min_x;
        };
        const auto to_local_y = [&](const char* name) {
            return require_number(params, name) - view.canvas_rect_min_y;
        };
        if (input_kind == "hover") {
            frame.active = false;
            frame.hovered = true;
            frame.mouse_local_x = to_local_x("screen_x");
            frame.mouse_local_y = to_local_y("screen_y");
        } else if (input_kind == "measure") {
            frame.measure_pick_requested = true;
        } else if (input_kind == "focus") {
            frame.point_double_clicked = true;
            frame.mouse_local_x = to_local_x("screen_x");
            frame.mouse_local_y = to_local_y("screen_y");
        } else if (input_kind == "region") {
            frame.stats_select_completed = true;
            frame.stats_select_min_x = to_local_x("screen_min_x");
            frame.stats_select_min_y = to_local_y("screen_min_y");
            frame.stats_select_max_x = to_local_x("screen_max_x");
            frame.stats_select_max_y = to_local_y("screen_max_y");
        } else {
            throw ComponentError(-32602, "unknown pick input kind: " + input_kind);
        }
        app_state_.control_actions.viewport_frames.push_back(frame);
        return {{"id", info_.id}, {"queued", true}};
    }

private:
    static float require_number(const nlohmann::json& params, const char* name) {
        const auto& value = params.at(name);
        if (!value.is_number()) throw ComponentError(-32602, std::string(name) + " must be numeric");
        return value.get<float>();
    }

    gs3d::app::AppState& app_state_;
    ComponentInfo info_{"runtime.pick", "原生拾取输入", "将 Flutter 指针事件送入 GPU 拾取、测量和区域统计", ComponentType::kCanvas, false};
    std::vector<CommandSpec> capabilities_{{"input", "提交原生拾取事件", true}, {"get_state", "查询原生画布与统计状态", false}};
};

class RuntimeDiagnosticsComponent final : public Component {
public:
    explicit RuntimeDiagnosticsComponent(gs3d::app::AppState& app_state) : app_state_(app_state) {}
    [[nodiscard]] const ComponentInfo& info() const noexcept override { return info_; }
    [[nodiscard]] const std::vector<CommandSpec>& capabilities() const noexcept override { return capabilities_; }
    [[nodiscard]] nlohmann::json get_state() override {
        return {{"id", info_.id}, {"fps", app_state_.status_bar.fps},
                {"visible_points", app_state_.status_bar.visible_points},
                {"loaded_tiles", app_state_.status_bar.loaded_tiles},
                {"pending_tiles", app_state_.status_bar.pending_tiles},
                {"gpu_memory_bytes", app_state_.status_bar.gpu_memory_bytes},
                {"camera_position", app_state_.status_bar.camera_position},
                {"cache_hit_rate", app_state_.render_settings.cache_hit_rate}};
    }
    [[nodiscard]] nlohmann::json execute(const std::string& command, const nlohmann::json&) override {
        if (command == "get_state") return get_state();
        if (command == "clear_cache") {
            app_state_.control_actions.clear_cache_requested = true;
            return {{"id", info_.id}, {"queued", true}};
        }
        throw ComponentError(-32601, "unknown command: " + command);
    }
private:
    gs3d::app::AppState& app_state_;
    ComponentInfo info_{"runtime.diagnostics", "原生性能诊断", "查询 ViewerApp 实时指标并清空瓦片缓存", ComponentType::kStatus, false};
    std::vector<CommandSpec> capabilities_{{"get_state", "查询实时性能指标", false}, {"clear_cache", "清空原生瓦片缓存", false}};
};

// ── 状态栏组件（只读） ──────────────────────────────────────────

class StatusBarComponent final : public ReadOnlyComponentBase {
public:
    StatusBarComponent(const gs3d::app::AppState& s)
        : ReadOnlyComponentBase(s,
            {"status.bar", "状态栏", "底部状态栏显示 FPS、点数、GPU 内存等",
             ComponentType::kStatus, false},
            {{"get_state", "获取状态栏完整状态", false}}) {}

    nlohmann::json get_state() override {
        return {
            {"id", info().id},
            {"fps", app_state_.status_bar.fps},
            {"visible_points", app_state_.status_bar.visible_points},
            {"loaded_tiles", app_state_.status_bar.loaded_tiles},
            {"pending_tiles", app_state_.status_bar.pending_tiles},
            {"gpu_memory_bytes", app_state_.status_bar.gpu_memory_bytes},
            {"camera_position", app_state_.status_bar.camera_position},
            {"crs", app_state_.status_bar.crs},
            {"ready_state", app_state_.status_bar.ready_state}
        };
    }
};

// ── Overlay 组件 ──────────────────────────────────────────────────

class ShortcutOverlayComponent final : public ToolbarComponentBase {
public:
    ShortcutOverlayComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"overlay.shortcut", "快捷键总览", "显示/隐藏快捷键总览 overlay",
             ComponentType::kOverlay, false},
            {{"toggle", "切换快捷键总览显示/隐藏", false},
             {"set_visible", "设置快捷键总览可见性", true},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        app_state_.ui_chrome.shortcut_overlay_open =
            !app_state_.ui_chrome.shortcut_overlay_open;
    }

    nlohmann::json execute(const std::string& command, const nlohmann::json& params) override {
        if (command == "toggle") {
            app_state_.ui_chrome.shortcut_overlay_open =
                !app_state_.ui_chrome.shortcut_overlay_open;
            return {{"id", info().id}, {"visible", app_state_.ui_chrome.shortcut_overlay_open}};
        }
        if (command == "set_visible") {
            app_state_.ui_chrome.shortcut_overlay_open = params.value("visible", true);
            return {{"id", info().id}, {"visible", app_state_.ui_chrome.shortcut_overlay_open}};
        }
        return ToolbarComponentBase::execute(command, params);
    }

    nlohmann::json get_state() override {
        return {
            {"id", info().id},
            {"visible", app_state_.ui_chrome.shortcut_overlay_open}
        };
    }
};

class PanelPaletteOverlayComponent final : public ToolbarComponentBase {
public:
    PanelPaletteOverlayComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"overlay.panel_palette", "面板命令面板", "显示/隐藏面板命令面板 (Ctrl+P)",
             ComponentType::kOverlay, false},
            {{"toggle", "切换面板命令面板", false},
             {"set_visible", "设置面板命令面板可见性", true},
             {"get_state", "获取状态", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {
        app_state_.ui_chrome.panel_palette_open =
            !app_state_.ui_chrome.panel_palette_open;
        if (app_state_.ui_chrome.panel_palette_open)
            app_state_.ui_chrome.panel_palette_query[0] = '\0';
    }

    nlohmann::json execute(const std::string& command, const nlohmann::json& params) override {
        if (command == "toggle") {
            app_state_.ui_chrome.panel_palette_open =
                !app_state_.ui_chrome.panel_palette_open;
            if (app_state_.ui_chrome.panel_palette_open)
                app_state_.ui_chrome.panel_palette_query[0] = '\0';
            return {{"id", info().id}, {"visible", app_state_.ui_chrome.panel_palette_open}};
        }
        if (command == "set_visible") {
            app_state_.ui_chrome.panel_palette_open = params.value("visible", true);
            if (app_state_.ui_chrome.panel_palette_open)
                app_state_.ui_chrome.panel_palette_query[0] = '\0';
            return {{"id", info().id}, {"visible", app_state_.ui_chrome.panel_palette_open}};
        }
        return ToolbarComponentBase::execute(command, params);
    }

    nlohmann::json get_state() override {
        return {
            {"id", info().id},
            {"visible", app_state_.ui_chrome.panel_palette_open}
        };
    }
};

// ── Gizmo 组件 ───────────────────────────────────────────────────

class NavigationGizmoComponent final : public ToolbarComponentBase {
public:
    NavigationGizmoComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"gizmo.navigation", "导航球", "三维导航球 gizmo",
             ComponentType::kGizmo, false},
            {{"click", "沿轴锁定视角", true},
             {"get_state", "获取导航球状态", false}}) {}
protected:
    void do_click(const nlohmann::json& params) override {
        const int axis = params.value("axis", -1);
        if (axis >= 0 && axis <= 5) {
            app_state_.control_actions.camera_view_axis = axis;
        }
    }

    nlohmann::json get_state() override {
        const auto& view = app_state_.render_views.empty()
            ? gs3d::app::RenderViewState{}
            : app_state_.render_views[
                static_cast<std::size_t>(
                    gs3d::app::resolve_viewport_index(
                        app_state_, app_state_.active_viewport_index))];
        return {
            {"id", info().id},
            {"gizmo_valid", view.gizmo_axes_valid},
            {"x_dx", view.gizmo_x_axis.dx},
            {"x_dy", view.gizmo_x_axis.dy},
            {"y_dx", view.gizmo_y_axis.dx},
            {"y_dy", view.gizmo_y_axis.dy},
            {"z_dx", view.gizmo_z_axis.dx},
            {"z_dy", view.gizmo_z_axis.dy}
        };
    }
};

// ── Canvas 组件 ──────────────────────────────────────────────────

class ViewportCanvasComponent final : public ToolbarComponentBase {
public:
    ViewportCanvasComponent(gs3d::app::AppState& s)
        : ToolbarComponentBase(s,
            {"canvas.viewport", "视口画布", "三维视口渲染画布",
             ComponentType::kCanvas, false},
            {{"get_state", "获取画布状态", false},
             {"focus_point", "聚焦到选中点", false},
             {"reset_camera", "重置相机", false}}) {}
protected:
    void do_click(const nlohmann::json&) override {}

    nlohmann::json execute(const std::string& command, const nlohmann::json& params) override {
        if (command == "focus_point") {
            return {{"id", info().id}, {"focused", true}};
        }
        if (command == "reset_camera") {
            app_state_.control_actions.reset_camera_index = app_state_.active_viewport_index;
            return {{"id", info().id}, {"reset", true}};
        }
        return ToolbarComponentBase::execute(command, params);
    }

    nlohmann::json get_state() override {
        const int active = gs3d::app::resolve_viewport_index(
            app_state_, app_state_.active_viewport_index);
        std::uint32_t width = 0, height = 0;
        if (active >= 0 && active < static_cast<int>(app_state_.render_views.size())) {
            width = app_state_.render_views[static_cast<std::size_t>(active)].image_width;
            height = app_state_.render_views[static_cast<std::size_t>(active)].image_height;
        }
        return {
            {"id", info().id},
            {"active_viewport", active},
            {"viewport_count", app_state_.render_views.size()},
            {"width", width},
            {"height", height}
        };
    }
};

} // namespace

std::vector<std::unique_ptr<Component>> make_toolbar_components(
    gs3d::app::AppState& app_state,
    gs3d::app::UiActions& /*actions*/
) {
    std::vector<std::unique_ptr<Component>> result;
    result.push_back(std::make_unique<ToolbarOpenComponent>(app_state));
    result.push_back(std::make_unique<ToolbarScreenshotComponent>(app_state));
    result.push_back(std::make_unique<ToolbarAddViewComponent>(app_state));
    result.push_back(std::make_unique<ToolbarMeasureComponent>(app_state));
    result.push_back(std::make_unique<ToolbarLinkCameraComponent>(app_state));
    result.push_back(std::make_unique<ToolbarPanelPaletteComponent>(app_state));
    result.push_back(std::make_unique<RuntimeRenderSettingsComponent>(app_state));
    result.push_back(std::make_unique<RuntimeCameraComponent>(app_state));
    result.push_back(std::make_unique<RuntimePickComponent>(app_state));
    result.push_back(std::make_unique<RuntimeDiagnosticsComponent>(app_state));
    return result;
}

std::vector<std::unique_ptr<Component>> make_status_components(
    gs3d::app::AppState& app_state
) {
    std::vector<std::unique_ptr<Component>> result;
    result.push_back(std::make_unique<StatusBarComponent>(app_state));
    return result;
}

std::vector<std::unique_ptr<Component>> make_overlay_components(
    gs3d::app::AppState& app_state
) {
    std::vector<std::unique_ptr<Component>> result;
    result.push_back(std::make_unique<ShortcutOverlayComponent>(app_state));
    result.push_back(std::make_unique<PanelPaletteOverlayComponent>(app_state));
    return result;
}

std::vector<std::unique_ptr<Component>> make_gizmo_components(
    gs3d::app::AppState& app_state,
    gs3d::app::UiActions& /*actions*/
) {
    std::vector<std::unique_ptr<Component>> result;
    result.push_back(std::make_unique<NavigationGizmoComponent>(app_state));
    return result;
}

std::vector<std::unique_ptr<Component>> make_canvas_components(
    gs3d::app::AppState& app_state,
    gs3d::app::UiActions& /*actions*/
) {
    std::vector<std::unique_ptr<Component>> result;
    result.push_back(std::make_unique<ViewportCanvasComponent>(app_state));
    return result;
}

} // namespace gs3d::control
