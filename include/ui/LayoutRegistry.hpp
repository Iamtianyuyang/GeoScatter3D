#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gs3d::ui {

/*
 * 布局描述符（Layout Descriptor）。
 * 描述一套界面的空间排布与视口组织模式，与颜色系统完全正交。
 */
struct LayoutDescriptor {
    std::string id;          // 配置文件与代码中的唯一标识，如 "workbench"
    std::string name;        // 菜单与 UI 显示名，如 "标准工作台"
    std::string description; // 描述，如 "多面板停靠工作区 + 状态栏 + 顶栏"
    bool enabled = true;
};

/*
 * 布局注册表（Layout Registry）。
 * 统一管理所有已注册的 UI 布局模式。未来新增布局只需调用 register_layout。
 * 与颜色主题（Theme）完全解耦：任何布局都可以运行在任何颜色主题下。
 */
class LayoutRegistry {
public:
    static LayoutRegistry& instance() noexcept;

    // 注册新布局（若已存在相同 id 则覆盖更新）
    bool register_layout(LayoutDescriptor desc);

    // 查询已注册布局
    [[nodiscard]] std::size_t layout_count() const noexcept;
    [[nodiscard]] const LayoutDescriptor* layout_at(std::size_t index) const noexcept;
    [[nodiscard]] const LayoutDescriptor* find_layout(std::string_view id) const noexcept;

    // 当前活动布局
    [[nodiscard]] std::string_view active_layout_id() const noexcept;
    [[nodiscard]] std::string_view active_layout_name() const noexcept;
    void set_active_layout_id(std::string_view id);

private:
    LayoutRegistry();
    void init_builtin_layouts();

    std::vector<LayoutDescriptor> layouts_;
    std::string active_layout_id_ = "workbench";
};

// 全局便捷接口
[[nodiscard]] inline std::size_t layout_count() noexcept {
    return LayoutRegistry::instance().layout_count();
}

[[nodiscard]] inline const LayoutDescriptor* layout_at(std::size_t index) noexcept {
    return LayoutRegistry::instance().layout_at(index);
}

[[nodiscard]] inline const LayoutDescriptor* find_layout(std::string_view id) noexcept {
    return LayoutRegistry::instance().find_layout(id);
}

inline bool register_layout(LayoutDescriptor desc) {
    return LayoutRegistry::instance().register_layout(std::move(desc));
}

} // namespace gs3d::ui
