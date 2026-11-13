#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gs3d::pipeline {

inline constexpr const char* kStatsStageName = "stats";
inline constexpr const char* kLodStageName = "lod";

struct BundleBuildOptions {
    std::filesystem::path source;
    std::filesystem::path bundle_dir;
    std::uint32_t points_per_chunk = 65536;
    std::string value_field;
    /// 建包末尾是否写 lod.gs3dlod。关掉省一次读回与建链（50M 约 50 s），
    /// 代价是引擎每次打开都要运行时建链（约 22 s）。
    bool write_lod = true;
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
// Called at stage boundaries; throwing stops the build before the next stage.
BundleBuildResult build_bundle(const BundleBuildOptions& options,
                               const std::function<void(const char*)>& on_stage = {});

// Restores the source and options saved in the bundle checkpoint.
BundleBuildResult resume_bundle(const std::filesystem::path& bundle_dir,
                                const std::function<void(const char*)>& on_stage = {});

} // namespace gs3d::pipeline
