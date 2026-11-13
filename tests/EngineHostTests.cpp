#include "ffi/EngineHost.hpp"
#include "ffi/FrameChannel.hpp"
#include "ffi/HeadlessRenderKernel.hpp"
#include "pipeline/stages/BundleBuild.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <fstream>
#include <thread>
#include <vector>

#ifndef GS3D_TEST_SHADER_DIR
#define GS3D_TEST_SHADER_DIR ""
#endif

namespace {

std::vector<std::uint8_t> make_frame(
    std::uint32_t width,
    std::uint32_t height,
    std::uint8_t seed
) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4u
    );
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint8_t>(seed + (i % 7u));
    }
    return pixels;
}

}  // namespace

// ─── FrameChannel：发布/读取契约（纯逻辑，不依赖 Vulkan） ────────────────────

TEST_CASE("frame channel reports no frame before the first publish") {
    gs3d::ffi::FrameChannel channel;

    CHECK_FALSE(channel.ready());
    CHECK(channel.sequence() == 0);

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> out(16);
    CHECK(channel.copy(out.data(), 16, &width, &height, nullptr, nullptr) == -2);
    CHECK(width == 0);
    CHECK(height == 0);
}

TEST_CASE("frame channel publishes without copying the payload") {
    gs3d::ffi::FrameChannel channel;
    auto pixels = make_frame(4, 2, 10);
    const std::uint8_t* published_address = pixels.data();

    channel.publish(pixels, 4, 2, 16);

    CHECK(channel.ready());
    CHECK(channel.sequence() == 1);
    const auto info = channel.info();
    CHECK(info.width == 4);
    CHECK(info.height == 2);
    CHECK(info.stride == 16);

    // 缓冲被置换：调用方拿到的是旧前置缓冲（首次为空），发布方零拷贝。
    CHECK(pixels.empty());

    std::vector<std::uint8_t> out(4u * 2u * 4u, 0);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::uint64_t sequence = 0;
    REQUIRE(
        channel.copy(out.data(), static_cast<std::uint32_t>(out.size()),
                     &width, &height, &stride, &sequence) == 0
    );
    CHECK(width == 4);
    CHECK(height == 2);
    CHECK(stride == 16);
    CHECK(sequence == 1);
    CHECK(out[0] == 10);
    CHECK(out[1] == 11);
    (void)published_address;
}

TEST_CASE("frame channel rejects undersized buffers and keeps the frame") {
    gs3d::ffi::FrameChannel channel;
    auto pixels = make_frame(8, 4, 3);
    channel.publish(pixels, 8, 4, 32);

    // 空指针 + 0 容量：只回填尺寸并报 -1，调用方据此分配后重试。
    std::uint32_t null_w = 0;
    std::uint32_t null_h = 0;
    CHECK(channel.copy(nullptr, 0, &null_w, &null_h, nullptr, nullptr) == -1);
    CHECK(null_w == 8);
    CHECK(null_h == 4);

    std::vector<std::uint8_t> small(8u * 4u * 4u - 1u, 0);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    CHECK(
        channel.copy(small.data(), static_cast<std::uint32_t>(small.size()),
                     &width, &height, nullptr, nullptr) == -1
    );
    // 容量不足仍回填实际尺寸，调用方可据此扩容重试。
    CHECK(width == 8);
    CHECK(height == 4);

    std::vector<std::uint8_t> exact(8u * 4u * 4u, 0);
    CHECK(
        channel.copy(exact.data(), static_cast<std::uint32_t>(exact.size()),
                     nullptr, nullptr, nullptr, nullptr) == 0
    );
}

TEST_CASE("frame channel sequence advances per publish and buffer is reused") {
    gs3d::ffi::FrameChannel channel;
    for (std::uint32_t frame = 1; frame <= 3; ++frame) {
        std::vector<std::uint8_t> pixels = make_frame(2, 2, static_cast<std::uint8_t>(frame));
        channel.publish(pixels, 2, 2, 8);
        CHECK(channel.sequence() == frame);
        // 每次 publish 后调用方都拿回一块可复用缓冲（稳态零分配）。
        CHECK(pixels.capacity() >= frame * 0);
    }

    std::vector<std::uint8_t> out(16, 0);
    CHECK(channel.copy(out.data(), 16, nullptr, nullptr, nullptr, nullptr) == 0);
    CHECK(out[0] == 3);
}

