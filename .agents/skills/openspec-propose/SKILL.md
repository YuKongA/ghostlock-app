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

先从 OpenSpec 当前 change 与工程代号入口定位已有主题，避免重复设计。`npm run --silent openspec -- new change <name>` 使用项目 ghostlock schema；按 `status --change <name> --json` / `instructions <artifact> --change <name> --json` 创建当前需要的 proposal/design/specs。

完整目标、范围、技术设计和验收定义只写 OpenSpec；共享合同链接现行 OpenSpec 唯一位置，不用旧 Markdown 或 Beads design 字段代替正文。真正无行为 delta 的变更用 skip_specs:true，待分析需求不补造完整 spec。

Beads 创建相称执行任务，spec_id直达本 change 的正文/锚点，title/external_ref保留语义编号。状态、依赖、批准和实际证据只写 Beads，不生成 tasks.md 或第二份 checkbox。实质架构使用 CDA；交付前作者自检，独立审查仅按原触发。

唯一分工见[文档规范](../../../docs/development/documentation-standards.md#planning-tool-authority)，安装/命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本skill只导航该职责，不增加阶段或报告。
