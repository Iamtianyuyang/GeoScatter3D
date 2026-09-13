#include "ffi/geoscatter3d_ffi.h"

#include "ffi/EngineHost.hpp"
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

/*
 * 引擎句柄：唯一渲染线程由 EngineHost 持有，所有 Vulkan 调用都在该线程执行。
 * 公开 API 不向调用方暴露任何 Vulkan 类型或平台窗口句柄。
 */
struct gs3d_engine {
    gs3d::ffi::EngineHost host;
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
        // 未显式指定时回退到与 DLL 同目录解析出的资产路径。否则
        // PointPipeline 会退回相对路径 assets/shaders，一旦工作目录不是
        // 仓库根就会静默取不到着色器。
        if (kcfg.shader_dir.empty()) {
            kcfg.shader_dir = resolve_default_shader_dir();
        }
        auto engine = std::make_unique<gs3d_engine>();
        const int rc = engine->host.start(kcfg);
        if (rc != 0) {
            g_engine_global_error = engine->host.last_error();
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
    const std::string dataset_path = path;
    return e->host.invoke([&dataset_path](gs3d::ffi::HeadlessRenderKernel& k) {
        return k.load_dataset(dataset_path);
    });
}

int32_t gs3d_engine_load_sample(gs3d_engine* e) {
    if (e == nullptr) return -1;
    return e->host.invoke([](gs3d::ffi::HeadlessRenderKernel& k) {
        return k.load_sample();
    });
}

int32_t gs3d_engine_load_synthetic(gs3d_engine* e, uint64_t point_count) {
    if (e == nullptr) return -1;
    return e->host.invoke([point_count](gs3d::ffi::HeadlessRenderKernel& k) {
        return k.load_synthetic(point_count);
    });
}

int32_t gs3d_engine_set_surface(
    gs3d_engine* e, const gs3d_surface_provider* provider,
    uint32_t width, uint32_t height) {
    if (e == nullptr || provider == nullptr) return -1;
    gs3d::render::VulkanSurfaceProvider surface_provider{};
    surface_provider.instance_extensions = provider->instance_extensions;
    surface_provider.instance_extension_count = provider->instance_extension_count;
    surface_provider.create_surface = provider->create_surface;
    surface_provider.user_data = provider->user_data;
    const auto captured = surface_provider;
    return e->host.invoke(
        [captured, width, height](gs3d::ffi::HeadlessRenderKernel& k) {
            return k.attach_surface(captured, width, height);
        }
    );
}

int32_t gs3d_engine_clear_surface(gs3d_engine* e) {
    if (e == nullptr) return -1;
    return e->host.invoke([](gs3d::ffi::HeadlessRenderKernel& k) {
        k.detach_surface();
        return 0;
    });
}

int32_t gs3d_engine_present_frame(gs3d_engine* e) {
    if (e == nullptr) return -1;
    return e->host.invoke([](gs3d::ffi::HeadlessRenderKernel& k) {
        return k.present_frame();
    });
}

int32_t gs3d_engine_has_surface(gs3d_engine* e) {
    if (e == nullptr) return 0;
    return e->host.invoke([](gs3d::ffi::HeadlessRenderKernel& k) {
        return k.has_surface() ? 1 : 0;
    });
}

int32_t gs3d_engine_set_point_budget(gs3d_engine* e, uint64_t max_points) {
    if (e == nullptr) return -1;
    return e->host.invoke([max_points](gs3d::ffi::HeadlessRenderKernel& k) {
        k.set_point_budget(max_points);
        return 0;
    });
}

uint64_t gs3d_engine_source_point_count(gs3d_engine* e) {
    if (e == nullptr) return 0;
    auto count = std::make_shared<std::uint64_t>(0);
    const int rc = e->host.invoke([count](gs3d::ffi::HeadlessRenderKernel& k) {
        *count = k.lod_state().source_points;
        return 0;
    });
    return rc == 0 ? *count : 0;
}

uint64_t gs3d_engine_drawn_point_count(gs3d_engine* e) {
    if (e == nullptr) return 0;
    auto count = std::make_shared<std::uint64_t>(0);
    const int rc = e->host.invoke([count](gs3d::ffi::HeadlessRenderKernel& k) {
        *count = k.lod_state().drawn_points;
        return 0;
    });
    return rc == 0 ? *count : 0;
}

int32_t gs3d_engine_unload_dataset(gs3d_engine* e) {
    if (e == nullptr) return -1;
    return e->host.invoke([](gs3d::ffi::HeadlessRenderKernel& k) {
        k.unload_dataset();
        return 0;
    });
}

int32_t gs3d_engine_resize(gs3d_engine* e, uint32_t w, uint32_t h) {
    if (e == nullptr) return -1;
    return e->host.invoke([w, h](gs3d::ffi::HeadlessRenderKernel& k) {
        return k.resize(w, h);
    });
}

int32_t gs3d_engine_set_point_size(gs3d_engine* e, float size) {
    if (e == nullptr) return -1;
    return e->host.invoke([size](gs3d::ffi::HeadlessRenderKernel& k) {
        k.set_point_size(size);
        return 0;
    });
}

int32_t gs3d_engine_set_view_scale(gs3d_engine* e, float scale) {
    if (e == nullptr) return -1;
    return e->host.invoke([scale](gs3d::ffi::HeadlessRenderKernel& k) {
        return k.set_view_scale(scale);
    });
}

int32_t gs3d_engine_set_colormap_index(gs3d_engine* e, int32_t index) {
    if (e == nullptr) return -1;
    return e->host.invoke([index](gs3d::ffi::HeadlessRenderKernel& k) {
        k.set_colormap_index(index);
        return 0;
    });
}

int32_t gs3d_engine_set_point_shape(gs3d_engine* e, int32_t shape) {
    if (e == nullptr) return -1;
    return e->host.invoke([shape](gs3d::ffi::HeadlessRenderKernel& k) {
        k.set_point_shape(shape);
        return 0;
    });
}

int32_t gs3d_engine_set_value_clip(
    gs3d_engine* e, int32_t enabled, float min_v, float max_v) {
    if (e == nullptr) return -1;
    return e->host.invoke(
        [enabled, min_v, max_v](gs3d::ffi::HeadlessRenderKernel& k) {
            k.set_value_clip(enabled != 0, min_v, max_v);
            return 0;
        }
    );
}

int32_t gs3d_engine_set_height_source(gs3d_engine* e, int32_t source) {
    if (e == nullptr) return -1;
    return e->host.invoke([source](gs3d::ffi::HeadlessRenderKernel& k) {
        k.set_height_source(source);
        return 0;
    });
}

int32_t gs3d_engine_set_color_source(gs3d_engine* e, int32_t source) {
    if (e == nullptr) return -1;
    return e->host.invoke([source](gs3d::ffi::HeadlessRenderKernel& k) {
        k.set_color_source(source);
        return 0;
    });
}

int32_t gs3d_engine_set_height_exaggeration(gs3d_engine* e, float mult) {
    if (e == nullptr) return -1;
    return e->host.invoke([mult](gs3d::ffi::HeadlessRenderKernel& k) {
        k.set_height_exaggeration(mult);
        return 0;
    });
}

int32_t gs3d_engine_set_background(
    gs3d_engine* e, float r, float g, float b, float a) {
    if (e == nullptr) return -1;
    return e->host.invoke([r, g, b, a](gs3d::ffi::HeadlessRenderKernel& k) {
        k.set_background(r, g, b, a);
        return 0;
    });
}

int32_t gs3d_engine_export(
    gs3d_engine* e, const char* path, const char* format) {
    if (e == nullptr || path == nullptr) return -1;
    const std::string out_path = path;
    const std::string out_format = format != nullptr ? format : "ply";
    return e->host.invoke(
        [&out_path, &out_format](gs3d::ffi::HeadlessRenderKernel& k) {
            return k.export_dataset(out_path, out_format);
        }
    );
}

int32_t gs3d_engine_pick_point(
    gs3d_engine* e, float screen_x, float screen_y,
    float tolerance_px, float* out_point_xyzv) {
    if (e == nullptr) return -1;
    return e->host.invoke(
        [screen_x, screen_y, tolerance_px, out_point_xyzv](
            gs3d::ffi::HeadlessRenderKernel& k
        ) {
            return k.pick_point(screen_x, screen_y, tolerance_px, out_point_xyzv);
        }
    );
}

int32_t gs3d_engine_gizmo_axis(gs3d_engine* e, int32_t axis) {
    if (e == nullptr) return -1;
    return e->host.invoke([axis](gs3d::ffi::HeadlessRenderKernel& k) {
        return k.gizmo_axis(axis);
    });
}

int32_t gs3d_engine_clear_tile_cache(gs3d_engine* e) {
    if (e == nullptr) return -1;
    return e->host.invoke([](gs3d::ffi::HeadlessRenderKernel& k) {
        return k.clear_tile_cache();
    });
}

int32_t gs3d_engine_read_frame_png(
    gs3d_engine* e, uint8_t* out, uint32_t max_bytes, uint32_t* out_len) {
    if (e == nullptr || out == nullptr || out_len == nullptr) return -1;
    return e->host.invoke([out, max_bytes, out_len](
                              gs3d::ffi::HeadlessRenderKernel& k
                          ) {
        return k.encode_last_frame_png(out, max_bytes, out_len);
    });
}

int32_t gs3d_engine_set_camera_look_at(
    gs3d_engine* e,
    float pos_x, float pos_y, float pos_z,
    float tgt_x, float tgt_y, float tgt_z,
    float up_x, float up_y, float up_z) {
    if (e == nullptr) return -1;
    return e->host.invoke(
        [=](gs3d::ffi::HeadlessRenderKernel& k) {
            k.set_camera_look_at(
                pos_x, pos_y, pos_z, tgt_x, tgt_y, tgt_z, up_x, up_y, up_z
            );
            return 0;
        }
    );
}

int32_t gs3d_engine_reset_camera(gs3d_engine* e) {
    if (e == nullptr) return -1;
    return e->host.invoke([](gs3d::ffi::HeadlessRenderKernel& k) {
        k.reset_camera_to_bounds();
        return 0;
    });
}

int32_t gs3d_engine_camera_input(
    gs3d_engine* e,
    float rotate_dx, float rotate_dy,
    float pan_dx, float pan_dy, float scroll_y) {
    if (e == nullptr) return -1;
    return e->host.invoke(
        [=](gs3d::ffi::HeadlessRenderKernel& k) {
            return k.camera_input(rotate_dx, rotate_dy, pan_dx, pan_dy, scroll_y);
        }
    );
}

int32_t gs3d_engine_render_rgba(
    gs3d_engine* e, uint8_t* out_rgba, uint32_t max_bytes,
    uint32_t* out_w, uint32_t* out_h) {
    if (e == nullptr) return -1;
    return e->host.invoke([out_rgba, max_bytes, out_w, out_h](
                              gs3d::ffi::HeadlessRenderKernel& k
                          ) {
        return k.render_rgba(out_rgba, max_bytes, out_w, out_h);
    });
}

int32_t gs3d_engine_request_frame(gs3d_engine* e) {
    if (e == nullptr) return -1;
    return e->host.request_frame();
}

int32_t gs3d_engine_read_frame_rgba(
    gs3d_engine* e, uint8_t* out_rgba, uint32_t max_bytes,
    uint32_t* out_w, uint32_t* out_h,
    uint32_t* out_stride, uint64_t* out_sequence) {
    if (e == nullptr) return -1;
    // 注意：out_rgba 可为 NULL（max_bytes=0），此时只回填实际宽高并返回 -1，
    // 让调用方据此扩容 —— 首次读取时调用方还不知道帧尺寸。
    return e->host.read_frame_rgba(
        out_rgba, max_bytes, out_w, out_h, out_stride, out_sequence
    );
}

uint64_t gs3d_engine_frame_sequence(gs3d_engine* e) {
    if (e == nullptr) return 0;
    return e->host.frame_sequence();
}

double gs3d_engine_last_render_ms(gs3d_engine* e) {
    if (e == nullptr) return 0.0;
    return e->host.last_render_ms();
}

const char* gs3d_engine_get_snapshot_json(gs3d_engine* e) {
    if (e == nullptr) return retain_cstr("{}");
    // 结果通过 shared_ptr 在渲染线程写入；invoke 的 future 已建立先行发生关系。
    auto payload = std::make_shared<std::string>();
    e->host.invoke([payload](gs3d::ffi::HeadlessRenderKernel& k) {
        const auto cam = k.camera_state();
        const auto ren = k.render_state();
        const auto vp = k.viewport_state();
        const auto lod = k.lod_state();
        nlohmann::json snap;
        snap["id"] = "engine.snapshot";
        snap["ready"] = k.has_dataset();
        snap["point_count"] = lod.source_points;
        snap["drawn_point_count"] = lod.drawn_points;
        snap["lod_level"] = lod.level_index;
        snap["lod_level_count"] = lod.level_count;
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
        *payload = snap.dump();
        return 0;
    });
    if (payload->empty()) return retain_cstr("{}");
    return retain_cstr(*payload);
}

const char* gs3d_engine_get_diagnostics_json(gs3d_engine* e) {
    if (e == nullptr) return retain_cstr("{}");
    auto payload = std::make_shared<std::string>();
    const int rc = e->host.invoke([e, payload](gs3d::ffi::HeadlessRenderKernel& k) {
        const auto vp = k.viewport_state();
        const auto gpu = k.gpu_diagnostics();
        const auto lod = k.lod_state();
        nlohmann::json diag;
        diag["id"] = "engine.diagnostics";
        diag["ready"] = k.has_dataset();
        diag["point_count"] = lod.source_points;
        diag["last_error"] = k.last_error();
        diag["frame_ready"] = vp.frame_ready;
        diag["fps"] = vp.fps;
        diag["viewport"] = {{"width", vp.width}, {"height", vp.height}};
        diag["render_thread"] = true;
        diag["frame_sequence"] = e->host.frame_sequence();
        diag["frame_time_ms"] = e->host.last_render_ms();
        diag["lod"] = {
            {"source_points", lod.source_points},
            {"drawn_points", lod.drawn_points},
            {"gpu_points", lod.gpu_points},
            {"budget", lod.budget},
            {"level_index", lod.level_index},
            {"level_count", lod.level_count},
            {"estimated_spacing", lod.estimated_spacing},
            {"build_ms", lod.build_ms},
            {"upload_ms", lod.upload_ms},
            {"automatic_budget", lod.automatic_budget},
            {"tiles_available", lod.tiles_available},
            {"tile_count", lod.tile_count},
            {"tile_needed", lod.tile_needed},
            {"tile_resident", lod.tile_resident},
            {"tile_drawn_points", lod.tile_drawn_points},
            {"tile_uploaded_bytes_frame", lod.tile_uploaded_bytes_frame},
            {"tile_clip_active", lod.tile_clip_active},
            {"tiles_fully_resident", lod.tiles_fully_resident},
            {"tile_cover_view", lod.tile_cover_view},
            {"tile_below_threshold", lod.tile_below_threshold},
            {"tile_skipped_by_coverage", lod.tile_skipped_by_coverage},
        };
        diag["gpu"] = {
            {"name", gpu.name},
            {"type", gpu.type_label},
            {"uuid", gpu.uuid},
            {"profile", gpu.profile},
            {"selection", gpu.selection},
            {"api_version", gpu.api_version},
            {"driver_version", gpu.driver_version},
            {"memory_bytes", gpu.memory_bytes},
        };
        *payload = diag.dump();
        return 0;
    });
    if (payload->empty()) {
        nlohmann::json diag;
        diag["id"] = "engine.diagnostics";
        diag["ready"] = false;
        diag["render_thread"] = true;
        diag["frame_sequence"] = e->host.frame_sequence();
        diag["last_error"] = rc < 0 ? e->host.last_error() : std::string();
        *payload = diag.dump();
    }
    return retain_cstr(*payload);
}

const char* gs3d_engine_last_error(gs3d_engine* e) {
    if (e == nullptr) return gs3d_engine_last_global_error();
    auto kernel_error = std::make_shared<std::string>();
    const int rc = e->host.invoke([kernel_error](gs3d::ffi::HeadlessRenderKernel& k) {
        *kernel_error = k.last_error();
        return 0;
    });
    // 宿主（渲染线程）记录的错误最新：渲染线程内的失败不一定回到内核。
    const std::string host_error = e->host.last_error();
    if (rc == 0 && !host_error.empty()) return retain_cstr(host_error);
    if (rc == 0 && !kernel_error->empty()) return retain_cstr(*kernel_error);
    return retain_cstr(host_error);
}

uint64_t gs3d_engine_point_count(gs3d_engine* e) {
    if (e == nullptr) return 0;
    auto count = std::make_shared<std::uint64_t>(0);
    const int rc = e->host.invoke([count](gs3d::ffi::HeadlessRenderKernel& k) {
        *count = k.point_count();
        return 0;
    });
    return rc == 0 ? *count : 0;
}

const char* gs3d_engine_version(void) {
    return "0.3.0-engine";
}

int32_t gs3d_engine_preprocess_csv(
    const char* csv_path,
    const char* output_gs3d_path,
    uint32_t threads,
    uint32_t finest_target_points) {
    return gs3d_ffi_preprocess_csv(
        csv_path, output_gs3d_path, threads, finest_target_points
    );
}

}  // extern "C"
