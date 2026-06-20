# Spec: GeoScatter3D 比赛版查看器

依据：[docs/intent/geoscatter3d-competition.md](../intent/geoscatter3d-competition.md)（已确认意图）

## Objective

把 GeoScatter3D 现有的 Vulkan + ImGui 桌面查看器，补全为能在比赛中演示的 1 亿点级
2.5D 散点（x, y, fold, elevation）查看器。用户是参赛者本人，在比赛现场用独显或核显
笔记本演示交互浏览。完成的定义见下方「Success Criteria」。

代码现状：核心瓦片化 LOD 管线已经存在（`PointCloudTileGpu` 后台磁盘读取 + 主线程
GPU 上传两步流程、`TilePointCache` 字节预算 LRU 缓存、`FrameUploadBudget` 限速、
`ViewportManager`/`CameraHub` 多视口框架）。本次工作是在这套架构上补齐规模、性能
和交互功能，不是重写。

## Tech Stack

- C++20，CMake ≥ 3.28
- Vulkan（渐进式 LOD/瓦片点渲染管线）
- GLFW 3（窗口/输入）
- Dear ImGui（`third_party/imgui`，docking 分支，git submodule）
- tomlplusplus（`third_party/tomlplusplus`，header-only，配置文件解析）
- 自定义二进制格式：GS3D（全量点）、GS3D-LOD（多级降采样 sidecar）、GS3D-Tile
  （空间分块 + 索引）
- 不引入新的第三方依赖（除非走「Ask first」流程获批）——这是开源协议合规约束下
  最简单可靠的选择，也避免比赛前 4 周内引入未验证的新依赖风险。

## Commands

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/GeoScatter3D --config config/viewer.toml
```

性能优先的构建调整（相对现状的优化点，需要落地到 `CMakeLists.txt`）：

- `CMAKE_BUILD_TYPE=Release` 始终用于性能验证和演示构建；Debug 仅用于功能调试。
- 开启 LTO（`CMAKE_INTERPROCEDURAL_OPTIMIZATION`），跨 TU 内联收益大，构建时间增量
  可接受（项目规模中等）。
- 不默认开启 `-march=native`：比赛现场机器的具体 CPU 未知，硬编码会有移植风险；
  如需针对已知评测机型单独调优，作为可选 CMake cache 变量提供，不进默认路径。
- 新增一个手动运行的性能基准目标（见 Testing Strategy），不进入默认 `ctest` 套件，
  避免日常开发循环被大数据集加载拖慢。

## Project Structure

```
include/<module>/   → 头文件，按模块分组：app, camera, data, gui, platform,
                       preprocess, render, ui, util
src/<module>/       → 对应实现，镜像 include 结构
src/main.cpp        → 入口
tests/              → 手写断言式测试可执行文件，按关注点分文件
  Gs3dFormatTests.cpp        → 数据格式正确性
  RuntimePerformanceTests.cpp → 运行时逻辑（resize 调度、相机等）单元行为
  <新增>BenchmarkSuite.cpp    → 性能基准（见下）
third_party/        → git submodule（imgui）+ vendored header-only（tomlplusplus）
config/viewer.toml  → 运行时配置入口
data/               → 测试数据（test.csv 等），赛方提供同结构数据
assets/shaders/     → 编译后的 SPIR-V
docs/
  architecture.md   → 历史架构笔记（本次工作不依赖其内容判断现状，已用 codegraph
                       重新扫描确认）
  intent/           → 确认过的意图记录
  spec/             → 本文件及后续规格
```

## Code Style

以现有 `include/render/ViewportManager.hpp` 为范例：

```cpp
namespace gs3d::render {

// 设计来源的简短说明放在类声明前的注释里（如适用），解释“为什么这样设计”
// 而非“这个类是做什么的”。
class ViewportManager {
public:
    void init(
        const VulkanContext&          context,
        VkFormat                      color_format,
        int                           count,
        VkExtent2D                    initial_extent,
        const gs3d::camera::Camera&   initial_camera
    );

