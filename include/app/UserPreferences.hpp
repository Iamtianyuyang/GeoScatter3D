#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gs3d::app {

[[nodiscard]]
std::filesystem::path user_preferences_path();

[[nodiscard]]
std::optional<std::string> load_preferred_gpu_preference();

bool save_preferred_gpu_preference(std::string_view preferred_gpu);

struct UiPreferences {
    std::string theme;
    std::string layout;
};

[[nodiscard]]
std::optional<UiPreferences> load_ui_preferences();

bool save_ui_preferences(const UiPreferences& preferences);

} // namespace gs3d::app
