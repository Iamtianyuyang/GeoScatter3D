# Dear ImGui Task Playbook

## Lookup modes

- `python scripts/find_imgui_targets.py [terms...]`: local-first auto mode with upstream fallback.
- `python scripts/find_imgui_targets.py --mode local [terms...]`: stay on the local checkout only.
- `python scripts/find_imgui_targets.py --mode upstream --ref master [terms...]`: query upstream GitHub only.
- `python scripts/find_imgui_targets.py --repo /path/to/imgui [terms...]`: point at a vendored subtree explicitly.

## Core widget or public API lookup

- Start with `imgui.h`.
- Search `imgui_demo.cpp` for real usage.
- Open the implementation file only after identifying the API family:
  - widgets: `imgui_widgets.cpp`
  - tables: `imgui_tables.cpp`
  - low-level drawing or fonts: `imgui_draw.cpp`

Suggested commands:

```bash
python scripts/find_imgui_targets.py inputtext
rg -n "ImGui::BeginMainMenuBar|ImGui::TableNextRow|ImGui::InputText|ImGui::ColorEdit4" imgui.h imgui_demo.cpp
rg -n "BeginMainMenuBar|TableNextRow|InputText|ColorEdit4" imgui_widgets.cpp imgui_tables.cpp imgui_demo.cpp
```

## App feature scouting via open-source projects

- Use this path when the user wants a whole feature, workflow, or tool panel rather than a single Dear ImGui API answer.
- Search for open-source Dear ImGui projects that already implement the same feature or a close interaction pattern.
- After finding a promising project, verify the concrete APIs and lifecycle against local or upstream Dear ImGui before suggesting code.
- Keep external references and official Dear ImGui facts clearly separated in the final answer.

Suggested search patterns:

```text
"Dear ImGui" "file browser"
"Dear ImGui" "asset browser"
"Dear ImGui" "log console"
"Dear ImGui" "timeline"
"Dear ImGui" "node editor"
site:github.com "Dear ImGui" "<feature>"
```

## Backend integration lookup

- Open the backend headers first.
- Open the matching example `main.cpp` next.
- Open the backend `.cpp` only when you need event flow, renderer details, or a bug fix.

Suggested commands:

```bash
python scripts/find_imgui_targets.py win32 dx11
python scripts/find_imgui_targets.py --mode upstream --ref master glfw opengl3
rg -n "ImGui_ImplWin32_|ImGui_ImplDX9_|ImGui_ImplGlfw_|ImGui_ImplOpenGL3_" backends
rg -n "CreateContext|ConfigFlags|Init|NewFrame|RenderDrawData|Shutdown" examples/example_*/main.cpp
```

## Images, textures, and custom drawing

- Read the `docs/FAQ.md` entry about images and `ImTextureID` first.
- Confirm the concrete texture handle type in the active renderer backend header.
- For low-level drawing, inspect `ImDrawList` declarations in `imgui.h`, then open `imgui_draw.cpp` and matching demo code.

Suggested commands:

```bash
python scripts/find_imgui_targets.py texture image
rg -n "ImTextureID|display an image|custom shapes|ImDrawList" docs/FAQ.md imgui.h imgui_demo.cpp imgui_draw.cpp
rg -n "ImDrawList" imgui.h imgui_demo.cpp imgui_draw.cpp
```

## Fonts, icons, DPI, and text rendering

- Start with `docs/FONTS.md`.
- Read the related FAQ entries for DPI and non-Latin text.
- Inspect `imgui_draw.cpp` if the task involves atlas building or font internals.

Suggested commands:

```bash
python scripts/find_imgui_targets.py font dpi
rg -n "DPI|font|glyph|icon|Chinese|Japanese|Korean|Cyrillic" docs/FONTS.md docs/FAQ.md imgui_draw.cpp imgui_demo.cpp
```

## Input routing, IDs, focus, clipping, and integration bugs

- Start with `docs/FAQ.md`.
- Search `imgui.cpp` for the relevant internals only after the FAQ context is clear.
- Inspect the platform backend `.cpp` if the issue is event translation or cursor/input handling.

Suggested commands:

```bash
python scripts/find_imgui_targets.py input focus clipping
rg -n "dispatch mouse|gamepad|ID Stack|clipping|hovered|focus" docs/FAQ.md imgui.cpp backends
```

## Docking, menu bars, and desktop-tool layout

- Confirm the current source supports the requested docking or viewport feature before relying on it.
- Search for `DockSpace`, `DockingEnable`, `ViewportsEnable`, `GetMainViewport`, and `BeginMainMenuBar`.
- Prefer local demo code and current source over old internet snippets.

Suggested commands:

```bash
python scripts/find_imgui_targets.py docking menu viewport
rg -n "DockSpace|DockingEnable|ViewportsEnable|GetMainViewport|BeginMainMenuBar" imgui.h imgui_demo.cpp imgui.cpp docs backends examples
```

## Build and run lookup

- Search the exact example directory first.
- Read `README.md`, `Makefile`, or `CMakeLists.txt` before proposing commands.
- Do not assume there is a root build system unless a root build file exists locally or upstream.

Suggested commands:

```bash
python scripts/find_imgui_targets.py build glfw opengl3
rg --files examples -g 'README*' -g 'Makefile' -g 'CMakeLists.txt'
```

## Response rules

- Prefer exact matches over broad summaries.
- Mention the closest example directory by path or GitHub URL.
- When an external project informed the answer, cite that repo and the exact files you used when possible.
- Explain when a conclusion comes from the public header, example glue code, docs, or internal implementation.
- Explain when a pattern came from an external reference implementation instead of official Dear ImGui sources.
- State whether the answer came from the local checkout or upstream fallback.
