---
name: openspec-explore
description: 在本项目讨论可行性、澄清需求或调查设计选择时使用；不自行转成实施或新审批。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 探索与澄清

从真实调用者和具名未知出发，只读相关 OpenSpec 设计/规范、Beads 当前条件与实际实现；不默认通读全部工件或归档。既有方案被新事实取代时明确差异，不为未接通 backend 或未来可能性预造兼容层。

探索可以直接回答，不要求先有 change、Beads 任务或 spec_id，也不自动认领、实施或发起批准。普通小问题不运行整套 CDA；实质架构探索使用 [CDA](../constraint-driven-architecture/SKILL.md)，只分析当前未知。需要保存的结论更新所属 OpenSpec；确需新计划才转 [propose](../openspec-propose/SKILL.md)，修订既有设计转 [update](../openspec-update-change/SKILL.md)。调查证据沿原任务保存，弱相关后续需求不变成本批 blocker；不另交 CDA 报告。

共同分工只见[文档规范](../../../../docs/development/documentation-standards.md#planning-tool-authority)；安装与命令只见[开发环境](../../../../docs/development/development-environment.md#planning-tools)。
