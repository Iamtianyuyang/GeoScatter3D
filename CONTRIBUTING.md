# Contributing

GeoScatter3D 是一个单进程应用：C++ 引擎负责数据、渲染和状态，Flutter 只负责界面。
这份文件是入口，规则细节见 [docs/RULES.md](docs/RULES.md)，目录与契约见
[docs/architecture.md](docs/architecture.md)，加功能照 [docs/recipes/](docs/recipes/) 做。

> 仓库处于重写期，迁移顺序见 `docs/architecture.md` 末尾的映射表。提交必须让仓库
> 在当次提交后仍能配置、构建、通过测试。

## 环境依赖

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| CMake | 3.28+ | 配置期硬性要求 `Python3`（跑两个守卫脚本） |
| C++ 编译器 | C++20 | Linux: g++ 16 或 clang 23；Windows: MSVC 19.4+ |
| Vulkan | loader + headers 1.3+ | 三端唯一图形后端，不做 GLES 降级 |
| glslangValidator | 任意近期版本 | 编译 `engine/render/*/shaders/*.glsl` 到 SPIR-V |
| Flutter | 3.50+ / Dart 3.12+ | 前端与 Android/Windows/Linux 宿主 |
| JDK | 17+ | Android 构建；`flutter doctor --android-licenses` 需已接受 |
| catch2 | submodule | `git submodule update --init third_party/catch2` |

Linux 上 `Vulkan` 只需 `vulkan-icd-loader` + `vulkan-headers`，着色器编译用
`glslangValidator`。CI 里额外安装 `mesa-vulkan-drivers` 以使用软件光栅（lavapipe）。

## 构建

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Linux Flutter 桌面应用（另需 GTK 3 开发包和 Ninja）：

```bash
scripts/build_flutter_linux.sh
# ui_flutter/build/linux/x64/release/bundle/ui_flutter
```

Windows：

```powershell
vcpkg install vulkan-headers:x64-windows vulkan-loader:x64-windows glslang[tools]:x64-windows
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
```

Android（产物为含 `libgs3d_ffi.so` 的 APK/AAB）：

```bash
cd app && flutter build apk --release
```

## 测试与门禁

提交前必须全绿，四项缺一不可：

```bash
ctest --test-dir build --output-on-failure          # C++，含守卫脚本自测
cd app && flutter analyze && flutter test           # Dart
python3 scripts/check_include_deps.py               # 依赖方向
python3 scripts/check_engineering_guardrails.py     # 诊断输出与预算
```

- 渲染改动额外在 Linux 上跑一次软件光栅冒烟：`ctest -R RenderSmoke`（迁移第 6 步落地），它用
  lavapipe 渲染一帧并比对像素与帧时间；没有 GPU 的 CI 也靠它发现渲染崩溃。
- 性能：`build/gs3d_bench`（第 6 步从 `tools/gs3d_bench_bundle.cpp` 移植）输出开包毫秒、
  预处理各阶段秒数、一帧耗时、峰值 RSS。CI 与基线比较，超过 1.5× 判为回归。
- `GeoScatter3D.FreshClonePreprocess` / `FreshCloneBundle` 是无 GPU 的端到端
  冒烟：样例 CSV → 工程包 → 用生产 reader 回读。
- CI 三矩阵：Windows、Linux、Android。Android 只做构建，不做设备测试。

## 提交与 PR

1. 直接在 `flutter` 分支上小步提交，不建长期分支；每个提交都要能构建、能过测试。
2. 提交前缀：`feat:` / `fix:` / `perf:` / `refactor:` / `test:` / `docs:` /
   `chore:`，必要时带模块标签，例如 `feat(render): ...`。
3. 一个提交只做一件事：删代码、搬文件、改行为不要混在一起——搬文件提交里不夹带
   逻辑修改，否则无法审阅历史。
4. PR 描述写三行：改了什么、怎么验证的（命令与结果）、已知边界。
5. 合并前四项门禁全绿。

## 加功能

不要凭感觉新建目录或新模块。先查 [docs/recipes/](docs/recipes/)：

| 要做的事 | 配方 |
| --- | --- |
| 加一个 Flutter 面板 | [recipes/add-panel.md](docs/recipes/add-panel.md) |
| 加一个引擎命令 | [recipes/add-engine-command.md](docs/recipes/add-engine-command.md) |
| 加一种数据块 | [recipes/add-data-chunk.md](docs/recipes/add-data-chunk.md) |
| 加一个平台适配 | [recipes/add-platform-adapter.md](docs/recipes/add-platform-adapter.md) |
| 加一个测试 | [recipes/add-test.md](docs/recipes/add-test.md) |

配方里没有对应类型的，先补一篇再动手。

## 不允许

- 提交密钥、凭据、本机绝对路径。
- 编辑 `third_party/` 下的 vendored 代码（catch2 submodule、tomlplusplus、stb）。
- 为实现某个功能引入新的第三方依赖而不在 PR 里说明理由与体积代价。
- 删除失败测试来让 CI 变绿；测试失败先当作真问题查。
- 在 Dart 侧保存一份与引擎冲突的权威状态（见规则三）。
- 启用 `-march=native` 或绑定特定 CPU 的编译选项。
