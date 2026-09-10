# GeoScatter3D Flutter Desktop 前端 Agent 治理规范

## Agent 角色定义
**Role: flutter-desktop-engineer / cross-platform-ui-architect**

负责 GeoScatter3D 桌面端 Flutter 前台应用组件、状态中枢、交互视口与跨语言 FFI 调度的开发与维护。输出必须遵循工程护栏，保持代码优雅、高内聚低耦合。

---

## 核心红线约束

1. **状态单向数据流**：
   - 业务状态与全局配置统一由 `GeoScatter3dService` 管理，页面控件通过 `ListenableBuilder` 或响应式监听消费；
   - 严禁在组件生命周期内部直接侵入或变异全局底层对象。
2. **纯 C-ABI 边界与图形解耦**：
   - 前端通过 `geoscatter3d_bindings.dart` 访问 C-ABI 原生接口；
   - 绝不引入底层图形 API（如 Vulkan 驱动头文件），确保跨平台可移植性。
3. **仅 gs3d_engine 渲染**：
   - 视口只呈现 `gs3d_engine_*` 的 RGBA 帧；**禁止**纯 Dart 软件点云预览与双进程 Viewer；
   - engine 失败时显式报错，不得静默假可用；拾取/统计优先 A 组 C-ABI，失败返回空/错误。
4. **测试自清理与临时目录收敛**：
   - 任何由测试或前台导出的文件必须写入 `tmp/` 目录；
   - 测试生命周期结束必须在 `tearDown` 中主动删除测试产物，严禁污染源码和 Git 工作区。
5. **UI 动作原子化注册**：
   - 所有工作台、视口、图层及分析动作统一下沉至 `executeAction` 动作分发表（支持 45 项统一动作），便于前台命令面板 (`Ctrl+P`)、快捷键以及外部 JSON-RPC 控制面即时调度。

---

## 质量验证标准

每次提交前必须在 `ui_flutter` 目录下执行并通过全部自动化测试：
```powershell
flutter test
```
必须保证 58 项测试通过（3 项 e2e 默认跳过），且测试后工作区保持 clean。
可选：`GS3D_ENGINE_E2E=1 flutter test test/engine_e2e_test.dart` 验证单进程渲染内核门禁。