    [[nodiscard]] VkRenderPass render_pass() const noexcept;

private:
    std::vector<Entry> entries_;
    int active_count_ = 0;
};

} // namespace gs3d::render
```

约定：

- 命名空间按模块分层：`gs3d::<module>`。
- 类型 `PascalCase`，方法/函数 `snake_case`，私有成员以尾随下划线结尾（`entries_`）。
- 多参数函数声明在头文件里按列对齐，便于扫读类型。
- `[[nodiscard]]` 标注查询类 const 方法。
- 注释解释非显而易见的约束（线程安全要求、调用顺序、设计来源），不解释代码本身
  在做什么。中文/英文混用现状保持不变（注释惯例已是中英混排，不强行统一）。

## Testing Strategy

延续现有的两层手写断言测试模型（无外部测试框架，`tests/*.cpp` 自带 `expect()` 计数器
+ `main` 返回失败数），新增第三层性能基准：

1. **数据正确性**（`GeoScatter3DDataTests`，对应 `Gs3dFormatTests.cpp`）
   覆盖 GS3D/LOD/Tile 格式的读写一致性。每次改格式必须跑这层。
2. **运行时逻辑**（`GeoScatter3DRuntimeTests`，对应 `RuntimePerformanceTests.cpp`）
   覆盖纯逻辑（resize 防抖、相机变换等），不需要 GPU，跑得快，进默认 `ctest`。
3. **性能基准**（新增，`GeoScatter3DBenchmark`，手动运行，不进默认 `ctest`）
   先用现有 `data/test.csv`（3300 万行）跑出基线，1 亿点规模暂按架构设计推导留
   余量，等拿到更大数据再复测。测量项：
   - 进程峰值内存——核显平台用人工读取系统工具（`htop`/`smem`/任务管理器），
     不自动化，记录在基准结果文档里；
   - 交互期间帧时间分布（目标 P95 ≤ 16.6ms，即 60fps）；
   - 视角切换/抽稀级别切换后的重新填充延迟——先测当前基线，再据此定阈值；
   - 抽稀前后的结构保真量化指标（具体指标在 PLAN 阶段细化）。

默认开发循环只跑前两层（`ctest --test-dir build`），保持快速反馈；性能基准作为
里程碑验收手段单独运行。

## Boundaries

- **Always**：
  - 提交前跑 `ctest --test-dir build --output-on-failure`；
  - 新代码遵循现有命名空间/命名/注释惯例；
  - 任何触及 tile 缓存、LOD 选择、帧上传预算的改动，必须用接近 1 亿点规模的数据
    手动验证一次内存峰值和帧时间，不能只靠小数据集通过单元测试就认为完成；
  - 多视口/相机联动相关改动要在至少 2 个视口同时打开的情况下验证。
- **Ask first**：
  - 引入任何新第三方依赖（即使是开源协议）；
  - 修改 GS3D/LOD/Tile 二进制格式（会让现有 `data/test.gs3d` 等产物失效）；
  - 改动 `CMakeLists.txt` 的目标结构（新增/拆分可执行文件、改变构建产物布局）；
  - 改变 `config/viewer.toml` 中影响开箱默认行为的字段（演示当天不希望有意外）。
- **Never**：
  - 提交密钥/凭据；
  - 编辑 `third_party/` 下的 vendored 代码（imgui submodule、tomlplusplus）；
  - 默认启用 `-march=native` 或其他绑定特定 CPU 的代码生成选项；
  - 在未经批准的情况下删除失败的测试来让 CI 变绿。

## Success Criteria

来自已确认的意图文档，转写为可验证条件：

- [ ] 1 亿点数据集加载后，进程总内存占用（含核显共享显存）≤ 20GB。
- [ ] 在至少一个独显平台和一个核显平台（Intel UHD/Iris Xe 或 AMD APU）上，交互
      （旋转/平移/缩放）期间帧时间 P95 ≤ 16.6ms，且无可见闪烁。
- [ ] 抽稀级别切换、视角切换后的重新填充耗时有实测数字并低于某个明确阈值
      （具体阈值待定，见 Open Questions）。
- [ ] 抽稀后的低密度视图与原始数据的整体结构（地形/构造形态）有量化指标证明无明显
      失真（指标定义见 Resolved Decisions #4，在 PLAN 阶段细化为具体脚本/测试）。
- [ ] 按 fold/elevation 切换着色属性，无需重新加载数据，切换延迟可感知为「即时」。
- [ ] 鼠标悬停显示最近点的 x/y/fold/elevation。
- [ ] 三维坐标轴/网格框带刻度，随旋转缩放联动更新。
- [ ] 支持单机多显示器、可自由添加视口；各视口默认独立相机，提供联动开关；
      每个视口可显示同数据集不同视角或不同数据集。
- [ ] 框选放大：拖框松开后相机自动聚焦缩放到该区域。
- [ ] 区域书签：可保存、重新打开、隐藏；存于独立 sidecar 文件（每数据集一份）；
      重启程序后书签仍可用；不修改原始点数据。
- [ ] UI 为深色主题 + 可拖拽停靠/浮动面板；基础交互手势（滚轮缩放、拖拽旋转/平移）
      可用；面板布局通过 ImGui docking `.ini` 机制持久化，重启后恢复上次布局。
- [ ] 全部新增第三方依赖（如有）均为开源协议。

## Resolved Decisions

1. **测试数据规模**：开发与性能验证阶段先用现有 `data/test.csv`（约 3300 万行）
   作为基准数据，不外推生成 1 亿行合成数据。1 亿点的内存/帧率目标先按架构设计
   （字节预算、LRU 驱逐、分块上传）推导和留足余量，等赛方提供或确认更大规模数据
   后再用真实数据复测、校准阈值。
2. **核显内存测量方法**：手动测量，不引入自动化内存监控工具链。即在核显设备上
   用系统自带工具（Linux: `htop`/`smem`；Windows: 任务管理器/资源监视器）在交互
   过程中人工读取 RSS 峰值，记录在性能基准结果里。不在 CI/CTest 里自动化这一步。
3. **重新填充延迟阈值**：先用现有 3300 万行数据跑出当前基线数字（视角切换、抽稀
   级别切换后的重新填充耗时），再据此定具体阈值，不预设目标数字。
4. **LOD/抽稀"结构保真"验证**：需要量化指标，不能只靠目视。具体指标待性能基准
   阶段设计（候选方向：抽稀前后 elevation 分布的统计偏差、关键地形特征点
   ——如局部极值/fold 边界——的保留率），在写 PLAN 阶段细化为可执行的对比脚本/测试。
5. **区域书签持久化位置**：独立的 sidecar 文件，每个数据集一份，不写进
   `viewer.toml`（书签是用户产物，与静态配置分离）。
6. **工作台布局持久化**：需要记住面板布局，重启程序后恢复上次的停靠/浮动状态，
   沿用 ImGui docking 自带的 `.ini` 布局持久化机制。
