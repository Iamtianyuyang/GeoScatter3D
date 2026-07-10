#include "app/ViewerApp.hpp"
#include "app/ViewerAppGpuPick.hpp"
#include "app/ViewerAppInternal.hpp"
#include "app/ViewerAppRunState.hpp"
#include "app/ViewerAppTileStreaming.hpp"

#include "camera/Camera.hpp"
#include "render/OffscreenFramebuffer.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/TileSelection.hpp"
#include "render/ViewportManager.hpp"
#include "util/Stopwatch.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace gs3d::app {

namespace {

void fill_push_constants(
    gs3d::render::PointPushConstants& push,
    const gs3d::camera::Camera& camera
) {
    const auto mvp = camera.view_projection_matrix();
    std::copy(mvp.m.begin(), mvp.m.end(), push.mvp);
    // flags (colormap, value_clip, spatial_clip) are managed on the
    // template push object and copied per-viewport — do NOT reset here.
}

} // namespace

/*
 * pre_pass 的逐视口离屏渲染：每个可见视口按序绘制 LOD 安全网(最粗层,
 * 永不裁剪,保证无空洞)、当前 LOD 层(或全量点云)、已驻留的全分辨率
 * tile 叠加；随后录制该视口的 GPU pick 请求和(可选的) pick 调试 dump。
 */
