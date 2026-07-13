#include "app/ViewerAppInternal.hpp"
#include "util/Log.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace gs3d::app {

namespace {

[[nodiscard]]
std::string join_uint64_list(const std::vector<std::uint64_t>& values)
{
    if (values.empty()) {
        return "-";
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << values[i];
    }
    return oss.str();
}

[[nodiscard]]
std::array<std::uint8_t, 3> bright_hash_color(std::uint32_t id) noexcept
{
    if (id == 0) {
        return {0, 0, 0};
    }

    // Keep zero strictly black; every non-zero id gets a bright HSV color
    // so "has value but too dark" cannot be mistaken for empty.
    std::uint32_t hash = id;
    hash ^= hash >> 16;
    hash *= 0x7feb352dU;
    hash ^= hash >> 15;
    hash *= 0x846ca68bU;
    hash ^= hash >> 16;

    const float hue =
        static_cast<float>(hash % 360u) / 60.0f;
    const float saturation = 0.90f;
    const float value = 0.98f;

    const float chroma = value * saturation;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    if (hue < 1.0f) {
        r = chroma;
        g = x;
    } else if (hue < 2.0f) {
        r = x;
        g = chroma;
    } else if (hue < 3.0f) {
        g = chroma;
        b = x;
    } else if (hue < 4.0f) {
        g = x;
        b = chroma;
    } else if (hue < 5.0f) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }

    const float match = value - chroma;
    r += match;
    g += match;
    b += match;

    return {
        static_cast<std::uint8_t>(std::round(r * 255.0f)),
        static_cast<std::uint8_t>(std::round(g * 255.0f)),
        static_cast<std::uint8_t>(std::round(b * 255.0f))
    };
}

[[nodiscard]]
std::string dump_index_label(std::uint64_t dump_index)
{
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << dump_index;
    return oss.str();
}

void set_rgb_pixel(
    std::vector<std::uint8_t>& image,
    std::uint32_t width,
    std::uint32_t height,
    int x,
    int y,
    const std::array<std::uint8_t, 3>& color
)
{
    if (x < 0 || y < 0 ||
        x >= static_cast<int>(width) ||
        y >= static_cast<int>(height)) {
        return;
    }

    const std::size_t index =
        (static_cast<std::size_t>(y) * width +
         static_cast<std::size_t>(x)) * 3u;
    if (index + 2 >= image.size()) {
        return;
    }
    image[index + 0] = color[0];
    image[index + 1] = color[1];
    image[index + 2] = color[2];
}

void draw_rect_rgb(
    std::vector<std::uint8_t>& image,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t left,
    std::uint32_t top,
    std::uint32_t rect_width,
    std::uint32_t rect_height,
    const std::array<std::uint8_t, 3>& color
)
{
    if (rect_width == 0 || rect_height == 0) {
        return;
    }

    const int right =
        static_cast<int>(left + rect_width - 1);
    const int bottom =
        static_cast<int>(top + rect_height - 1);
    for (int x = static_cast<int>(left); x <= right; ++x) {
        set_rgb_pixel(image, width, height, x, static_cast<int>(top), color);
        set_rgb_pixel(image, width, height, x, bottom, color);
    }
    for (int y = static_cast<int>(top); y <= bottom; ++y) {
        set_rgb_pixel(image, width, height, static_cast<int>(left), y, color);
        set_rgb_pixel(image, width, height, right, y, color);
    }
}

void draw_cross_rgb(
    std::vector<std::uint8_t>& image,
    std::uint32_t width,
    std::uint32_t height,
    int center_x,
    int center_y,
    int radius,
    const std::array<std::uint8_t, 3>& color
)
{
    for (int dx = -radius; dx <= radius; ++dx) {
        set_rgb_pixel(
            image,
            width,
            height,
            center_x + dx,
            center_y,
            color
        );
    }
    for (int dy = -radius; dy <= radius; ++dy) {
        set_rgb_pixel(
            image,
            width,
            height,
            center_x,
            center_y + dy,
            color
        );
    }
}

