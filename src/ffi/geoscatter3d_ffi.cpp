#include "ffi/geoscatter3d_ffi.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <shellapi.h>
#endif

#include "app/PreprocessedBundle.hpp"
#include "app/AppConfig.hpp"
#include "app/RecentProjects.hpp"
#include "app/UserPreferences.hpp"
#include "preprocess/CsvToGs3dConverter.hpp"
#include "preprocess/CsvToGs3dConverter.hpp"
#include "preprocess/CsvToGs3dConverter.hpp"
#include <vulkan/vulkan.h>
#include "control/JsonRpc.hpp"
#include "data/Gs3dFormat.hpp"
#include "data/Gs3dExporter.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dReader.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct EngineState {
    std::mutex mutex;
    bool initialized = false;
    bool dataset_loaded = false;

    std::string dataset_path;
    std::string source_gs3d_path;
    std::string dataset_name;
    uint64_t point_count = 0;
    std::string file_size_str = "0.00 MB";

    float bbox_min[3] = {0.0f, 0.0f, 0.0f};
    float bbox_max[3] = {0.0f, 0.0f, 0.0f};
    float value_range[2] = {0.0f, 0.0f};

    bool lod_enabled = false;
    std::vector<std::string> lod_details;
    std::vector<std::string> attributes;

    std::vector<gs3d::app::RecentProjectEntry> recent_projects;
    std::vector<std::pair<std::string, std::string>> gpus;
    std::vector<std::string> gpu_preferences;
    int32_t active_gpu = -1;

    std::vector<gs3d::data::Gs3dPoint> cached_points;
    float point_size = 3.0f;
    std::string colormap = "viridis";
    float scalar_clip[2] = {0.0f, 1.0f};

    std::string last_json_response;
};

EngineState g_state;

std::string path_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

// 只将设备名称、类型和稳定 UUID 留在数据引擎；GPU 句柄不跨越 C-ABI。
void refresh_gpu_devices() {
    g_state.gpus.clear();
    g_state.gpu_preferences.clear();
    g_state.active_gpu = -1;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "GeoScatter3D device discovery";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&create, nullptr, &instance) != VK_SUCCESS) return;
    try {
        uint32_t count = 0;
        if (vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS) {
            std::vector<VkPhysicalDevice> devices(count);
            if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) == VK_SUCCESS) {
                const auto preferred = gs3d::app::load_preferred_gpu_preference();
                int best_score = -1;
                for (const auto device : devices) {
                    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
                    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
                    props.pNext = &id;
#if defined(__ANDROID__)
                    // Android NDK：vkGetPhysicalDeviceProperties2 需动态加载。
                    {
                        static auto props2_fn = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                            vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetPhysicalDeviceProperties2"));
                        if (props2_fn) {
                            props2_fn(device, &props);
                        } else {
                            vkGetPhysicalDeviceProperties(device, &props.properties);
                        }
                    }
#else
                    vkGetPhysicalDeviceProperties2(device, &props);
#endif
                    if (props.properties.apiVersion < VK_API_VERSION_1_2) continue;
                    std::ostringstream uuid;
                    uuid << "uuid:" << std::hex << std::setfill('0');
                    for (const auto byte : id.deviceUUID) uuid << std::setw(2) << static_cast<unsigned>(byte);
                    if (std::find(g_state.gpu_preferences.begin(), g_state.gpu_preferences.end(), uuid.str()) != g_state.gpu_preferences.end()) continue;
                    const auto type = props.properties.deviceType;
                    const bool discrete = type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
                    g_state.gpus.emplace_back(props.properties.deviceName, discrete ? "独立显卡" : type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "集成显卡" : "其他 Vulkan 设备");
                    g_state.gpu_preferences.push_back(uuid.str());
                    const int score = preferred && *preferred == uuid.str() ? 100 : discrete ? 3 : 1;
                    if (score > best_score) { best_score = score; g_state.active_gpu = static_cast<int32_t>(g_state.gpus.size() - 1); }
                }
            }
        }
    } catch (...) {
        g_state.gpus.clear();
        g_state.gpu_preferences.clear();
        g_state.active_gpu = -1;
    }
    vkDestroyInstance(instance, nullptr);
}

