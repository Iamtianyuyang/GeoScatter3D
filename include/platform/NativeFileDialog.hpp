#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace gs3d::platform {

struct NativeFileDialogResult {
    std::optional<std::filesystem::path> path;
    std::string error;
};

[[nodiscard]]
NativeFileDialogResult choose_project_directory();

[[nodiscard]]
NativeFileDialogResult choose_raw_data_file();

} // namespace gs3d::platform
