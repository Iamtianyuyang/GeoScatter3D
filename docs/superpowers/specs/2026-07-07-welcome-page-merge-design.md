# Welcome Page Merge — Design Spec

**Date:** 2026-07-07
**Branch:** feat/welcome-page

## Goal

Merge the duplicate "新建项目" and "加载原始数据" entries on the welcome page into a single "新建项目" flow that includes project naming.

## Current vs Target

| Aspect | Current | Target |
|--------|---------|--------|
| Action cards | 3: 新建项目, 打开项目, 加载原始数据 | 2: 新建项目, 打开项目 |
| OpenRawData | Separate card + result kind | Removed; merged into NewProject |
| Project naming | Auto-derived from data filename | User-provided; validated; becomes bundle dir name |

## Detailed Design

### 1. WelcomePage.hpp — New enums and state

- Remove `OpenRawData` from `WelcomePageActionKind`
- Add `NewProject` to `WelcomePageActionKind`
- Add `std::string project_name` to `WelcomePageAction`
- Add `NewProjectDialogState` struct:
  - `bool active = false`
  - `std::string data_file_path` (from native file dialog)
  - `std::string project_name` (ImGui input buffer)
  - `std::string error_message`
  - `bool name_conflict = false`
- Add dialog state to `WelcomePageModel`

### 2. WelcomeWindow.hpp — New result kind and project name

- Remove `OpenRawData` from `WelcomeWindowResultKind`
- Add `NewProject`
- Add `std::string project_name` to `WelcomeWindowResult`

### 3. WelcomePage.cpp — Modal dialog and 2-card layout

**Cards:**
- Remove "加载原始数据" card entirely
- Update "新建项目" subtitle: "导入 DAT/CSV 数据创建项目"
- Clicking "新建项目" initializes dialog state (clears fields)

**Modal dialog (ImGui popup):**
- File selection button: calls `choose_raw_data_file()` (existing native dialog)
- On file selected: populate path display, auto-fill project name = file stem
- Project name input: `ImGui::InputText` with real-time illegal char filtering (`/ \ : * ? " < > |`)
- Show hint: "项目名不能包含: / \\ : * ? \" < > |"
- Confirm button:
  - Validate: name not empty (show error if empty)
  - Validate: name not only whitespace
  - Check: `<name>.gs3d.bundle` exists? If yes → show conflict options:
    - "覆盖" — proceed, will overwrite
    - "重命名" — go back to editing
  - If valid + no conflict (or user chose overwrite): return `NewProject` action
- Cancel button: close dialog, no action

### 4. WelcomeWindow.cpp — Wire NewProject

- Handle `NewProject` action kind → map to `WelcomeWindowResultKind::NewProject`
- Copy `project_name` from action to result

### 5. main.cpp — Handle NewProject result

- Replace `OpenRawData` handling with `NewProject`:
  - `csv_input_path = result.path`
  - `input_mode` derived from file extension (csv/dat)
  - `bundle_dir = result.path.parent_path() / (result.project_name + ".gs3d.bundle")`
- Remove `apply_open_request` for RawData case path

## Validation Checklist

1. Welcome page shows only 2 cards: "新建项目" + "打开项目"
2. New project dialog: file selection + project name (default = file stem)
3. Illegal chars filtered in real-time with hint shown
4. Empty name blocked on confirm with error message
5. Existing folder conflict offers overwrite/rename
6. Confirm creates `<name>.gs3d.bundle` bundle, converts data, enters main view
7. Build + ctest all green
