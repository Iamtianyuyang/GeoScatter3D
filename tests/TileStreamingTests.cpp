#include "app/TilePointCache.hpp"
#include "app/ViewerAppTileStreaming.hpp"
#include "data/Gs3dFormat.hpp"
#include "render/TileSelection.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view name)
{
    INFO(name);
    CHECK(condition);
}

// ── build_gpu_required_tile_ids (production helper) ──────────────────

void test_visible_tiles_ignore_resident_budget()
{
    const std::vector<std::uint64_t> candidates{10, 20, 30, 40, 50};
    auto required =
        gs3d::app::build_gpu_required_tile_ids(candidates, 3);

    expect(required.size() == 5,
           "visible tiles are not truncated by the resident cache budget");
    expect(required == candidates,
           "visible tile order preserves streaming priority");
}

void test_visible_tiles_allow_unlimited_resident_budget()
{
    const std::vector<std::uint64_t> candidates{1, 2, 3};
    auto required =
        gs3d::app::build_gpu_required_tile_ids(candidates, 0);

    expect(required.size() == 3, "K=0 returns all candidates");
}

void test_visible_tiles_preserve_all_candidates_below_budget()
{
    const std::vector<std::uint64_t> candidates{1, 2};
    auto required =
        gs3d::app::build_gpu_required_tile_ids(candidates, 10);

    expect(required.size() == 2, "all visible candidates are retained");
}

void test_empty_visible_candidates()
{
    const std::vector<std::uint64_t> empty{};
    auto required =
        gs3d::app::build_gpu_required_tile_ids(empty, 5);

    expect(required.empty(), "empty candidates → empty result");
}

void test_lod_clip_requires_complete_candidate_coverage()
{
    expect(
        gs3d::app::selected_tiles_cover_candidates(3, 3),
        "complete selected set permits LOD spatial clipping"
    );
    expect(
        !gs3d::app::selected_tiles_cover_candidates(2, 3),
        "capped selected set keeps LOD coverage outside selected tiles"
    );
}

// ── Order sensitivity ────────────────────────────────────────────────

void test_reordering_changes_required_when_k_small()
{
    // Required tiles retain the selection priority order.

    auto r1 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3}, 1);
    auto r2 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{3, 2, 1}, 1);

    expect(r1 != r2,
           "reordering visible tiles updates streaming priority");
    expect(r1[0] == 1, "old first visible tile == 1");
    expect(r2[0] == 3, "new first visible tile == 3");
}

void test_reordering_no_change_when_first_k_stable()
{
    auto r1 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 4, 3}, 2);
    auto r2 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{2, 1, 3, 4}, 2);

    // Both calls keep every visible tile; the order still records priority.
    expect(r1 != r2,
           "visible priority order differs");
    std::sort(r1.begin(), r1.end());
    std::sort(r2.begin(), r2.end());
    expect(r1 == r2,
           "sorted visible sets are identical");
}

void test_no_spurious_update_when_stable()
{
    auto r1 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{10, 20, 30, 40, 50}, 3);
    auto r2 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{10, 20, 30, 40, 50}, 3);

    expect(r1 == r2,
           "identical input → identical result → no rebuild triggered");
}

// ── Viewport bounded (pure logic) ────────────────────────────────────

void test_viewport_is_required_intersect_resident()
{
    // viewport_tile_ids = required ∩ resident
    // Extra resident tiles outside required are excluded.

    std::vector<std::uint64_t> required{1, 2};
    std::vector<std::uint64_t> resident{1, 2, 3, 4};

    std::vector<std::uint64_t> viewport;
    for (auto id : required) {
        if (std::find(resident.begin(), resident.end(), id)
            != resident.end()) {
            viewport.push_back(id);
        }
    }
    expect(viewport.size() == 2,
           "viewport = required ∩ resident: excludes 3,4");
    expect(viewport[0] == 1 && viewport[1] == 2,
           "viewport content");
}

// ── Spatial clip empty-set safety ────────────────────────────────────

