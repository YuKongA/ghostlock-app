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

先区分内容：格式、代号、链接或原证据补录只就地整理，不启用 CDA／critic；只有进度变化时直接更新 Beads。普通 S/M 没有设计工件时按[原分级](../../../../docs/development/engineering-standards.md#engineering-change-levels)处理，不补建 change。纯实施转 [apply](../openspec-apply-change/SKILL.md)；确需新主题设计才转 [propose](../openspec-propose/SKILL.md)。

从 Beads spec_id 或 OpenSpec change 直达当前 proposal/design/specs，按原大纲就地更新；不追加一串替代章节、不回写旧 Markdown 计划，也不在 Beads 保存第二份正文。共享合同只在它的唯一位置修正。

用户纠正立即同步直接执行入口。实质架构差异使用 [CDA](../constraint-driven-architecture/SKILL.md)修订同一正文；交付前做[作者自检](../ghostlock-author-self-check/SKILL.md)。是否独立审查以 [AGENTS §7](../../../../AGENTS.md#agents-review)的实质触发为准：**任务即使标成 M，只要命中合同、owner 等原触发，仍须 Lead 委派非作者 [critic](../ghostlock-design-critic/SKILL.md)定点审差异并按原规则取得批准**；不因未用 CDA 或用户要求尽快实施而漏掉。保留原批准来源、取代关系及历史证据；Beads 只同步受影响任务，不把整理写成 PASS。

本 skill 不自行实施生产代码。它也不是升级生成器的 `openspec update`；升级时须保留本项目适配，不能覆盖为 Markdown tasks 流程。

共同分工只见[文档规范](../../../../docs/development/documentation-standards.md#planning-tool-authority)；安装与命令只见[开发环境](../../../../docs/development/development-environment.md#planning-tools)。
