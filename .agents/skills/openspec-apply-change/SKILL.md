---
name: openspec-apply-change
description: 在本项目按现有 OpenSpec 设计执行或继续任务时使用；读取设计，任务状态只操作 Beads。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 按设计执行

先读取本任务 `npm run --silent bd -- show <id> --json --brief-deps` 的 spec_id，再按疑点读取该 OpenSpec proposal/design/specs 及原批准/证据。范围和行为判据以 OpenSpec 唯一正文为准，不能从工具 ready 推断获准。

只认领获准的当前任务，实施与适用验证沿原分级/门禁；进度、依赖、实际命令/结果写同一 Beads。未执行仍是 NOT_RUN，关闭按该任务原判据，不因工件齐备或没有 tasks.md 提前关闭。

遇到实质偏离先更新同一 OpenSpec 设计并走原差异审查/批准；普通已批准实施不重复 design-critic。不要在 OpenSpec 填实施 checkbox 或复制 Beads 结果。

唯一分工见[文档规范](../../../docs/development/documentation-standards.md#planning-tool-authority)，安装/命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本skill只导航该职责，不增加阶段或报告。
