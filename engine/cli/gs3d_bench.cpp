/*
 * 无界面基准：打开工程包、渲染若干帧，输出可复现的数字（见 docs/architecture.md
 * 的「性能预算」）。只建在桌面目标上，Android 交叉编译不包含它。
 *
 * 用法：
 *   gs3d_bench --bundle <dir> [--frames N] [--width W] [--height H]
 *   gs3d_bench --csv <source> --out <dir>     # 预处理各阶段耗时
 *
 * 输出一行 JSON：打开毫秒、首帧毫秒、稳态帧毫秒、点数、峰值 RSS。
 * 数字必须在 Release 构建里取；Debug 构建只能用于功能验证。
 */
#include "ffi/HeadlessRenderKernel.hpp"

#include "pipeline/stages/BundleBuild.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double since_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Linux 下读 VmHWM；其他平台返回 0（基准只在桌面跑）。
std::uint64_t peak_rss_kib() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            return std::strtoull(line.c_str() + 6, nullptr, 10);
        }
    }
#endif
    return 0;
}

std::string json_escape(const std::string& text) {
    std::string out;
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

int run_open(const std::string& bundle, std::uint32_t width, std::uint32_t height,
             std::uint32_t frames) {
    gs3d::ffi::HeadlessRenderKernel kernel;
    gs3d::ffi::HeadlessRenderKernel::Config config;
    config.width = width;
    config.height = height;
    if (kernel.create(config) != 0) {
        std::fprintf(stderr, "create failed: %s\n", kernel.last_error().c_str());
        return 1;
    }

    const auto open_started = Clock::now();
    const int rc = kernel.load_dataset(bundle);
    const double open_ms = since_ms(open_started);
    if (rc != 0) {
        std::fprintf(stderr, "open failed: %s\n", kernel.last_error().c_str());
        kernel.destroy();
        return 1;
    }
    const std::uint64_t points = kernel.point_count();

    std::vector<std::uint8_t> pixels(std::size_t{width} * height * 4);
    std::uint32_t out_w = 0;
    std::uint32_t out_h = 0;

    const auto first_started = Clock::now();
    const int first_rc = kernel.render_rgba(pixels.data(),
                                            static_cast<std::uint32_t>(pixels.size()),
                                            &out_w, &out_h);
    const double first_ms = since_ms(first_started);
    if (first_rc != 0) {
        std::fprintf(stderr, "first frame failed: %s\n", kernel.last_error().c_str());
        kernel.destroy();
        return 1;
    }

    // 稳态帧：相机不动，测的是绘制与合成，不含输入处理。
    double steady_ms = 0.0;
    const std::uint32_t steady_frames = frames > 1 ? frames - 1 : 0;
    for (std::uint32_t i = 0; i < steady_frames; ++i) {
        const auto started = Clock::now();
        kernel.render_rgba(pixels.data(), static_cast<std::uint32_t>(pixels.size()),
                           &out_w, &out_h);
        steady_ms += since_ms(started);
    }
    if (steady_frames > 0) {
        steady_ms /= steady_frames;
    }

    std::printf(
        "{\"open_ms\": %.1f, \"first_frame_ms\": %.1f, \"frame_ms\": %.1f, "
        "\"points\": %llu, \"peak_rss_mib\": %llu, \"width\": %u, \"height\": %u}\n",
        open_ms, first_ms, steady_ms, static_cast<unsigned long long>(points),
        static_cast<unsigned long long>(peak_rss_kib() / 1024), out_w, out_h);
    kernel.destroy();
    return 0;
}

int run_pack(const std::string& source, const std::string& out_dir) {
    gs3d::pipeline::BundleBuildOptions options;
    options.source = source;
    options.bundle_dir = out_dir;

    std::string stage = "start";
    auto stage_started = Clock::now();
    std::string stages_json;
    const auto close_stage = [&]() {
        if (!stages_json.empty()) {
            stages_json += ", ";
        }
        stages_json += "\"" + json_escape(stage) + "\": " +
                       std::to_string(since_ms(stage_started) / 1000.0);
    };

    gs3d::pipeline::BundleBuildResult built;
    const auto started = Clock::now();
    try {
        built = gs3d::pipeline::build_bundle(options, [&](const char* next_stage) {
            close_stage();
            stage = next_stage;
            stage_started = Clock::now();
        });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pack failed: %s\n", error.what());
        return 1;
    }
    close_stage();
    const double total_s = since_ms(started) / 1000.0;

    std::printf(
        "{\"total_s\": %.1f, \"points\": %llu, \"peak_rss_mib\": %llu, "
        "\"stages_s\": {%s}}\n",
        total_s, static_cast<unsigned long long>(built.points),
        static_cast<unsigned long long>(peak_rss_kib() / 1024), stages_json.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string bundle;
    std::string source;
    std::string out_dir;
    std::uint32_t frames = 31;
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string {
            return i + 1 < argc ? std::string(argv[++i]) : std::string{};
        };
        if (arg == "--bundle") {
            bundle = next();
        } else if (arg == "--csv") {
            source = next();
        } else if (arg == "--out") {
            out_dir = next();
        } else if (arg == "--frames") {
            frames = static_cast<std::uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        } else if (arg == "--width") {
            width = static_cast<std::uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        } else if (arg == "--height") {
            height = static_cast<std::uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return 2;
        }
    }

    if (!source.empty()) {
        if (out_dir.empty()) {
            std::fprintf(stderr, "--csv 需要 --out\n");
            return 2;
        }
        return run_pack(source, out_dir);
    }
    if (bundle.empty()) {
        std::fprintf(stderr, "用法: gs3d_bench --bundle <dir> | --csv <source> --out <dir>\n");
        return 2;
    }
    return run_open(bundle, width, height, frames);
}
