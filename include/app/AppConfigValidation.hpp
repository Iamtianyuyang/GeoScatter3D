#pragma once

#include "app/AppConfig.hpp"

namespace gs3d::app {

// Validates cross-field runtime invariants that TOML's primitive type checks
// cannot express. Call after parsing, before constructing render resources.
void validate_app_config(const AppConfig& config);

} // namespace gs3d::app
