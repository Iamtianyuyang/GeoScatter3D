#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gs3d::app {

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
bool save_preferred_gpu_preference(std::string_view preferred_gpu);

} // namespace gs3d::app
