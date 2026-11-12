# 配方：加一个平台适配

平台代码只有两种去处：`platform/<os>/`（窗口、表面、文件对话框、后台服务）和
`app/<os>/`（Flutter 宿主）。**引擎从不 include、也不链接 `platform/`**，方向只有
一条：宿主 → `engine/abi`。

## 步骤

1. **定数据形状**（`engine/abi/`）：平台间的差异先收敛成一个 POD，放进 ABI 头。
   例如原生表面只有 `{ void* handle; uint32_t width; uint32_t height; uint32_t kind; }`，
   引擎不认识 `ANativeWindow*`、`HWND` 这些类型。
2. **宿主实现**（`platform/<os>/`）：负责创建与销毁句柄，拿到后按第 1 步的 POD
   填好，调用 ABI（例如 `gs3d_engine_attach_surface`）。
3. **生命周期写清楚**：谁创建、谁销毁、销毁前必须调用哪个 ABI、哪一侧在哪个线程
   执行、是否需要 GPU 空闲。Android 旋转与锁屏是必须覆盖的用例。
4. **桩实现**（`platform/null/`）：返回"不支持"，让其他平台与 CI 能编。
5. **CMake**：平台源文件在 `platform/CMakeLists.txt` 里按 `WIN32` / `ANDROID` /
   `UNIX` 选择，只链接进宿主目标，不链接进 `engine` 目标。
6. **测试**：
   - 用桩实现跑一条单测，断言"不支持"的错误码；
   - 生命周期用例：attach → detach → 再使用句柄必须报错而不是崩溃；
   - 真实路径进对应 CI 矩阵（Android 只构建，不跑设备测试）。

## 反面例子

在 `engine/render/device/VulkanContext.cpp` 里写
`#ifdef _WIN32 ... #elif defined(__ANDROID__) ...` 去创建表面。
后果：引擎再也无法在无窗口环境（CI、预处理命令行）里编译，平台逻辑散落在渲染
代码里；同时 `patch platform` 与 `include platform` 两个方向同时存在，形成循环。

## 检查

- [ ] `engine/` 下没有任何平台宏
- [ ] 引擎目标不链接 `platform/`
- [ ] 句柄生命周期有测试，且 detach 后使用会报错
- [ ] 存在桩实现，三端都能编
