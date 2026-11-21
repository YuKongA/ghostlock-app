---
name: openspec-update-change
description: 在本项目修订现有设计、纳入用户纠正或整理计划结构时使用；不是执行代码或刷新上游生成器。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 修订同一设计

从 Beads spec_id 或 OpenSpec change 直达当前 proposal/design/specs，按原大纲就地更新；不追加一串替代章节、不回写旧 Markdown 计划，也不在 Beads 保存第二份正文。共享合同只在它的唯一位置修正。

用户纠正立即同步直接执行入口。已批准范围/合同的实质变化仍按原自检、独立审查及批准规则；保留原批准来源、取代关系和历史证据。Beads 只同步实际受影响的任务描述/指针、依赖及执行状态，不把整理写成 PASS。

本 skill 不自行实施生产代码。它也不是升级生成器的 `openspec update`；升级时须保留本项目适配，不能覆盖为 Markdown tasks 流程。

唯一分工见[文档规范](../../../docs/development/documentation-standards.md#planning-tool-authority)，安装/命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本skill只导航该职责，不增加阶段或报告。