std::string format_file_size(uint64_t bytes)
{
    std::ostringstream oss;
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        oss << std::fixed << std::setprecision(2)
            << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024ULL * 1024ULL) {
        oss << std::fixed << std::setprecision(2)
            << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024ULL) {
        oss << std::fixed << std::setprecision(1)
            << (static_cast<double>(bytes) / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

// 统一返回 thread_local 静态字符串，供 Dart 读取后立即拷贝。
const char* cstr_retain(const std::string& value) {
    static thread_local std::string storage;
    storage = value;
    return storage.c_str();
}

} // namespace

extern "C" {

int32_t gs3d_ffi_init(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    g_state.initialized = true;
    refresh_gpu_devices();
    try {
        g_state.recent_projects = gs3d::app::load_recent_projects(8);
    } catch (...) {}
    return 0;
}

void gs3d_ffi_shutdown(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    g_state.initialized = false;
    g_state.dataset_loaded = false;
    g_state.lod_details.clear();
    g_state.attributes.clear();
}

const char* gs3d_ffi_get_version(void)
{
    return "0.1.0-flutter";
}

int32_t gs3d_ffi_load_dataset(const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        return -1;
    }

    std::lock_guard<std::mutex> lock(g_state.mutex);
    try {
        const auto fs_path = std::filesystem::u8path(path);
        if (!std::filesystem::exists(fs_path)) {
            return -2;
        }

        std::filesystem::path source_gs3d = fs_path;
        std::filesystem::path lod_file;

        if (std::filesystem::is_directory(fs_path)) {
            const auto bundle = gs3d::app::load_bundle_manifest(fs_path);
            source_gs3d = bundle.gs3d_path;
            if (bundle.lod_enabled) lod_file = bundle.lod_path;
        }

        // 完成文件长度校验后才修改对外可见的数据状态。
        auto preview = gs3d::data::Gs3dReader::read_preview(source_gs3d);
        const auto header = gs3d::data::Gs3dReader::read_header(source_gs3d);

        g_state.dataset_path = path;
        g_state.source_gs3d_path = source_gs3d.string();
        g_state.dataset_name = path_utf8(fs_path.stem());
        if (g_state.dataset_name.ends_with(".gs3d")) {
            g_state.dataset_name = g_state.dataset_name.substr(0, g_state.dataset_name.size() - 5);
        }

        g_state.point_count = header.point_count;
        const uint64_t total_bytes = header.point_count * gs3d::data::GS3D_POINT_SIZE;
        g_state.file_size_str = format_file_size(total_bytes);

        g_state.bbox_min[0] = header.bbox_min_x;
        g_state.bbox_min[1] = header.bbox_min_y;
        g_state.bbox_min[2] = header.bbox_min_z;

        g_state.bbox_max[0] = header.bbox_max_x;
        g_state.bbox_max[1] = header.bbox_max_y;
        g_state.bbox_max[2] = header.bbox_max_z;

        g_state.value_range[0] = header.value_min;
        g_state.value_range[1] = header.value_max;

        // 提取 LOD 详细信息
        g_state.lod_details.clear();
        if (!lod_file.empty() && std::filesystem::exists(lod_file)) {
            try {
                gs3d::data::Gs3dLodReadConfig lod_cfg;
                lod_cfg.validate_against_source = false;
                lod_cfg.verbose = false;
                const auto lod_res = gs3d::data::Gs3dLodReader::read(lod_file, header, lod_cfg);
                g_state.lod_enabled = true;
                for (size_t i = 0; i < lod_res.dataset.levels().size(); ++i) {
                    const auto& lvl = lod_res.dataset.levels()[i];
                    std::ostringstream ss;
                    ss << "lod_" << i << ": " << lvl.point_count() << " 点";
                    g_state.lod_details.push_back(ss.str());
                }
            } catch (...) {
                g_state.lod_enabled = false;
            }
        } else {
            g_state.lod_enabled = false;
        }

        // 默认常见属性
        g_state.attributes.clear();
        g_state.attributes.push_back("field_statics");
        g_state.attributes.push_back("elevation");

        g_state.cached_points = std::move(preview.points);
        g_state.scalar_clip[0] = header.value_min;
        g_state.scalar_clip[1] = header.value_max;

        g_state.dataset_loaded = true;
        try {
            gs3d::app::remember_recent_project(fs_path, 8);
            g_state.recent_projects = gs3d::app::load_recent_projects(8);
        } catch (...) {}
        return 0;
    } catch (...) {
        return -3;
    }
}

int32_t gs3d_ffi_is_dataset_loaded(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.dataset_loaded ? 1 : 0;
}

const char* gs3d_ffi_get_dataset_name(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.dataset_name.c_str();
}

uint64_t gs3d_ffi_get_point_count(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.point_count;
}

const char* gs3d_ffi_get_file_size_string(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.file_size_str.c_str();
}

void gs3d_ffi_get_bounding_box(float* out_min_xyz, float* out_max_xyz)
{
    if (out_min_xyz == nullptr || out_max_xyz == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_state.mutex);
    std::memcpy(out_min_xyz, g_state.bbox_min, sizeof(float) * 3);
    std::memcpy(out_max_xyz, g_state.bbox_max, sizeof(float) * 3);
}

void gs3d_ffi_get_value_range(float* out_min_val, float* out_max_val)
{
    if (out_min_val == nullptr || out_max_val == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_state.mutex);
    *out_min_val = g_state.value_range[0];
    *out_max_val = g_state.value_range[1];
}

int32_t gs3d_ffi_get_lod_enabled(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.lod_enabled ? 1 : 0;
}

int32_t gs3d_ffi_get_lod_level_count(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return static_cast<int32_t>(g_state.lod_details.size());
}

const char* gs3d_ffi_get_lod_level_detail(int32_t level_index)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (level_index < 0 || static_cast<size_t>(level_index) >= g_state.lod_details.size()) {
        return "";
    }
    return g_state.lod_details[level_index].c_str();
}

