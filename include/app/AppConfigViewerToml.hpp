#pragma once

#include "app/AppConfig.hpp"

#include <toml++/toml.hpp>

namespace gs3d::app::detail {

// Applies the viewer/runtime TOML sections to a parsed application config.
// Input and CSV conversion remain the loader's responsibility.
void apply_viewer_toml_sections(const toml::table& root, AppConfig& config);

} // namespace gs3d::app::detail
