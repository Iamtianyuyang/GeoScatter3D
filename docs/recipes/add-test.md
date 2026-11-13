# 配方：加一个测试

测试的价值在于"坏了会红"，不在于覆盖率数字。

## C++

1. **位置**：`tests/<模块><对象>Tests.cpp`，一个文件一个可执行目标。
2. **注册**（`tests/CMakeLists.txt`）：
   ```cmake
   add_executable(GeoScatter3D<模块><对象>Tests
       <模块><对象>Tests.cpp
       ${PROJECT_SOURCE_DIR}/src/ffi/TaskBoard.cpp   # 需要时列出被测源文件
   )
   target_include_directories(GeoScatter3D<模块><对象>Tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
   target_link_libraries(GeoScatter3D<模块><对象>Tests PRIVATE Catch2::Catch2WithMain gs3d_data)
   add_test(NAME GeoScatter3D.<模块>.<对象> COMMAND GeoScatter3D<模块><对象>Tests)
   ```
   测试名对齐已有风格（`GeoScatter3D.TaskBoard`、`GeoScatter3D.BundleBuild`）。
   链接 DLL 的目标要加 `POST_BUILD` 拷贝（Windows 没有 rpath，缺 DLL 时进程在
   `main()` 之前就卡住，CTest 只会报超时）。
3. **写什么**：至少三条——正常路径、边界（空/零/最后一项/最大值）、错误输入
   （断言得到预期错误码或异常，而不是"没崩"）。
4. **标签**：用 Catch2 的 `[gs3d]`、`[tile]`、`[abi]` 分节，方便 `-R` 过滤。
5. **GPU**：需要真实 Vulkan 的用例放 `tests/EngineHostTests.cpp`；设备创建失败时用
   `SUCCEED("no Vulkan device; …")` 明确跳过（见该文件第 367、447 行），
   不要靠断言失败来表达"没显卡"。其余用例不许需要显卡。
6. **并发**：改快照或任务状态这类跨线程代码时，加一条压测——多个线程反复读写，
   断言 `generation` 单调、同一代内字段自洽、终态只被读取一次。

## Dart

1. **位置**：`ui_flutter/test/<对象>_test.dart`；需要真实动态库的用例用环境变量门控，
   默认跳过（见 `engine_test.dart` 的 `GS3D_ENGINE_E2E`、`pipeline_service_test.dart`
   的 `GS3D_PIPELINE_LIBRARY`）。
2. **不碰真引擎**：界面测试用 `ui_flutter/test/support/fake_engine.dart` 的
   FakeEngine，记录调用并伪造数据集信息。
3. **布局断言**：结构体镜像都要有 `sizeOf` 与字段偏移断言
   （`ui_flutter/test/engine_state_layout_test.dart`）。这类错误不崩，只会显示错数，
   唯一能拦住它的就是测试。
4. **别在 widget 测试里做文件 IO**：`testWidgets` 的 FakeAsync 会让真实 `await`
   永远挂住；把落盘逻辑放在 service 单测里，`pumpAndSettle` 遇到 service 定时器
   不收敛，用 `pump(Duration(...))`。

## 端到端

- 原生包链路：`scripts/package-linux.sh` 或 CI 的 Android 作业构建后由 Dart 测试
  实际建包（`GS3D_PIPELINE_LIBRARY` 指到 `libgs3d_pipeline_ffi.so`）。
- 三端 CI 在 `.github/workflows/windows-build.yml`，作业名 `windows` / `linux` /
  `android`；`ctest` 带 `--timeout 300`，避免无 GPU runner 卡满作业时限。

## 检查

- [ ] 失败用例断言了具体错误码或异常类型
- [ ] GPU / 设备相关用例有门控，默认能跳过
- [ ] 新增结构体有布局断言
- [ ] `cd build && ctest -j8` 全绿，`cd ui_flutter && flutter test --no-pub` 全绿