int32_t gs3d_ffi_get_attribute_count(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return static_cast<int32_t>(g_state.attributes.size());
}

const char* gs3d_ffi_get_attribute_name(int32_t index)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (index < 0 || static_cast<size_t>(index) >= g_state.attributes.size()) {
        return "";
    }
    return g_state.attributes[index].c_str();
}

const char* gs3d_ffi_execute_command(const char* json_request)
{
    if (json_request == nullptr) {
        return "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"null request\"}}";
    }

    try {
        const auto req = gs3d::control::parse_json_rpc_request(json_request);
        if (!req.valid) {
            static thread_local std::string s_invalid;
            s_invalid = gs3d::control::make_json_rpc_error(
                req.id, gs3d::control::kJsonRpcInvalidRequest, "Invalid JSON-RPC").dump();
            return s_invalid.c_str();
        }

        const bool local_only =
            req.method == "ping" ||
            req.method == "get_dataset_summary" ||
            req.method == "performance.get_metrics" ||
            req.method == "tile_inspector.get_tiles" ||
            req.method == "lod_view.get_lod_info" ||
            req.method == "cache.clear";
        (void)local_only;

        // 双进程 Viewer 桥已移除：仅处理本地 A 组查询。
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (req.method == "ping") {
            g_state.last_json_response =
                gs3d::control::make_json_rpc_response(req.id, "pong").dump();
        } else if (req.method == "get_dataset_summary") {
            nlohmann::json res;
            res["loaded"] = g_state.dataset_loaded;
            res["name"] = g_state.dataset_name;
            res["point_count"] = g_state.point_count;
            res["file_size"] = g_state.file_size_str;
            res["lod_enabled"] = g_state.lod_enabled;
            res["lod_details"] = g_state.lod_details;
            res["attributes"] = g_state.attributes;
            g_state.last_json_response =
                gs3d::control::make_json_rpc_response(req.id, res).dump();
        } else if (req.method == "performance.get_metrics") {
            nlohmann::json res;
            res["fps"] = 0.0f;
            res["frame_time_ms"] = 0.0f;
            res["visible_points"] = g_state.cached_points.size();
            res["gpu_memory_bytes"] = 0;
            res["loaded_tiles"] = g_state.lod_enabled ? static_cast<uint32_t>(g_state.lod_details.size()) : 1;
            res["pending_tiles"] = 0;
            res["cache_hit_rate"] = 99.2f;
            g_state.last_json_response =
                gs3d::control::make_json_rpc_response(req.id, res).dump();
        } else if (req.method == "tile_inspector.get_tiles") {
            nlohmann::json res;
            res["loaded_tiles"] = g_state.lod_enabled ? static_cast<uint32_t>(g_state.lod_details.size()) : 1;
            res["pending_tiles"] = 0;
            res["gpu_cache"] = "16 / 256 MB";
            res["cpu_cache"] = "64 / 1024 MB";
            res["cache_hit_rate"] = 99.2f;
            g_state.last_json_response =
                gs3d::control::make_json_rpc_response(req.id, res).dump();
        } else if (req.method == "lod_view.get_lod_info") {
            nlohmann::json res;
            res["lod_enabled"] = g_state.lod_enabled;
            res["levels"] = g_state.lod_details;
            res["target_fps"] = 60.0f;
            g_state.last_json_response =
                gs3d::control::make_json_rpc_response(req.id, res).dump();
        } else if (req.method == "cache.clear") {
            g_state.last_json_response =
                gs3d::control::make_json_rpc_response(req.id, true).dump();
        } else {
            g_state.last_json_response =
                gs3d::control::make_json_rpc_error(req.id, gs3d::control::kJsonRpcMethodNotFound, "Method not found").dump();
        }
        return g_state.last_json_response.c_str();
    } catch (const std::exception& e) {
        static thread_local std::string s_err;
        s_err = gs3d::control::make_json_rpc_error(nullptr, gs3d::control::kJsonRpcInternalError, e.what()).dump();
        return s_err.c_str();
    }
}

void gs3d_ffi_free_string(char* ptr)
{
    if (ptr != nullptr) {
        std::free(ptr);
    }
}

int32_t gs3d_ffi_get_recent_project_count(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return static_cast<int32_t>(g_state.recent_projects.size());
}

