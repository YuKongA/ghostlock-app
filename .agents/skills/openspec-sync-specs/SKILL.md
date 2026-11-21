---
name: openspec-sync-specs
description: 仅在已完成并验证的行为合同变更需要同步到 OpenSpec 主规范时使用；纯工具/状态迁移不需要此步骤。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 同步行为合同

确认目标 change 的相关 Beads 任务、批准与原行为门已经满足；工具状态不替代验收。读取 delta 与受影响主 spec，按 ADDED/MODIFIED/REMOVED/RENAMED 的真实语义合并，保留无关合同与场景，不把文档差异机械覆盖成整文件。

运行 `npm run --silent openspec -- validate <name> --type change`，并对修改的主 spec 做对应验证；schema 检查不代表产品 PASS。没有 delta（skip_specs）时不创建空 spec。

既有 docs/development 规范和旧计划的唯一合同不为同步复制到 specs。需要迁移合同 authority 时按原范围/审查规则明确一次切换；本 skill 不归档 change、不修改生产代码。

唯一分工与门禁边界见[项目工具规则](../../../docs/development/documentation-standards.md#planning-tool-authority)；安装和命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本文件只提供该操作入口，不增加阶段或报告。
