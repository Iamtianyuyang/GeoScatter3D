#pragma once

#include "pipeline/stages/BundleBuild.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace gs3d::pipeline {

enum class BundleJobState { idle, running, done, failed, cancelled };

struct BundleJobStatus {
    std::uint64_t id = 0;
    BundleJobState state = BundleJobState::idle;
    std::string stage;
    std::string error;
    std::uint64_t points = 0;
    std::uint64_t chunks = 0;
};

// One job per runner. Destruction requests cancellation and joins the worker;
// callers must not destroy a runner from a latency-sensitive thread.
class BundleJob {
public:
    BundleJob() = default;
    ~BundleJob();
    BundleJob(const BundleJob&) = delete;
    BundleJob& operator=(const BundleJob&) = delete;

    std::uint64_t start(BundleBuildOptions options);
    std::uint64_t resume(std::filesystem::path bundle_dir);
    bool cancel(std::uint64_t id);
    BundleJobStatus poll() const;

private:
    using StageCallback = std::function<void(const char*)>;
    using Work = std::function<BundleBuildResult(const StageCallback&)>;
    std::uint64_t launch(Work work);

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> cancel_requested_{false};
    BundleJobStatus status_;
    std::uint64_t next_id_ = 1;
};

} // namespace gs3d::pipeline
