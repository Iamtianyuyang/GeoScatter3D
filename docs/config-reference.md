# viewer.toml 配置参考

> 核对基准: 2026-08-16, main@8162b1a (TIA-111 之后；主题/布局现状已按
> 代码核验，见 [operations.md](operations.md) 第 1 节)。
> 解析实现: `src/app/AppConfig.cpp` (input/csv_convert) 与
> `src/app/AppConfigViewerToml.cpp` (其余小节)。
> 默认模板: `config/viewer.toml`; 可提交样例: `config/sample-viewer.toml`。

所有相对路径按 `ResourcePath` 搜索根依次解析 (`src/app/ResourcePath.cpp`):
配置文件所在目录的父目录 (发布根)、可执行文件目录、配置文件所在目录,
最后才是当前工作目录 (仅作兼容回退, 避免开发树混入其他 checkout 的资源)。

## [input]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `mode` | string | — | `csv` / `dat` / `gs3d` / `bundle` 之一。`csv` 与 `dat` 启动时重新转换并生成 GS3D 与启用的 LOD/tile; `gs3d` 直接打开现有 `.gs3d`; `bundle` 打开目录式 `.gs3d.bundle` 项目 (默认 `config/viewer.toml` 即 bundle 模式) |
| `csv_path` | path | — | csv/dat 输入文件 (mode=csv/dat) |
| `gs3d_path` | path | — | 现有 GS3D 文件 (mode=gs3d) |
| `bundle_dir` | path | — | bundle 项目目录 (mode=bundle) |

## [csv_convert]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `num_threads` | int | 硬件并发 | 预处理共享线程数 (CSV/DAT 转换 + tile 构建); 0 或省略为自动 |
| `chunk_bytes` | int | 16 MiB | 并行解析分块大小 |
| `min_parallel_file_bytes` | int | 64 MiB | 小于该字节数时单线程解析 |
| `x_field` | string | `X` | CSV 列名 → 点 x |
| `y_field` | string | `Y` | CSV 列名 → 点 y |
| `z_field` | string | `elevation` | CSV 列名 → 点 z (高程) |
| `primary_value_field` | string | `field_statics` | CSV 列名 → 点 value (主值/fold) |

## [shader]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `vertex_shader_path` | path | `assets/shaders/point.vert.spv` | 顶点着色器 SPIR-V |
| `fragment_shader_path` | path | `assets/shaders/point.frag.spv` | 片元着色器 SPIR-V |

## [window]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `width` / `height` | int | 1280 × 720 | 初始窗口尺寸 |
| `title` | string | GeoScatter3D 三维散点查看器 | 窗口标题 |
| `resizable` | bool | true | 窗口可缩放 |
| `ui_layout_ini_path` | path | `config/imgui_layout.ini` | ImGui docking 布局持久化文件。留空字符串 `""` 表示不持久化, 每次启动用默认布局; 非空时首次启动写入默认布局, 之后跨重启保留用户停靠/浮动布局 (`keep_current_dock_layout` 逻辑在 `src/ui/UiRoot.cpp`) |
| `ui_scale_multiplier` | float | 1.15 | PPI 推导 ui_scale 之上的用户舒适系数, 最终 `clamp(ppi_scale × multiplier, 1.0, 2.5)` |
| `theme` | string | `carbon-blue` | UI 主题，共 **5 套**（`include/ui/Theme.hpp`、`src/ui/Theme.cpp`）：`carbon-blue` (碳蓝·浅色) / `carbon-blue-dark` (碳蓝 2.0·深色) / `deep-graphite` (石墨·深色) / `instrument-amber` (仪器琥珀·深色) / `high-contrast` (高对比·浅色)。**代码默认是深色 `kCarbonBlueDark`** (`src/ui/Theme.cpp:294`)，本文件默认值将其覆盖为浅色 `carbon-blue`。运行期可在 视图→主题 菜单切换（5 套全列），写入用户偏好 `[ui] theme` 后优先于本文件；不回写本文件 |
| `layout` | string | `workbench` | 顶层布局键（`workbench` / `floating-dock` / `analysis-rail`），解析见 `AppState.hpp` `ui_layout_from_string`。**TIA-111 后仅 workbench 实际渲染**：floating-dock / analysis-rail 的绘制代码无调用点（休眠），该键在 HEAD 上无视觉效果（实测三值启动窗口像素一致）。详见 [operations.md](operations.md) §1.1 |
| `multi_viewports` | bool | true | 启用 Dear ImGui Multi-Viewports: 停靠面板可拖成原生 OS 窗口。Wayland 等平台 ImGui 可能自动禁用 |

