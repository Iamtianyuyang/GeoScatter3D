# Contributing

GeoScatter3D 是一个单进程应用：C++ 引擎负责数据、渲染和状态，Flutter 只负责界面。
这份文件是入口，规则细节见 [docs/RULES.md](docs/RULES.md)，目录与契约见
[docs/architecture.md](docs/architecture.md)，加功能照 [docs/recipes/](docs/recipes/) 做。

> 提交必须让仓库在当次提交后仍能配置、构建、通过测试。

## 环境依赖

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| CMake | 3.28+ | 配置期硬性要求 `Python3`（跑两个守卫脚本） |
| C++ 编译器 | C++20 | Linux: g++ 16 或 clang 23；Windows: MSVC 19.4+ |
| Vulkan | loader + headers 1.3+ | 三端唯一图形后端，不做 GLES 降级 |
| glslangValidator | 任意近期版本 | 编译 `assets/shaders/*.{vert,frag}` 到 SPIR-V |
| Flutter | 3.50+ / Dart 3.12+ | 前端与 Android/Windows/Linux 宿主 |
| JDK | 17+ | Android 构建；`flutter doctor --android-licenses` 需已接受 |
| catch2 | submodule | `git submodule update --init third_party/catch2` |

Linux 上 `Vulkan` 只需 `vulkan-icd-loader` + `vulkan-headers`，着色器编译用
`glslangValidator`（把 `assets/shaders/point.{vert,frag}` 编成 `.spv`，由 `gs3d_shaders`
目标完成）。需要真实 GPU 的测试在没有 Vulkan 设备的机器上会用 `SUCCEED(...)` 跳过。

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

Android（APK 里带 `libgs3d_ffi.so` 与 `libgs3d_pipeline_ffi.so`）：

```bash
./scripts/build_android_arm64.ps1                 # 编译 arm64-v8a 原生库
cd ui_flutter && flutter build apk --release      # 签名配置见 android/key.properties
```

## 测试与门禁

提交前必须全绿，四项缺一不可：

```bash
ctest --test-dir build --output-on-failure          # C++，41 项
cd ui_flutter && flutter analyze --no-pub && flutter test --no-pub   # Dart，60 项
python3 scripts/check_include_deps.py               # 依赖方向
python3 scripts/check_engineering_guardrails.py     # 诊断输出与预算
```

- 渲染改动额外在 Linux 上跑 `ctest -R GeoScatter3D.EngineHost`：它用真实 GPU 回读像素、
  执行命令与取消；没有 Vulkan 设备时用例显式跳过。
- 性能：`gs3d_bench`（`engine/cli/gs3d_bench.cpp`）输出开包毫秒、首帧与稳态帧毫秒、
  峰值 RSS；`scripts/measure_performance.py pack|open` 给出同一组数字。注意 `build/`
  是 Debug，性能必须用 Release 目录（如 `tmp/build-linux`）。CI 目前只构建与测试，
  还没有自动性能门禁。
- `GeoScatter3D.FreshClonePreprocess` / `FreshCloneBundle` 是无 GPU 的端到端
  冒烟：样例 CSV → 工程包 → 用生产 reader 回读。
- CI 三个作业：`windows`、`linux`、`android`（见 `.github/workflows/windows-build.yml`）。
  Android 只做构建与包内容检查，不做设备测试。

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
