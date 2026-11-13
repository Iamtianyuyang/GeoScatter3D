#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gs3d::pipeline {

inline constexpr const char* kStatsStageName = "stats";

struct BundleBuildOptions {
    std::filesystem::path source;
    std::filesystem::path bundle_dir;
    std::uint32_t points_per_chunk = 65536;
    std::string value_field;
};

struct BundleBuildResult {
    std::uint64_t points = 0;
    std::uint64_t chunks = 0;
    std::vector<std::string> stages_run;
    std::vector<std::string> stages_skipped;
};

// Builds (or resumes) a v3 bundle from a CSV/DAT source. Stages are recorded in
// manifest.json only after they finish; a run continues only when the source
// fingerprint and the options hash match, so two runs are never mixed.
BundleBuildResult build_bundle(const BundleBuildOptions& options);

} // namespace gs3d::pipeline