void write_binary_ppm(
    const std::filesystem::path& output_path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgb_pixels
)
{
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            "ViewerApp: failed to open debug ppm path: " +
            output_path.string()
        );
    }
    out << "P6\n" << width << ' ' << height << "\n255\n";
    out.write(
        reinterpret_cast<const char*>(rgb_pixels.data()),
        static_cast<std::streamsize>(rgb_pixels.size())
    );
}

[[nodiscard]]
std::vector<std::uint8_t> convert_color_image_to_rgb(
    const std::vector<std::uint8_t>& color_pixels,
    std::uint32_t width,
    std::uint32_t height,
    VkFormat color_format
)
{
    if (color_pixels.size() !=
        static_cast<std::size_t>(width) * height * 4u) {
        throw std::runtime_error(
            "ViewerApp: debug color dump size mismatch"
        );
    }

    const bool is_bgra =
        color_format == VK_FORMAT_B8G8R8A8_UNORM ||
        color_format == VK_FORMAT_B8G8R8A8_SRGB;
    const bool is_rgba =
        color_format == VK_FORMAT_R8G8B8A8_UNORM ||
        color_format == VK_FORMAT_R8G8B8A8_SRGB;
    if (!is_bgra && !is_rgba) {
        throw std::runtime_error(
            "ViewerApp: unsupported debug color format for dump"
        );
    }

    std::vector<std::uint8_t> rgb(
        static_cast<std::size_t>(width) * height * 3u
    );
    for (std::uint32_t out_y = 0; out_y < height; ++out_y) {
        const std::uint32_t src_y = height - 1 - out_y;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t src_index =
                (static_cast<std::size_t>(src_y) * width + x) * 4u;
            const std::size_t dst_index =
                (static_cast<std::size_t>(out_y) * width + x) * 3u;
            if (is_bgra) {
                rgb[dst_index + 0] = color_pixels[src_index + 2];
                rgb[dst_index + 1] = color_pixels[src_index + 1];
                rgb[dst_index + 2] = color_pixels[src_index + 0];
            } else {
                rgb[dst_index + 0] = color_pixels[src_index + 0];
                rgb[dst_index + 1] = color_pixels[src_index + 1];
                rgb[dst_index + 2] = color_pixels[src_index + 2];
            }
        }
    }
    return rgb;
}

[[nodiscard]]
std::vector<std::uint8_t> visualize_pick_ids_to_rgb(
    const std::vector<std::uint32_t>& pick_ids,
    std::uint32_t width,
    std::uint32_t height
)
{
    if (pick_ids.size() != static_cast<std::size_t>(width) * height) {
        throw std::runtime_error(
            "ViewerApp: debug pick-id dump size mismatch"
        );
    }

    std::vector<std::uint8_t> rgb(
        static_cast<std::size_t>(width) * height * 3u
    );
    for (std::uint32_t out_y = 0; out_y < height; ++out_y) {
        const std::uint32_t src_y = height - 1 - out_y;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t src_index =
                static_cast<std::size_t>(src_y) * width + x;
            const std::size_t dst_index =
                (static_cast<std::size_t>(out_y) * width + x) * 3u;
            const auto color = bright_hash_color(pick_ids[src_index]);
            rgb[dst_index + 0] = color[0];
            rgb[dst_index + 1] = color[1];
            rgb[dst_index + 2] = color[2];
        }
    }
    return rgb;
}

