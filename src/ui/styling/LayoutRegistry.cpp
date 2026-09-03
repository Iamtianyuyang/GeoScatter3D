#include "ui/LayoutRegistry.hpp"

#include <algorithm>

namespace gs3d::ui {

LayoutRegistry& LayoutRegistry::instance() noexcept {
    static LayoutRegistry registry;
    return registry;
}

LayoutRegistry::LayoutRegistry() {
    init_builtin_layouts();
}

void LayoutRegistry::init_builtin_layouts() {
    // 默认内置标准工作台布局
    register_layout({
        .id = "workbench",
        .name = "标准工作台",
        .description = "多面板停靠工作区 + 状态栏 + 顶栏",
        .enabled = true
    });

    // 预注册未来扩展插槽布局示例（可根据需要启用）
    register_layout({
        .id = "fullscreen_viewport",
        .name = "沉浸视口",
        .description = "全屏视口沉浸工作模式",
        .enabled = true
    });
}

bool LayoutRegistry::register_layout(LayoutDescriptor desc) {
    if (desc.id.empty()) {
        return false;
    }
    auto it = std::find_if(layouts_.begin(), layouts_.end(), [&](const LayoutDescriptor& l) {
        return l.id == desc.id;
    });
    if (it != layouts_.end()) {
        *it = std::move(desc);
    } else {
        layouts_.push_back(std::move(desc));
    }
    return true;
}

std::size_t LayoutRegistry::layout_count() const noexcept {
    return layouts_.size();
}

const LayoutDescriptor* LayoutRegistry::layout_at(std::size_t index) const noexcept {
    if (index >= layouts_.size()) {
        return nullptr;
    }
    return &layouts_[index];
}

const LayoutDescriptor* LayoutRegistry::find_layout(std::string_view id) const noexcept {
    for (const auto& l : layouts_) {
        if (l.id == id) {
            return &l;
        }
    }
    return nullptr;
}

std::string_view LayoutRegistry::active_layout_id() const noexcept {
    return active_layout_id_;
}

std::string_view LayoutRegistry::active_layout_name() const noexcept {
    const auto* l = find_layout(active_layout_id_);
    if (l != nullptr) {
        return l->name;
    }
    return "标准工作台";
}

void LayoutRegistry::set_active_layout_id(std::string_view id) {
    const auto* l = find_layout(id);
    if (l != nullptr) {
        active_layout_id_ = std::string(id);
    }
}

} // namespace gs3d::ui
