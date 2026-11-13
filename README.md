# GeoScatter3D

大规模三维散点／点云的离线可视化工具。C++20 + Vulkan 引擎负责数据、渲染与状态，
Flutter 只负责界面，两者在同一进程里通过 C ABI 相连：Windows、Linux、Android
三端都能自己完成预处理，工程包在三端之间可以直接拷贝。

- 引擎不依赖窗口系统，也不依赖 Dart；图形句柄不出 C++ 边界。
- 视图只显示 `gs3d_engine_*` 产出的 RGBA 帧；引擎不可用时明确报错，不用 Dart 软件渲染冒充。
- 重任务（预处理、开包）有任务号、阶段与取消点，界面线程不等它们；导出与读帧是同步调用，
  因此有明确点数上限。

## 能力

**预处理**：CSV / DAT → 工程包（GS3D v3）。阶段依次是 `fingerprint`（SHA-256 与选项摘要）、
`stats`（点数、范围、值域、属性）、`points`（分块解析、并行写点块）、`lod`（写 `lod.gs3dlod`
sidecar）。每个阶段完成时原子提交检查点，中断后用 `gs3d_pack --resume <bundle-dir>` 从
检查点续跑；源文件或选项变了、块校验失败、检查点不完整都会被拒绝，不会覆盖已有工程包。

**渲染**：离屏 Vulkan 点管线，多层 LOD（`lod.gs3dlod`，按绘制预算选层）与有限预算的
GPU 上传。配色、点大小、点形状、背景、高程与着色来源、值域裁剪、绘制上限都能在界面里改。
瓦片流式只对旧版带 `tiles.gs3dtiles` 的数据集生效，v3 工程包没有瓦片。
帧经回读到 Flutter 纹理显示，1080p 回读加绘制约 4.5 ms。

**异步 ABI**（`include/ffi/geoscatter3d_ffi.h`）：4 槽任务表与整代状态快照
（`gs3d_engine_poll_state`），开包在单独的 IO 线程读盘、渲染线程装载；普通命令
（相机、点大小、配色、背景、缩放、出帧、卸载）经 `gs3d_engine_submit` 入队即返回。
无 GPU 的 `gs3d_pipeline_ffi` 提供预处理任务 ABI，供 Flutter 与 Android 前台服务使用。

**界面**：单一控制台。顶部是数据集与「打开工程包」，左侧 52px 工具轨加一列面板
（工程、预处理、渲染、相机、测量、成果、诊断），面板展开时挤压视口而不是盖住点云，
底部一条状态条（点数、`源 → 绘制`、LOD、FPS、帧时间、渲染时间、GPU）。
视口里只有视图轴、缩放、色标图例和测量读数。

**Android**：前台服务持有预处理任务，退出界面后继续跑，重新进入界面时重连；
用 `dataSync` 服务类型与部分唤醒锁，系统回收进程后按最近项目手动续跑。

## 构建

需要 CMake 3.28+、C++20 编译器、Vulkan SDK、Flutter 3.50+；Windows 另需 Visual Studio
桌面 C++ 工具与 vcpkg（glslang、glfw、nlohmann-json）。

```bash
# Linux：引擎 + Flutter 发布包（产物在 ui_flutter/build/linux/x64/release/bundle/）
scripts/build_flutter_linux.sh
# 只构建引擎与命令行工具
cmake -B tmp/build-linux -DCMAKE_BUILD_TYPE=Release && cmake --build tmp/build-linux -j
```

```powershell
# Windows：构建 DLL、复制 shader 与中文字体、出 Flutter 包
./scripts/build_flutter_windows.ps1 -Config Release
# Android：两个 ABI 的原生库 + APK
./scripts/build_android_arm64.ps1
./scripts/build_android_release.ps1   # 需要 ui_flutter/android/key.properties
```

Android 包内的 `libgs3d_ffi.so`、`libgs3d_pipeline_ffi.so` 随仓库跟踪；改过引擎代码后要
用上面的脚本重新交叉编译并覆盖 `ui_flutter/android/app/src/main/jniLibs/<abi>/`。

## 测试与测量