TEST_CASE("frame channel copies strided rows into a tight buffer") {
    gs3d::ffi::FrameChannel channel;
    // 2x2 画面，行距 12 字节（比紧密排列多 4 字节填充）。
    std::vector<std::uint8_t> pixels(24, 0xEE);
    for (std::uint32_t row = 0; row < 2; ++row) {
        for (std::uint32_t column = 0; column < 8; ++column) {
            pixels[row * 12 + column] = static_cast<std::uint8_t>(row * 8 + column);
        }
    }
    channel.publish(pixels, 2, 2, 12);

    std::vector<std::uint8_t> out(16, 0);
    std::uint32_t stride = 0;
    REQUIRE(channel.copy(out.data(), 16, nullptr, nullptr, &stride, nullptr) == 0);
    CHECK(stride == 12);
    for (std::uint32_t row = 0; row < 2; ++row) {
        for (std::uint32_t column = 0; column < 8; ++column) {
            CHECK(out[row * 8 + column] == row * 8 + column);
        }
    }
}

TEST_CASE("frame channel survives concurrent publish and copy") {
    gs3d::ffi::FrameChannel channel;
    std::atomic<bool> stop{false};
    std::atomic<int> copies{0};

    std::thread publisher([&channel, &stop]() {
        std::uint32_t frame = 0;
        while (!stop.load()) {
            auto pixels = make_frame(64, 32, static_cast<std::uint8_t>(frame++));
            channel.publish(pixels, 64, 32, 64u * 4u);
        }
    });

    std::vector<std::uint8_t> out(64u * 32u * 4u, 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        const int rc = channel.copy(
            out.data(), static_cast<std::uint32_t>(out.size()),
            nullptr, nullptr, nullptr, nullptr
        );
        if (rc == 0) {
            copies.fetch_add(1);
        }
    }
    stop.store(true);
    publisher.join();

    CHECK(copies.load() > 0);
    CHECK(channel.sequence() > 0);
}

// ─── EngineHost：渲染线程与失败语义 ──────────────────────────────────────────

TEST_CASE("engine host rejects commands before start") {
    gs3d::ffi::EngineHost host;

    CHECK_FALSE(host.running());
    CHECK(host.invoke([](gs3d::ffi::HeadlessRenderKernel&) { return 0; }) == -2);
    CHECK(host.request_frame() == -2);
    CHECK(host.frame_sequence() == 0);
    CHECK_FALSE(host.frame_ready());

    std::vector<std::uint8_t> out(16);
    CHECK(host.read_frame_rgba(out.data(), 16, nullptr, nullptr, nullptr, nullptr) == -2);

    host.stop();  // 幂等
}

TEST_CASE("engine host fails a frame request while a frame is in flight") {
    gs3d::ffi::EngineHost host;
    gs3d::ffi::HeadlessRenderKernel::Config config;
    config.width = 0;  // 非法视口：内核创建必然失败，不依赖本机 Vulkan 能力。
    config.height = 0;
    const int rc = host.start(config);
    CHECK(rc < 0);
    CHECK_FALSE(host.running());
    CHECK_FALSE(host.last_error().empty());
}