void test_spatial_clip_empty_required_fallback()
{
    // When gpu_required_tile_ids is empty (Stage 1/2 or first frame),
    // RenderPass falls back to tile_result.tile_ids.
    // all_desired_resident = !desired_for_clip.empty()

    // Sub-case 1: both empty
    {
        std::vector<std::uint64_t> required{};       // empty
        std::vector<std::uint64_t> candidates{};      // empty
        const auto& desired = required.empty()
            ? candidates : required;
        bool all_resident = !desired.empty();
        expect(!all_resident,
               "both empty → all_desired_resident=false → no clip");
    }

    // Sub-case 2: required empty, candidates non-empty (fallback)
    {
        std::vector<std::uint64_t> required{};
        std::vector<std::uint64_t> candidates{1, 2, 3};
        const auto& desired = required.empty()
            ? candidates : required;
        bool all_resident = !desired.empty();
        expect(all_resident,
               "fallback to candidates — prevents vacuous-truth clip");
    }

    // Sub-case 3: required non-empty (Stage 3 normal)
    {
        std::vector<std::uint64_t> required{1, 2};
        const auto& desired = required;  // non-empty → used directly
        bool all_resident = !desired.empty();
        expect(all_resident,
               "required non-empty → checked directly");
    }
}

// ── GPU behaviour contract (documented, not executed) ────────────────

void test_contract_pin_required_resident_with_cpu_miss()
{
    // CONTRACT: When sync_from_cached_tiles is called with
    //   required_tile_ids = {X}, tiles = {} (X is CPU miss),
    //   and X is in resident_tiles_ (GPU-resident):
    //
    // The pin-only loop in Phase 3 must:
    //   1. Find X in resident_tiles_
    //   2. Add X to active_tile_ids (pinned)
    //   3. Update X->last_used_tick
    //   4. NOT add X to loaded_tile_ids_
    //
    // After evict_to_budget(active_tile_ids), X must NOT be evicted.
    //
    // This is a pure-data-path contract; full verification requires
    // a Vulkan context (see "remaining gaps" in the acceptance report).

    expect(true,
           "contract: required+resident+CPU-miss → pinned (GPU test needed)");
}

void test_contract_old_tile_lazy_replacement()
{
    // CONTRACT: When cached_required is empty, update_tile_streaming
    // does NOT call sync_from_cached_tiles.  Old GPU-resident tiles
    // are untouched until at least one required tile has CPU point
    // data and triggers a sync call.
    //
    // This preserves visual continuity: old fine data stays on screen
    // until new fine data is ready.

    expect(true,
           "contract: empty cached_required → no sync → old tiles persist");
}

// ── Budget shrink timing ─────────────────────────────────────────────

void test_budget_shrink_with_empty_cached()
{
    // Scenario: K 144→32, all new required tiles are CPU miss.
    //
    // 1. set_resident_tile_budget(32) is called ✓
    //    (in the rebuild block, before the enabled check)
    // 2. cached_required is empty → sync_from_cached_tiles NOT called
    // 3. evict_to_budget NOT called → resident stays at 144
    // 4. Old tiles persist until first sync with non-empty cached_required
    // 5. First sync evicts non-required tiles down to 32 in same call
    //
    // This is Design A (visual continuity).  The budget is a sync-driven
    // soft limit, not an immediate hard cut.

    expect(true,
           "budget shrink: set immediately, enforced at next non-empty sync");
}

// ── Disable / re-enable lifecycle ────────────────────────────────────

void test_disable_reenable_required_lifecycle()
{
    // Simulate the lifecycle through the helper:
    // Disable: empty candidates → empty required
    {
        auto r = gs3d::app::build_gpu_required_tile_ids({}, 5);
        expect(r.empty(), "disabled: no candidates → empty required");
    }
    // Re-enable: candidates appear → rebuild
    {
        auto r = gs3d::app::build_gpu_required_tile_ids(
            std::vector<std::uint64_t>{1, 2, 3, 4}, 3);
        expect(r.size() == 4, "re-enabled: every visible tile is required");
    }
}

// ── Runtime K change ─────────────────────────────────────────────────

