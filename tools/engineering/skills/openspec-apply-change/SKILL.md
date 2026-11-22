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

已有任务先用 `npm --prefix tools/engineering run --silent bd -- show <id> --json --brief-deps` 的 spec_id 读取相关 OpenSpec 设计与原批准/证据。没有设计时先按[改动分级](../../../../docs/development/engineering-standards.md#engineering-change-levels)判断：普通 S/M 直接按原任务处理，不补造 change/spec_id；需要新 L 设计则转 [propose](../openspec-propose/SKILL.md)，未过原设计／审查／批准门不进入实施。工具 ready 不等于获准。

只认领获准的当前任务，实施与适用验证沿原分级/门禁；进度、依赖、实际命令/结果写同一 Beads。未执行仍是 NOT_RUN，关闭按该任务原判据，不因工件齐备或没有 tasks.md 提前关闭。

**按已批准设计实施，包括 L 级计划的实施，不重跑 CDA 或 design-critic。** 交付前仍做[作者自检](../ghostlock-author-self-check/SKILL.md)、普通代码/测试审查与适用验证。出现实质设计偏离时停止受影响实施，转 [update](../openspec-update-change/SKILL.md)处理原差异审查/批准；不以“只是实现”绕过 [AGENTS §7](../../../../AGENTS.md#agents-review)。

共同分工只见[文档规范](../../../../docs/development/documentation-standards.md#planning-tool-authority)；安装与命令只见[开发环境](../../../../docs/development/development-environment.md#planning-tools)。
