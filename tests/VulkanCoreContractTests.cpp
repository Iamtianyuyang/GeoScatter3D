#include "render/VulkanSurface.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

VkResult create_surface_stub(
    VkInstance,
    void*,
    VkSurfaceKHR*
) {
    return VK_ERROR_INITIALIZATION_FAILED;
}

} // namespace

TEST_CASE("headless surface provider is invalid without a callback") {
    const gs3d::render::VulkanSurfaceProvider provider{};

    CHECK_FALSE(provider.valid());
}

TEST_CASE("surface provider is an opaque platform callback contract") {
    const char* extensions[] = {"VK_KHR_win32_surface"};
    const gs3d::render::VulkanSurfaceProvider provider{
        .instance_extensions = extensions,
        .instance_extension_count = 1,
        .create_surface = &create_surface_stub,
        .user_data = nullptr,
    };

    CHECK(provider.valid());
    CHECK(provider.instance_extension_count == 1);
    CHECK(provider.create_surface != nullptr);
}