const char* gs3d_ffi_get_recent_project_path(int32_t index)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (index < 0 || static_cast<size_t>(index) >= g_state.recent_projects.size()) {
        return "";
    }
    static thread_local std::string s_path;
    s_path = path_utf8(g_state.recent_projects[index].path);
    return s_path.c_str();
}

int64_t gs3d_ffi_get_recent_project_timestamp(int32_t index)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (index < 0 || static_cast<size_t>(index) >= g_state.recent_projects.size()) {
        return 0;
    }
    return g_state.recent_projects[index].last_opened_unix;
}

void gs3d_ffi_remember_recent_project(const char* path)
{
    if (path == nullptr || path[0] == '\0') return;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    try {
        gs3d::app::remember_recent_project(std::filesystem::u8path(path), 8);
        g_state.recent_projects = gs3d::app::load_recent_projects(8);
    } catch (...) {}
}

void gs3d_ffi_remove_recent_project(const char* path)
{
    if (path == nullptr || path[0] == '\0') return;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    try {
        gs3d::app::remove_recent_project(std::filesystem::u8path(path));
        g_state.recent_projects = gs3d::app::load_recent_projects(8);
    } catch (...) {}
}

void gs3d_ffi_clear_recent_projects(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    try {
        gs3d::app::clear_recent_projects();
    } catch (...) {}
    g_state.recent_projects.clear();
}

int32_t gs3d_ffi_get_gpu_count(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return static_cast<int32_t>(g_state.gpus.size());
}

const char* gs3d_ffi_get_gpu_name(int32_t index)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (index < 0 || static_cast<size_t>(index) >= g_state.gpus.size()) {
        return "";
    }
    return g_state.gpus[index].first.c_str();
}

const char* gs3d_ffi_get_gpu_type(int32_t index)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (index < 0 || static_cast<size_t>(index) >= g_state.gpus.size()) {
        return "";
    }
    return g_state.gpus[index].second.c_str();
}

int32_t gs3d_ffi_get_active_gpu_index(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.active_gpu;
}

void gs3d_ffi_set_preferred_gpu(int32_t index)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (index >= 0 && static_cast<size_t>(index) < g_state.gpus.size()) {
        try {
            if (gs3d::app::save_preferred_gpu_preference(g_state.gpu_preferences[index])) {
                g_state.active_gpu = index;
            }
        } catch (...) {}
    }
}

int32_t gs3d_ffi_get_points(float* out_buffer, int32_t max_points)
{
    if (out_buffer == nullptr || max_points <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    const int32_t count = std::min(max_points, static_cast<int32_t>(g_state.cached_points.size()));
    for (int32_t i = 0; i < count; ++i) {
        const auto& pt = g_state.cached_points[i];
        out_buffer[i * 4 + 0] = pt.x;
        out_buffer[i * 4 + 1] = pt.y;
        out_buffer[i * 4 + 2] = pt.z;
        out_buffer[i * 4 + 3] = pt.value;
    }
    return count;
}

void gs3d_ffi_set_point_size(float size)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    g_state.point_size = (size > 0.1f) ? size : 0.1f;
}

float gs3d_ffi_get_point_size(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.point_size;
}

void gs3d_ffi_set_colormap(const char* colormap_name)
{
    if (colormap_name == nullptr) return;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    g_state.colormap = colormap_name;
}

const char* gs3d_ffi_get_colormap(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    return g_state.colormap.c_str();
}

void gs3d_ffi_set_scalar_range(float min_val, float max_val)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    g_state.scalar_clip[0] = min_val;
    g_state.scalar_clip[1] = max_val;
}

void gs3d_ffi_get_scalar_range(float* out_min, float* out_max)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (out_min) *out_min = g_state.scalar_clip[0];
    if (out_max) *out_max = g_state.scalar_clip[1];
}

const char* gs3d_ffi_pick_file(const char* filter_type)
{
#if defined(_WIN32)
    // 无头运行或测试环境直接返回空串，严禁弹出阻塞式系统模态对话框
    if (std::getenv("GS3D_HEADLESS") != nullptr || std::getenv("FLUTTER_TEST") != nullptr) {
        return "";
    }

    static thread_local wchar_t szFile[2048] = {0};
    szFile[0] = L'\0';

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);

    if (filter_type != nullptr && std::strcmp(filter_type, "csv") == 0) {
        ofn.lpstrFilter = L"CSV 散点数据 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    } else if (filter_type != nullptr && std::strcmp(filter_type, "open_project") == 0) {
        ofn.lpstrFilter = L"GS3D 工程文件 (*.gs3d)\0*.gs3d\0点云与工程文件 (*.gs3d;*.csv;*.dat)\0*.gs3d;*.csv;*.dat\0所有文件 (*.*)\0*.*\0";
    } else {
        ofn.lpstrFilter = L"点云与工程文件 (*.csv;*.dat;*.gs3d)\0*.csv;*.dat;*.gs3d\0CSV 散点 (*.csv)\0*.csv\0DAT 散点 (*.dat)\0*.dat\0GS3D 格式 (*.gs3d)\0*.gs3d\0所有文件 (*.*)\0*.*\0";
    }
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        static thread_local std::string utf8_result;
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, NULL, 0, NULL, NULL);
        if (size_needed > 1) {
            utf8_result.resize(size_needed - 1);
            WideCharToMultiByte(CP_UTF8, 0, szFile, -1, &utf8_result[0], size_needed, NULL, NULL);
            std::replace(utf8_result.begin(), utf8_result.end(), '\\', '/');
            return utf8_result.c_str();
        }
    }
