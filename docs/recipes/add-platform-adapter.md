# 配方：加一个平台适配

三端各自的差异只允许出现在两处：Flutter 宿主（`ui_flutter/<os>/`）与 C++ 侧的
JNI/ABI 胶水（`engine/abi/AndroidPipelineJni.cpp`、`src/ffi/AndroidSurfaceJni.cpp`）。
**引擎核心（`engine/`）不含平台宏，也不链接宿主代码**，方向只有一条：宿主 → C ABI。

## 现有位置

| 平台 | 窗口与文件选择 | 后台任务 |
| --- | --- | --- |
| Linux | `ui_flutter/linux/runner/my_application.cc`（GTK 窗口与文件对话框方法通道） | 无，界面退出即取消 |
| Windows | `ui_flutter/windows/runner/*` + `src/ffi` 的原生选择器 | 无 |
| Android | `ui_flutter/android/app/src/main/kotlin/com/geoscatter3d/ui_flutter/MainActivity.kt`（SAF 选择、把内容复制到持久目录） | `PipelineJobService.kt` 持有 JNI 管线任务 |

## 步骤

1. **先定形状**：平台差异收敛成一个 POD 或一个方法通道契约，放进
   `include/ffi/geoscatter3d_ffi.h` 或 Dart 的 `MethodChannel` 方法表。
   引擎不认识 `ANativeWindow*`、`HWND`、`GdkWindow*` 这些类型。
2. **C++ 胶水**：需要 JNI 的放 `engine/abi/`（管线）或 `src/ffi/`（表面与选择器），
   只依赖 C ABI 与标准库；错误转成负返回码或 Java 异常，不抛过 JNI 边界。
3. **生命周期写清楚**：谁创建、谁销毁、销毁前必须停哪个任务、在哪个线程执行。
   Android 的旋转、锁屏、进程回收必须各有用例；界面销毁不等于任务结束。
4. **兜底实现**：平台缺能力时返回明确错误（例如 Linux 无后台服务时提示"退出即取消"），
   不做静默降级。
5. **构建**：宿主代码进 `ui_flutter/<os>/` 的构建文件（Linux 是
   `ui_flutter/linux/CMakeLists.txt`，Windows 是 `scripts/build_flutter_windows.ps1`
   调用的 CMake，Android 是 `ui_flutter/android/app/build.gradle.kts` 与
   `scripts/build_android_arm64.ps1`），不进 `engine/` 目标。
6. **测试**：
   - C++：`tests/PipelineAbiTests.cpp` 覆盖建包与取消；
   - JNI：用 host JDK 编译 `engine/abi/AndroidPipelineJni.cpp` 并链接
     `libgs3d_pipeline_ffi.so` 跑一次（验证 UTF-8 路径、poll、非法输入拒绝）。
     仓库里还没有这条脚本，需要时放 `tests/` 并在 CI 里接上；
   - 三端 CI 只做构建 + 无设备测试，设备行为要写进 `docs/architecture.md` 的验收记录。

## 反面例子

在 `src/render/**/VulkanContext.cpp` 里写 `#ifdef _WIN32 … #elif defined(__ANDROID__)`
去创建表面。后果：引擎再也无法在无窗口环境（CI、预处理命令行）里编译，平台逻辑
散落在渲染代码里，`include platform` 与 `patch platform` 两个方向同时存在。

## 检查

- [ ] `engine/` 下没有平台宏
- [ ] 引擎目标不链接宿主代码
- [ ] 句柄与任务生命周期有测试，界面销毁不影响任务
- [ ] 缺能力时返回明确错误，没有静默降级
