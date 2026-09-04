#pragma once

#include "ui/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs3d::ui {

/*
 * 主题/颜色注册表（Theme & Color Registry）。
 * 统一管理所有已注册的颜色方案。未来可通过 register_theme 自由新增任意主题。
 * 与布局（Layout）正交解耦：更换颜色不影响布局结构，新增颜色无需改动布局系统。
 */
class ThemeRegistry {
public:
    static ThemeRegistry& instance() noexcept;

    // 注册新主题（若已存在相同 id 则更新覆盖）
    bool register_theme(ThemeTokens tokens);

    // 查询已注册主题
    [[nodiscard]] std::size_t theme_count() const noexcept;
    [[nodiscard]] const ThemeTokens* theme_at(std::size_t index) const noexcept;
    [[nodiscard]] const ThemeTokens* find_theme(std::string_view id) const noexcept;
    [[nodiscard]] const ThemeTokens* find_theme(ThemeId id) const noexcept;

    // 当前活动主题
    [[nodiscard]] ThemeId active_theme_id() const noexcept;
    [[nodiscard]] std::string_view active_theme_name() const noexcept;
    [[nodiscard]] const ThemeTokens& active_theme_tokens() const noexcept;

    void set_active_theme(ThemeId id);
    void set_active_theme(std::string_view id);

private:
    ThemeRegistry();
    void init_builtin_themes();

    std::vector<ThemeTokens> themes_;
    std::size_t active_theme_index_ = 0;
};

} // namespace gs3d::ui