```bash
cd build && ctest -j8                     # 41 项 C++ 测试
cd ui_flutter && flutter test --no-pub    # 60 项：54 通过、6 跳过
# 跳过的是 3 个真实 GPU 用例（需 GS3D_ENGINE_E2E=1）与 3 个管线库用例（需 GS3D_PIPELINE_LIBRARY）
GS3D_ENGINE_E2E=1 GS3D_PIPELINE_LIBRARY=$PWD/../tmp/build-linux/src/ffi/libgs3d_pipeline_ffi.so \
  LD_LIBRARY_PATH=../tmp/build-linux/src/ffi ../tmp/build-linux/src \
  flutter test --no-pub                   # 60 项全跑，含真实 GPU 与 JNI 管线
python3 scripts/check_include_deps.py
python3 scripts/check_engineering_guardrails.py
```

性能用 `scripts/measure_performance.py`（`pack` / `open`）与 `gs3d_bench`，两者都读 `VmHWM`，
`open`/`gs3d_bench` 必须在带 `assets/shaders` 的目录（`ui_flutter/`）下运行。本机实测
（Ryzen 9 7945HX，GCC Release，`data/field_and_ele_source_rec_50m.dat`，50,000,000 点、3.35 GB）：

| 项目 | 结果 | 预算 |
| --- | --- | --- |
| 预处理（含 lod sidecar） | 115 s，峰值 3.09 GiB | ≤ 60 s、≤ 2 GB，未达标 |
| 预处理 `--no-lod` | 55 s，峰值 637 MiB | 达标 |
| 打开工程包 | 2.40 s，1.20 GiB | ≤ 1.5 s，未达标 |
| 1080p 帧 / 首帧 | 4.5 ms / 22 ms | ≤ 16 ms，达标 |

`build/` 是 Debug 目录，其中的耗时不能当性能依据。

## 目录

```text
engine/         引擎核心（不依赖窗口与 Dart）
  foundation/     diagnostics、io、concurrency、math、util
  data/           csv 解析、format（manifest、点块、v3 读取）、lod、tile
  pipeline/       stages（阶段机、检查点、恢复）、compute、writers、verify
include/ffi/     C ABI 头（geoscatter3d_ffi.h）与宿主侧头（EngineHost、TaskBoard…）
src/             ffi（C ABI 实现、内核、宿主）、render、camera、app、control、ui、platform
ui_flutter/      Flutter 应用：lib/src/{layouts,widgets,ui,ffi,theme}、android/、linux/
tests/           C++ 测试（Catch2）
scripts/         构建、打包、依赖与护栏检查、性能测量
docs/            架构、规则、格式规范、配置参考、配方
data/            样例数据与 shader
```

依赖方向与边界见 [docs/RULES.md](docs/RULES.md)；线程、ABI、工程包格式与实测记录见
[docs/architecture.md](docs/architecture.md)。

## 已知限制

- Android 只有构建与 JVM 级冒烟（JNI 建包、污染输入拒绝），没有在设备上跑过完整流程。
- 打开工程包 2.40 s、带 lod 的预处理 115 s 都超预算，原因与改法记在 `docs/architecture.md` 的「未决」。
- 导出仍是同步调用，超过 200 万点直接拒绝；拾取在渲染线程线性扫描，50M 数据集点一次要数秒。
- `gs3d_ffi_*` 旧段里的区域统计、导航图缩略图、属性列表与 `performance_metrics` 读的是旧数据集缓存，
  本应用走 `gs3d_engine_*` 开包，这些调用返回空数据或 stub，界面没有接入。

## 文档

- [docs/architecture.md](docs/architecture.md)：目标、线程模型、ABI 契约、工程包格式、平台差异、前端结构、验收记录
- [docs/RULES.md](docs/RULES.md)：六条硬规则与校验命令
- [docs/config-reference.md](docs/config-reference.md)：`viewer.toml` 与 `preferences.toml`（旧查看器与 CLI 使用）
- [docs/spec/](docs/spec/)：旧版单文件格式与 Vulkan 核心边界
- [docs/recipes/](docs/recipes/)：加数据块、加命令、加面板、加平台适配、加测试的步骤
- [ui_flutter/README.md](ui_flutter/README.md)：前端结构、交互与测试
