# GS3D / GS3D-LOD / GS3D-Tile 二进制格式规范

> 状态: 现行 | 核对基准: 2026-08-14, main@1f0eb84 + TIA-93 数据格式修复
> (LOD v1 废弃 / tile stride 强校验 / LOD·tile 显式小端)
>
> 本规范描述磁盘上的字节布局。实现位于
> `include/data/Gs3dFormat.hpp`、`Gs3dLodFormat.hpp`、`Gs3dTileFormat.hpp`、
> `Gs3dByteOrder.hpp` 与 `src/data/`、`src/preprocess/` 对应读写器。
> 权威语义以代码为准，本文档与代码不一致时以代码为准并应更新本文档。

## 通用约定

- 所有多字节整数与 IEEE-754 浮点字段一律**显式小端（little-endian）编码**，
  与宿主字节序无关。参考实现见 `src/data/Gs3dFormat.cpp:21-59` 的
  `write_u32_le` / `write_u64_le` / `write_float_le` / `write_double_le`。
  小端主机上该编码与 C++ struct 原生直写字节级一致，因此既有产物读取兼容。
- 文件头中的 `header_size` 字段保留前向兼容：读取端允许 v2 头部
  `header_size` 大于已知最小头部。
- 点属性语义: `x`/`y` 为平面坐标, `z` 为高程(elevation), `value` 为
  主值(fold/field_statics), 均为 `float`。
- 扩展名: 全量点 `.gs3d`; LOD sidecar `.gs3dlod`; tile 数据
  `.gs3dtiles`; tile 索引 `.gs3dtiles.index`。

## 1. GS3D 全量点格式 (`.gs3d`)

### 1.1 文件头 (v2, 108 字节)

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|---|---|---|---|---|
| 0 | 4 | char[4] | magic | `'G' 'S' '3' 'D'` |
| 4 | 4 | u32 | version | 2 |
| 8 | 4 | u32 | header_size | 108 (v2); 允许更大值以前向兼容 |
| 12 | 8 | u64 | point_count | 点总数 |
| 20 | 8 | u64 | point_data_offset | 点数据起始偏移, 必须 >= header_size |
| 28 | 8 | f64 | origin_x | 原点 x (双精度) |
| 36 | 8 | f64 | origin_y | 原点 y (双精度) |
| 44 | 8 | f64 | origin_z | 原点 z (双精度) |
| 52 | 24 | f32 × 6 | bbox_min_x/y/z, bbox_max_x/y/z | 数据包围盒, min <= max |
| 76 | 8 | f32 × 2 | value_min, value_max | value 值域, min <= max |
| 84 | 4 | u32 | flags | 当前恒为 0 |
| 88 | 4 | u32 | reserved0 | 0 |
| 92 | 8 | u64 | reserved1 | 0 |
| 100 | 8 | u64 | reserved2 | 0 |

对应 `Gs3dHeader` (`include/data/Gs3dFormat.hpp`)。

### 1.2 点数据

紧随 `point_data_offset` 处连续存放 `point_count` 个
`Gs3dPoint { x, y, z, value }`, 每点 16 字节 (f32 × 4), 全部小端。
文件总大小 == `point_data_offset + point_count × 16`。

### 1.3 版本与校验

- **v2 (现行)**: 写入端固定写 v2; `header_size` 必须 >= 108;
  `point_data_offset` 必须 >= `header_size`; bbox/value 必须 min <= max;
  点数上限防溢出 (`point_count <= UINT64_MAX / 16`)。
- **v1 (遗留)**: 仅保留**读取**兼容路径, 按原生布局直读 (非显式小端),
  `header_size` 必须恰好等于 108; 不提供 v1 写入。已有 v1 数据应重建为 v2。
- 未知版本被拒绝。

## 2. GS3D-LOD sidecar (`.gs3dlod`)

### 2.1 文件头 (96 字节)

