# Contributing

## 构建与测试

依赖: CMake 3.28+, C++20 编译器, Vulkan SDK/开发包, GLFW 3,
线程库, Python 3 (配置期硬性依赖: `find_package(Python3 REQUIRED)`,
驱动 `check_include_deps.py` 与 `check_engineering_guardrails.py` 测试),
`glslangValidator` (CMake 从 GLSL 自动生成 SPIR-V)。Git submodule:
`third_party/imgui`、`third_party/catch2`。

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Windows (vcpkg):

```powershell
vcpkg install glfw3:x64-windows vulkan-headers:x64-windows vulkan-loader:x64-windows glslang[tools]:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

详细说明见 [README](README.md)。

## 测试约定

- 单元测试为 Catch2 (`tests/*.cpp`), 每个文件一个可执行目标
  (`GeoScatter3D*Tests`), 全部进默认 ctest 套件; 可按标签或用例名过滤:
  `./build/GeoScatter3DDataTests "[gs3d]"`。
- `GeoScatter3DBenchmark` 为手动运行的性能基准, 不进默认 ctest。
- `GeoScatter3D.FreshClonePreprocess` / `GeoScatter3D.FreshCloneBundle`
  是无 GPU 环境的端到端冒烟测试 (样例 CSV → bundle → 生产 reader 回读)。
- `GeoScatter3D.IncludeDeps` / `GeoScatter3D.EngineeringGuardrails`
  为 Python 脚本测试: include 依赖卫生与架构行数护栏。

## 工程护栏

- `scripts/check_engineering_guardrails.py` 约束关键文件行数上限
  (当前: ViewerApp.cpp 912 / run() 716 / UiRoot.cpp 1207 /
  AppConfig.cpp 763)。PR CI 与 `merge-base(base, HEAD)` 比较, 预算只允许
  下降; 本地快速检查与 `HEAD^` 比较。
- `scripts/check_include_deps.py` 校验 include 依赖分层。
- 修改这三种二进制格式 (GS3D/LOD/tile) 必须同时更新
  `docs/spec/gs3d-format.md` 与相关测试 (格式语义以代码为准)。
- 新增/修改 viewer.toml 配置键时同步更新 `docs/config-reference.md`。

## PR 流程

1. 从 `main` 新建分支, 按仓库惯例命名 (如 `fix/...`、`feat/...`)。
2. 提交信息遵循仓库风格: 前缀 `feat:`/`fix:`/`perf:`/`refactor:`/
   `test:`/`docs:`/`chore:`, 必要时附模块标签 `(data)`/`(ui)`/`(render)`。
3. 本地 `ctest --test-dir build --output-on-failure` 全绿后再推送。
4. 打开 PR 到 `main`; PR 标题/正文含关联任务标识 (如 `TIA-<N>`), 正文
   列出改动、验证证据 (测试/日志路径) 与已知边界。
5. CI (GitHub Actions) 会执行无窗口测试与护栏检查; 合并前等待评审。
6. 合并后如需, 在 CHANGELOG.md 补一条里程碑记录。

## 行为准则

- 不提交密钥/凭据; 不编辑 `third_party/` 下的 vendored 代码
  (imgui submodule、tomlplusplus、stb、nanosvg)。
- 不默认启用 `-march=native` 或绑定特定 CPU 的代码生成选项。
- 不删除失败测试来让 CI 变绿。
- 文档与代码保持同步: 实现变更涉及的行为描述 (README / architecture.md
  / config-reference.md / 格式规范) 须随 PR 更新。
