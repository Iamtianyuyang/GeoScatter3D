# Flutter 后端接入记录

更新：2026-09-08。本轮验收范围是欢迎页，工作台其余功能不能据此视为全部验收完成。

## 欢迎页已验证流程

- 新建：CSV/DAT → 真实 Preprocess → 工程包 → C-ABI 元数据和有界预览 → 原生 Viewer → Vulkan 首帧 → 工作台。
- 参数：线程数用于转换和分块；XY/XYZ 与最细层目标点数用于 LOD。小型数据跳过空 LOD 文件，清单按实际产物记录。
- 打开：支持 bundle 和独立 GS3D；独立文件禁用外部分块/LOD 依赖。失败显示错误，只有原生首帧就绪才跳转。
- 示例：部署包含原始 CSV，首次点击自动构建，后续直接打开生成的工程。
- 最近工程：真实记录、打开、移除和清空，重开复用普通加载流程。
- GPU：Vulkan 查询真实设备，按 UUID 保存偏好，下一次打开工程生效；页脚显示实际引擎连接状态。
- 取消与防重：单个工程操作串行执行；取消等待子进程退出；专属 tmp 目录自动清理；同名工程拒绝覆盖。
- 中文与空格：FFI 和 TOML 路径使用 UTF-8；中文工程名与中文 DAT 文件通过真实测试。

耗时操作使用 `executeActionAsync`；JSON-RPC 使用 `executeJsonRpcAsync` 的 `ui_action` 方法。
欢迎页按钮、拖放、命令面板已接到异步入口。同步 `executeAction` 保留旧调用兼容，不能作为完整渲染就绪的确认接口。
开发时工程默认写入仓库 data；安装运行时写入用户 GeoScatter3D/projects。
`GS3D_PROJECTS_DIR` 可指定工程目录，`GS3D_RUNTIME_DIR` 可指定完整原生运行目录。

## F5 与构建

根目录 F5 运行 Flutter Windows Debug；预构建部署 FFI、Viewer、Preprocess、依赖 DLL、配置、着色器和示例。
检测到 Visual Studio 生成器变化时，将旧 Flutter 生成目录移入 tmp 保留，再重新配置。
正常退出等待任务和原生进程；Runner 的 Windows Job Object 用于调试器停止时回收子进程。
Debug 和 Release Runner 均已成功构建；没有通过 VS Code 界面实际按键，验证的是其对应构建命令和运行时。

## 验证结果

- Flutter：50 项通过，原生集成测试默认单独启用。
- `scripts/test_welcome_backend.ps1 -Config Debug`：真实按钮新建、CSV、中文 DAT、JSON-RPC、中文工程名、首帧、最近重开、单体文件、损坏文件、同名冲突、取消、防重、首次示例、GPU 偏好保存通过。
- Release 原生链路通过；DAT/JSON-RPC/GPU 保存的扩展断言在 Debug 执行。
- CTest：当前注册 43 项全部通过，所有测试程序已重编译。
- 两项工程护栏均为 OK，没有提高行数预算。
- 集成测试使用专属工程、偏好和最近记录目录，退出后停止进程并清理测试产物；没有进行 OS 截屏。

## 清理与剩余边界

src、include、cmake 和根构建文件已无 ImGui/ImPlot 引用。
已删除旧字体占位接口、布局 ini/多平台窗口配置、对应测试、专属导航 skill 和按名称识别的旧索引文件。
历史规划/分析文档仍可能提到旧界面；没有改写 Git 历史或清空全部 clangd 缓存。

工作台已补充原生交互快照与测量/统计结果同步，移除了固定性能数据和示例 LOD 展示，但本轮没有完成其全部动作组合的端到端验收。
视口目前使用 PNG 回读，超大数据交互性能和低延迟纹理传输尚未验证。
无 DLL 时有部分 Dart 数学回退；原生构建和加载会明确报错，不能宣称完整应用无原生运行时也全部可用。
