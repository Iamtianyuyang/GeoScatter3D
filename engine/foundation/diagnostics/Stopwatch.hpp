#pragma once

#include <chrono>
#include <string>

namespace gs3d::util {

class Stopwatch {
public:
    using Clock = std::chrono::steady_clock;

    Stopwatch()
        : start_(Clock::now())
    {
    }

    void reset() noexcept {
        start_ = Clock::now();
    }

    [[nodiscard]]
    double elapsed_seconds() const noexcept {
        return std::chrono::duration<double>(
            Clock::now() - start_
        ).count();
    }

    [[nodiscard]]
    double elapsed_milliseconds() const noexcept {
        return elapsed_seconds() * 1000.0;
    }

private:
    Clock::time_point start_;
};

} // namespace gs3d::util
