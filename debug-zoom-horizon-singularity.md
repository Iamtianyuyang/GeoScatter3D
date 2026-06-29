# Debug Session: zoom-horizon-singularity
- **Status**: [OPEN]
- **Issue**: zoom-to-cursor in `CameraController::zoom_view()` becomes unstable when camera pitch approaches the horizon; suspected singular pivot from ray/horizontal-plane intersection.
- **Debug Server**: http://127.0.0.1:7777/event
- **Log File**: .dbg/trae-debug-log-zoom-horizon-singularity.ndjson

## Reproduction Steps
1. Launch `GeoScatter3D` with the default `config/viewer.toml`.
2. Rotate the camera to a near-horizontal viewing direction.
3. Scroll to zoom in on an off-center cursor position.
4. Observe whether pivot, distance, or camera position jumps abnormally.

## Hypotheses & Verification
| ID | Hypothesis | Likelihood | Effort | Evidence |
|----|------------|------------|--------|----------|
| A | Near-horizontal pitch makes the cursor ray almost parallel to the `target.z` horizontal plane, so the computed pivot becomes extremely far away and zoom compensation explodes. | High | Low | Rejected for this run: early visible-scale zooms happen at `pitch_deg≈-55.7` with `plane_denom≈-0.80` and finite pivots; no singular near-zero denominator is present when `position_delta` is large. |
| B | The plane intersection returns `nullopt` near the horizon, so the code silently falls back to `target`, producing a discontinuous transition between cursor-anchored zoom and target-centered zoom. | High | Low | Partially confirmed but not as the visible-jump cause in this run: near `pitch_deg≈-0.05°`, `hit_valid=false` appears, but `target_delta=0` and `position_delta` is only `1e-8~1e-7`, so the fallback is numerically smooth here. |
| C | Even with a finite pivot, the single-step `pivot + (position - pivot) * factor` update can create an excessive world-space move when pivot distance is orders of magnitude larger than camera distance. | Medium | Low | Rejected for this run: `position_delta` scales with zoom and camera distance monotonically; no abnormal spike or discontinuity appears. |
| D | The instability is amplified only when the cursor is off-center because the unprojected ray direction differs more from the camera forward vector there than at screen center. | Medium | Low | Inconclusive: logs contain off-center cursor samples, but without a contrasting center-screen reproduction we cannot isolate cursor offset as the main trigger. |
| E | Distance clamping is happening after the pivot-based move, so it cannot prevent the transient target/position jump in the same frame. | Medium | Low | Rejected for this run: `new_distance_before_clamp == new_distance_after_clamp` throughout the captured logs, so clamp did not participate in any observed transition. |

## Log Evidence
- Line 1 / 2: `pitch_deg=-55.7052`, `plane_denom=-0.811829`, `hit_valid=true`, `hit_distance=15485`, `camera_distance_before=15231.5`, `position_delta=3875.05`, `target_delta=205.768`. This is a large but internally consistent zoom step, not a near-horizontal singularity.
- Lines 55 / 56: `scroll_y=3` still keeps `hit_valid=true`, `plane_denom=-0.799054`, and the resulting `position_delta=2.8904` is proportional to the tiny `old_distance=4.8357`; no explosion appears.
- Lines 83 / 84 onward: `hit_valid=false` starts appearing, but only after `camera_distance_before` has already shrunk to `0.0484524` and smaller; corresponding `target_delta=0` and `position_delta` becomes tiny (`1e-2` down to `1e-8`), showing a smooth fallback to target-centered zoom.
- Lines 131 / 132: at `pitch_deg=-17.7081`, `plane_denom=-0.301197`, `hit_valid=false`, but `position_delta=1.0044e-05`; still no visible jump.
- Lines 151 / 152: at `pitch_deg=-1.03011`, `plane_denom=-0.0154768`, `hit_valid=false`, and `position_delta=5.65618e-07`; the near-horizontal case exists, but it is numerically stable in this capture.

## Verification Conclusion
- This run does **not** confirm the suspected visible bug path. The horizontal-plane intersection does become invalid near the horizon, but only after the camera distance has already collapsed to a microscopic scale, where the fallback to `pivot = target` produces negligible movement. The originally suspected "pivot runs to infinity and causes a visible jump" was not observed in the collected evidence.
