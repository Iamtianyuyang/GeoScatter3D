# GeoScatter3D Windows 可移植性状态

**更新日期：2026-07-13**

## 已验证的代码路径

- 截图保存通过 `NativeFileDialog::choose_save_file()`：Windows 使用
  `GetSaveFileNameW`，Linux 使用 Zenity/KDialog，macOS 使用 osascript。取消操作不会
  写文件；没有系统对话框时才回退到 `screenshots/` 下的时间戳文件。
- 最近项目和 GPU 首选项不再写入 Git 跟踪的 TOML。Windows 使用 `%APPDATA%\geoscatter3d`，
  Linux 使用 `$XDG_CONFIG_HOME/geoscatter3d`（默认 `~/.config/geoscatter3d`）。
- 打包资源优先相对配置文件和可执行文件解析。Windows 通过 `GetModuleFileNameW` 定位当前
  可执行文件；字体只从仓库随附的 Regular/Bold 文件加载，不依赖系统盘符或系统字体。
- CMake 在 MSVC 下启用 UTF-8 源码处理；CI 的 Windows job 通过 vcpkg 安装 GLFW、Vulkan
  loader/headers 和 `glslangValidator`，并执行 `ctest`。Linux job 覆盖主开发平台。
- 文件对话框所需的 `comdlg32`、`ole32`、`shell32` 仅在 Windows 上链接。

## 仍需在真实 Windows runner 上确认

本地 Linux 构建和逻辑测试不能替代 Windows 的实际执行。每次改动 Windows 相关代码后，应
由 GitHub Actions 的 Windows job 证明：CMake 配置、MSVC 编译、shader 编译和全部无窗口
CTest 都通过。图形窗口、GPU 驱动组合与原生文件选择器仍需在有桌面会话的 Windows 机器上
手工冒烟测试。

## 不变的依赖要求

- CMake 3.28+ 与支持 C++20 的 MSVC。
- Vulkan SDK/运行时。
- GLFW、Vulkan loader/headers 和 glslang tools；CI 提供 vcpkg 安装命令，开发者可使用
  同一 toolchain 文件复现。
