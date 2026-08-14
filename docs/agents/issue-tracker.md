# Issue tracker: Multica 平台

> 核对: 2026-08-14。项目任务跟踪已迁移至 **Multica** 平台（GitHub 仓库仅作
> 代码托管与 PR 评审）。本文档供技能/代理读取，说明当前实际工作流。

## 平台与命令

- 任务（issue）、评论、元数据、子任务层级均通过 `multica` CLI 操作：
  - 读任务: `multica issue get <id> --output json`
  - 读评论: `multica issue comment list <id> --roots-only --summary --compact --output json`
  - 写评论: `multica issue comment add <id> --content-file <path>`
  - 改状态: `multica issue status <id> <todo|in_progress|in_review|done|blocked|...>`
  - 子任务: `multica issue create --parent <parent-id> ...`
- 代码拉取: `multica repo checkout <url>`（在专用分支上创建仓库 checkout）。
- 代理身份与工作流规则见仓库根 `AGENTS.md`（MULTICA-RUNTIME 区块）。

## 约定

- 任务标题即 H1，正文从 `##` 小节开始；中文汇报。
- 评论正文写入 UTF-8 文件后用 `--content-file` 发布（Windows 下禁止
  `--content-stdin` 与非 ASCII 内联内容）。
- 每个代码修改任务完成后按仓库流程提交 PR 到 `main`，PR 标题/正文含任务标识
  （如 `TIA-<N>`），并在任务最终评论给出 PR 链接。
- 阶段性任务（审查/提案）以评论作为交付通道。

## When a skill says "publish to the issue tracker"

在 Multica 平台创建 issue（`multica issue create`），不是 GitHub issue。

## When a skill says "fetch the relevant ticket"

`multica issue get <id> --output json` 并读取评论历史。