## [vulkan]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `validation_layers` | bool | false | 启用 Vulkan validation layers。调试用; 对 tile 预加载的放大明显, 日常关闭 |

## [graphics]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `preferred_gpu` | string | `auto` | 图形设备: `auto` (自动, 优先独显/显存大者) 或 `uuid:<32位十六进制>`。欢迎页的个人选择写入用户偏好 (见下), 不修改本文件 |

## [render]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `clear_color` | float[4] | [0.118, 0.133, 0.165, 1.0] | 视口清屏色 RGBA |
| `initial_point_size` | float | 1.5 | 初始点大小 (像素) |

## [camera]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `mode` | string | `fit` | `fit` (按数据集 bbox 适配) 或其他 (使用下方显式参数) |
| `position` / `target` / `up` | float[3] | — | 初始相机位姿 |
| `fov_y` | float | 45.0 | 垂直视场角 (度) |
| `near` / `far` | float | 1.0 / 100000.0 | 近/远裁剪面 |

## [controller]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `rotate_speed` / `pan_speed` / `zoom_speed` | float | 1.0 | 操作灵敏度系数 |
| `invert_rotate_x` / `invert_rotate_y` / `invert_pan_x` / `invert_pan_y` | bool | false | 各方向反转 |

## [lod]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 启用 LOD sidecar 路径 |
| `keep_full_buffer` | bool | false | 保留全量点数组常驻 (关闭时 LOD 模式只读 GS3D header) |
| `sidecar_path` | path | 自动推导 | 显式 LOD sidecar 路径; 缺省按 GS3D 路径推导 |
| `auto_load_sidecar` | bool | true | 自动加载存在的 sidecar |
| `finest_target_points` | int | 2000000 | Potree 式自动分层: 最精细层目标点数 (反推锚定 voxel_size_0)。替代旧 `target_point_counts` |
| `growth_factor` | float | 1.414 | voxel_size 倍增系数 (√2 每层点数约减半; XYZ 体积数据推荐 2.0) |
| `min_points_per_level` | int | 100000 | 最粗层点数下限, 低于此值停止分层 |
| `voxel_mode` | string | `XY` | `XY` (2D 网格) / `XYZ` (体积) 降采样 |
| `voxel_scale` | float | 1.0 | voxel_size 修正系数 (>1 点数更少) |
| `medium_delay_seconds` | float | 0.20 | 交互停止后进入中等 LOD 的延迟 |
| `high_delay_seconds` | float | 0.80 | 交互停止后进入高 LOD 的延迟 |
| `use_lowest_while_interacting` | bool | true | 交互期间使用最低 LOD 层级 |
| `adaptive_interacting_level` | bool | true | 帧时间自适应交互层级: 帧时间有余量时在交互中爬升细节, 吃紧时立即回退 (优先于 use_lowest_while_interacting) |
| `frame_time_budget_ms` | float | 14.0 | 自适应交互层级的帧时间预算 |
| `interactive_display_mode` | string | `keep_stable` | 交互期间前台显示策略: `keep_stable` (默认, 保持交互前 LOD 与已驻留全分辨率 tile, 交互结束再更新, 防闪烁) / `coarse` (交互时用帧预算自适应 LOD 并关闭 tile 叠加, 保帧率) / `freeze_texture` (极端兜底, 当前按 keep_stable 处理) |
| `verbose` | bool | true | 输出 LOD 诊断日志 |