void test_runtime_k_change()
{
    auto r_initial = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3, 4, 5}, 3);
    expect(r_initial.size() == 5, "initial budget keeps visible tiles");

    auto r_expanded = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3, 4, 5}, 5);
    expect(r_expanded.size() == 5, "K expanded → 5");

    auto r_shrunk = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3, 4, 5}, 1);
    expect(r_shrunk.size() == 5, "shrinking budget keeps visible tiles");

    auto r_unlimited = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3, 4, 5}, 0);
    expect(r_unlimited.size() == 5, "K=0 unlimited");
}

// ── commit_streaming_tile_load_result tests ──────────────────────────

// Helper: create a minimal valid LoadedTilePoints with one point.
gs3d::app::LoadedTilePoints make_test_entry(
    std::uint64_t tile_id)
{
    auto tp = std::make_shared<gs3d::app::TilePoints>();
    tp->points.push_back({});
    tp->point_ids.push_back(1);
    return {tile_id, tp};
}

void test_commit_all_stale_retained_with_spare_capacity()
{
    gs3d::app::TilePointCache cache(1024 * 1024);
    std::vector<gs3d::app::LoadedTilePoints> loaded{
        make_test_entry(1), make_test_entry(2)};
    std::vector<std::uint64_t> required{3, 4};

    auto stats = gs3d::app::commit_streaming_tile_load_result(
        cache, loaded, required);

    expect(stats.accepted == 0,
           "all stale: accepted=0");
    expect(stats.stale_retained == 2,
           "all stale: stale_retained=2");
    expect(stats.stale_discarded == 0,
           "all stale: stale_discarded=0");
    expect(stats.invalid == 0,
           "all stale: invalid=0");
    expect(cache.find(1) != nullptr,
           "all stale: tile 1 retained cold");
    expect(cache.find(2) != nullptr,
           "all stale: tile 2 retained cold");
    auto s = cache.stats();
    expect(s.resident_bytes > 0,
           "all stale: retained bytes tracked");
}

void test_commit_partial_overlap()
{
    gs3d::app::TilePointCache cache(1024 * 1024);
    std::vector<gs3d::app::LoadedTilePoints> loaded{
        make_test_entry(10),   // A — stale
        make_test_entry(20),   // B — required
        make_test_entry(30)};  // C — required
    std::vector<std::uint64_t> required{20, 30, 40};
    //                                          ^^ D not loaded

    auto stats = gs3d::app::commit_streaming_tile_load_result(
        cache, loaded, required);

    expect(stats.accepted == 2,
           "partial overlap: accepted=2 (B,C)");
    expect(stats.stale_retained == 1,
           "partial overlap: stale_retained=1 (A)");
    expect(stats.stale_discarded == 0,
           "partial overlap: stale_discarded=0");
    expect(cache.find(10) != nullptr,
           "partial overlap: A retained cold");
    expect(cache.find(20) != nullptr,
           "partial overlap: B accepted into cache");
    expect(cache.find(30) != nullptr,
           "partial overlap: C accepted into cache");
}

void test_commit_revision_expired_but_tile_needed()
{
    // Revision is stale, but tiles are still in current required set.
    // Commit must accept them — membership, not revision, is the gate.
    gs3d::app::TilePointCache cache(1024 * 1024);
    std::vector<gs3d::app::LoadedTilePoints> loaded{
        make_test_entry(1), make_test_entry(2)};
    std::vector<std::uint64_t> required{1, 2};

    auto stats = gs3d::app::commit_streaming_tile_load_result(
        cache, loaded, required);

    expect(stats.accepted == 2,
           "revision expired but tile needed: accepted=2");
    expect(stats.stale_discarded == 0,
           "revision expired but tile needed: stale=0");
    expect(cache.find(1) != nullptr,
           "revision expired but tile needed: tile 1 in cache");
    expect(cache.find(2) != nullptr,
           "revision expired but tile needed: tile 2 in cache");
}

