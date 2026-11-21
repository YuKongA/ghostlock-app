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

讨论本身不授权实施、认领或新的批准。需要保存的有效设计结论就地写入所属 OpenSpec proposal/design；任务未知/依赖/调查证据写 Beads，弱相关后续需求保留触发条件而不成为当前 blocker。实质架构可用 CDA，不另交一份报告。

唯一分工见[文档规范](../../../docs/development/documentation-standards.md#planning-tool-authority)，安装/命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本skill只导航该职责，不增加阶段或报告。