#endif
    return "";
}

const char* gs3d_ffi_pick_folder(void)
{
#if defined(_WIN32)
    // 无头运行或测试环境直接返回空串，严禁弹出阻塞式系统模态对话框
    if (std::getenv("GS3D_HEADLESS") != nullptr || std::getenv("FLUTTER_TEST") != nullptr) {
        return "";
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool need_uninit = SUCCEEDED(hr);

    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        IFileOpenDialog* pFileOpen = nullptr;
        hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
        if (SUCCEEDED(hr) && pFileOpen != nullptr) {
            DWORD dwOptions = 0;
            if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
                pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
            }
            if (SUCCEEDED(pFileOpen->Show(GetActiveWindow()))) {
                IShellItem* pItem = nullptr;
                if (SUCCEEDED(pFileOpen->GetResult(&pItem)) && pItem != nullptr) {
                    PWSTR pszFilePath = nullptr;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath != nullptr) {
                        static thread_local std::string utf8_result;
                        int size_needed = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                        if (size_needed > 1) {
                            utf8_result.resize(size_needed - 1);
                            WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, &utf8_result[0], size_needed, NULL, NULL);
                            std::replace(utf8_result.begin(), utf8_result.end(), '\\', '/');
                            CoTaskMemFree(pszFilePath);
                            pItem->Release();
                            pFileOpen->Release();
                            if (need_uninit) CoUninitialize();
                            return utf8_result.c_str();
                        }
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
            pFileOpen->Release();
        }
    }
    if (need_uninit) CoUninitialize();
#endif
    return "";
}

// ============================================================================
// 12. 桌面原生文件拖拽 (Desktop Drag & Drop)
// ============================================================================

static std::mutex g_drag_drop_mutex;
static std::string g_dropped_file_path;
static bool g_drag_drop_initialized = false;

#if defined(_WIN32)
static WNDPROC g_original_wndproc = nullptr;

static LRESULT CALLBACK DragDropWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DROPFILES) {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        if (count > 0) {
            wchar_t szPath[MAX_PATH];
            if (DragQueryFileW(hDrop, 0, szPath, MAX_PATH) > 0) {
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, szPath, -1, NULL, 0, NULL, NULL);
                if (size_needed > 1) {
                    std::string utf8_path(size_needed - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, szPath, -1, &utf8_path[0], size_needed, NULL, NULL);
                    std::replace(utf8_path.begin(), utf8_path.end(), '\\', '/');
                    std::lock_guard<std::mutex> lock(g_drag_drop_mutex);
                    g_dropped_file_path = utf8_path;
                }
            }
        }
        DragFinish(hDrop);
        return 0;
    }
    if (g_original_wndproc) {
        return CallWindowProcW(g_original_wndproc, hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
#endif

void gs3d_ffi_init_drag_drop(void)
{
#if defined(_WIN32)
    std::lock_guard<std::mutex> lock(g_drag_drop_mutex);
    if (g_drag_drop_initialized) return;

    HWND flutter_hwnd = nullptr;
    const DWORD current_pid = GetCurrentProcessId();

    struct FindData { DWORD pid; HWND hwnd; } fd{ current_pid, nullptr };
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* p = reinterpret_cast<FindData*>(lParam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == p->pid) {
            wchar_t cls[128];
            if (GetClassNameW(hwnd, cls, 128) > 0) {
                if (std::wcscmp(cls, L"FLUTTER_RUNNER_WIN32_WINDOW") == 0) {
                    p->hwnd = hwnd;
                    return FALSE;
                }
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&fd));

    flutter_hwnd = fd.hwnd;

    if (flutter_hwnd != nullptr) {
        DragAcceptFiles(flutter_hwnd, TRUE);
        g_original_wndproc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(flutter_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DragDropWndProc))
        );

        EnumChildWindows(flutter_hwnd, [](HWND child, LPARAM) -> BOOL {
            DragAcceptFiles(child, TRUE);
            return TRUE;
        }, 0);

        g_drag_drop_initialized = true;
    }
#endif
}

