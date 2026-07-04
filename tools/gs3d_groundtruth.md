# gs3d_groundtruth — GS3D 真值查询工具

独立 CLI 工具，直接读取原始数据（GS3D 二进制或 CSV/DAT 文本），不经过 LOD/抽稀/tile/渲染管线，提供精确真值用于验证查看器。

## 构建

```bash
cmake --build build --target gs3d_groundtruth -j$(nproc)
```

产物：`build/gs3d_groundtruth`。无 Vulkan/GLFW/ImGui 依赖。

## 数据格式

| 格式 | 识别方式 | 读取方式 |
|------|---------|---------|
| `.gs3d` | 扩展名 | `Gs3dReader::read_all()` — 全量加载到内存 |
| CSV/DAT (文本) | 其他扩展名 | `CsvStreamReader` — 流式扫描，不占内存 |

## 三个子命令

### `info` — 数据集基本真值

```
gs3d_groundtruth info <file> [--gs3d <ref.gs3d>]
    [--x-field <name>] [--y-field <name>] [--z-field <name>] [--value-field <name>]
```

输出总点数、各字段 min/max/mean。对 GS3D 同时输出相对坐标和重建绝对坐标；对 CSV/DAT 输出绝对坐标，加 `--gs3d` 同时输出减去 origin 后的相对坐标。

### `nearest` — 近邻查询

```
gs3d_groundtruth nearest <file> <x> <y> <radius> [--gs3d <ref.gs3d>]
    [--x-field <name>] [--y-field <name>] [--z-field <name>] [--value-field <name>]
```

给定绝对坐标 (x, y) 和搜索半径，返回 XY 平面最近点。同时输出绝对坐标和相对坐标（如有 `--gs3d`）。

### `region` — 矩形区域查询

```
gs3d_groundtruth region <file> <x_min> <x_max> <y_min> <y_max> [--list]
    [--gs3d <ref.gs3d>]
    [--x-field <name>] [--y-field <name>] [--z-field <name>] [--value-field <name>]
```

统计落在矩形 `[x_min, x_max] × [y_min, y_max]` 内的点数。`--list` 列出每个点的完整字段。

## 坐标系对齐

| 数据源 | 存储坐标 | 显示坐标 |
|--------|---------|---------|
| CSV/DAT | 绝对坐标 (double) | 绝对坐标 |
| GS3D | 相对坐标 (float32, = 绝对 − origin) | 重建绝对坐标 |

工具输出同时标注 `ABSOLUTE` 和 `RELATIVE`。

**`--gs3d <ref.gs3d>`**：从参考 GS3D 文件读取 origin（只读 header，不加载点数据），用于 CSV/DAT 与 GS3D 的坐标系对齐。在同一文件既做 GS3D 又做 DAT 查询时，确保 origin 一致。

## 字段映射

默认 CSV 列名：`x`, `y`, `elevation`, `fold`（`DataSchema::default_fold_elevation()`）。

通过 `--x-field` / `--y-field` / `--z-field` / `--value-field` 覆盖。

本项目的 DAT 文件列名为 `X`, `Y`, `elevation`, `field_statics`，典型用法：

```bash
--x-field X --y-field Y --z-field elevation --value-field field_statics
```

## 精度特性

| 字段 | 精度 | 说明 |
|------|------|------|
| value (fold/field_statics) | **精确** | CSV→GS3D 直通 float32，零损失 |
| z (elevation) | **精确** | ±400 范围内 float32 足够 |
| x, y | **±1–2 单位** | 相对坐标 > 16.7M (2^24) 时 float32 ULP ≥ 2 |
| 总点数 | **精确** | 50M 零遗漏 |

## 验证工作流示例

```bash
GT=./build/gs3d_groundtruth
DAT=data/field_and_ele_source_rec_50m.dat
GS3D=data/field_and_ele_source_rec_50m.gs3d.bundle/source.gs3d
DAT_FLAGS="--x-field X --y-field Y --value-field field_statics"

# 1. 交叉核对：DAT ↔ GS3D
$GT info $GS3D
$GT info $DAT --gs3d $GS3D $DAT_FLAGS
# 对比 point_count / min / max / mean

# 2. 验证 pick 精度：查看器点 vs DAT 真值
$GT nearest $DAT 29749652 46308652 10 $DAT_FLAGS

# 3. 验证区域无遗漏：查看器框选点数 vs DAT 真值
$GT region $DAT 29749000 29750000 46308000 46309000 $DAT_FLAGS
# 对照查看器 region_stats 的 point_count

# 4. 验证 GS3D 精度损失：同一查询 DAT vs GS3D
$GT nearest $DAT 45009336 67282848 10 $DAT_FLAGS
$GT nearest $GS3D 45009336 67282848 10
# x/y 差 ≤ 2 单位属正常 float32 取整
```

## 性能参考

| 操作 | GS3D (binary, 800MB) | DAT (text, 3.35GB) |
|------|---------------------|-------------------|
| info | ~1.7s | ~34s |
| nearest | ~1.6s | ~34s |
| region | ~1.6s | ~34s |

50M 点，Intel i7-13700K。GS3D 受限于内存带宽，DAT 受限于文本解析。