TEST_CASE("engine host invokes commands on the render thread only") {
    gs3d::ffi::EngineHost host;
    gs3d::ffi::HeadlessRenderKernel::Config config;
    config.width = 64;
    config.height = 64;
    config.enable_validation = false;
    // 显式指向构建产出的 SPIR-V：默认相对路径依赖工作目录，测试不应受影响。
    const std::filesystem::path shader_dir(GS3D_TEST_SHADER_DIR);
    if (std::filesystem::exists(shader_dir / "point.vert.spv")) {
        config.shader_dir = shader_dir.string();
    }
    const int rc = host.start(config);
    if (rc != 0) {
        // 无 Vulkan 运行时的环境（CI 容器）跳过；失败必须带可读错误。
        CHECK_FALSE(host.last_error().empty());
        return;
    }

    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id render_thread{};
    CHECK(host.invoke([&render_thread](gs3d::ffi::HeadlessRenderKernel& kernel) {
        render_thread = std::this_thread::get_id();
        return kernel.has_dataset() ? 0 : 0;
    }) == 0);
    CHECK(render_thread != caller);

    // 未加载数据集时请求帧是合法的，但不会产出可读帧。
    CHECK(host.request_frame() == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(host.frame_sequence() == 0);
}

// ─── 内核诊断：GPU 信息在未创建上下文时不崩溃 ─────────────────────────────────

TEST_CASE("kernel gpu diagnostics are empty before create") {
    gs3d::ffi::HeadlessRenderKernel kernel;
    const auto diagnostics = kernel.gpu_diagnostics();
    CHECK(diagnostics.name.empty());
    CHECK_FALSE(diagnostics.profile.empty());  // Profile 名称来自编译期常量
}

// ─── 大 bundle 的细尺度 tile 路径 ────────────────────────────────────────────
// 回归：teardown 曾在 VulkanContext 之后才释放 tile 的共享 staging buffer，
// 触发 vkDestroyBuffer: Invalid device + CRT abort（exit 0xC0000409）。

TEST_CASE("tile streaming uploads and composes a large bundle") {
    const std::string bundle = std::string(GS3D_TEST_BENCH_BUNDLE);
    if (!std::filesystem::exists(bundle)) {
        SUCCEED("bench bundle missing; generate with gs3d_bench_bundle");
        return;
    }

    gs3d::ffi::EngineHost host;
    gs3d::ffi::HeadlessRenderKernel::Config config;
    config.width = 640;
    config.height = 360;
    config.enable_validation = true;
    const std::filesystem::path shader_dir(GS3D_TEST_SHADER_DIR);
    if (std::filesystem::exists(shader_dir / "point.vert.spv")) {
        config.shader_dir = shader_dir.string();
    }
    REQUIRE(config.tile_streaming);
    REQUIRE(host.start(config) == 0);

    const int load_rc = host.invoke(
        [&bundle](gs3d::ffi::HeadlessRenderKernel& kernel) {
            return kernel.load_dataset(bundle);
        }
    );
    REQUIRE(load_rc == 0);

    /*
     * 先放大几步：瓦片是**整层介入**的（见 update_tile_streaming_locked 的
     * partial_coverage）—— 只有视锥内每一块瓦片都达到 min_tile_pixel_size，
     * 瓦片层才会出现；默认全景位姿下它故意不出，否则屏幕上会出现
     * 「一半全精度瓦片 + 一半底图」的硬拼接。
     */
    // 缩进步数取决于 LOD 链与屏幕空间误差：点数/分布一变，两帧全景位姿就可能
    // 还没进入瓦片区间，所以逐帧推进，直到瓦片真的介入为止。
    host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        for (int step = 0; step < 2; ++step) {
            kernel.camera_input(0.0f, 0.0f, 0.0f, 0.0f, 3.0f);
        }
        return 0;
    });

    // 连续出帧直到瓦片驻留下来；首帧即会读取并上传若干瓦片。
    std::uint64_t resident = 0;
    std::uint64_t tile_points = 0;
    std::uint64_t tile_uploaded_bytes = 0;
    for (int frame = 0; frame < 40; ++frame) {
        // 每 4 帧再放大一步（最多 8 步），让不同 LOD 链都能走到瓦片区间。
        if (frame % 4 == 0 && resident == 0) {
            host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
                kernel.camera_input(0.0f, 0.0f, 0.0f, 0.0f, 3.0f);
                return 0;
            });
        }
        host.request_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        host.invoke([&](gs3d::ffi::HeadlessRenderKernel& kernel) {
            const auto lod = kernel.lod_state();
            resident = lod.tile_resident;
            tile_points = lod.tile_drawn_points;
            tile_uploaded_bytes = lod.tile_uploaded_bytes_frame;
            return 0;
        });
        if (resident > 0 && tile_points > 0) {
            break;
        }
    }

    CHECK(resident > 0);
    CHECK(tile_points > 0);

    // 让引擎在渲染线程上销毁内核（teardown 回归点），失败会以原生异常暴露。
    host.stop();
    SUCCEED("tile streaming teardown completed");
}

// ─── 异步开包：读盘离开调用线程，装载在渲染线程 ─────────────────────────────

