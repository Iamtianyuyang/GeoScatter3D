#pragma once

#include <cstdint>

namespace gs3d::platform {

// Returns the number of logical CPU threads visible to the process.
[[nodiscard]]
std::uint32_t logical_cpu_thread_count() noexcept;

// Returns the number of physical CPU cores when the operating system exposes
// topology information. Falls back to logical_cpu_thread_count().
[[nodiscard]]
std::uint32_t physical_cpu_core_count() noexcept;

} // namespace gs3d::platform
