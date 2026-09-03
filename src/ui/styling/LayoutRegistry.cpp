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
    // 方案 A：标准工作台布局（默认激活）
    register_layout({
        .id = "workbench",
        .name = "标准工作台",
        .description = "多面板停靠工作区 + 状态栏 + 顶栏",
        .enabled = true
    });

    // 方案 B：悬浮胶囊 Dock 布局
    register_layout({
        .id = "floating-dock",
        .name = "悬浮胶囊 Dock",
        .description = "全沉浸视口 + 底部悬浮胶囊工具栏",
        .enabled = true
    });

    // 方案 C：暗色分析舱布局
    register_layout({
        .id = "analysis-rail",
        .name = "暗色分析舱",
        .description = "左侧工具图标轨 + 互斥抽屉 + 侧边分析栏",
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