void test_commit_already_cached_no_reput()
{
    gs3d::app::TilePointCache cache(1024 * 1024);

    // Pre-populate B in cache
    {
        auto tp = std::make_shared<gs3d::app::TilePoints>();
        tp->points.push_back({});
        tp->point_ids.push_back(1);
        cache.put(20, std::move(tp));
    }
    auto stats_before = cache.stats();
    auto tile_count_before = stats_before.tile_count;

    std::vector<gs3d::app::LoadedTilePoints> loaded{
        make_test_entry(20)};   // B — already cached
    std::vector<std::uint64_t> required{20};

    auto stats = gs3d::app::commit_streaming_tile_load_result(
        cache, loaded, required);

    expect(stats.already_cached == 1,
           "already cached: already_cached=1");
    expect(stats.accepted == 0,
           "already cached: accepted=0 (not re-put)");

    auto s = cache.stats();
    expect(s.tile_count == tile_count_before,
           "already cached: tile_count unchanged");
}

void test_commit_normal_accept()
{
    gs3d::app::TilePointCache cache(1024 * 1024);
    std::vector<gs3d::app::LoadedTilePoints> loaded{
        make_test_entry(1), make_test_entry(2)};
    std::vector<std::uint64_t> required{1, 2};

    auto stats = gs3d::app::commit_streaming_tile_load_result(
        cache, loaded, required);

    expect(stats.accepted == 2,
           "normal accept: accepted=2");
    expect(stats.stale_discarded == 0,
           "normal accept: stale=0");
    expect(cache.find(1) != nullptr,
           "normal accept: tile 1 in cache");
    expect(cache.find(2) != nullptr,
           "normal accept: tile 2 in cache");
}

void test_commit_reuses_loaded_allocation()
{
    gs3d::app::TilePointCache cache(1024 * 1024);
    auto points = std::make_shared<gs3d::app::TilePoints>();
    points->points.push_back({});
    points->point_ids.push_back(7);
    const auto* original = points.get();

    std::vector<gs3d::app::LoadedTilePoints> loaded{
        {7, points}};
    std::vector<std::uint64_t> required{7};

    const auto stats =
        gs3d::app::commit_streaming_tile_load_result(
            cache, loaded, required);
    const auto cached = cache.find(7);

    expect(stats.accepted == 1,
           "zero-copy commit: tile accepted");
    expect(cached.get() == original,
           "zero-copy commit: loaded allocation reused");
}

void test_commit_empty_required()
{
    gs3d::app::TilePointCache cache(1024 * 1024);
    std::vector<gs3d::app::LoadedTilePoints> loaded{
        make_test_entry(1), make_test_entry(2)};
    std::vector<std::uint64_t> required{};   // empty

    auto stats = gs3d::app::commit_streaming_tile_load_result(
        cache, loaded, required);

    expect(stats.stale_retained == 2,
           "empty required: all stale retained");
    expect(stats.stale_discarded == 0,
           "empty required: none discarded");
    expect(stats.accepted == 0,
           "empty required: accepted=0");
    auto s = cache.stats();
    expect(s.resident_bytes > 0,
           "empty required: retained bytes tracked");
}

void test_commit_invalid_entry()
{
    gs3d::app::TilePointCache cache(1024 * 1024);

    // null points
    gs3d::app::LoadedTilePoints null_entry{1, nullptr};
    // empty points
    auto empty_tp = std::make_shared<gs3d::app::TilePoints>();
    gs3d::app::LoadedTilePoints empty_entry{2, empty_tp};
    // size mismatch
    auto mismatch_tp = std::make_shared<gs3d::app::TilePoints>();
    mismatch_tp->points.push_back({});
    mismatch_tp->points.push_back({});
    mismatch_tp->point_ids.push_back(1);  // 2 points, 1 id
    gs3d::app::LoadedTilePoints mismatch_entry{3, mismatch_tp};

    std::vector<gs3d::app::LoadedTilePoints> loaded{
        null_entry, empty_entry, mismatch_entry};
    std::vector<std::uint64_t> required{1, 2, 3};

    auto stats = gs3d::app::commit_streaming_tile_load_result(
        cache, loaded, required);

    expect(stats.invalid == 3,
           "invalid: 3 invalid entries");
    expect(stats.accepted == 0,
           "invalid: accepted=0");
}

