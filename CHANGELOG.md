# Changelog

版本号沿用 Git 提交历史; 未打 release tag 前按里程碑组织。条目按时间倒序,
日期为提交日期 (GitHub 仓库 `Iamtianyuyang/GeoScatter3D`)。

## 2026-09-12 — 大规模点云：粗尺度 LOD + 细尺度 tile

- **粗尺度 LOD（千万级可用）**: `LodBuilder` 体素降采样建链（间距用连续块
  采样估计，层体素按 2 的幂增长并带过度降采样自纠正）；优先读预处理产出的
  `lod.gs3dlod`，缺失才现场建链；运行期按点预算选层绘制。
- **细尺度 tile（全精度原始点）**: kernel 解析 `tiles.gs3dtiles(.index)`，
  按旧 Viewer 的策略选瓦片——视锥剔除 + 屏幕空间误差（投影 ≥50px 才要
  全精度），先读后传（每帧限流读取 + 上传字节预算），合成顺序为
  coarsest 兜底 → 选中 LOD（全套所需瓦片驻留时按并集 bbox 做 `kSpatialClip`）
  → 瓦片全精度叠加；选择变化不清空当前显示集（防闪烁）。
- **修复真机必现崩溃**: `PointCloudTileGpu` 的共享 staging buffer 不在驻留表，
  `clear()` 不释放它，导致其在 `VulkanContext` 之后析构——悬垂 context 调
  `vkDestroyBuffer` 触发校验层报错与 CRT abort（`0xC0000409`）。新增
  `PointCloudTileGpu::destroy()` 并在释放设备前调用。
- **可观测性**: 引擎诊断暴露 `lod.*`（源/绘制点数、层号、建链/上传耗时）与
  `tile_*`（需求/驻留/上传字节/裁剪状态），HUD 显示规模行；
  Android 日志改走 `__android_log_write`（tag `gs3d`），真机 logcat 可用。
- **工具**: `tools/gs3d_bench_bundle` 生成合成 bundle（source + lod + tiles），
  1M 点 ≈ 16MB + 46MB(5 层) + 100 瓦片(20MB)。
- **验收**: 真机 Tab S7 —— 1000 万合成点 37 FPS / 核心 5.6ms（LOD 5/5）；
  250k 点 + 400 瓦片 bundle 拉近后 `tile 3.3 万 点 · 驻留 52/129`（细尺度
  切换成功）；Windows E2E 覆盖 lod sidecar + 瓦片驻留/合成断言；
  ctest 30/30、flutter test 17/17、analyze 与 guardrails 全绿。

## 2026-09-12 — Vulkan 核心：渲染线程 + 非阻塞帧通道

- **渲染线程独占 Vulkan (vulkan-core.md §4)**: 新增 `gs3d::ffi::EngineHost`
  (`include/ffi/EngineHost.hpp`)，内核在渲染线程创建/销毁，全部 `Vk*` 调用
  收敛到该线程；控制面命令经 `invoke()` 入队执行，调用线程不再接触 Vulkan。
- **非阻塞帧通道**: 新增 `gs3d::ffi::FrameChannel`（发布=O(1) 缓冲置换，
  读取=单次 memcpy，不触发 Vulkan）；C-ABI 新增 `gs3d_engine_request_frame` /
  `gs3d_engine_read_frame_rgba` / `gs3d_engine_frame_sequence` /
  `gs3d_engine_last_render_ms`；`gs3d_engine_render_rgba` 保留为同步兼容路径。
  首帧尺寸探测允许 `out_rgba = NULL`（只回填宽高返回 -1），修复了首帧
  读到 0x0 而报错的缺陷（真机回归发现）。
- **帧资源跨帧复用**: 内核不再每帧创建/销毁命令缓冲、栅栏、staging 缓冲与
  内存映射（原先每帧 4 MiB 级分配）；staging 常驻映射，扩容前等设备空闲；
  提交后异常路径先等栅栏再抛出，避免下一帧复用正在执行的 GPU 资源。