magic 为 8 字节 `'G' 'S' '3' 'D' 'L' 'O' 'D' '\0'` (`GS3D_LOD_MAGIC`)。

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|---|---|---|---|---|
| 0 | 8 | char[8] | magic | `GS3DLOD\0` |
| 8 | 4 | u32 | version | 2 |
| 12 | 4 | u32 | header_size | 96 |
| 16 | 8 | u64 | source_point_count | 源 GS3D 点数 |
| 24 | 8 | u64 | level_count | LOD 层数 |
| 32 | 24 | f32 × 6 | bbox_min/max x/y/z | 与源数据集一致 |
| 56 | 8 | f32 × 2 | value_min, value_max | 与源数据集一致 |
| 64 | 8 | u64 | build_finest_target_points | 构建锚参数: finest_target_points |
| 72 | 8 | u64 | build_growth_factor_x1000 | growth_factor × 1000 (如 1414 = 1.414) |
| 80 | 8 | u64 | build_min_points_per_level | min_points_per_level |
| 88 | 8 | u64 | reserved | 0 |

对应 `Gs3dLodFileHeader` (static_assert 96 字节)。

### 2.2 层头 (每层 72 字节)

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|---|---|---|---|---|
| 0 | 4 | u32 | level_index | 从 0 开始 |
| 4 | 4 | u32 | voxel_mode | 1=XY, 2=XYZ (`Gs3dLodFormatVoxelMode`) |
| 8 | 8 | u64 | source_point_count | 该层输入点数 |
| 16 | 8 | u64 | target_point_count | 构建目标点数 |
| 24 | 8 | u64 | point_count | 实际点数 |
| 32 | 8 | u64 | point_data_bytes | 点数 × 16 |
| 40 | 4 | f32 | voxel_size | 该层体素尺寸 |
| 44 | 4 | u32 | level_header_size | 72 |
| 48 | 24 | u64 × 3 | reserved0/1/2 | 0 |

对应 `Gs3dLodLevelHeader` (static_assert 72 字节)。

### 2.3 文件布局

```
文件头 (96 B)
层 0 头 (72 B) + 层 0 点数据 (16 B × point_count)
层 1 头 (72 B) + 层 1 点数据 (16 B × point_count)
...
```

点数据为 `Gs3dPoint` 序列, 全部小端。读取端校验
`point_data_bytes == point_count × 16`, 不匹配即报错。

### 2.4 版本与校验

- **v2 (现行)**: 唯一受支持版本。v1 已**正式废弃**: `validate_file_header`
  对 v1 显式报错 "v1 is deprecated and no longer supported; regenerate
  the file with v2", 不再提供任何 v1 兼容读取路径。
- 层头校验: `level_index` 必须等于序号; `point_count`/`point_data_bytes`
  非零且匹配; `voxel_mode` 必须为 1 或 2。
- 文件头与源 GS3D 头交叉校验 (`validate_against_source`): 点数、
  bbox、value 值域近似相等 (epsilon 1e-4)。

### 2.5 分层语义 (Potree 式自动分层)

LOD 层级由 `Gs3dLodBuildConfig` 自动决定, 不写死层数:

- 从 `finest_target_points` 反推最精细层 `voxel_size_0`
  (锚定, 不要求精确命中);
- 每层 `voxel_size ×= growth_factor`, 直到点数低于
  `min_points_per_level` 停止;
- 默认值 (33M 点 XY 工区数据调优): finest=2M, growth=1.414 (√2),
  min=100K → 约 5 层, 最粗层约 125K 点;
- `voxel_mode = XY` (2D 网格降采样) 或 `XYZ` (体积降采样);
- `voxel_scale` 为体素尺寸修正系数 (>1 点数更少, <1 点数更多);
- 每层为体素内代表点 (体素取点), 层间点数约按 growth_factor 递减。

## 3. GS3D-Tile (`.gs3dtiles` + `.gs3dtiles.index`)

tile 为 XY 空间网格分块。两个文件:

- **索引文件** `.gs3dtiles.index`: 文件头 + tile 记录表;
- **数据文件** `.gs3dtiles`: 文件头 + 连续点载荷。

