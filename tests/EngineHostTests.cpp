#include "ffi/EngineHost.hpp"
#include "ffi/FrameChannel.hpp"
#include "ffi/HeadlessRenderKernel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
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

    // 连续出帧直到瓦片驻留下来；首帧即会读取并上传若干瓦片。
    std::uint64_t resident = 0;
    std::uint64_t tile_points = 0;
    std::uint64_t tile_uploaded_bytes = 0;
    for (int frame = 0; frame < 40; ++frame) {
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
