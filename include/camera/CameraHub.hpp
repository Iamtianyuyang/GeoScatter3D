#pragma once

#include "camera/Camera.hpp"

#include <functional>
#include <vector>
#include <cstdint>

namespace gs3d::camera {

/*
 * Observer-style camera synchronization registry.
 *
 * Cameras are partitioned into numbered sync groups (0, 1, 2 …).
 * When one camera in a group is updated the caller invokes propagate()
 * which copies that camera's view state to all other members of the
 * same group, preserving each member's own viewport dimensions.
 *
 * kIndependent (-1) = not synchronized with any other camera.
 *
 * Design follows Polyscope's view-linkage model (MIT) — events flow
 * outward from the "just-moved" camera rather than being pulled by a
 * central tick, so the caller decides when propagation happens.
 */
class CameraHub {
public:
    static constexpr int kIndependent = -1;

    struct Entry {
        int     viewport_index = 0;
        int     group_id       = 0;
        Camera* camera         = nullptr;
    };

    // Optional per-link pivot mirror. Called with (src_viewport, dst_viewport)
    // once for each propagated pair, AFTER the view itself has been copied.
    // Use it to keep the orbit pivot (controller state, not stored on Camera)
    // in lockstep across linked viewports. If null, pivots are not touched.
    using PivotMirror = std::function<void(int src_viewport, int dst_viewport)>;

    // Register a camera pointer (must remain valid for the hub's lifetime).
    // initial_group = 0 = default sync group; kIndependent = no sync.
    void add(int viewport_index, Camera* camera, int initial_group = 0);
    void remove(int viewport_index);

    int viewport_count() const noexcept;

    // Query / change the sync group for a registered viewport.
    int  group_of(int viewport_index) const noexcept;
    void set_group(int viewport_index, int group_id) noexcept;

    // After updating camera[viewport_index], propagate its view to all
    // other cameras that share the same (non-independent) group.
    // If `mirror_pivot` is non-null, also call it for each (src, dst) pair
    // so the caller can keep orbit pivots in sync across the group.
    void propagate(int viewport_index, PivotMirror mirror_pivot = {});

    // For each group, propagate its first member to the rest.
    // Use after bulk state changes (camera reset, file reload).
    void propagate_all(PivotMirror mirror_pivot = {});

    const std::vector<Entry>& entries() const noexcept;

private:
    std::vector<Entry> entries_;

    Entry*       find(int viewport_index) noexcept;
    const Entry* find(int viewport_index) const noexcept;

    // Copy view (position/target/up/perspective) while keeping dst's viewport size.
    static void copy_view(const Camera& src, Camera& dst) noexcept;
};

} // namespace gs3d::camera
