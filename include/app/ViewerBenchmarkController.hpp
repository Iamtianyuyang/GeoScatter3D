#pragma once

#include "app/BenchmarkSession.hpp"
#include "app/ViewerAppInternal.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace gs3d::app {

// Owns the full benchmark lifecycle instead of scattering its script cursor,
// timings and observed results across ViewerApp::run().
class ViewerBenchmarkController {
public:
    ViewerBenchmarkController(
        bool enabled,
        std::uint32_t frame_count,
        std::uint32_t frames_in_flight,
        const std::filesystem::path& pick_script_path
    );

    [[nodiscard]] BenchmarkSession& session() noexcept;
    [[nodiscard]] const BenchmarkSession& session() const noexcept;
    [[nodiscard]] bool pick_enabled() const noexcept;
    [[nodiscard]] const std::vector<BenchmarkPickScriptQuery>& queries() const noexcept;
    [[nodiscard]] std::size_t& issue_index() noexcept;
    [[nodiscard]] std::vector<double>& issue_cpu_ms() noexcept;
    [[nodiscard]] std::vector<BenchmarkPickIssuedMetadata>& issue_metadata() noexcept;
    [[nodiscard]] std::vector<BenchmarkPickObservedResult>& results() noexcept;
    [[nodiscard]] const std::vector<BenchmarkPickObservedResult>& results() const noexcept;

private:
    BenchmarkSession session_;
    bool pick_enabled_ = false;
    std::vector<BenchmarkPickScriptQuery> queries_;
    std::vector<double> issue_cpu_ms_;
    std::vector<BenchmarkPickIssuedMetadata> issue_metadata_;
    std::vector<BenchmarkPickObservedResult> results_;
    std::size_t issue_index_ = 0;
};

} // namespace gs3d::app