### 3.1 索引文件头 (136 字节)

magic 为 8 字节 `'G' 'S' '3' 'D' 'T' 'I' 'X' '\0'` (`GS3D_TILE_INDEX_MAGIC`)。

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|---|---|---|---|---|
| 0 | 8 | char[8] | magic | `GS3DTIX\0` |
| 8 | 4 | u32 | version | 2 |
| 12 | 4 | u32 | header_size | 136 |
| 16 | 8 | u64 | source_point_count | 源 GS3D 点数 |
| 24 | 8 | u64 | tile_count | 非空 tile 记录数 |
| 32 | 8 | u64 | total_point_count | 全部 tile 点数合计 (== 源点数) |
| 40 | 4 | u32 | point_stride | 16 (v1) 或 20 (v2) |
| 44 | 4 | u32 | tile_record_size | 80 |
| 48 | 8 | f32 × 2 | tile_size_x, tile_size_y | 网格单元尺寸 |
| 56 | 8 | f32 × 2 | grid_origin_x, grid_origin_y | 网格原点 |
| 64 | 8 | u32 × 2 | grid_count_x, grid_count_y | 网格维度 |
| 72 | 24 | f32 × 6 | bbox_min/max x/y/z | 全数据集包围盒 |
| 96 | 8 | f32 × 2 | value_min, value_max | 全数据集值域 |
| 104 | 4 | u32 | split_mode | 1=XY (`Gs3dTileSplitMode`) |
| 108 | 4 | u32 | reserved_u32 | 0 |
| 112 | 24 | u64 × 3 | reserved0/1/2 | 0 |

对应 `Gs3dTileIndexFileHeader` (static_assert 136 字节)。

### 3.2 Tile 记录 (每条 80 字节)

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|---|---|---|---|---|
| 0 | 8 | u64 | tile_id | 非空 tile 的稳定序号, 从 0 递增 |
| 8 | 4 | u32 | tile_x | 网格列 |
| 12 | 4 | u32 | tile_y | 网格行 |
| 16 | 8 | u64 | point_count | 本 tile 点数 |
| 24 | 8 | u64 | point_data_offset | 载荷起始 (相对数据文件头, 见下) |
| 32 | 8 | u64 | point_data_bytes | == point_count × point_stride |
| 40 | 24 | f32 × 6 | bbox_min/max x/y/z | 本 tile 包围盒 |
| 64 | 8 | f32 × 2 | value_min, value_max | 本 tile 值域 |
| 72 | 8 | u64 | reserved0 | 0 |

对应 `Gs3dTileRecord` (static_assert 80 字节)。

`tile_id` 的几何含义: 空间网格中的线性序号 `tile_y × grid_count_x +
tile_x`; 但写入端只对**非空** tile 分配连续递增的稳定序号, 空 tile
不占记录。`tile_x`/`tile_y` 由 `floor((坐标 - grid_origin) / tile_size)`
并裁剪到 [0, grid_count-1] 得到。

### 3.3 数据文件头 (80 字节)

magic 为 8 字节 `'G' 'S' '3' 'D' 'T' 'I' 'L' '\0'` (`GS3D_TILE_DATA_MAGIC`)。

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|---|---|---|---|---|
| 0 | 8 | char[8] | magic | `GS3DTIL\0` |
| 8 | 4 | u32 | version | 2 |
| 12 | 4 | u32 | header_size | 80 |
| 16 | 8 | u64 | source_point_count | 源 GS3D 点数 |
| 24 | 8 | u64 | tile_count | 非空 tile 数 (与索引一致) |
| 32 | 8 | u64 | total_point_count | 全部 tile 点数合计 |
| 40 | 8 | u64 | total_point_bytes | 载荷总字节数 |
| 48 | 4 | u32 | point_stride | 16 或 20 |
| 52 | 4 | u32 | reserved_u32 | 0 |
| 56 | 24 | u64 × 3 | reserved0/1/2 | 0 |

对应 `Gs3dTileDataFileHeader` (static_assert 80 字节)。