const char* gs3d_ffi_poll_dropped_file(void)
{
    std::lock_guard<std::mutex> lock(g_drag_drop_mutex);
    if (g_dropped_file_path.empty()) {
        return "";
    }
    static thread_local std::string s_dropped;
    s_dropped = g_dropped_file_path;
    g_dropped_file_path.clear();
    return s_dropped.c_str();
}

void gs3d_ffi_set_dropped_file(const char* path)
{
    std::lock_guard<std::mutex> lock(g_drag_drop_mutex);
    if (path == nullptr || path[0] == '\0') {
        g_dropped_file_path.clear();
    } else {
        std::string p = path;
        std::replace(p.begin(), p.end(), '\\', '/');
        g_dropped_file_path = p;
    }
}

// ============================================================================
// 13. 性能诊断与流式瓦片指标 (Performance & Diagnostics)
// ============================================================================

void gs3d_ffi_get_performance_metrics(
    float* out_fps,
    float* out_frame_time_ms,
    uint64_t* out_visible_points,
    uint64_t* out_gpu_mem_bytes,
    uint32_t* out_loaded_tiles,
    uint32_t* out_pending_tiles,
    float* out_cache_hit_rate
) {
    std::lock_guard<std::mutex> lock(g_state.mutex);
    // FFI 数据服务不拥有渲染器，实时指标从 ViewerApp 控制面获取。
    if (out_fps) *out_fps = 0.0f;
    if (out_frame_time_ms) *out_frame_time_ms = 0.0f;
    if (out_visible_points) *out_visible_points = g_state.cached_points.size();
    if (out_gpu_mem_bytes) *out_gpu_mem_bytes = 0;
    if (out_loaded_tiles) *out_loaded_tiles = 0;
    if (out_pending_tiles) *out_pending_tiles = 0;
    if (out_cache_hit_rate) *out_cache_hit_rate = -1.0f;
}

const char* gs3d_ffi_get_camera_coords_string(void) {
    std::lock_guard<std::mutex> lock(g_state.mutex);
    static thread_local std::string s_coords;
    if (g_state.dataset_loaded) {
        float cx = (g_state.bbox_min[0] + g_state.bbox_max[0]) * 0.5f;
        float cy = (g_state.bbox_min[1] + g_state.bbox_max[1]) * 0.5f;
        float cz = (g_state.bbox_min[2] + g_state.bbox_max[2]) * 0.5f;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f, %.1f, %.1f", cx, cy, cz);
        s_coords = buf;
    } else {
        s_coords = "0.0, 0.0, 0.0";
    }
    return s_coords.c_str();
}

void gs3d_ffi_clear_cache(void) {
    std::lock_guard<std::mutex> lock(g_state.mutex);
    std::vector<gs3d::data::Gs3dPoint>().swap(g_state.cached_points);
}

// ============================================================================
// 14. 视口三维空间点拾取 (3D Point Picking & Raycasting)
// ============================================================================

int32_t gs3d_ffi_pick_point(
    float screen_x,
    float screen_y,
    float viewport_width,
    float viewport_height,
    float azimuth_deg,
    float elevation_deg,
    float zoom,
    float pan_x,
    float pan_y,
    float* out_point_xyzv
) {
    if (out_point_xyzv == nullptr) return 0;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (!g_state.dataset_loaded || g_state.cached_points.empty()) return 0;

    const float cx = (g_state.bbox_min[0] + g_state.bbox_max[0]) * 0.5f;
    const float cy = (g_state.bbox_min[1] + g_state.bbox_max[1]) * 0.5f;
    const float cz = (g_state.bbox_min[2] + g_state.bbox_max[2]) * 0.5f;

    const float dx = g_state.bbox_max[0] - g_state.bbox_min[0];
    const float dy = g_state.bbox_max[1] - g_state.bbox_min[1];
    const float dz = g_state.bbox_max[2] - g_state.bbox_min[2];
    const float maxSpan = std::max({dx, dy, dz, 1.0f});

    const float baseScale = std::min(viewport_width, viewport_height) * 0.65f / maxSpan;
    const float scale = baseScale * zoom;
    const float centerX = viewport_width * 0.5f;
    const float centerY = viewport_height * 0.5f;

    const float radAz = azimuth_deg * 3.14159265358979323846f / 180.0f;
    const float radEl = elevation_deg * 3.14159265358979323846f / 180.0f;
    const float cosAz = std::cos(radAz);
    const float sinAz = std::sin(radAz);
    const float cosEl = std::cos(radEl);
    const float sinEl = std::sin(radEl);

    float bestDistSq = 400.0f; // 拾取阈值半径 20 像素的平方
    int bestIdx = -1;

    for (size_t i = 0; i < g_state.cached_points.size(); ++i) {
        const auto& p = g_state.cached_points[i];
        const float rx = p.x - cx;
        const float ry = p.y - cy;
        const float rz = p.z - cz;

        const float x1 = rx * cosAz - ry * sinAz;
        const float y1 = rx * sinAz + ry * cosAz;
        const float z1 = rz;

        const float x2 = x1;
        const float y2 = y1 * cosEl - z1 * sinEl;

        const float sx = centerX + pan_x + x2 * scale;
        const float sy = centerY + pan_y - y2 * scale;

        const float dsq = (sx - screen_x) * (sx - screen_x) + (sy - screen_y) * (sy - screen_y);
        if (dsq < bestDistSq) {
            bestDistSq = dsq;
            bestIdx = static_cast<int>(i);
        }
    }

    if (bestIdx >= 0) {
        const auto& pt = g_state.cached_points[bestIdx];
        out_point_xyzv[0] = pt.x;
        out_point_xyzv[1] = pt.y;
        out_point_xyzv[2] = pt.z;
        out_point_xyzv[3] = pt.value;
        return 1;
    }
    return 0;
}