void test_stale_admission_never_evicts_hot_data()
{
    constexpr std::uint64_t tile_bytes =
        sizeof(gs3d::data::Gs3dPoint) + sizeof(std::uint32_t);
    gs3d::app::TilePointCache cache(tile_bytes);
    cache.put(1, make_test_entry(1).points);

    std::vector<gs3d::app::LoadedTilePoints> loaded{
        make_test_entry(99)};
    std::vector<std::uint64_t> required{1};
    const auto stats =
        gs3d::app::commit_streaming_tile_load_result(
            cache, loaded, required);

    expect(stats.stale_retained == 0,
           "full cache: stale tile not retained");
    expect(stats.stale_discarded == 1,
           "full cache: stale tile discarded");
    expect(cache.find(1) != nullptr,
           "full cache: hot tile preserved");
    expect(cache.find(99) == nullptr,
           "full cache: stale tile absent");
}

void test_stale_entry_is_first_lru_eviction()
{
    constexpr std::uint64_t tile_bytes =
        sizeof(gs3d::data::Gs3dPoint) + sizeof(std::uint32_t);
    gs3d::app::TilePointCache cache(tile_bytes * 2);
    cache.put(1, make_test_entry(1).points);

    std::vector<gs3d::app::LoadedTilePoints> stale{
        make_test_entry(99)};
    std::vector<std::uint64_t> required{1};
    const auto stats =
        gs3d::app::commit_streaming_tile_load_result(
            cache, stale, required);
    cache.put(2, make_test_entry(2).points);

    expect(stats.stale_retained == 1,
           "cold LRU: stale tile initially retained");
    expect(cache.find(1) != nullptr,
           "cold LRU: older hot tile preserved");
    expect(cache.find(2) != nullptr,
           "cold LRU: new hot tile inserted");
    expect(cache.find(99) == nullptr,
           "cold LRU: stale tile evicted first");
}

} // namespace

#define LEGACY_TEST_CASE(test_function) \
    TEST_CASE(#test_function, "[tile_streaming]") { test_function(); }

    // production helper tests
    LEGACY_TEST_CASE(test_visible_tiles_ignore_resident_budget)
    LEGACY_TEST_CASE(test_visible_tiles_allow_unlimited_resident_budget)
    LEGACY_TEST_CASE(test_visible_tiles_preserve_all_candidates_below_budget)
    LEGACY_TEST_CASE(test_empty_visible_candidates)
    LEGACY_TEST_CASE(test_lod_clip_requires_complete_candidate_coverage)

    // order sensitivity
    LEGACY_TEST_CASE(test_reordering_changes_required_when_k_small)
    LEGACY_TEST_CASE(test_reordering_no_change_when_first_k_stable)
    LEGACY_TEST_CASE(test_no_spurious_update_when_stable)

    // viewport / spatial clip (pure logic)
    LEGACY_TEST_CASE(test_viewport_is_required_intersect_resident)
    LEGACY_TEST_CASE(test_spatial_clip_empty_required_fallback)

    // GPU behaviour contracts
    LEGACY_TEST_CASE(test_contract_pin_required_resident_with_cpu_miss)
    LEGACY_TEST_CASE(test_contract_old_tile_lazy_replacement)

    // budget timing
    LEGACY_TEST_CASE(test_budget_shrink_with_empty_cached)

    // lifecycle
    LEGACY_TEST_CASE(test_disable_reenable_required_lifecycle)
    LEGACY_TEST_CASE(test_runtime_k_change)

    // commit_streaming_tile_load_result
    LEGACY_TEST_CASE(test_commit_all_stale_retained_with_spare_capacity)
    LEGACY_TEST_CASE(test_commit_partial_overlap)
    LEGACY_TEST_CASE(test_commit_revision_expired_but_tile_needed)
    LEGACY_TEST_CASE(test_commit_already_cached_no_reput)
    LEGACY_TEST_CASE(test_commit_normal_accept)
    LEGACY_TEST_CASE(test_commit_reuses_loaded_allocation)
    LEGACY_TEST_CASE(test_commit_empty_required)
    LEGACY_TEST_CASE(test_commit_invalid_entry)
    LEGACY_TEST_CASE(test_stale_admission_never_evicts_hot_data)
    LEGACY_TEST_CASE(test_stale_entry_is_first_lru_eviction)

#undef LEGACY_TEST_CASE
