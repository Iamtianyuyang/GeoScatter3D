#include "render/VulkanGpuInfo.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view name)
{
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        ++failures;
    }
}

// ── UUID conversion ──────────────────────────────────────────────────

void test_uuid_round_trip()
{
    // Construct a VkPhysicalDeviceIDProperties with known bytes
    VkPhysicalDeviceIDProperties id{};
    id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    for (std::uint32_t i = 0; i < VK_UUID_SIZE; ++i) {
        id.deviceUUID[i] = static_cast<std::uint8_t>(i);
    }

    const std::string hex =
        gs3d::render::device_uuid_from_properties(id);
    expect(hex.size() == 32,
           "UUID hex string length is 32");

    const std::string round_trip =
        gs3d::render::device_uuid_from_hex(hex);
    // Convert back to hex for comparison
    VkPhysicalDeviceIDProperties rt_id{};
    rt_id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    for (std::size_t i = 0; i < VK_UUID_SIZE; ++i) {
        rt_id.deviceUUID[i] =
            static_cast<std::uint8_t>(round_trip[i]);
    }
    const std::string rt_hex =
        gs3d::render::device_uuid_from_properties(rt_id);
    expect(hex == rt_hex,
           "UUID hex → bytes → hex round trip preserved");
}

void test_uuid_from_hex_invalid_length()
{
    expect(gs3d::render::device_uuid_from_hex("abc").empty(),
           "short hex string returns empty bytes");
    expect(gs3d::render::device_uuid_from_hex(
               "000102030405060708090a0b0c0d0e0f00").empty(),
           "33-char hex (odd length) returns empty");
    expect(gs3d::render::device_uuid_from_hex(
               "000102030405060708090a0b0c0d0e0f").size()
               == VK_UUID_SIZE,
           "32-char hex returns VK_UUID_SIZE bytes");
}

void test_uuid_from_hex_invalid_chars()
{
    expect(gs3d::render::device_uuid_from_hex(
               "gggggggggggggggggggggggggggggggg").empty(),
           "non-hex chars → empty");
}

void test_uuid_normalisation()
{
    // Uppercase → lowercase
    expect(gs3d::render::normalise_device_uuid(
               "000102030405060708090A0B0C0D0E0F") ==
               "000102030405060708090a0b0c0d0e0f",
           "uppercase normalised to lowercase");

    // Whitespace stripped
    expect(gs3d::render::normalise_device_uuid(
               "  000102030405060708090a0b0c0d0e0f  ") ==
               "000102030405060708090a0b0c0d0e0f",
           "whitespace stripped");

    // Invalid length
    expect(gs3d::render::normalise_device_uuid("abc").empty(),
           "short input → empty");

    // Auto unchanged
    expect(gs3d::render::normalise_device_uuid("auto").empty(),
           "\"auto\" is not a valid UUID → empty");
}

// ── GPU type label ────────────────────────────────────────────────────

void test_gpu_type_label()
{
    expect(std::string(gs3d::render::gpu_type_label(
               VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)) == "独立显卡",
           "discrete → 独立显卡");
    expect(std::string(gs3d::render::gpu_type_label(
               VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)) == "集成显卡",
           "integrated → 集成显卡");
}

// ── select_gpu ────────────────────────────────────────────────────────

// Helper: build a minimal suitable GPU info entry.
gs3d::render::VulkanGpuInfo make_gpu(
    const std::string& name,
    const std::string& uuid,
    VkPhysicalDeviceType type,
    bool suitable = true,
    std::uint64_t memory = 8ULL * 1024 * 1024 * 1024)
{
    gs3d::render::VulkanGpuInfo g;
    g.name = name;
    g.device_uuid = uuid;
    g.device_type = type;
    g.suitable = suitable;
    g.device_local_memory_bytes = memory;
    g.api_version = VK_API_VERSION_1_2;
    return g;
}

void test_select_gpu_auto_prefers_discrete()
{
    std::vector<gs3d::render::VulkanGpuInfo> gpus{
        make_gpu("Integrated",
                 "00000000000000000000000000000001",
                 VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU),
        make_gpu("Discrete",
                 "00000000000000000000000000000002",
                 VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU),
    };

    auto sel = gs3d::render::select_gpu(gpus, "auto");
    expect(sel.index.has_value(), "auto: selection succeeds");
    expect(!sel.fallback_to_auto, "auto: no fallback");
    expect(gpus[*sel.index].name == "Discrete",
           "auto: prefers discrete over integrated");
}