- **设备诊断上 ABI**: `get_diagnostics_json` 新增 `gpu.{name,type,uuid,profile,
  selection,api_version,driver_version,memory_bytes}` 与 `render_thread` /
  `frame_sequence` / `frame_time_ms`；HUD 因此能显示真实显卡（真机：
  Adreno (TM) 650）而非占位串。
- **Flutter 侧**: `GeoScatter3dService.renderNextFrame` 改为「请求帧 + 序号判定 +
  非阻塞读取」，UI isolate 不再阻塞 ~20 ms 等 Vulkan 提交；HUD 新增
  「引擎 N ms」行区分核心成本与前端解码/绘制成本；测试替身
  (`FakeEngine`) 按 request→publish→read 建模。
- **验证**: 新增 `tests/EngineHostTests.cpp`（FrameChannel 契约 + 渲染线程
  语义，10 用例 68 断言）；ctest 29/29；`flutter test` 16/16
  (`GS3D_ENGINE_E2E=1` 含原生读写帧)；真机 Tab S7 (Adreno 650)：
  Maestro 流程断言 HUD 与画面，帧差比对证实「旋转改变画面、复位完全恢复」
  (MAD 4.13 / 0.00)，`integration_test/engine_smoke_test.dart` 真机全绿；
  核心渲染耗时 `引擎 8.69 ms`，帧间隔 24.57 ms → 瓶颈已从核心移到前端
  解码/绘制（零拷贝纹理呈现见 spec §8 第 3 步）。

## 2026-09-11 — 平板适配（响应式 UI + 触控 + 引擎显式降级）

- **响应式断点中枢**: 新增 `Gs3dFormFactorScope`
  (`gs3d_form_factor.dart`)，由窗口宽度派生 compact<600 / tablet<1280 /
  expanded 三档，经 `MaterialApp.builder` 与 `Gs3dA11yScope` 并排注入；
  组件一律经 `context.gs3dFormFactor` 消费，禁止散落判宽。折叠屏姿态
  (P1) 预留同一工厂扩展点。
- **触控目标**: 新增 `hitTargetTouch = 48` 与 `Gs3dTouchTarget` 包裹器
  (视觉不变、命中区撑足)；视口浮层工具条 5 处与 Gizmo 预设标签在
  触控档 ≥48，expanded 档保持桌面 28 不变。
- **视口多指手势**: `center_viewport` 裸 `Listener` 路径扩展多指追踪
  ——单指拖拽=旋转、双指=平移+捏合缩放、第三指不崩溃、抬指复位；
  桌面键鼠路径零回退。
- **平板工作台形态**: 新增 `panels/tablet/`（drawer_scaffold +
  action_bar）：侧栏收合为图标 rail、拇指区动作条、对话框经
  `Gs3dResponsiveDialog` 在窄档落底部 sheet（桌面保持居中模态）、
  命令面板入口按钮化（无键盘可达）。
- **引擎显式降级 (R3)**: service 新增 `EngineAvailability` 状态机，
  Android 上 `DynamicLibrary.open('libgs3d_ffi.so')` 失败不再被吞——
  显式 unavailable 状态 + 视口错误卡 + 13 类引擎依赖动作前置 guard
  返回明确错误；新增 `viewport_engine_error_card`。零 C++/CMake 改动。
- **测试基线**: 60/3 → 82/3（e2e 62/1 → 84/1）；新增平板三档 surface
  用例、断点边界、触控命中区、R3 降级直测、多指状态机用例；离屏预览
  新增 `05-tablet.png` / `06-tablet-welcome.png`。
- **遗留**: Android 模拟器 APK 走查因沙箱 gradle 受限未完成，补做步骤
  见 `deliverables/software-company/tablet-qa-report.md`；引擎 NDK
  移植（GLFW → Android surface）留待下迭代。

## 2026-09-10 — 前后端全量接入 + 前端统一重构

### 后端接入（Flutter ↔ C-ABI）