## [viewport]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `count` | int | 1 | 启动时显示的视图数 (1-4); 之后可在 UI 添加 |

## [tile]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 启用 tile 路径 |
| `index_path` / `data_path` | path | 自动推导 | 显式 tile 索引/数据路径; 缺省按 GS3D 路径推导 |
| `min_tile_pixel_size` | float | 50 | tile 投影宽度 >= 该像素值时才加载全精度数据 (Potree 屏幕空间阈值) |
| `max_visible_tiles` | int | 0 | **已废弃兼容字段**: 任何值都不截断可见 tile (硬截断会致块状伪影), 默认 0; 极端视图预算应由连续自适应 LOD 解决 |
| `use_full_z_range` | bool | true | 使用全 z 值域做 tile 判定 |
| `verbose` | bool | true | 输出 tile 诊断日志 |
| `gpu_cache_max_tiles` | int | 288 | GPU LRU 软驻留预算 (tile 数)。可见 tile 会被 pin, 极端视图可暂时超过 |
| `gpu_upload_budget_bytes` | int | 32 MiB | 每帧 tile GPU 上传预算 (原 8 MiB 上限主导冷加载延迟, 已提高) |
| `cpu_cache_max_bytes` | int | 4 GiB | CPU tile 缓存 LRU 预算 (原 512 MiB → 4 GiB, 使大部分/全部 tile 常驻 CPU, 减少重复磁盘读) |
| `preload_all` | bool | true | 全量预加载: 启动后把全部 tile 读入并常驻 GPU (仅当总字节 <= preload_max_bytes); 之后零加载延迟, tile 选择每帧纯 CPU 运行; 超预算自动回退按需流式 |
| `preload_max_bytes` | int | 1.5 GiB | 预加载上限, 超过则回退流式 |
| `preload_upload_budget_bytes` | int | 64 MiB | 预加载阶段每帧上传预算 (几帧传完) |

## [debug]

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `pick_debug_dump_enabled` | bool | false | 开启 GPU pick 调试 dump |
| `pick_debug_dump_dir` | path | — | dump 输出目录 |
| `pick_debug_dump_once_on_hover` | bool | false | 仅首次 hover 时 dump |

## 用户偏好 (preferences.toml)

运行期修改不回写 `viewer.toml`, 而是写入用户偏好文件:
Linux `$XDG_CONFIG_HOME/geoscatter3d/preferences.toml` (默认
`~/.config/geoscatter3d/`), Windows `%APPDATA%\geoscatter3d\preferences.toml`
(实现: `src/app/UserPreferences.cpp`)。当前键:

| 小节 | 键 | 说明 |
|---|---|---|
| `[graphics]` | `preferred_gpu` | 欢迎页选择的 GPU UUID |
| `[ui]` | `theme` | 运行期选定的主题 id（如 `carbon-blue` / `deep-graphite`）；启动时优先于 `viewer.toml [window] theme` |
| `[ui]` | `layout` | 运行期选定的布局字符串；启动时优先于 `viewer.toml [window] layout`（HEAD 上仅 workbench 有视觉效果） |
| `[render_settings]` | `point_size` | 点大小 |
| `[render_settings]` | `point_shape` | 点形状 (方形/圆形/菱形/三角形) |
| `[render_settings]` | `height_attr_index` | 高度属性 (value/z) |
| `[render_settings]` | `color_attr_index` | 着色属性 (value/z) |
| `[render_settings]` | `height_exaggeration` | 高度缩放 |
| `[render_settings]` | `colormap_index` | 色带索引 |
| `[render_settings]` | `value_clip_enabled` / `value_clip_min` / `value_clip_max` | 值域裁切开关与范围 |

## 环境变量

| 变量 | 说明 |
|---|---|
| `GS3D_LOG_LEVEL` | 日志级别: trace/debug/info(默认)/warning/error/off |
| `GS3D_LOG_BENCHMARK` | 置 `0` 关闭 benchmark 通道输出 |