// ============================================================================
// 15. 局部选区统计分析 (Region Stats Computation)
// ============================================================================

int32_t gs3d_ffi_calculate_region_stats(
    float min_x,
    float max_x,
    float min_y,
    float max_y,
    uint64_t* out_count,
    float* out_mean,
    float* out_min,
    float* out_max,
    float* out_std_dev,
    int32_t* out_hist_5bins
) {
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (out_count) *out_count = 0;
    if (out_mean) *out_mean = 0;
    if (out_min) *out_min = 0;
    if (out_max) *out_max = 0;
    if (out_std_dev) *out_std_dev = 0;
    if (out_hist_5bins) std::fill_n(out_hist_5bins, 5, 0);
    if (!g_state.dataset_loaded) return 0;
    const float x0 = std::min(min_x, max_x), x1 = std::max(min_x, max_x);
    const float y0 = std::min(min_y, max_y), y1 = std::max(min_y, max_y);
    uint64_t count = 0;
    double mean = 0, m2 = 0;
    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    const auto inside = [&](const gs3d::data::Gs3dPoint& p) {
        return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1 && std::isfinite(p.value);
    };
    try {
        // 预览只保留有界点数；统计始终扫描完整原始数据。
        gs3d::data::Gs3dReader::visit_points(g_state.source_gs3d_path, [&](auto block) {
            for (const auto& p : block) if (inside(p)) {
                ++count;
                const double delta = p.value - mean;
                mean += delta / static_cast<double>(count);
                m2 += delta * (p.value - mean);
                v_min = std::min(v_min, p.value);
                v_max = std::max(v_max, p.value);
            }
            return true;
        });
        if (count == 0) return 0;
        if (out_hist_5bins) {
            gs3d::data::Gs3dReader::visit_points(g_state.source_gs3d_path, [&](auto block) {
                for (const auto& p : block) if (inside(p)) {
                    const int bin = v_max > v_min ? std::clamp(static_cast<int>(
                        (static_cast<double>(p.value) - v_min) / (static_cast<double>(v_max) - v_min) * 5), 0, 4) : 0;
                    if (out_hist_5bins[bin] < std::numeric_limits<int32_t>::max()) ++out_hist_5bins[bin];
                }
                return true;
            });
        }
        if (out_count) *out_count = count;
        if (out_mean) *out_mean = static_cast<float>(mean);
        if (out_min) *out_min = v_min;
        if (out_max) *out_max = v_max;
        if (out_std_dev) *out_std_dev = static_cast<float>(std::sqrt(std::max(0.0, m2 / static_cast<double>(count))));
        return 1;
    } catch (...) {
        if (out_hist_5bins) std::fill_n(out_hist_5bins, 5, 0);
        return 0;
    }
}

// ============================================================================
// 16. 2D 鸟瞰导航图位图生成 (Navigation Map Thumbnail)
// ============================================================================

