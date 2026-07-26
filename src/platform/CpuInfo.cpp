#include "platform/CpuInfo.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace gs3d::platform {

namespace {

#if defined(__linux__)

bool is_cpu_directory_name(const std::string& name)
{
    return name.size() > 3 &&
        name.starts_with("cpu") &&
        std::all_of(
            name.begin() + 3,
            name.end(),
            [](unsigned char value) {
                return std::isdigit(value) != 0;
            }
        );
}

bool read_integer(
    const std::filesystem::path& path,
    int& value
)
{
    std::ifstream stream(path);
    return static_cast<bool>(stream >> value);
}

std::uint32_t linux_physical_cpu_core_count() noexcept
{
    try {
        const std::filesystem::path cpu_root{
            "/sys/devices/system/cpu"
        };
        std::set<std::pair<int, int>> cores;
        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(cpu_root, ec)) {
            if (ec || !entry.is_directory(ec)) {
                continue;
            }

            const auto name = entry.path().filename().string();
            if (!is_cpu_directory_name(name)) {
                continue;
            }

            int online = 1;
            const auto online_path = entry.path() / "online";
            if (std::filesystem::exists(online_path, ec) &&
                read_integer(online_path, online) &&
                online == 0) {
                continue;
            }

            int package_id = 0;
            int core_id = 0;
            if (read_integer(
                    entry.path() / "topology/physical_package_id",
                    package_id
                ) &&
                read_integer(
                    entry.path() / "topology/core_id",
                    core_id
                )) {
                cores.emplace(package_id, core_id);
            }
        }
        return static_cast<std::uint32_t>(cores.size());
    } catch (...) {
        return 0;
    }
}

#elif defined(__APPLE__)

std::uint32_t apple_physical_cpu_core_count() noexcept
{
    std::uint32_t count = 0;
    std::size_t size = sizeof(count);
    if (sysctlbyname(
            "hw.physicalcpu",
            &count,
            &size,
            nullptr,
            0
        ) != 0) {
        return 0;
    }
    return count;
}

#elif defined(_WIN32)

std::uint32_t windows_physical_cpu_core_count() noexcept
{
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(
        RelationProcessorCore,
        nullptr,
        &bytes
    );
    if (bytes == 0) {
        return 0;
    }

    std::vector<unsigned char> storage(bytes);
    auto* first = reinterpret_cast<
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
    >(storage.data());
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            first,
            &bytes
        )) {
        return 0;
    }

    std::uint32_t count = 0;
    DWORD offset = 0;
    while (offset < bytes) {
        const auto* entry = reinterpret_cast<
            const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*
        >(storage.data() + offset);
        if (entry->Relationship == RelationProcessorCore) {
            ++count;
        }
        if (entry->Size == 0) {
            break;
        }
        offset += entry->Size;
    }
    return count;
}

#endif

} // namespace

std::uint32_t logical_cpu_thread_count() noexcept
{
    return std::max(1u, std::thread::hardware_concurrency());
}

std::uint32_t physical_cpu_core_count() noexcept
{
    const auto logical = logical_cpu_thread_count();
    std::uint32_t physical = 0;

#if defined(__linux__)
    physical = linux_physical_cpu_core_count();
#elif defined(__APPLE__)
    physical = apple_physical_cpu_core_count();
#elif defined(_WIN32)
    physical = windows_physical_cpu_core_count();
#endif

    if (physical == 0) {
        return logical;
    }
    return std::clamp(physical, 1u, logical);
}

} // namespace gs3d::platform
