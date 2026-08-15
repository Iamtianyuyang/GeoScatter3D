#include "app/ResourcePath.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <array>
#include <sstream>
#include <stdexcept>

namespace gs3d::app {

std::filesystem::path ResourcePath::resolve_existing_file(
    const std::filesystem::path& path,
    const ResourcePathContext& context
) {
    const auto resolved =
        resolve_optional_file(path, context);

    if (!resolved.empty()) {
        return resolved;
    }

    throw std::runtime_error(
        make_not_found_message(
            path,
            search_roots(context)
        )
    );
}

std::filesystem::path ResourcePath::resolve_existing_file(
    const std::filesystem::path& path,
    const std::filesystem::path& config_path,
    const std::filesystem::path& executable_path
) {
    ResourcePathContext context;
    context.config_path = config_path;
    context.executable_path = executable_path;

    return resolve_existing_file(path, context);
}

std::filesystem::path ResourcePath::resolve_optional_file(
    const std::filesystem::path& path,
    const ResourcePathContext& context
) {
    if (path.empty()) {
        return {};
    }

    if (path.is_absolute()) {
        const auto normalized = normalize(path);

        if (is_existing_regular_file(normalized)) {
            return normalized;
        }

        return {};
    }

    for (const auto& root : search_roots(context)) {
        if (root.empty()) {
            continue;
        }

        const auto candidate =
            normalize(root / path);

        if (is_existing_regular_file(candidate)) {
            return candidate;
        }
    }

    return {};
}

std::filesystem::path ResourcePath::resolve_writable_file(
    const std::filesystem::path& path,
    const ResourcePathContext& context
) {
    if (path.empty()) {
        return {};
    }

    if (path.is_absolute()) {
        return normalize(path);
    }

    const auto roots = search_roots(context);

    // 已有持久化文件时优先命中既有位置（防止 CWD 漂移导致读写分离）。
    for (const auto& root : roots) {
        if (root.empty()) {
            continue;
        }
        const auto candidate = normalize(root / path);
        if (is_existing_regular_file(candidate)) {
            return candidate;
        }
    }

    // 尚无文件：落到第一个搜索根下创建；无可用根时退回 CWD（旧行为）。
    for (const auto& root : roots) {
        if (!root.empty()) {
            return normalize(root / path);
        }
    }

    return normalize(path);
}

std::filesystem::path ResourcePath::current_working_directory() {
    return normalize(std::filesystem::current_path());
}

std::filesystem::path ResourcePath::current_executable_path() {
#if defined(_WIN32)
    std::array<wchar_t, 32768> buffer{};
    const auto length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );
    if (length == 0 || length == buffer.size()) {
        return {};
    }
    return normalize(
        std::filesystem::path(std::wstring(buffer.data(), length))
    );
#elif defined(__linux__)
    std::error_code ec;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : normalize(path);
#else
    return {};
#endif
}

std::filesystem::path ResourcePath::config_directory(
    const std::filesystem::path& config_path
) {
    if (config_path.empty()) {
        return {};
    }

    const auto absolute_config_path =
        normalize(config_path.is_absolute()
            ? config_path
            : std::filesystem::current_path() / config_path);

    if (absolute_config_path.has_parent_path()) {
        return normalize(absolute_config_path.parent_path());
    }

    return {};
}

std::filesystem::path ResourcePath::executable_directory(
    const std::filesystem::path& executable_path
) {
    if (executable_path.empty()) {
        return {};
    }

    const auto absolute_executable_path =
        normalize(executable_path.is_absolute()
            ? executable_path
            : std::filesystem::current_path() / executable_path);

    if (absolute_executable_path.has_parent_path()) {
        return normalize(absolute_executable_path.parent_path());
    }

    return {};
}

std::vector<std::filesystem::path> ResourcePath::search_roots(
    const ResourcePathContext& context
) {
    std::vector<std::filesystem::path> roots;

    const auto config_dir = config_directory(context.config_path);
    /*
     * 常见发布结构：
     *
     * release/
     * ├── GeoScatter3D
     * ├── config/viewer.toml
     * ├── assets/shaders/...
     * └── data/...
     *
     * 如果 config_path 是 release/config/viewer.toml，
     * 那 config_dir 是 release/config。
     * 这里额外加入 release 作为搜索根。
     */
    if (!config_dir.empty() && config_dir.has_parent_path()) {
        roots.push_back(
            normalize(config_dir.parent_path())
        );
    }

    const auto executable_dir = executable_directory(context.executable_path);
    if (!executable_dir.empty()) {
        roots.push_back(executable_dir);
    }

    if (!config_dir.empty()) {
        roots.push_back(config_dir);
    }

    // The working directory is only a compatibility fallback. Configured or
    // packaged assets must win so launching from a development build tree
    // cannot silently mix assets from a different checkout or release.
    const auto cwd = current_working_directory();
    if (!cwd.empty()) {
        roots.push_back(cwd);
    }

    /*
     * 去重。
     */
    std::vector<std::filesystem::path> unique_roots;
    unique_roots.reserve(roots.size());

    for (const auto& root : roots) {
        const auto normalized = normalize(root);

        bool exists = false;
        for (const auto& saved : unique_roots) {
            if (saved == normalized) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            unique_roots.push_back(normalized);
        }
    }

    return unique_roots;
}

std::filesystem::path ResourcePath::normalize(
    const std::filesystem::path& path
) {
    if (path.empty()) {
        return {};
    }

    std::error_code ec;

    const auto absolute =
        std::filesystem::absolute(path, ec);

    if (ec) {
        return path.lexically_normal();
    }

    return absolute.lexically_normal();
}

bool ResourcePath::is_existing_regular_file(
    const std::filesystem::path& path
) {
    std::error_code ec;

    const bool exists =
        std::filesystem::exists(path, ec);

    if (ec || !exists) {
        return false;
    }

    const bool regular =
        std::filesystem::is_regular_file(path, ec);

    if (ec) {
        return false;
    }

    return regular;
}

std::string ResourcePath::make_not_found_message(
    const std::filesystem::path& path,
    const std::vector<std::filesystem::path>& roots
) {
    std::ostringstream oss;

    oss << "ResourcePath: failed to resolve file: "
        << path.string()
        << '\n';

    oss << "Searched roots:\n";

    for (const auto& root : roots) {
        oss << "  - " << root.string() << '\n';
    }

    return oss.str();
}

} // namespace gs3d::app
