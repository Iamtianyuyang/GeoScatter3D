# 配方：加一个平台适配

平台相关代码只有两种去处：`platform/<os>/`（窗口、表面、文件对话框、后台任务）和
`app/<os>/`（Flutter 宿主）。引擎里不许出现 `#ifdef _WIN32`。

## 步骤

1. **接口**（`platform/<thing>.hpp`）：只声明平台差异，不暴露平台类型。
   用枚举/不透明句柄描述结果，不用 `HWND`、`ANativeWindow*` 直接进引擎接口。
2. **三端实现**：`platform/windows/`、`platform/linux/`、`platform/android/`。
   缺一端就先写 `platform/null/` 的桩实现，返回"不支持"，让其他平台能编。
3. **CMake**：在 `platform/CMakeLists.txt` 里按 `if(WIN32)` / `if(ANDROID)` 选择源
   文件，导出统一的目标名给上层链接。
4. **接缝**：引擎或 ABI 只调用第 1 步的接口；调用点必须能注入假实现，方便测试。
5. **启动检查**：不支持时返回可读错误并让界面显示（规则五），不许静默退出。
6. **测试**：
   - 用 `null` 实现跑一条单测，断言"不支持"路径的返回值与错误码；
   - 真实路径写进 CI 矩阵的对应任务（Android 只构建不跑设备测试）。

## 反面例子

在 `engine/render/device/VulkanContext.cpp` 里写
`#ifdef _WIN32 ... #elif defined(__ANDROID__) ...` 去创建表面。
后果：引擎再也无法在无窗口环境（CI、预处理工具）里编译，平台逻辑散落在渲染代码里。

## 检查

- [ ] `engine/` 下没有任何平台宏
- [ ] 存在桩实现，其他平台能编
- [ ] 不支持路径有测试
- [ ] 三端 CI 都能构建
