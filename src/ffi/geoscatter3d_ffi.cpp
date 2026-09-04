#include "ffi/geoscatter3d_ffi.h"

#include "app/PreprocessedBundle.hpp"
#include "app/RecentProjects.hpp"
#include "control/JsonRpc.hpp"
#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dReader.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct EngineState {
    std::mutex mutex;
    bool initialized = false;
    bool dataset_loaded = false;

    std::string dataset_path;
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
    std::vector<std::pair<std::string, std::string>> gpus = {
        {"NVIDIA GeForce RTX 5060 Laptop GPU", "独立显卡 (7.7 GB 显存)"},
        {"Intel(R) Graphics", "集成显卡 (共享内存)"}
    };
    int32_t active_gpu = 0;

    std::string last_json_response;
};

EngineState g_state;

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

} // namespace

extern "C" {

int32_t gs3d_ffi_init(void)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    g_state.initialized = true;
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
        std::filesystem::path fs_path(path);
        if (!std::filesystem::exists(fs_path)) {
            return -2;
        }

        std::filesystem::path source_gs3d = fs_path;
        std::filesystem::path lod_file;

        if (std::filesystem::is_directory(fs_path)) {
            // Bundle 目录
            const auto candidate_source = fs_path / "source.gs3d";
            if (std::filesystem::exists(candidate_source)) {
                source_gs3d = candidate_source;
            }
            const auto candidate_lod = fs_path / "source.gs3dlod";
            if (std::filesystem::exists(candidate_lod)) {
                lod_file = candidate_lod;
            }
        }

        const auto header = gs3d::data::Gs3dReader::read_header(source_gs3d);

        g_state.dataset_path = path;
        g_state.dataset_name = fs_path.stem().string();
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

        g_state.dataset_loaded = true;
        try {
            gs3d::app::remember_recent_project(path, 8);
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

    std::lock_guard<std::mutex> lock(g_state.mutex);
    try {
        const auto req = gs3d::control::parse_json_rpc_request(json_request);
        if (!req.valid) {
            g_state.last_json_response =
                gs3d::control::make_json_rpc_error(req.id, gs3d::control::kJsonRpcInvalidRequest, "Invalid JSON-RPC").dump();
            return g_state.last_json_response.c_str();
        }

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
        } else {
            g_state.last_json_response =
                gs3d::control::make_json_rpc_error(req.id, gs3d::control::kJsonRpcMethodNotFound, "Method not found").dump();
        }
        return g_state.last_json_response.c_str();
    } catch (const std::exception& e) {
        g_state.last_json_response =
            gs3d::control::make_json_rpc_error(nullptr, gs3d::control::kJsonRpcInternalError, e.what()).dump();
        return g_state.last_json_response.c_str();
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
    s_path = g_state.recent_projects[index].path.string();
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
        gs3d::app::remember_recent_project(path, 8);
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
        g_state.active_gpu = index;
    }
}

} // extern "C"
