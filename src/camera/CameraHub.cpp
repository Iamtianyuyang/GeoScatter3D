#include "camera/CameraHub.hpp"

#include <algorithm>

namespace gs3d::camera {

void CameraHub::add(int viewport_index, Camera* camera, int initial_group)
{
    entries_.push_back({viewport_index, initial_group, camera});
}

void CameraHub::remove(int viewport_index)
{
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [viewport_index](const Entry& e) {
                return e.viewport_index == viewport_index;
            }),
        entries_.end()
    );
}

int CameraHub::viewport_count() const noexcept
{
    return static_cast<int>(entries_.size());
}

int CameraHub::group_of(int viewport_index) const noexcept
{
    const auto* e = find(viewport_index);
    return e ? e->group_id : kIndependent;
}

void CameraHub::set_group(int viewport_index, int group_id) noexcept
{
    auto* e = find(viewport_index);
    if (e) e->group_id = group_id;
}

void CameraHub::propagate(int viewport_index)
{
    const auto* src = find(viewport_index);
    if (!src || src->group_id == kIndependent) return;
    const int group = src->group_id;
    for (auto& e : entries_) {
        if (e.viewport_index != viewport_index && e.group_id == group) {
            copy_view(*src->camera, *e.camera);
        }
    }
}

void CameraHub::propagate_all()
{
    // Collect distinct non-independent group IDs.
    std::vector<int> groups;
    for (const auto& e : entries_) {
        if (e.group_id != kIndependent) {
            if (std::find(groups.begin(), groups.end(), e.group_id) == groups.end()) {
                groups.push_back(e.group_id);
            }
        }
    }
    // For each group, let the first member drive the rest.
    for (const int g : groups) {
        const Entry* leader = nullptr;
        for (const auto& e : entries_) {
            if (e.group_id == g) { leader = &e; break; }
        }
        if (!leader) continue;
        for (auto& e : entries_) {
            if (e.group_id == g && e.viewport_index != leader->viewport_index) {
                copy_view(*leader->camera, *e.camera);
            }
        }
    }
}

const std::vector<CameraHub::Entry>& CameraHub::entries() const noexcept
{
    return entries_;
}

CameraHub::Entry* CameraHub::find(int viewport_index) noexcept
{
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [viewport_index](const Entry& e) { return e.viewport_index == viewport_index; });
    return (it != entries_.end()) ? &*it : nullptr;
}

const CameraHub::Entry* CameraHub::find(int viewport_index) const noexcept
{
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [viewport_index](const Entry& e) { return e.viewport_index == viewport_index; });
    return (it != entries_.end()) ? &*it : nullptr;
}

void CameraHub::copy_view(const Camera& src, Camera& dst) noexcept
{
    const std::uint32_t w = dst.viewport_width();
    const std::uint32_t h = dst.viewport_height();
    dst.look_at(src.position(), src.target(), src.up());
    dst.set_perspective(src.fov_y_degrees(), src.near_plane(), src.far_plane());
    dst.set_viewport(w, h);
}

} // namespace gs3d::camera