void annotate_pick_debug_image(
    std::vector<std::uint8_t>& rgb_pixels,
    const PickDebugDumpMetadata& metadata,
    std::uint32_t width,
    std::uint32_t height
)
{
    static constexpr std::array<std::uint8_t, 3> kRectColor{
        255u, 255u, 255u
    };
    static constexpr std::array<std::uint8_t, 3> kCrossColor{
        255u, 32u, 255u
    };

    draw_rect_rgb(
        rgb_pixels,
        width,
        height,
        metadata.sample_left,
        metadata.sample_top,
        metadata.sample_width,
        metadata.sample_height,
        kRectColor
    );
    draw_cross_rgb(
        rgb_pixels,
        width,
        height,
        static_cast<int>(std::floor(metadata.mouse_x)),
        static_cast<int>(std::floor(metadata.mouse_y)),
        8,
        kCrossColor
    );
}

} // namespace

void write_pick_debug_dump(
    const std::filesystem::path& output_dir,
    const PickDebugDumpFrame& dump
)
{
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);

    const std::string label = dump_index_label(dump.metadata.dump_index);
    const auto color_path =
        output_dir / ("frame_" + label + "_color.ppm");
    const auto pick_path =
        output_dir / ("frame_" + label + "_pick_id.ppm");
    const auto meta_path =
        output_dir / ("frame_" + label + "_meta.txt");

    auto color_rgb = convert_color_image_to_rgb(
        dump.color_pixels,
        dump.width,
        dump.height,
        dump.color_format
    );
    auto pick_rgb = visualize_pick_ids_to_rgb(
        dump.pick_ids,
        dump.width,
        dump.height
    );
    annotate_pick_debug_image(
        color_rgb,
        dump.metadata,
        dump.width,
        dump.height
    );
    annotate_pick_debug_image(
        pick_rgb,
        dump.metadata,
        dump.width,
        dump.height
    );

    write_binary_ppm(color_path, dump.width, dump.height, color_rgb);
    write_binary_ppm(pick_path, dump.width, dump.height, pick_rgb);

    std::ofstream meta(meta_path, std::ios::trunc);
    if (!meta) {
        throw std::runtime_error(
            "ViewerApp: failed to open pick debug metadata path: " +
            meta_path.string()
        );
    }
    meta << "dump_index=" << dump.metadata.dump_index << '\n';
    meta << "frame_index=" << dump.metadata.frame_index << '\n';
    meta << "viewport_index=" << dump.metadata.viewport_index << '\n';
    meta << "viewport_width=" << dump.metadata.viewport_width << '\n';
    meta << "viewport_height=" << dump.metadata.viewport_height << '\n';
    meta << "mouse_x=" << dump.metadata.mouse_x << '\n';
    meta << "mouse_y=" << dump.metadata.mouse_y << '\n';
    meta << "sample_left=" << dump.metadata.sample_left << '\n';
    meta << "sample_top=" << dump.metadata.sample_top << '\n';
    meta << "sample_width=" << dump.metadata.sample_width << '\n';
    meta << "sample_height=" << dump.metadata.sample_height << '\n';
    meta << "active_lod_level=" << dump.metadata.active_lod_level << '\n';
    meta << "tile_overlay_rendered="
         << (dump.metadata.tile_overlay_rendered ? "true" : "false") << '\n';
    meta << "all_tiles_resident="
         << (dump.metadata.all_tiles_resident ? "true" : "false") << '\n';
    meta << "render_source=" << dump.metadata.render_source << '\n';
    meta << "trigger_reason=" << dump.metadata.trigger_reason << '\n';
    meta << "selected_tile_ids="
         << join_uint64_list(dump.metadata.selected_tile_ids) << '\n';
    meta << "resident_tile_ids="
         << join_uint64_list(dump.metadata.resident_tile_ids) << '\n';

    gs3d::util::log::info() << "[PICK_DEBUG] wrote color dump: "
              << color_path.string() << '\n';
    gs3d::util::log::info() << "[PICK_DEBUG] wrote pick-id dump: "
              << pick_path.string() << '\n';
    gs3d::util::log::info() << "[PICK_DEBUG] wrote metadata: "
              << meta_path.string() << '\n';
}

} // namespace gs3d::app
