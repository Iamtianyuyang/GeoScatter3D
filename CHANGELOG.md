# Changelog

版本号沿用 Git 提交历史; 未打 release tag 前按里程碑组织。条目按时间倒序,
日期为提交日期 (GitHub 仓库 `Iamtianyuyang/GeoScatter3D`)。

## 2026-08-14 — TIA-93 数据格式修复 (PR #3)

- **LOD v1 正式废弃**: `validate_file_header` 对 v1 显式报错并提示
  重新生成 v2; 移除 `file_header_summary` 的 `version >= 2` 死分支;
  清理头部注释。LOD 锚参数字段 (`build_finest_target_points` 等) 从
  死代码变为实际写入 (头部 96 字节布局不变)。
- **tile stride 强校验**: 新增 `validate_tile_record(record, stride)`
  严格重载; index/data 头版本 ↔ stride 一致性校验 (拒绝 "v2 头 +
  v1 stride" 混合文件); reader 逐记录校验
  `point_data_bytes == count × stride` 与 payload 边界/连续性
  (拒绝空洞与越界); 两条读取函数读取前再次校验 stride, 杜绝静默置零。
- **LOD/tile 字节序统一**: 新增 `include/data/Gs3dByteOrder.hpp`,
  LOD/tile 四个 Writer/Reader 全部改为逐字段显式小端编解码
  (与 GS3D v2 可移植编码一致)。小端主机字节级兼容既有产物。
- 新增 10 个 Catch2 用例: LOD v1 拒绝、锚参数 round-trip、显式小端
  字节级一致性、大端视图模拟、tile 严格 stride 校验、混合文件拒绝、
  payload 空洞拒绝、round-trip 一致性。

## 2026-08-14 — TIA-90 UI 一致性修复 (PR #4)

- **dock 布局持久化生效**: `build_default_layout` 首帧不再无条件整树
  重建, 新增 `keep_current_dock_layout` (已有 ini 布局时跳过默认布局
  重建); ini 路径纳入 ResourcePath 解析策略 (三级回退)。
- **移除 `auto_save_sidecar` 死配置**: 键与死路径删除, 不再存在
  运行时抛异常的可能。
- **运行期设置持久化**: RenderSettingsState 关键修改 (点大小/形状/
  着色属性/高度缩放/色带/值域裁切) 写入用户偏好
  `preferences.toml` 的 `[render_settings]` 小节, 重启恢复;
  不回写 `viewer.toml`。GPU 选择仍走 `[graphics] preferred_gpu` 偏好。

## 2026-08-14 — TIA-91 Vulkan validation 修复 (PR #2)

- **S1 信号量复用**: 2 帧 in-flight 复用 `render_finished_semaphores_`
  导致的 VUID-vkQueueSubmit-pSignalSemaphores-00067 修复, 消除
  present 永不完成 → 渲染循环死锁冻结。
- **S2 independentBlend**: 启用 `independentBlend` feature 以匹配
  3 attachment 不同 colorWriteMask (RGBA/R/R)。
- **S3 device layer**: 移除 create logical device 时传入的
  `VK_LAYER_KHRONOS_validation`, 仅保留 instance layer。
- Debug + validation layer 日志确认 S1/S2/S3 不再触发。

## 1f0eb84 (main HEAD) — 导航球与自适应坐标轴刻度

- feat: navigation-ball gizmo, adaptive axis ticks, workbench default。
- 前序里程碑摘要 (自项目初始化, 按主题合并):

### 数据格式与预处理

- GS3D v2 可移植编码: 固定小端、显式 IEEE-754 字段、header_size
  前向兼容 (`ed53fb9`)。
- tile 数据文件嵌入全局 point_id (v2 格式, 20 字节/点, `9e03e71`)。
- Potree 式空间 LOD 自动分层: 从 finest_target_points 反推 voxel_size
  并逐层倍增, 替代固定 target_point_counts (`d7b2712`)。
- 双通道属性选择: 高度/着色来源独立 (`f604bcf`); 可配置 CSV 列映射 +
  `.dat` 输入支持 + 双精度坐标 (`5c8a994`)。
- bundle 目录式预处理产物 (`4f60f76`); 预处理线程数统一 (`0f01e54`)。
- 增量 tile 流式、共享 staging 批量上传、显存 arena 子分配、
  预加载后台化 (`4005700`/`8ead542`/`de980eb`/`10c1735`)。

### 交互与可视化

- 框选放大 (BoxSelect + fit_screen_rect 反投影)、F 聚焦、双击选轨道
  枢轴、光标锚定缩放 (`a8406f8`/`d48a33b`/`e7acb0b`)。
- 距离测量工具 (`139b0b4`); 区域统计 (Shift+拖拽, 异步计算,
  `f58519c`/`7c08ce9`); 测量持久化到 bundle_dir/analysis.toml
  (`0ebe45d`)。
- 截图为 PNG (swapchain 捕获, `3c756d6`); 原生存盘对话框 (`b2ba67a`)。
- hover 十字线与坐标读出、GPU pick 生产路径 (`eeb1ead`/`5e1f5e6`)。
- 8 个色带 + Rainbow256 离散 Jet LUT + 值域裁切 (`bc856bb`/`019c5b2`)。
- 点形状选择: 方形/圆形/菱形/三角形 (`e17711d`)。
- 导航图/概览图: 离线预渲染缩略图 + 实时视野框叠加
  (`ac9972e`/`71d4ebb`/`4f32700`)。
- 高度缩放 (原名高度夸张, 0.01-100x, `6926892`)。
- C 键复制悬停点坐标到剪贴板 (`a5b4d60`)。

### UI 与布局

- 欢迎页: 最近项目、新建项目对话框、原生文件对话框、GPU 选择器
  (`cdaf83d`/`8557b71`/`3f87d2d`)。
- 三套可切换主题 (carbon-blue / deep-graphite / instrument-amber) +
  自定义主题控件库 (`4265a8f`)。
- 三套顶层布局: workbench (方案 A) / floating-dock (方案 B, `fe0afb7`)
  / analysis-rail (方案 C, `f5c620c`), 运行期切换。
- PPI 自适应 ui_scale + 用户系数 (`14ccc85`/`f11f68c`)。
- SVG logo (nanosvg + Vulkan 纹理, `d4e0910`); VSCode Dark+ 字体层级
  (`163e9e6`)。
- 导航球 gizmo (`1f0eb84`); 自适应坐标轴刻度 (`64aec13`)。

### 性能与稳定性

- 3300 万点读取 0.57s → 0.00003s (LOD 模式只读 header);
- tile 按帧预算切片上传, 消除逐 tile vkQueueWaitIdle;
- 交互期间保持稳定显示 (keep_stable, 防闪烁, `546847d`);
- 视口 resize 防抖批量重建; 隐藏标签页停止离屏渲染;
- 可见 tile 集完整性/驻留边界修复 (`d36cf84`/`0da8aca`);
- swapchain 统一 UNORM, 移除 sRGB 预线性化 (`b36d6bd`);
- 线程池取消语义: TaskCancelled 显式抛出 (`df0d484`/`16842a1`)。

### 工程化

- Catch2 测试框架 (替代手写断言, `917d8c0`/`9fae972`);
- 架构行数护栏 (check_engineering_guardrails.py + PR 历史预算比较,
  `669c043`/`bc9f647`/`58f1246`); include 依赖检查;
- FreshClone 冒烟测试: 跟踪 CSV → bundle → 生产 reader 回读 25 点
  (`c0be413`);
- Windows 自包含分发: install/CPack、一键打包脚本、Windows CI
  (`bff8c5d`/`156c5e2`/`dd8d1c9`);
- 构建期从 GLSL 编译 SPIR-V (`319bea9`);
- 提交历史可追踪 blob 约 28 MiB (两个 CJK 字体约 25 MiB, 见
  docs/architecture.md 风险 8)。
