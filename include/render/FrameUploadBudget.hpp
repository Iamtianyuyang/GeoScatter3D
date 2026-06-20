#pragma once

#include <cstdint>
#include <limits>

namespace gs3d::render {

class FrameUploadBudget {
public:
    explicit FrameUploadBudget(
        std::uint64_t max_bytes =
            std::numeric_limits<std::uint64_t>::max()
    ) noexcept
        : max_bytes_(max_bytes)
    {
    }

    [[nodiscard]]
    bool try_reserve(std::uint64_t bytes) noexcept
    {
        if (reserved_bytes_ == 0) {
            reserved_bytes_ = bytes;
            return true;
        }

        if (max_bytes_ != std::numeric_limits<std::uint64_t>::max() &&
            (reserved_bytes_ > max_bytes_ ||
             bytes > max_bytes_ - reserved_bytes_)) {
            return false;
        }

        reserved_bytes_ += bytes;
        return true;
    }

    [[nodiscard]]
    std::uint64_t reserved_bytes() const noexcept
    {
        return reserved_bytes_;
    }

private:
    std::uint64_t max_bytes_ = 0;
    std::uint64_t reserved_bytes_ = 0;
};

} // namespace gs3d::render
