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
#include <fstream>
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

    std::vector<gs3d::data::Gs3dPoint> cached_points;
    float point_size = 3.0f;
    std::string colormap = "viridis";
    float scalar_clip[2] = {0.0f, 1.0f};

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
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(fs_path)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".gs3d") {
                        source_gs3d = entry.path();
                        break;
                    }
                }
            }
            const auto candidate_lod = fs_path / "source.gs3dlod";
            if (std::filesystem::exists(candidate_lod)) {
                lod_file = candidate_lod;
            } else {
                const auto candidate_lod2 = fs_path / "lod.gs3dlod";
                if (std::filesystem::exists(candidate_lod2)) {
                    lod_file = candidate_lod2;
                } else {
                    for (const auto& entry : std::filesystem::directory_iterator(fs_path)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".gs3dlod") {
                            lod_file = entry.path();
                            break;
                        }
                    }
                }
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

        try {
            const auto res = gs3d::data::Gs3dReader::read_all(source_gs3d);
            g_state.cached_points = std::move(res.points);
            g_state.scalar_clip[0] = header.value_min;
            g_state.scalar_clip[1] = header.value_max;
        } catch (...) {
            g_state.cached_points.clear();
            std::ifstream in(source_gs3d, std::ios::binary);
            if (in.is_open()) {
                in.seekg(header.point_data_offset);
                const size_t preview_count = std::min<uint64_t>(header.point_count, 65536);
                g_state.cached_points.resize(preview_count);
                in.read(reinterpret_cast<char*>(g_state.cached_points.data()), preview_count * sizeof(gs3d::data::Gs3dPoint));
            }
            g_state.scalar_clip[0] = header.value_min;
            g_state.scalar_clip[1] = header.value_max;
        }

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

void gs3d_ffi_remove_recent_project(const char* path)
{
    if (path == nullptr || path[0] == '\0') return;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    try {
        gs3d::app::remove_recent_project(path);
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

} // extern "C"
