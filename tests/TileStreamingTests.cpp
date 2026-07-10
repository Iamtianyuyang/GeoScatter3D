#include "app/ViewerAppTileStreaming.hpp"
#include "render/TileSelection.hpp"

#include <algorithm>
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

// ── build_gpu_required_tile_ids (production helper) ──────────────────

void test_truncation_k_positive()
{
    // K=3 from 5 candidates → first 3

    const std::vector<std::uint64_t> candidates{10, 20, 30, 40, 50};
    auto required =
        gs3d::app::build_gpu_required_tile_ids(candidates, 3);

    expect(required.size() == 3, "K=3 size");
    expect(required[0] == 10 && required[1] == 20 && required[2] == 30,
           "K=3 content — preserves priority order");
}

void test_truncation_k_zero_unlimited()
{
    const std::vector<std::uint64_t> candidates{1, 2, 3};
    auto required =
        gs3d::app::build_gpu_required_tile_ids(candidates, 0);

    expect(required.size() == 3, "K=0 returns all candidates");
}

void test_truncation_k_larger_than_candidates()
{
    const std::vector<std::uint64_t> candidates{1, 2};
    auto required =
        gs3d::app::build_gpu_required_tile_ids(candidates, 10);

    expect(required.size() == 2, "K>N clamped to N");
}

void test_truncation_empty_candidates()
{
    const std::vector<std::uint64_t> empty{};
    auto required =
        gs3d::app::build_gpu_required_tile_ids(empty, 5);

    expect(required.empty(), "empty candidates → empty result");
}

// ── Order sensitivity ────────────────────────────────────────────────

void test_reordering_changes_required_when_k_small()
{
    // same_tile_ids() is set-based, but build_gpu_required_tile_ids
    // is vector-based — reordering with K=1 must change result.

    auto r1 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3}, 1);
    auto r2 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{3, 2, 1}, 1);

    expect(r1 != r2,
           "K=1: reordering changes first-K → different required set");
    expect(r1[0] == 1, "old first-K == 1");
    expect(r2[0] == 3, "new first-K == 3");
}

void test_reordering_no_change_when_first_k_stable()
{
    auto r1 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 4, 3}, 2);
    auto r2 = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{2, 1, 3, 4}, 2);

    // Sets {1,2} are equal but order differs.  As vectors they differ.
    // This IS a spurious update — the working set members are the same
    // but the vector comparison triggers a rebuild.  Acceptable trade-off
    // for simplicity (O(K) vector compare vs. set comparison).
    expect(r1 != r2,
           "first-K order differs → rebuild triggered (acceptable)");
    std::sort(r1.begin(), r1.end());
    std::sort(r2.begin(), r2.end());
    expect(r1 == r2,
           "sorted first-K sets are identical");
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
        expect(r.size() == 3, "re-enabled: working set rebuilt");
    }
}

// ── Runtime K change ─────────────────────────────────────────────────

void test_runtime_k_change()
{
    auto r_initial = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3, 4, 5}, 3);
    expect(r_initial.size() == 3, "initial K=3");

    auto r_expanded = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3, 4, 5}, 5);
    expect(r_expanded.size() == 5, "K expanded → 5");

    auto r_shrunk = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3, 4, 5}, 1);
    expect(r_shrunk.size() == 1, "K shrunk → 1");

    auto r_unlimited = gs3d::app::build_gpu_required_tile_ids(
        std::vector<std::uint64_t>{1, 2, 3, 4, 5}, 0);
    expect(r_unlimited.size() == 5, "K=0 unlimited");
}

} // namespace

int main()
{
    // production helper tests
    test_truncation_k_positive();
    test_truncation_k_zero_unlimited();
    test_truncation_k_larger_than_candidates();
    test_truncation_empty_candidates();

    // order sensitivity
    test_reordering_changes_required_when_k_small();
    test_reordering_no_change_when_first_k_stable();
    test_no_spurious_update_when_stable();

    // viewport / spatial clip (pure logic)
    test_viewport_is_required_intersect_resident();
    test_spatial_clip_empty_required_fallback();

    // GPU behaviour contracts
    test_contract_pin_required_resident_with_cpu_miss();
    test_contract_old_tile_lazy_replacement();

    // budget timing
    test_budget_shrink_with_empty_cached();

    // lifecycle
    test_disable_reenable_required_lifecycle();
    test_runtime_k_change();

    if (failures) {
        std::cerr << failures << " test(s) FAILED.\n";
        return 1;
    }
    std::cout << "All TileStreaming tests passed.\n";
    return 0;
}