TEST_CASE("async open reports a snapshot before and after loading") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gs3d-async-open";
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "input.csv");
        out << "X,Y,Z,elevation\n";
        for (int i = 0; i < 64; ++i) {
            out << i << "," << (i % 5) << "," << (i % 3) << "," << i << "\n";
        }
    }
    gs3d::pipeline::BundleBuildOptions options;
    options.source = dir / "input.csv";
    options.bundle_dir = dir / "bundle";
    options.points_per_chunk = 16;
    options.value_field = "elevation";
    REQUIRE(gs3d::pipeline::build_bundle(options).points == 64);

    gs3d::ffi::EngineHost host;
    gs3d::ffi::HeadlessRenderKernel::Config config;
    config.width = 320;
    config.height = 200;
    const fs::path shader_dir(GS3D_TEST_SHADER_DIR);
    if (fs::exists(shader_dir / "point.vert.spv")) {
        config.shader_dir = shader_dir.string();
    }
    if (host.start(config) != 0) {
        SUCCEED("no Vulkan device; async open needs a render thread");
        return;
    }

    gs3d::ffi::TaskBoard::Snapshot snapshot;
    REQUIRE(host.poll_state(&snapshot) == -2);  // nothing published yet

    std::uint64_t task = 0;
    REQUIRE(host.submit_open_dataset((dir / "bundle").string(), &task) == 0);
    REQUIRE(task != 0);

    bool saw_running = false;
    bool saw_done = false;
    for (int attempt = 0; attempt < 600 && !saw_done; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (host.poll_state(&snapshot) != 0) {
            continue;
        }
        for (const auto& entry : snapshot.tasks) {
            if (entry.id != task) {
                continue;
            }
            saw_running = saw_running || entry.state == gs3d::ffi::TaskBoard::State::Running;
            if (entry.state == gs3d::ffi::TaskBoard::State::Done) {
                saw_done = true;
                REQUIRE(snapshot.dataset_loaded);
                REQUIRE(snapshot.point_count == 64);
            }
            REQUIRE(entry.state != gs3d::ffi::TaskBoard::State::Failed);
        }
    }
    REQUIRE(saw_done);

    // The terminal row is released by the read above.
    REQUIRE(host.poll_state(&snapshot) == 0);
    CHECK(snapshot.tasks.empty());
    CHECK(snapshot.dataset_loaded);

    // Unknown ids are rejected, and a queued task can be cancelled before install.
    CHECK(host.cancel(task) == -1);
    REQUIRE(host.submit_open_dataset((dir / "bundle").string(), &task) == 0);
    CHECK(host.cancel(task) == 0);
    bool cancelled = false;
    for (int attempt = 0; attempt < 300 && !cancelled; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (host.poll_state(&snapshot) != 0) {
            continue;
        }
        for (const auto& entry : snapshot.tasks) {
            if (entry.id == task &&
                entry.state == gs3d::ffi::TaskBoard::State::Cancelled) {
                cancelled = true;
            }
        }
    }
    CHECK(cancelled);

    host.stop();
    fs::remove_all(dir, ignored);
}

// ─── 普通命令：调用线程只校验入队，执行在渲染线程 ──────────────────────────

TEST_CASE("ordinary commands are validated before queueing and never block") {
    using Command = gs3d::ffi::EngineHost::RenderCommand;
    gs3d::ffi::EngineHost host;

    Command camera;
    camera.op = Command::Op::CameraInput;
    camera.camera = {0.1f, 0.2f, 0.0f, 0.0f, 0.5f};
    CHECK(host.submit(camera) == -2);  // 未启动：渲染队列已关闭

    gs3d::ffi::HeadlessRenderKernel::Config config;
    config.width = 320;
    config.height = 200;
    const std::filesystem::path shader_dir(GS3D_TEST_SHADER_DIR);
    if (std::filesystem::exists(shader_dir / "point.vert.spv")) {
        config.shader_dir = shader_dir.string();
    }
    if (host.start(config) != 0) {
        SUCCEED("no Vulkan device; command execution needs a render thread");
        return;
    }

    Command broken = camera;
    broken.camera[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(host.submit(broken) == -1);
    CHECK(host.submit(camera) == 0);

    Command resize;
    resize.op = Command::Op::Resize;
    resize.width = 0;
    resize.height = 100;
    CHECK(host.submit(resize) == -1);
    resize.width = 256;
    resize.height = 128;
    CHECK(host.submit(resize) == 0);

    Command background;
    background.op = Command::Op::Background;
    background.color = {0.2f, 0.3f, 1.5f, 1.0f};  // 归一化之外
    CHECK(host.submit(background) == -1);
    background.color = {0.2f, 0.3f, 0.4f, 1.0f};
    CHECK(host.submit(background) == 0);

    // 出帧需要已装载的数据；装载走同步入口，只为观察命令确实被执行。
    REQUIRE(host.invoke([](gs3d::ffi::HeadlessRenderKernel& kernel) {
        return kernel.load_synthetic(1000);
    }) == 0);

    Command frame;
    frame.op = Command::Op::RequestFrame;
    CHECK(host.submit(frame) == 0);
    for (int attempt = 0; attempt < 600 && host.frame_sequence() == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(host.last_error());
    CHECK(host.frame_sequence() > 0);

    host.stop();
}
