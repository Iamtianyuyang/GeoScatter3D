# GeoScatter3D Windows 跨平台可移植性分析报告

**日期**: 2026-07-07
**范围**: 扫描全部 `src/` `include/` `CMakeLists.txt`，不改代码
**结论**: 项目对跨平台已有良好意识（NativeFileDialog 有完整 Windows 实现，字体路径覆盖三平台，路径用 std::filesystem），但仍有 **2 个阻塞项** 和 **3 个高风险项** 需要处理。

---

## ⛔ CRITICAL — 编译/运行崩溃

### C1. 截图保存对话框硬编码 `popen("zenity...")`，Windows 无 zenity

**文件**: [src/app/ViewerApp.cpp:6146-6151](src/app/ViewerApp.cpp#L6146-L6151)

```cpp
FILE* zf = popen(
    "zenity --file-selection --save "
    "--confirm-overwrite "
    "--filename=screenshot.png "
    "--file-filter='PNG Images | *.png' "
    "2>/dev/null", "r");
```

- **问题**: 这段代码不在 `#if defined(_WIN32)` 保护内，Windows 上没有 `zenity` 也没有 `popen`（MSVC 用 `_popen`）。
- **影响**: 截图功能在 Windows 上编译失败或运行崩溃。
- **修复方向**: 复用 `NativeFileDialog.cpp` 的模式 —— Windows 上用 `GetSaveFileNameW` 或 `IFileSaveDialog`，或者让 `NativeFileDialog` 暴露 `choose_save_file()` 接口。
- **工作量**: ~1h（参考已有 `choose_windows_raw_file` 实现）。

---

## 🔴 HIGH — 行为错误 / 数据丢失

### H1. 「最近项目」配置路径硬编码 Linux XDG 规范

**文件**: [src/app/RecentProjects.cpp:39-51](src/app/RecentProjects.cpp#L39-L51)

```cpp
if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) { // Linux only
    config_root = xdg_config;
}
if (config_root.empty()) {
    if (const char* home = std::getenv("HOME")) {  // Linux/macOS only
        config_root = std::filesystem::path(home) / ".config";
    }
}
```

**结果路径**: `~/.config/geoscatter3d/recent-projects.txt`

- **问题**: Windows 上没有 `XDG_CONFIG_HOME` 和 `HOME`（或者 HOME 指向不相关路径）。`temp_directory_path()` fallback 会把历史项目列表放在临时目录，用户重启后数据丢失。
- **Windows 标准**: `%APPDATA%\geoscatter3d\recent-projects.txt` 或 `%LOCALAPPDATA%\geoscatter3d\recent-projects.txt`。
- **修复方向**: 在 `#if defined(_WIN32)` 分支读取 `%APPDATA%` 环境变量。
- **工作量**: ~30min。

### H2. 系统字体路径假设 `C:\Windows\Fonts`

**文件**: [src/gui/ImGuiLayer.cpp:278-281](src/gui/ImGuiLayer.cpp#L278-L281)

```cpp
"C:/Windows/Fonts/msyh.ttc",
"C:/Windows/Fonts/msyh.ttf",
"C:/Windows/Fonts/segoeui.ttf",
"C:/Windows/Fonts/simhei.ttf",
```

- **问题**: Windows 可能安装在 D: 盘；路径硬编码不符合 Windows 规范。`std::filesystem::path` 能处理正斜杠，但 `C:` 是硬假设。
- **修复方向**: 用 `std::getenv("WINDIR")` 或 `std::getenv("SystemRoot")` 拼接 `\\Fonts\\`。
- **工作量**: ~15min。

### H3. 截图保存 `zenity` 失败后的 fallback 路径写法 Linux 风格

**文件**: [src/app/ViewerApp.cpp:6166](src/app/ViewerApp.cpp#L6166)

```cpp
std::filesystem::create_directories("screenshots");
```

- 这本身 `create_directories` 是跨平台的。但如果 C1 没修好，走到这里的逻辑不可达；修好 C1 后这行没问题。
- **影响**: 低（依赖 C1 修复）。

---

## 🟡 MEDIUM — 构建系统 / 依赖

### M1. CMake 依赖获取：Windows 上缺少 find_package 的默认搜索路径

**文件**: [CMakeLists.txt:10](CMakeLists.txt#L10)

```cmake
find_package(glfw3 REQUIRED)
```

- **Linux**: `apt install libglfw3-dev` 后 CMake 自动找到。
- **Windows**: GLFW 不随系统自带。需要：
  - vcpkg: `vcpkg install glfw3:x64-windows`
  - 或手动编译 GLFW 并用 `-DCMAKE_PREFIX_PATH=...` 指定
  - 或把 GLFW 源码加入 third_party（如 imgui 的模式）
- **建议**: 提供 Windows 构建文档说明依赖安装方式；或添加 CMake `FetchContent` fallback。
- **工作量**: 文档 ~30min；FetchContent ~2h。

### M2. Vulkan SDK 依赖

**文件**: [CMakeLists.txt:9](CMakeLists.txt#L9)

```cmake
find_package(Vulkan REQUIRED)
```

- **Linux**: `apt install vulkan-sdk` 后自动找到。
- **Windows**: 需从 LunarG 官网下载安装 Vulkan SDK，安装后 CMake 自动找到。**这是 GPU 应用的正常依赖，不算平台问题**，但需在文档中说明。
- **影响**: 文档层面。

### M3. MSVC 编译器：C++20 语法兼容性

项目使用以下 C++20 特性，需 MSVC 2019 16.1+ 或 VS 2022：

| 特性 | 文件中出现次数 | MSVC 支持 |
|---|---|---|
| `[[nodiscard]]` | ~468 次 | ✅ VS 2017 15.3 |
| Designated initializers (`{.x = 1, .y = 2}`) | WelcomePage.cpp 等 | ⚠️ VS 2019 16.1+, 需 `/std:c++20` |
| `std::filesystem` | 全项目 | ✅ VS 2017 |
| `std::optional` | 多处 | ✅ VS 2017 |
| `<chrono>` | 多处 | ✅ VS 2015 |

- **需添加**: CMake 中 MSVC 需指定 `/std:c++20`（或至少 `/std:c++latest`）。
- **当前状态**: CMakeLists 设了 `CMAKE_CXX_STANDARD 20` 和 `CMAKE_CXX_EXTENSIONS OFF`，MSVC 需要 `CMAKE_CXX_STANDARD_REQUIRED ON`（已设置）——但 MSVC 的 C++20 标准版本标志需要 CMake 3.28+配合 MSVC 工具链正确处理。

### M4. min/max 宏冲突

**文件**: [src/platform/NativeFileDialog.cpp:9](src/platform/NativeFileDialog.cpp#L9)

```cpp
#if defined(_WIN32)
#define NOMINMAX
```

- ✅ 已处理。Windows 的 `<Windows.h>` 定义 `min`/`max` 宏会与 `std::min`/`std::max` 冲突，`NOMINMAX` 阻止这个行为。

---

## 🟢 GREEN — 已跨平台或无需修改

### ✅ 文件对话框

[src/platform/NativeFileDialog.cpp](src/platform/NativeFileDialog.cpp) 已有完整的三平台实现：

| 平台 | 项目选择(`choose_project_directory`) | 文件选择(`choose_raw_data_file`) |
|---|---|---|
| Windows | COM `IFileOpenDialog` + `FOS_PICKFOLDERS` | Win32 `GetOpenFileNameW` |
| macOS | `osascript choose folder` | `osascript choose file` |
| Linux | `zenity` → `kdialog` fallback | `zenity` → `kdialog` fallback |

### ✅ Vulkan Surface 创建

[src/render/VulkanContext.cpp:140-152](src/render/VulkanContext.cpp#L140-L152):
```cpp
glfwCreateWindowSurface(instance_, window.native_handle(), nullptr, &surface_);
```

GLFW 内部根据平台自动选择正确的 VK 扩展：
- Windows: `VK_KHR_win32_surface`
- Linux/X11: `VK_KHR_xlib_surface`
- Linux/Wayland: `VK_KHR_wayland_surface`
- macOS: `VK_EXT_metal_surface` (via MoltenVK)

`glfwGetRequiredInstanceExtensions()` 也自动返回对应扩展列表。**无需任何修改。**

### ✅ 文件 I/O / 大文件读取

- 全部使用 `std::ifstream` (`read()`, `seekg()`, `tellg()`) — 跨平台标准库
- **未使用 mmap** — 无移植问题
- 数据文件 (.gs3d, .csv, .dat) 用 binary 模式打开 — Windows 上 `std::ios::binary` 正确阻止 CRLF 转换
- `stbi_write_png` 截图输出 — 单头文件 C 库，跨平台

### ✅ 线程

[include/util/ThreadPool.hpp](include/util/ThreadPool.hpp):
- `std::thread`, `std::mutex`, `std::condition_variable` — C++11 标准跨平台
- 无 pthread 直接调用

### ✅ 时间

- `std::chrono::steady_clock`, `std::chrono::system_clock` — 跨平台
- `std::localtime`, `std::strftime` — C 标准跨平台（注：localtime 非线程安全，但 Linux/Windows 都有此问题）

### ✅ 路径处理

- 全部使用 `std::filesystem::path`，不手动拼 `/` 或 `\`
- `path.generic_string()` 用于序列化（统一用 `/`）— 跨平台
- `std::filesystem::create_directories` — 跨平台
- 字体路径和 Shader 路径用 `path / "subdir"` 语法 — 跨平台
- 硬编码 Linux 字体路径 (`/usr/share/fonts/...`) 在运行时探测中用 `file_exists()` 过滤 — 不存在就跳过，不会崩溃

### ✅ DPI 缩放 / UI 缩放

[src/gui/ImGuiLayer.cpp:70-235](src/gui/ImGuiLayer.cpp#L70-L235):
- 基于 GLFW 的 `glfwGetMonitorPhysicalSize()` 和 `glfwGetVideoMode()` — 跨平台
- 代码中有 fallback（physical size 不可用时用分辨率比例推测），甚至对 Linux/X11 返回 0x0 的 bug 做了防御
- Windows 上 `glfwGetMonitorPhysicalSize()` 返回正确的 EDID 值，DPI 缩放会正常工作

### ✅ CMake WIN32 条件

[CMakeLists.txt:210-212](CMakeLists.txt#L210-L212):
```cmake
if(WIN32)
    target_link_libraries(GeoScatter3D PRIVATE comdlg32 ole32 shell32)
endif()
```

已为 Windows 文件对话框的 COM/Win32 API 链接正确的系统库。

### ✅ 环境变量调试开关

`GS3D_LOD_DEBUG` 和 `GS3D_RECENT_PROJECTS_PATH` 用 `std::getenv()` — 跨平台。这些是开发者工具，不是用户功能。

---

## 📊 工作量汇总

| 优先级 | 问题 | 文件 | 预估工作量 | 风险 |
|---|---|---|---|---|
| ⛔ CRITICAL | 截图 `popen("zenity")` | ViewerApp.cpp:6146 | 1h | 截图功能 Windows 不可用 |
| 🔴 HIGH | 最近项目路径 `~/.config` | RecentProjects.cpp:39-51 | 30min | 重启丢失历史 |
| 🔴 HIGH | 字体路径 `C:\Windows` 硬编码 | ImGuiLayer.cpp:278-281 | 15min | Windows 非 C 盘安装出错 |
| 🟡 MEDIUM | GLFW 依赖获取 | CMakeLists.txt | 文档 30min | 首次构建需手动安装 |
| 🟡 MEDIUM | MSVC `/std:c++20` 确认 | CMakeLists.txt | 验证 15min | 需 VS 2019 16.1+ |
| 🟡 MEDIUM | Vulkan SDK 依赖文档 | 文档 | 15min | 首次构建需手动安装 |
| **合计** | | | **~3h** | |

---

## 🎯 建议修复优先级

1. **先修 C1（截图对话框）**—— 阻塞编译/运行，且已有 `NativeFileDialog.cpp` 可复用模式
2. **再修 H1（最近项目路径）**—— 用户体验问题，重启丢历史
3. **再修 H2（字体路径）**—— 边界情况，非 C 盘安装
4. **然后写 Windows 构建文档** —— 说明 GLFW/Vulkan SDK 安装
5. **最后 CI 验证** —— 在 Windows Runner 上跑一次编译确认

整体来看，项目跨平台基础扎实（Vulkan 通过 GLFW 抽象、文件对话框已有 Windows 实现、路径用 `std::filesystem`、线程用 C++ 标准库、无 mmap），**移植工程量的 90% 是修补截图对话框中一个遗漏的 `popen` 调用**。预计整个 Windows 适配工作可在半天内完成。
