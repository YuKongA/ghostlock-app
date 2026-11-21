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

先从 proposal 的 Beads 任务入口核对该变更的实施、原批准及适用验收已经关闭；decision.closed可能只是设计收录，不能代替相关运行任务。真实设备/产品验收未关闭时不归档整个变更为完成。

实际合同变更先按需同步主 specs，再用 `npm run --silent openspec -- archive <name> --yes`。没有 Markdown tasks、工具all_done或源码完成均不是归档依据；不伪造PASS，也不恢复旧计划系统。

归档后的指针在同批更新Beads spec_id和直接消费入口；保留原ID、批准及证据。历史资料批量迁移不自动触发本skill，也不按工具状态批量改写历史。

唯一分工见[文档规范](../../../docs/development/documentation-standards.md#planning-tool-authority)，安装/命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本skill只导航该职责，不增加阶段或报告。