void ViewerApp::record_viewport_passes(
    VkCommandBuffer cmd,
    const ViewerAppViewportDrawContext& ctx
) {
    bool pick_debug_dump_recorded_this_frame = false;
    for (const int viewport_index :
         ctx.visible_viewports) {
        auto& framebuffer =
            ctx.viewport_manager.framebuffer(
                viewport_index
            );
        const auto& viewport_camera =
            ctx.viewport_manager.camera(
                viewport_index
            );
        const auto request_index =
            static_cast<std::size_t>(viewport_index);
        const auto& pick_request =
            ctx.pick.requests[request_index];
        const auto& selected_tile_ids =
            ctx.tile_stream.viewport_tile_ids[request_index];
        bool any_tile_resident = false;
        bool all_tiles_resident =
            !selected_tile_ids.empty();
        std::vector<std::uint64_t> resident_tile_ids;
        resident_tile_ids.reserve(
            selected_tile_ids.size()
        );
        for (const auto tile_id : selected_tile_ids) {
            const bool resident =
                ctx.tile_gpu_cloud &&
                ctx.tile_gpu_cloud
                    ->has_resident_tile(tile_id);
            any_tile_resident =
                any_tile_resident || resident;
            all_tiles_resident =
                all_tiles_resident && resident;
            if (resident) {
                resident_tile_ids.push_back(tile_id);
            }
        }
        const bool tile_will_render =
            (config_.interactive_display_mode !=
                 gs3d::app::InteractiveDisplayMode::
                     AllowCoarseLOD ||
             !ctx.interacting) &&
            any_tile_resident;
        framebuffer.render(
            cmd,
            [&](VkCommandBuffer c) {
        // Per-viewport push: copy non-MVP fields from the view's own state,
        // then fill in the per-viewport MVP matrix.
        gs3d::render::PointPushConstants vp_push =
            request_index < ctx.viewport_pushes.size()
                ? ctx.viewport_pushes[request_index]
                : gs3d::render::PointPushConstants{};
        fill_push_constants(
            vp_push,
            viewport_camera
        );

        const VkExtent2D viewport_extent =
            framebuffer.extent();

        const auto view_index =
            static_cast<std::size_t>(
                viewport_index
            );
        /*
         * KeepStableHighQuality:交互期间继续绘制已驻留
         * 的全分辨率 tile —— 新 tile 流式本就在交互期
         * 冻结(见 update_tile_streaming 的 !interacting
         * 门控),所以这只是用当前相机继续画"交互开始前
         * 已上传好的高质量 buffer",无新上传、无中途驱逐,
         * 不空帧不闪烁。
         * AllowCoarseLOD 才在交互期关掉 tile 叠加。
         */
        gs3d::render::PointPushConstants lod_push = vp_push;
        /*
         * Clip LOD inside tile-covered areas — but ONLY
         * when every desired tile is resident, so a
         * partial clip bbox never creates black holes.
         * During incremental upload the clip stays off
         * (LOD + tiles may overdraw, but no holes).
         */
        // Stage 3 (streaming) uses the bounded GPU working set;
        // Stages 1/2 (preload / fully-resident) use the full
        // candidate set.
        const auto& desired_for_clip =
            ctx.tile_stream.gpu_required_tile_ids.empty()
                ? ctx.tile_result.tile_ids
                : ctx.tile_stream.gpu_required_tile_ids;
        bool all_desired_resident =
            !desired_for_clip.empty();
        if (all_desired_resident) {
            for (const auto tid : desired_for_clip) {
                if (!ctx.tile_gpu_cloud
                        ->has_resident_tile(tid)) {
                    all_desired_resident = false;
                    break;
                }
            }
        }
        if (tile_will_render &&
            all_desired_resident &&
            ctx.tile_stream.viewport_tile_query_boxes[view_index]
                .has_value()) {
            const auto& b =
                *ctx.tile_stream.viewport_tile_query_boxes[view_index];
            lod_push.flags |= gs3d::render::PointFlags::kSpatialClip;
            lod_push.clip_min[0] = b.min_x;
            lod_push.clip_min[1] = b.min_y;
            lod_push.clip_min[2] = b.min_z;
            // lod_push.clip_min[3] 保留 value_clip_min（可能已设置）
            lod_push.clip_max[0] = b.max_x;
            lod_push.clip_max[1] = b.max_y;
            lod_push.clip_max[2] = b.max_z;
            // lod_push.clip_max[3] 保留 value_clip_max（可能已设置）
        }

        ctx.point_pipeline.bind_for_viewport(
            c,
            viewport_extent
        );

        // --- LOD safety net: coarsest level, always drawn ---
        // spatial_clip=0 so it is never clipped
        // — guarantees no clear-colour holes.
        if (config_.lod_enabled) {
            gs3d::render::PointPushConstants safety_push =
                lod_push;
            safety_push.flags &= ~gs3d::render::PointFlags::kSpatialClip;
            ctx.point_pipeline.draw_per_tile(
                c,
                ctx.lod_gpu_cloud->lowest_detail().gpu_cloud,
                safety_push
            );
        }

        if (config_.lod_enabled) {
            ctx.point_pipeline.draw_per_tile(
                c,
                ctx.lod_gpu_cloud->gpu_cloud(ctx.lod_level_for_frame),
                lod_push
            );
        } else {
            ctx.point_pipeline.draw_per_tile(
                c,
                *ctx.full_gpu_cloud,
                lod_push
            );
        }

        if (tile_will_render) {
            for (const auto tile_id :
                 selected_tile_ids) {
                if (!ctx.tile_gpu_cloud
                        ->has_resident_tile(tile_id)) {
                    continue;
                }
                ctx.point_pipeline.draw_per_tile(
                    c,
                    ctx.tile_gpu_cloud->gpu_cloud_for_tile(tile_id),
                    vp_push
                );
            }
        }
            }
        );
        if (pick_request.valid()) {
            gs3d::util::Stopwatch issue_timer;
            ctx.gpu_pick_readback.record_request(
                cmd,
                ctx.pick.frame_slot,
                framebuffer,
                pick_request
            );
            if (ctx.benchmark_pick_enabled &&
                pick_request.benchmark_query_index >= 0) {
                const auto query_index =
                    static_cast<std::size_t>(
                        pick_request.benchmark_query_index
                    );
                if (query_index <
                    ctx.benchmark_pick_issue_cpu_ms.size()) {
                    ctx.benchmark_pick_issue_cpu_ms[query_index] =
                        issue_timer
                            .elapsed_milliseconds();
                }
                if (query_index <
                    ctx.benchmark_pick_issue_metadata.size()) {
                    auto& metadata =
                        ctx.benchmark_pick_issue_metadata[
                            query_index
                        ];
                    metadata.all_tiles_resident =
                        all_tiles_resident;
                    metadata.resident_tile_ids.clear();
                    metadata.resident_tile_ids =
                        resident_tile_ids;
                }
            }
            const bool should_dump_pick_debug =
                config_.pick_debug_dump_enabled &&
                pick_request.kind ==
                    GpuPickRequestKind::Hover &&
                !pick_debug_dump_recorded_this_frame &&
                (!config_.pick_debug_dump_once_on_hover ||
                 !ctx.pick_debug_dump_completed ||
                 ctx.pending_hover_miss_dump[viewport_index]);
            if (should_dump_pick_debug) {
                const int cursor_x = std::clamp(
                    static_cast<int>(
                        std::floor(pick_request.mouse_x)
                    ),
                    0,
                    static_cast<int>(
                        pick_request.viewport_width
                    ) - 1
                );
                const int cursor_y = std::clamp(
                    static_cast<int>(
                        std::floor(pick_request.mouse_y)
                    ),
                    0,
                    static_cast<int>(
                        pick_request.viewport_height
                    ) - 1
                );
                const std::uint32_t sample_left =
                    static_cast<std::uint32_t>(
                        std::max(
                            0,
                            cursor_x - 5
                        )
                    );
                const std::uint32_t sample_top =
                    static_cast<std::uint32_t>(
                        std::max(
                            0,
                            cursor_y - 5
                        )
                    );
                const std::uint32_t sample_right =
                    static_cast<std::uint32_t>(
                        std::min(
                            static_cast<int>(
                                pick_request.viewport_width
                            ) - 1,
                            cursor_x + 5
                        )
                    );
                const std::uint32_t sample_bottom =
                    static_cast<std::uint32_t>(
                        std::min(
                            static_cast<int>(
                                pick_request.viewport_height
                            ) - 1,
                            cursor_y + 5
                        )
                    );

                PickDebugDumpMetadata debug_metadata;
                debug_metadata.dump_index =
                    ctx.pick_debug_dump_count++;
                debug_metadata.frame_index =
                    ctx.app_frame_index;
                debug_metadata.viewport_index =
                    viewport_index;
                debug_metadata.viewport_width =
                    pick_request.viewport_width;
                debug_metadata.viewport_height =
                    pick_request.viewport_height;
                debug_metadata.mouse_x =
                    pick_request.mouse_x;
                debug_metadata.mouse_y =
                    pick_request.mouse_y;
                debug_metadata.sample_left =
                    sample_left;
                debug_metadata.sample_top =
                    sample_top;
                debug_metadata.sample_width =
                    sample_right - sample_left + 1;
                debug_metadata.sample_height =
                    sample_bottom - sample_top + 1;
                debug_metadata.active_lod_level =
                    ctx.lod_level_for_frame;
                debug_metadata.tile_overlay_rendered =
                    tile_will_render;
                debug_metadata.all_tiles_resident =
                    all_tiles_resident;
                debug_metadata.render_source =
                    tile_will_render
                        ? "tile_overlay+lod_level_" +
                              std::to_string(
                                  ctx.lod_level_for_frame
                              )
                        : "lod_level_" +
                              std::to_string(
                                  ctx.lod_level_for_frame
                              );
                debug_metadata.trigger_reason =
                    ctx.pending_hover_miss_dump[viewport_index]
                        ? "followup_after_hover_miss"
                        : "hover_frame";
                debug_metadata.selected_tile_ids =
                    selected_tile_ids;
                debug_metadata.resident_tile_ids =
                    resident_tile_ids;
                if (ctx.pick_debug_frame_dumper.record_request(
                        cmd,
                        ctx.pick.frame_slot,
                        framebuffer,
                        debug_metadata
                    )) {
                    pick_debug_dump_recorded_this_frame =
                        true;
                    ctx.pending_hover_miss_dump[viewport_index] =
                        false;
                    if (config_
                            .pick_debug_dump_once_on_hover) {
                        ctx.pick_debug_dump_completed = true;
                    }
                }
            }
        }
    }
}

} // namespace gs3d::app
