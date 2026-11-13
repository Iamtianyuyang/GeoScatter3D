#include "ffi/geoscatter3d_ffi.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace {

namespace fs = std::filesystem;

// setenv 不在 Windows 上；用一个小的跨平台包装。
void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env(const char* name) { set_env(name, ""); }

/// 把两个存储路径都指到临时目录，测试不碰真实用户配置。
class ScopedAppStateDir {
public:
    ScopedAppStateDir() {
        const auto stamp = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
        root_ = fs::temp_directory_path() / ("gs3d-app-abi-" + stamp);
        fs::create_directories(root_);
        set_env("GS3D_USER_CONFIG_DIR", root_.string().c_str());
        set_env("GS3D_RECENT_PROJECTS_PATH", (root_ / "recent.txt").string().c_str());
    }

    ~ScopedAppStateDir() {
        unset_env("GS3D_USER_CONFIG_DIR");
        unset_env("GS3D_RECENT_PROJECTS_PATH");
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    const fs::path& root() const { return root_; }

private:
    fs::path root_;
};

}  // namespace

TEST_CASE(
    "recent project ABI round-trips and survives a bad path",
    "[engine][abi][app]"
) {
    ScopedAppStateDir dir;
    REQUIRE(gs3d_engine_clear_recent_projects() == 0);

    const std::string first = (dir.root() / "甲 工程包").string();
    const std::string second = (dir.root() / "bundle-b").string();
    REQUIRE(gs3d_engine_remember_project(first.c_str()) == 0);
    REQUIRE(gs3d_engine_remember_project(second.c_str()) == 0);

    const auto listed = nlohmann::json::parse(gs3d_engine_recent_projects_json());
    REQUIRE(listed.is_array());
    REQUIRE(listed.size() == 2);
    // 最近打开的排在最前。
    REQUIRE(listed[0]["path"].get<std::string>() == second);
    REQUIRE(listed[1]["path"].get<std::string>() == first);
    REQUIRE(listed[0]["last_opened"].get<std::int64_t>() > 0);

    REQUIRE(gs3d_engine_forget_project(second.c_str()) == 0);
    const auto after_forget = nlohmann::json::parse(
        gs3d_engine_recent_projects_json()
    );
    REQUIRE(after_forget.size() == 1);
    REQUIRE(after_forget[0]["path"].get<std::string>() == first);

    REQUIRE(gs3d_engine_clear_recent_projects() == 0);
    REQUIRE(nlohmann::json::parse(gs3d_engine_recent_projects_json()).empty());

    REQUIRE(gs3d_engine_remember_project(nullptr) == -1);
    REQUIRE(gs3d_engine_remember_project("") == -1);
    REQUIRE(gs3d_engine_forget_project(nullptr) == -1);
}

TEST_CASE("dataset info is empty without an engine", "[engine][abi][app]") {
    REQUIRE(std::string(gs3d_engine_dataset_info_json(nullptr)) == "{}");
}

TEST_CASE("gpu enumeration always returns valid JSON", "[engine][abi][app]") {
    // CI 机器可能没有 Vulkan loader：那时返回空数组，而不是坏 JSON。
    const auto gpus = nlohmann::json::parse(gs3d_engine_gpus_json());
    REQUIRE(gpus.is_array());
    for (const auto& gpu : gpus) {
        REQUIRE(gpu.contains("index"));
        REQUIRE(gpu.contains("uuid"));
        REQUIRE(gpu["uuid"].get<std::string>().rfind("uuid:", 0) == 0);
        REQUIRE(gpu.contains("active"));
    }
    // 越界索引必须拒绝，不能静默写偏好。
    REQUIRE(gs3d_engine_set_preferred_gpu(99) == -1);
}
