> **历史归档，非当前计划入口** · 原路径：`.agents/skills/openspec-propose/SKILL.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:.agents/skills/openspec-propose/SKILL.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

---
name: openspec-propose
description: 在本项目新建已需要计划的设计变更时使用；沿既有分级与批准，不把小修升级为大流程。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 新建设计变更

先从 MASTER-PLAN 与 `npm run --silent openspec -- list --json` 定位已有主题，避免重复 change。使用 `npm run --silent openspec -- new change <name>`，默认 schema 为 `ghostlock`。

按 `status --change <name> --json` 与 `instructions <artifact> --change <name> --json` 生成当前需要的工件。实质架构选择使用 CDA；既有设计仅直达有效锚点，不复制长计划。无行为合同变化时在该 change 的 `.openspec.yaml` 声明 `skip_specs: true`，不为凑模板发明需求。

在 Beads 创建相称的任务，`--spec-id` 指向本 change，title / `--external-ref` 保留原语义编号，并在 proposal 放一处 Beads 入口。不生成 tasks.md；录入待设计需求不授权实施。

交付前使用 ghostlock-author-self-check；仅在原条件触发时委派独立 ghostlock-design-critic。批准与门禁沿原规则，不由工件存在代替。

唯一分工与门禁边界见[项目工具规则](../../../../development/documentation-standards.md#planning-tool-authority)；安装和命令见[开发环境](../../../../development/development-environment.md#planning-tools)。本文件只提供该操作入口，不增加阶段或报告。