- **Dart 重算消除**: `calculateStats()` 改为原生优先
  (`calculate_region_stats` 以包围盒为选区), 返回 `source` 标记
  (`native_region_stats` / `dart_sample`); 仅引擎不可用时回退 Dart 采样。
- **未绑定 ABI 补齐**: 绑定 `gs3d_engine_create` (完整
  `gs3d_engine_config`: 视口尺寸 / 首选 GPU UUID / 着色器目录 /
  validation) 与 `gs3d_engine_unload_dataset`; `hasEngineApi` 收紧为
  四项必备符号齐备。
- **空实现消灭**: 删除 `_queueNativeCommand` /
  `_queueNativeRenderSettings` / `_synchronizeNativeMeasurementMode`
  三个空壳转发, 渲染设置统一直接生效。
- **动作目录端到端门禁**: 新增 `action_coverage_test.dart`, 遍历
  `getAvailableActions()` 全量动作断言无未知 ID / 无 null / 均带
  `success`, 并对 stats / measure / export 做真机断言。

### 真实缺陷修复

- **DLL 相对路径加载**: `SetDllDirectoryA` 会改写相对路径基准, 含 `..`
  的候选路径必然失败 (Windows error 126); 改为先绝对化再打开。
- **引擎着色器目录回退**: `gs3d_engine_create` 在 `shader_dir` 为空时未
  回退, 导致 PointPipeline 退回相对 `assets/shaders`, 工作目录非仓库根
  即静默取不到着色器; 现统一回退到 DLL 同目录资产路径。
- **对比度缺陷**: 左停靠面板「清除选区」链接原为 white70 白字落在浅蓝
  底上近乎不可见, 改为强调色并同步下划线颜色。

### 前端统一重构（Apple HIG）

- **单一令牌源**: 新增 `lib/src/theme/gs3d_tokens.dart`, 分层为
  `Gs3dPalette` / `Gs3dTokens` / `Gs3dType` / `Gs3dMaterial` /
  `Gs3dMotion` / `Gs3dA11y`; 删除 `app_theme.dart` 与 `macos_tokens.dart`
  两套兼容适配层, 改由 `gs3d_theme.dart` 统一装配 `ThemeData` (含深色)。
- **流体交互组件库**: 新增 `lib/src/widgets/fluid.dart` — 可中断弹簧数值
  (`Gs3dSpringValue`)、悬停/按压反馈 (`Gs3dPressable`)、毛玻璃容器
  (`Gs3dGlass`)、令牌化卡片/菜单项/胶囊/状态点/指标磁贴/弹簧模态路由。
- **欢迎页重设计**: Hero、三联操作卡片、最近工程表格、抽屉侧栏、顶栏、
  页脚与五个弹窗全部令牌化; 抽屉折叠宽度由 sheet 弹簧驱动, 可被高频
  切换打断续接; 移除死代码, 关于页显卡信息与动作数量改为动态读取。
- **工作台重设计**: 底部状态栏改为分段遥测条 (可点击项带悬停/按压反馈,
  读数等宽表格数字); `CollapsibleCard` 折叠箭头由弹簧驱动; 悬浮 Dock /
  分析舱的 Handwritten `BackdropFilter` 收敛到 `Gs3dGlass`; 导轨项改为
  弹簧驱动三态颜色插值; 暗色舱新增 `Gs3dDark` 色阶。
- **无障碍**: `Gs3dA11yScope` 全局注入; 减弱动效停用装饰粒子并归零过渡,
  降低透明度时毛玻璃退化为实心, 高对比时描边增强。
- **材质取舍**: 毛玻璃仅用于浮于视口之上的形态 (悬浮 Dock / 分析舱 /
  模态 / 命令面板), 铺满整高的固定 chrome 用实心表面 + 发丝描边。

### 门禁

- `flutter analyze` 0 告警; `flutter test` 60 通过 / 3 跳过;
  `GS3D_ENGINE_E2E=1` 62 通过 / 1 跳过 / 0 失败 (RTX 4070);
  `ctest -C Release` 43/43 通过; 两项工程护栏 OK。

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
