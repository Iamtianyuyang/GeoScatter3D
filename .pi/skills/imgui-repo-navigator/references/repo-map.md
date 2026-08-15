# Dear ImGui Repo Map

## Source model

- Local and upstream use the same path layout.
- Prefer local paths when the current project vendors or checks out Dear ImGui.
- Prefer upstream GitHub when local sources are missing or local lookup does not answer the query.
- When using upstream, cite the GitHub blob URL with the chosen `ref`.

## Top-level source files

- `imgui.h`: public API declarations, enums, flags, structs, and inline docs.
- `imgui.cpp`: core runtime, windows/state handling, navigation, debug/metrics helpers, and many FAQ-linked behaviors.
- `imgui_demo.cpp`: authoritative usage examples for widgets, layout, menus, popups, tables, plotting, style, and debug helpers.
- `imgui_widgets.cpp`: most standard widget implementations.
- `imgui_tables.cpp`: table API and internals.
- `imgui_draw.cpp`: low-level drawing, font atlas, draw list plumbing, text rendering helpers.
- `imgui_internal.h`: private/internal APIs. Read only when a task explicitly needs internals, debugging, or a source change.

## Backends

- `backends/imgui_impl_<platform>.h/.cpp`: platform backend contract and implementation.
- `backends/imgui_impl_<renderer>.h/.cpp`: renderer backend contract and implementation.
- Backend headers are the fastest way to discover public entry points such as `Init`, `NewFrame`, `RenderDrawData`, event forwarding helpers, and device-object helpers.
- Backend `.cpp` files usually start with feature notes and backend-specific caveats; inspect those before guessing.

## Examples

- `examples/example_<platform>_<renderer>/main.cpp`: end-to-end integration recipe.
- Example `main.cpp` files are the most reliable place to learn setup order:
  1. create native window or device
  2. `ImGui::CreateContext()`
  3. set `io.ConfigFlags` and style
  4. initialize platform backend
  5. initialize renderer backend
  6. run per-frame `NewFrame()` calls
  7. call `ImGui::Render()` and renderer `RenderDrawData()`
  8. shutdown backends and destroy context
- Example build files live beside each example: `Makefile`, `CMakeLists.txt`, `README.md`, or Visual Studio project files.

## Docs

- `docs/BACKENDS.md`: backend concepts, platform vs renderer split, custom backend guidance, multi-viewport notes.
- `docs/EXAMPLES.md`: example catalog and build notes.
- `docs/FAQ.md`: first stop for usage, input routing, IDs, clipping, images, texture IDs, custom drawing, DPI, and common integration mistakes.
- `docs/FONTS.md`: font loading, icon merging, glyph ranges, and font atlas guidance.
- `docs/CHANGELOG.txt`: behavior changes across versions; useful when reconciling outdated snippets or version mismatches.

## Misc helpers

- `misc/cpp/`: optional helpers for STL/string integrations and related utilities.
- `misc/freetype/`: FreeType font builder integration.
- `misc/debuggers/`: debugger visualizers.

## Selection rules

1. Start with the public header for the subsystem.
2. Move to the closest example for lifecycle and glue code.
3. Open implementation only after the public contract is clear.
4. Use docs for conceptual questions before inferring from source.
5. When local search is inconclusive, retry the same path family on upstream instead of switching to unrelated blog posts or snippets.
