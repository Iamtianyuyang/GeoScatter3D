#include "ffi/geoscatter3d_ffi.h"
#include "ffi/HeadlessRenderKernel.hpp"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

std::mutex g_engine_api_mutex;
std::string g_engine_global_error;

const char* retain_cstr(const std::string& value) {
    static thread_local std::string storage;
    storage = value;
    return storage.c_str();
}

std::string resolve_default_shader_dir() {
    // 优先：与 gs3d_ffi.dll 同目录的 assets/shaders（Flutter 部署布局）。
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&resolve_default_shader_dir),
            &hm) != 0) {
        const DWORD n = GetModuleFileNameA(hm, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            const auto dir = std::filesystem::path(buf).parent_path() /
                             "assets" / "shaders";
            if (std::filesystem::exists(dir / "point.vert.spv")) {
                return dir.string();
            }
        }
    }
#endif
    const char* env = std::getenv("GS3D_SHADER_DIR");
    if (env != nullptr && env[0] != '\0') return env;
    return "assets/shaders";
}

}  // namespace

struct gs3d_engine {
    gs3d::ffi::HeadlessRenderKernel kernel;
};

extern "C" {

const char* gs3d_engine_last_global_error(void) {
    std::lock_guard<std::mutex> lock(g_engine_api_mutex);
    return retain_cstr(g_engine_global_error);
}

gs3d_engine* gs3d_engine_create(const gs3d_engine_config* cfg) {
    std::lock_guard<std::mutex> lock(g_engine_api_mutex);
    g_engine_global_error.clear();
    try {
        gs3d::ffi::HeadlessRenderKernel::Config kcfg;
        if (cfg != nullptr) {
            if (cfg->viewport_width > 0 && cfg->viewport_height > 0) {
                kcfg.width = cfg->viewport_width;
                kcfg.height = cfg->viewport_height;
            }
            if (cfg->preferred_gpu_uuid != nullptr &&
                cfg->preferred_gpu_uuid[0] != '\0') {
                kcfg.preferred_gpu = cfg->preferred_gpu_uuid;
            }
            if (cfg->shader_dir != nullptr && cfg->shader_dir[0] != '\0') {
                kcfg.shader_dir = cfg->shader_dir;
            }
            kcfg.enable_validation = cfg->enable_validation != 0;
        }
        auto engine = std::make_unique<gs3d_engine>();
        const int rc = engine->kernel.create(kcfg);
        if (rc != 0) {
            g_engine_global_error = engine->kernel.last_error();
            return nullptr;
        }
        return engine.release();
    } catch (const std::exception& ex) {
        g_engine_global_error = ex.what();
        return nullptr;
    } catch (...) {
        g_engine_global_error = "unknown error in gs3d_engine_create";
        return nullptr;
    }
}

gs3d_engine* gs3d_engine_create_simple(uint32_t width, uint32_t height) {
    gs3d_engine_config cfg{};
    cfg.viewport_width = width;
    cfg.viewport_height = height;
    cfg.preferred_gpu_uuid = nullptr;
    static const std::string shader_dir = resolve_default_shader_dir();
    cfg.shader_dir = shader_dir.c_str();
    cfg.enable_validation = 0;
    return gs3d_engine_create(&cfg);
}

void gs3d_engine_destroy(gs3d_engine* e) {
    delete e;
}

int32_t gs3d_engine_load_dataset(gs3d_engine* e, const char* path) {
    if (e == nullptr || path == nullptr) return -1;
    return e->kernel.load_dataset(path);
}

int32_t gs3d_engine_unload_dataset(gs3d_engine* e) {
    if (e == nullptr) return -1;
    e->kernel.unload_dataset();
    return 0;
}

int32_t gs3d_engine_resize(gs3d_engine* e, uint32_t w, uint32_t h) {
    if (e == nullptr) return -1;
    return e->kernel.resize(w, h);
}

int32_t gs3d_engine_set_point_size(gs3d_engine* e, float size) {
    if (e == nullptr) return -1;
    e->kernel.set_point_size(size);
    return 0;
}

int32_t gs3d_engine_set_colormap_index(gs3d_engine* e, int32_t index) {
    if (e == nullptr) return -1;
    e->kernel.set_colormap_index(index);
    return 0;
}

int32_t gs3d_engine_set_point_shape(gs3d_engine* e, int32_t shape) {
    if (e == nullptr) return -1;
    e->kernel.set_point_shape(shape);
    return 0;
}

int32_t gs3d_engine_set_value_clip(
    gs3d_engine* e, int32_t enabled, float min_v, float max_v) {
    if (e == nullptr) return -1;
    e->kernel.set_value_clip(enabled != 0, min_v, max_v);
    return 0;
}

int32_t gs3d_engine_set_height_source(gs3d_engine* e, int32_t source) {
    if (e == nullptr) return -1;
    e->kernel.set_height_source(source);
    return 0;
}

int32_t gs3d_engine_set_color_source(gs3d_engine* e, int32_t source) {
    if (e == nullptr) return -1;
    e->kernel.set_color_source(source);
    return 0;
}

int32_t gs3d_engine_set_height_exaggeration(gs3d_engine* e, float mult) {
    if (e == nullptr) return -1;
    e->kernel.set_height_exaggeration(mult);
    return 0;
}

int32_t gs3d_engine_set_background(
    gs3d_engine* e, float r, float g, float b, float a) {
    if (e == nullptr) return -1;
    e->kernel.set_background(r, g, b, a);
    return 0;
}

int32_t gs3d_engine_export(
    gs3d_engine* e, const char* path, const char* format) {
    if (e == nullptr || path == nullptr) return -1;
    return e->kernel.export_dataset(path, format != nullptr ? format : "ply");
}

int32_t gs3d_engine_pick_point(
    gs3d_engine* e, float screen_x, float screen_y,
    float tolerance_px, float* out_point_xyzv) {
    if (e == nullptr) return -1;
    return e->kernel.pick_point(screen_x, screen_y, tolerance_px, out_point_xyzv);
}

int32_t gs3d_engine_gizmo_axis(gs3d_engine* e, int32_t axis) {
    if (e == nullptr) return -1;
    return e->kernel.gizmo_axis(axis);
}

int32_t gs3d_engine_clear_tile_cache(gs3d_engine* e) {
    if (e == nullptr) return -1;
    return e->kernel.clear_tile_cache();
}

int32_t gs3d_engine_read_frame_png(
    gs3d_engine* e, uint8_t* out, uint32_t max_bytes, uint32_t* out_len) {
    if (e == nullptr || out == nullptr || out_len == nullptr) return -1;
    return e->kernel.encode_last_frame_png(out, max_bytes, out_len);
}

int32_t gs3d_engine_set_camera_look_at(
    gs3d_engine* e,
    float pos_x, float pos_y, float pos_z,
    float tgt_x, float tgt_y, float tgt_z,
    float up_x, float up_y, float up_z) {
    if (e == nullptr) return -1;
    e->kernel.set_camera_look_at(
        pos_x, pos_y, pos_z, tgt_x, tgt_y, tgt_z, up_x, up_y, up_z);
    return 0;
}

int32_t gs3d_engine_reset_camera(gs3d_engine* e) {
    if (e == nullptr) return -1;
    e->kernel.reset_camera_to_bounds();
    return 0;
}

int32_t gs3d_engine_camera_input(
    gs3d_engine* e,
    float rotate_dx, float rotate_dy,
    float pan_dx, float pan_dy, float scroll_y) {
    if (e == nullptr) return -1;
    return e->kernel.camera_input(
        rotate_dx, rotate_dy, pan_dx, pan_dy, scroll_y);
}

int32_t gs3d_engine_render_rgba(
    gs3d_engine* e, uint8_t* out_rgba, uint32_t max_bytes,
    uint32_t* out_w, uint32_t* out_h) {
    if (e == nullptr) return -1;
    return e->kernel.render_rgba(out_rgba, max_bytes, out_w, out_h);
}

const char* gs3d_engine_get_snapshot_json(gs3d_engine* e) {
    if (e == nullptr) return retain_cstr("{}");
    const auto cam = e->kernel.camera_state();
    const auto ren = e->kernel.render_state();
    const auto vp = e->kernel.viewport_state();
    nlohmann::json snap;
    snap["id"] = "engine.snapshot";
    snap["ready"] = e->kernel.has_dataset();
    snap["point_count"] = e->kernel.point_count();
    snap["camera"] = {
        {"azimuth", cam.azimuth_deg},
        {"elevation", cam.elevation_deg},
        {"zoom", cam.zoom},
        {"pan", {cam.pan[0], cam.pan[1]}},
        {"position", {cam.position[0], cam.position[1], cam.position[2]}},
        {"target", {cam.target[0], cam.target[1], cam.target[2]}},
        {"distance", cam.distance},
    };
    snap["render"] = {
        {"point_size", ren.point_size},
        {"colormap_index", ren.colormap_index},
        {"point_shape", ren.point_shape},
        {"height_source", ren.height_source},
        {"color_source", ren.color_source},
        {"height_mult", ren.height_mult},
        {"value_clip", {
            {"enabled", ren.value_clip_enabled != 0},
            {"min", ren.value_clip_min},
            {"max", ren.value_clip_max},
        }},
    };
    snap["viewport"] = {
        {"width", vp.width},
        {"height", vp.height},
        {"frame_ready", vp.frame_ready},
    };
    return retain_cstr(snap.dump());
}

const char* gs3d_engine_get_diagnostics_json(gs3d_engine* e) {
    if (e == nullptr) return retain_cstr("{}");
    const auto vp = e->kernel.viewport_state();
    nlohmann::json diag;
    diag["id"] = "engine.diagnostics";
    diag["ready"] = e->kernel.has_dataset();
    diag["point_count"] = e->kernel.point_count();
    diag["last_error"] = e->kernel.last_error();
    diag["frame_ready"] = vp.frame_ready;
    diag["fps"] = vp.fps;
    diag["viewport"] = {{"width", vp.width}, {"height", vp.height}};
    return retain_cstr(diag.dump());
}

const char* gs3d_engine_last_error(gs3d_engine* e) {
    if (e == nullptr) return gs3d_engine_last_global_error();
    return retain_cstr(e->kernel.last_error());
}

uint64_t gs3d_engine_point_count(gs3d_engine* e) {
    if (e == nullptr) return 0;
    return e->kernel.point_count();
}

}  // extern "C"