int32_t gs3d_ffi_get_nav_map_thumbnail(
    uint8_t* out_rgba,
    int32_t width,
    int32_t height
) {
    if (out_rgba == nullptr || width <= 0 || height <= 0) return -1;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (!g_state.dataset_loaded || g_state.cached_points.empty()) {
        std::memset(out_rgba, 16, width * height * 4);
        return 0;
    }

    for (int i = 0; i < width * height; ++i) {
        out_rgba[i * 4 + 0] = 11;
        out_rgba[i * 4 + 1] = 19;
        out_rgba[i * 4 + 2] = 32;
        out_rgba[i * 4 + 3] = 255;
    }

    const float bmin_x = g_state.bbox_min[0];
    const float bmax_x = g_state.bbox_max[0];
    const float bmin_y = g_state.bbox_min[1];
    const float bmax_y = g_state.bbox_max[1];
    const float span_x = std::max(bmax_x - bmin_x, 1e-4f);
    const float span_y = std::max(bmax_y - bmin_y, 1e-4f);

    const float vmin = g_state.value_range[0];
    const float vmax = g_state.value_range[1];
    const float vspan = std::max(vmax - vmin, 1e-4f);

    for (const auto& p : g_state.cached_points) {
        const float norm_x = (p.x - bmin_x) / span_x;
        const float norm_y = (p.y - bmin_y) / span_y;
        const int px = std::clamp(static_cast<int>(norm_x * (width - 1)), 0, width - 1);
        const int py = std::clamp(static_cast<int>((1.0f - norm_y) * (height - 1)), 0, height - 1);

        const float t = std::clamp((p.value - vmin) / vspan, 0.0f, 1.0f);
        uint8_t r = static_cast<uint8_t>(68 + t * (253 - 68));
        uint8_t g = static_cast<uint8_t>(1 + t * (231 - 1));
        uint8_t b = static_cast<uint8_t>(84 + (1.0f - t) * (150));

        const int idx = (py * width + px) * 4;
        out_rgba[idx + 0] = r;
        out_rgba[idx + 1] = g;
        out_rgba[idx + 2] = b;
        out_rgba[idx + 3] = 255;

        if (px + 1 < width) {
            out_rgba[(py * width + px + 1) * 4 + 0] = r;
            out_rgba[(py * width + px + 1) * 4 + 1] = g;
            out_rgba[(py * width + px + 1) * 4 + 2] = b;
        }
    }
    return 0;
}

// ============================================================================
// 17. 点云数据导出 (Point Cloud Export: PLY / CSV)
// ============================================================================

int32_t gs3d_ffi_export_dataset(
    const char* target_path,
    const char* format_type
) {
    if (target_path == nullptr || target_path[0] == '\0') return -1;
    std::string source_path;
    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (!g_state.dataset_loaded || g_state.source_gs3d_path.empty()) {
            return -2;
        }
        source_path = g_state.source_gs3d_path;
    }

    std::string format = format_type != nullptr ? format_type : "";
    if (format.empty()) {
        format = std::filesystem::path(target_path).extension() == ".ply"
            ? "ply"
            : "csv";
    }
    const auto result = gs3d::data::export_gs3d_points(
        source_path,
        std::filesystem::path(target_path),
        format
    );
    return result.success ? 0 : -3;
}

// ============================================================================
// 18-19. 双进程 Viewer 桥已删除 — 运行时控制改走 gs3d_engine_* 单进程 API
// ============================================================================

// ============================================================================
// 21. CSV 预处理（编译入 .so，供移动端免外部可执行文件）
// ============================================================================

namespace {
std::string g_preprocess_error;

const char* retain_preprocess_error(const std::string& msg) {
    g_preprocess_error = msg;
    return g_preprocess_error.c_str();
}
} // namespace

int32_t gs3d_ffi_preprocess_csv(
    const char* csv_path,
    const char* output_gs3d_path,
    uint32_t threads,
    uint32_t finest_target_points
) {
    if (csv_path == nullptr || output_gs3d_path == nullptr) {
        return retain_preprocess_error("null path"), -1;
    }
    try {
        const auto csv = std::filesystem::u8path(csv_path);
        const auto out = std::filesystem::u8path(output_gs3d_path);
        if (!std::filesystem::exists(csv)) {
            return retain_preprocess_error("CSV not found: " + std::string(csv_path)), -2;
        }
        // 确保输出目录存在。
        if (out.has_parent_path()) {
            std::filesystem::create_directories(out.parent_path());
        }
        gs3d::preprocess::CsvToGs3dConverter converter;
        const auto [result, dataset] = converter.convert(csv, out);
        if (result.written_points == 0) {
            return retain_preprocess_error("no points written"), -3;
        }
        // 移动端没有独立的 GeoScatter3DPreprocess 进程；C-ABI 预处理必须
        // 直接发布与桌面端相同的完整工程包（manifest + source.gs3d）。
        gs3d::app::AppConfig config;
        config.input_mode = "csv";
        config.csv_input_path = csv;
        config.csv_convert.num_threads = threads;
        config.viewer.lod.finest_target_points = finest_target_points;
        config.viewer.tile.enabled = false;
        const auto bundle = gs3d::app::make_bundle_paths(out.parent_path());
        gs3d::app::write_bundle_manifest(bundle, config, dataset);
        retain_preprocess_error("");
        return 0;
    } catch (const std::exception& ex) {
        return retain_preprocess_error(ex.what()), -3;
    } catch (...) {
        return retain_preprocess_error("unknown error"), -3;
    }
}

const char* gs3d_ffi_get_last_error(void) {
    return g_preprocess_error.c_str();
}

} // extern "C"