void test_select_gpu_by_uuid()
{
    std::vector<gs3d::render::VulkanGpuInfo> gpus{
        make_gpu("GPU A",
                 "000102030405060708090a0b0c0d0e0f",
                 VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU),
        make_gpu("GPU B",
                 "101112131415161718191a1b1c1d1e1f",
                 VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU),
    };

    auto sel = gs3d::render::select_gpu(
        gpus, "uuid:101112131415161718191a1b1c1d1e1f");
    expect(sel.index.has_value(), "uuid: selection succeeds");
    expect(!sel.fallback_to_auto, "uuid: no fallback");
    expect(gpus[*sel.index].name == "GPU B",
           "uuid: selects correct GPU by UUID");
}

void test_select_gpu_uuid_not_found_fallback()
{
    std::vector<gs3d::render::VulkanGpuInfo> gpus{
        make_gpu("Only GPU",
                 "00000000000000000000000000000001",
                 VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU),
    };

    auto sel = gs3d::render::select_gpu(
        gpus, "uuid:ffffffffffffffffffffffffffffffff");
    expect(sel.index.has_value(),
           "missing UUID: falls back to auto, selection succeeds");
    expect(sel.fallback_to_auto,
           "missing UUID: fallback_to_auto=true");
    expect(sel.fallback_reason.find("未找到") != std::string::npos,
           "missing UUID: fallback reason mentions not found");
}

void test_select_gpu_unsuitable_fallback()
{
    std::vector<gs3d::render::VulkanGpuInfo> gpus{
        make_gpu("Bad GPU",
                 "00000000000000000000000000000001",
                 VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                 false,  // not suitable
                 0),
        make_gpu("Good GPU",
                 "00000000000000000000000000000002",
                 VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
                 true),
    };

    auto sel = gs3d::render::select_gpu(
        gpus, "uuid:00000000000000000000000000000001");
    expect(sel.index.has_value(),
           "unsuitable preferred: falls back, selection succeeds");
    expect(sel.fallback_to_auto,
           "unsuitable preferred: fallback_to_auto=true");
    expect(gpus[*sel.index].name == "Good GPU",
           "unsuitable preferred: falls back to suitable GPU");
}

void test_select_gpu_auto_finds_only_suitable()
{
    std::vector<gs3d::render::VulkanGpuInfo> gpus{
        make_gpu("Unsuitable Discrete",
                 "00000000000000000000000000000001",
                 VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                 false),
        make_gpu("Suitable Integrated",
                 "00000000000000000000000000000002",
                 VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
                 true),
    };

    auto sel = gs3d::render::select_gpu(gpus, "auto");
    expect(sel.index.has_value(), "auto: skips unsuitable");
    expect(gpus[*sel.index].name == "Suitable Integrated",
           "auto: selects only suitable device");
}

void test_select_gpu_no_suitable_returns_empty()
{
    std::vector<gs3d::render::VulkanGpuInfo> gpus{
        make_gpu("Bad",
                 "00000000000000000000000000000001",
                 VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                 false),
    };

    auto sel = gs3d::render::select_gpu(gpus, "auto");
    expect(!sel.index.has_value(),
           "no suitable GPU → nullopt");
}

void test_select_gpu_invalid_format_fallback()
{
    std::vector<gs3d::render::VulkanGpuInfo> gpus{
        make_gpu("GPU",
                 "00000000000000000000000000000001",
                 VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU),
    };

    auto sel = gs3d::render::select_gpu(gpus, "garbage");
    expect(sel.index.has_value(),
           "invalid format: falls back to auto");
    expect(sel.fallback_to_auto,
           "invalid format: fallback_to_auto=true");
}

void test_select_gpu_tiebreak_by_memory()
{
    std::vector<gs3d::render::VulkanGpuInfo> gpus{
        make_gpu("Small Discrete",
                 "00000000000000000000000000000001",
                 VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                 true,
                 4ULL * 1024 * 1024 * 1024),
        make_gpu("Large Discrete",
                 "00000000000000000000000000000002",
                 VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                 true,
                 12ULL * 1024 * 1024 * 1024),
    };

    auto sel = gs3d::render::select_gpu(gpus, "auto");
    expect(sel.index.has_value(), "tiebreak: selection succeeds");
    expect(gpus[*sel.index].name == "Large Discrete",
           "tiebreak: same type → larger memory wins");
}

} // namespace

int main()
{
    test_uuid_round_trip();
    test_uuid_from_hex_invalid_length();
    test_uuid_from_hex_invalid_chars();
    test_uuid_normalisation();
    test_gpu_type_label();
    test_select_gpu_auto_prefers_discrete();
    test_select_gpu_by_uuid();
    test_select_gpu_uuid_not_found_fallback();
    test_select_gpu_unsuitable_fallback();
    test_select_gpu_auto_finds_only_suitable();
    test_select_gpu_no_suitable_returns_empty();
    test_select_gpu_invalid_format_fallback();
    test_select_gpu_tiebreak_by_memory();

    if (failures) {
        std::cerr << failures << " test(s) FAILED.\n";
        return 1;
    }
    std::cout << "All VulkanGpuInfo tests passed.\n";
    return 0;
}
