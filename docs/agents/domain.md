# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

> 核对: 2026-08-14。本仓库不使用 `CONTEXT.md` / `docs/adr/` 布局
> （`/grill-with-docs` 之类技能提及的懒生成文件在当前仓库不存在，可安全跳过）。

## Before exploring, read these

- **`docs/architecture.md`** — 架构与现状（数据流、逐帧流程、资源所有权、
  当前功能状态、重大风险）。以其中的“当前功能状态”清单为准，
  不要依赖过时的“尚未完成”表述（该节已重写为现状核验）。
- **`docs/config-reference.md`** — viewer.toml 全量配置键与用户偏好。
- **`docs/spec/gs3d-format.md`** — GS3D/LOD/tile 二进制格式规范。
- **`README.md`** — 构建/测试/分发/样例流程。
- **`CHANGELOG.md`** — 变更记录（按里程碑组织）。

## 文档-代码一致性约定

- 源码是最终事实：文档与代码冲突时以代码为准，并应更新文档。
- 涉及实现行为的修改需同步更新 README / architecture.md /
  config-reference.md / 格式规范（见 `CONTRIBUTING.md`）。
- 行号引用类的分析文档（如 `docs/analysis/`）应标注“行号截至日期”，
  避免引用漂移误导后续读者。