### 3.4 点载荷与 stride

- **v1**: `point_stride = 16`, 载荷为 `Gs3dPoint { x, y, z, value }`,
  不含 point_id。运行期需全量扫描源数据重建 tile → point_id 映射
  (慢路径), 仅旧文件读取用。
- **v2 (现行)**: `point_stride = 20`, 载荷为交错存储的
  `Gs3dPointWithId { x, y, z, value, point_id }`, 每点 20 字节。
  `point_id` 为源 GS3D 文件中全局**从 1 开始**的点序号
  (写入端 `point_index + 1`), 0 保留表示无效/无命中。嵌入 point_id
  免去启动扫描, 新预处理一律写 v2。
- 记录内载荷连续铺满, `point_data_offset` 是相对**数据文件起始**
  的偏移 (首条记录恒为 80 = 数据文件头大小), 相邻记录按
  `point_data_offset + point_data_bytes` 首尾相接。

### 3.5 严格校验 (TIA-93 强化)

- 版本 ↔ stride 一致性: v1 头必须配 stride 16, v2 头必须配 stride 20,
  混合文件 ("v2 头 + v1 stride" 等) 被显式拒绝;
- 每条 tile 记录强校验 `point_data_bytes == point_count × point_stride`
  (`validate_tile_record(record, stride)` 严格重载);
- 读取端逐记录校验 payload 边界与连续性: 记录区间必须位于
  `total_point_bytes` 之内且相邻记录无空洞、无越界;
- 索引头与数据头交叉校验 (`validate_index_against_data`): 版本、
  stride、tile_count、total_point_count 必须一致;
- 索引头与源 GS3D 头交叉校验 (`validate_against_source`)。
- 上述任一失败即显式报错, 损坏/手工拼装文件不会被静默解码
  (旧实现可对混合 stride 文件静默读到零字节)。

### 3.6 分块语义

- 网格: 以数据集 bbox 左下角为原点, 按 `tile_size_x/y` 划分
  `grid_count_x × grid_count_y` 网格, 每点按 XY 坐标落入唯一单元;
- 自动放大: 写入端自动放大 tile_size 使每轴网格数不超过 200
  (目标 ≤ 40000 tile), 避免稀疏数据产生海量空 tile;
- 只有非空 tile 写入索引记录; 单 tile 点数无固定上限 (固定空间分块,
  非固定点数分块)。

## 4. Bundle 目录布局

预处理器 (`GeoScatter3DPreprocess`) 产出目录式 bundle
(`<name>.gs3d.bundle/`, 见 `src/app/PreprocessedBundle.cpp`):

```
<name>.gs3d.bundle/
  manifest.toml         # 元数据清单 (TOML)
  source.gs3d           # 全量点 (GS3D v2)
  lod.gs3dlod           # LOD sidecar (GS3D-LOD v2)
  tiles.gs3dtiles       # tile 数据 (GS3D-Tile v2, stride 20)
  tiles.gs3dtiles.index # tile 索引
```

`manifest.toml` 含 `[bundle]` (version=1, source_mode/source_path/
source_size_bytes)、`[files]` (各文件名)、`[dataset]` (point_count、
bbox_min/max、value_min/max、origin) 与 `[csv_convert]` (字段映射)。
查看器 `input.mode = "bundle"` 时按 manifest 解析并打开上述文件。

## 5. 工具

- `gs3d_groundtruth`: 独立真值查询 CLI (`info` / `nearest` / `region`,
  `--list`, `--gs3d`), 用生产 reader 读取 GS3D 文件, 用于验证查看器
  查询结果。用法见 `tools/gs3d_groundtruth.md`。
- 测试: `GeoScatter3DDataTests` (Catch2) 覆盖三种格式的 round-trip、
  字节序字节级一致性 (memcmp vs 原生布局)、v1 LOD 拒绝、混合 stride
  拒绝、payload 空洞拒绝等; `GeoScatter3D.FreshCloneBundle` 用生产
  reader 回读预处理产物 (25 golden 点)。
