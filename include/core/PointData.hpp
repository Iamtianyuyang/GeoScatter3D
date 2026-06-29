#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace gs3d::core {

struct PointRecord {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float value = 0.0f;
    std::uint32_t point_id = 0;
};

enum class PointDataLayout {
    Interleaved,
    SeparateArrays
};

struct PointDataView {
    PointDataLayout layout = PointDataLayout::Interleaved;

    const void* interleaved_points = nullptr;
    std::size_t interleaved_stride = 0;
    std::size_t x_offset = 0;
    std::size_t y_offset = 0;
    std::size_t z_offset = 0;
    std::size_t value_offset = 0;
    std::size_t point_id_offset =
        std::numeric_limits<std::size_t>::max();

    const float* x = nullptr;
    const float* y = nullptr;
    const float* z = nullptr;
    const float* value = nullptr;
    const std::uint32_t* point_id = nullptr;

    std::uint64_t point_count = 0;

    [[nodiscard]]
    bool empty() const noexcept {
        return point_count == 0;
    }

    [[nodiscard]]
    bool valid() const noexcept {
        if (point_count == 0) {
            return true;
        }

        if (layout == PointDataLayout::Interleaved) {
            return interleaved_points != nullptr &&
                   interleaved_stride >= sizeof(float) * 4;
        }

        return x != nullptr &&
               y != nullptr &&
               z != nullptr &&
               value != nullptr;
    }

    [[nodiscard]]
    bool has_point_ids() const noexcept {
        if (point_count == 0) {
            return true;
        }

        if (layout == PointDataLayout::Interleaved) {
            return point_id != nullptr ||
                   point_id_offset !=
                       std::numeric_limits<std::size_t>::max();
        }

        return point_id != nullptr;
    }

    [[nodiscard]]
    std::uint32_t point_id_at(std::uint64_t index) const noexcept {
        if (index >= point_count || !has_point_ids()) {
            return 0;
        }

        if (layout == PointDataLayout::SeparateArrays) {
            return point_id[index];
        }

        if (point_id != nullptr) {
            return point_id[index];
        }

        std::uint32_t value = 0;
        const auto* base =
            static_cast<const std::byte*>(interleaved_points) +
            static_cast<std::size_t>(index) * interleaved_stride;
        std::memcpy(
            &value,
            base + point_id_offset,
            sizeof(std::uint32_t)
        );
        return value;
    }

    [[nodiscard]]
    PointRecord point_at(std::uint64_t index) const noexcept {
        PointRecord record;
        if (index >= point_count) {
            return record;
        }

        if (layout == PointDataLayout::SeparateArrays) {
            record.x = x[index];
            record.y = y[index];
            record.z = z[index];
            record.value = value[index];
            record.point_id = point_id_at(index);
            return record;
        }

        const auto* base =
            static_cast<const std::byte*>(interleaved_points) +
            static_cast<std::size_t>(index) * interleaved_stride;

        std::memcpy(&record.x, base + x_offset, sizeof(float));
        std::memcpy(&record.y, base + y_offset, sizeof(float));
        std::memcpy(&record.z, base + z_offset, sizeof(float));
        std::memcpy(&record.value, base + value_offset, sizeof(float));
        record.point_id = point_id_at(index);
        return record;
    }
};

[[nodiscard]]
inline PointDataView make_interleaved_point_data_view(
    const void* points,
    std::uint64_t point_count,
    std::size_t stride,
    std::size_t x_offset,
    std::size_t y_offset,
    std::size_t z_offset,
    std::size_t value_offset,
    std::size_t point_id_offset =
        std::numeric_limits<std::size_t>::max()
) noexcept {
    PointDataView view;
    view.layout = PointDataLayout::Interleaved;
    view.interleaved_points = points;
    view.interleaved_stride = stride;
    view.x_offset = x_offset;
    view.y_offset = y_offset;
    view.z_offset = z_offset;
    view.value_offset = value_offset;
    view.point_id_offset = point_id_offset;
    view.point_count = point_count;
    return view;
}

[[nodiscard]]
inline PointDataView make_separate_point_data_view(
    const float* x,
    const float* y,
    const float* z,
    const float* value,
    std::uint64_t point_count,
    const std::uint32_t* point_id = nullptr
) noexcept {
    PointDataView view;
    view.layout = PointDataLayout::SeparateArrays;
    view.x = x;
    view.y = y;
    view.z = z;
    view.value = value;
    view.point_id = point_id;
    view.point_count = point_count;
    return view;
}

class PointBuffer {
public:
    [[nodiscard]]
    std::uint64_t size() const noexcept {
        return static_cast<std::uint64_t>(x_.size());
    }

    [[nodiscard]]
    bool empty() const noexcept {
        return x_.empty();
    }

    void reserve(std::size_t count) {
        x_.reserve(count);
        y_.reserve(count);
        z_.reserve(count);
        value_.reserve(count);
        point_id_.reserve(count);
    }

    void clear() noexcept {
        x_.clear();
        y_.clear();
        z_.clear();
        value_.clear();
        point_id_.clear();
    }

    void push_back(const PointRecord& point) {
        x_.push_back(point.x);
        y_.push_back(point.y);
        z_.push_back(point.z);
        value_.push_back(point.value);
        point_id_.push_back(point.point_id);
    }

    [[nodiscard]]
    PointDataView view() const noexcept {
        return make_separate_point_data_view(
            x_.data(),
            y_.data(),
            z_.data(),
            value_.data(),
            size(),
            point_id_.data()
        );
    }

    [[nodiscard]]
    PointRecord point_at(std::uint64_t index) const noexcept {
        PointRecord record;
        if (index >= size()) {
            return record;
        }

        const auto i = static_cast<std::size_t>(index);
        record.x = x_[i];
        record.y = y_[i];
        record.z = z_[i];
        record.value = value_[i];
        record.point_id = point_id_[i];
        return record;
    }

private:
    std::vector<float> x_{};
    std::vector<float> y_{};
    std::vector<float> z_{};
    std::vector<float> value_{};
    std::vector<std::uint32_t> point_id_{};
};

} // namespace gs3d::core
