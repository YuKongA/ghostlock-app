---
name: openspec-archive-change
description: 在本项目完成并验收一个 OpenSpec 变更后归档时使用；未关闭任务和历史批量归档不自动触发。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 归档已完成变更

读取目标 change 的 proposal/design 与所链接 Beads 工作。核实任务确已按原条件关闭，取消/取代有明确结论，真实验收门仍按原范围；`tracks:null`、零个 Markdown tasks 或 artifact done 不证明可归档。

真正改变行为合同的 change 先确认 delta 与主 spec 同步语义；不跳过未完成任务，不强行 archive。运行 `npm run --silent openspec -- archive <name> --yes`，让 CLI 按原生 schema 处理；不要使用 `--no-validate` 绕过错误。

归档后仅机械更新受影响的 Beads spec_id 与现有入口链接，保持语义编号、批准与证据不变；不再手写一份完成报告。已有历史归档不批量改写。

唯一分工与门禁边界见[项目工具规则](../../../docs/development/documentation-standards.md#planning-tool-authority)；安装和命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本文件只提供该操作入口，不增加阶段或报告。
