#include "ui/color/ThemeRegistry.hpp"

#include <algorithm>

namespace gs3d::ui {

ThemeRegistry& ThemeRegistry::instance() noexcept {
    static ThemeRegistry registry;
    return registry;
}

ThemeRegistry::ThemeRegistry() {
    init_builtin_themes();
}

void ThemeRegistry::init_builtin_themes() {
    // 注册 5 套标准内置主题
    for (int i = 0; i < kThemeCount; ++i) {
        register_theme(theme_tokens(static_cast<ThemeId>(i)));
    }
}

bool ThemeRegistry::register_theme(ThemeTokens tokens) {
    if (tokens.id == nullptr || tokens.id[0] == '\0') {
        return false;
    }
    auto it = std::find_if(themes_.begin(), themes_.end(), [&](const ThemeTokens& t) {
        return std::string_view(t.id) == std::string_view(tokens.id);
    });
    if (it != themes_.end()) {
        *it = tokens;
    } else {
        themes_.push_back(tokens);
    }
    return true;
}

std::size_t ThemeRegistry::theme_count() const noexcept {
    return themes_.size();
}

const ThemeTokens* ThemeRegistry::theme_at(std::size_t index) const noexcept {
    if (index >= themes_.size()) {
        return nullptr;
    }
    return &themes_[index];
}

const ThemeTokens* ThemeRegistry::find_theme(std::string_view id) const noexcept {
    for (const auto& t : themes_) {
        if ((t.id != nullptr && std::string_view(t.id) == id) ||
            (t.alias_id != nullptr && std::string_view(t.alias_id) == id)) {
            return &t;
        }
    }
    return nullptr;
}

const ThemeTokens* ThemeRegistry::find_theme(ThemeId id) const noexcept {
    const int idx = static_cast<int>(id);
    if (idx >= 0 && static_cast<std::size_t>(idx) < kThemeCount && static_cast<std::size_t>(idx) < themes_.size()) {
        return &themes_[static_cast<std::size_t>(idx)];
    }
    if (id == ThemeId::kCustom && active_theme_index_ < themes_.size()) {
        return &themes_[active_theme_index_];
    }
    return nullptr;
}

ThemeId ThemeRegistry::active_theme_id() const noexcept {
    if (active_theme_index_ < static_cast<std::size_t>(kThemeCount)) {
        return static_cast<ThemeId>(active_theme_index_);
    }
    return ThemeId::kCustom;
}

std::string_view ThemeRegistry::active_theme_name() const noexcept {
    if (active_theme_index_ < themes_.size() && themes_[active_theme_index_].name != nullptr) {
        return themes_[active_theme_index_].name;
    }
    return "未知主题";
}

const ThemeTokens& ThemeRegistry::active_theme_tokens() const noexcept {
    if (active_theme_index_ < themes_.size()) {
        return themes_[active_theme_index_];
    }
    return themes_.front();
}

void ThemeRegistry::set_active_theme(ThemeId id) {
    const int idx = static_cast<int>(id);
    if (idx >= 0 && static_cast<std::size_t>(idx) < themes_.size() && idx < kThemeCount) {
        active_theme_index_ = static_cast<std::size_t>(idx);
    }
}

void ThemeRegistry::set_active_theme(std::string_view id) {
    for (std::size_t i = 0; i < themes_.size(); ++i) {
        if ((themes_[i].id != nullptr && std::string_view(themes_[i].id) == id) ||
            (themes_[i].alias_id != nullptr && std::string_view(themes_[i].alias_id) == id)) {
            active_theme_index_ = i;
            return;
        }
    }
}

bool register_theme(const ThemeTokens& tokens) {
    return ThemeRegistry::instance().register_theme(tokens);
}

} // namespace gs3d::ui
