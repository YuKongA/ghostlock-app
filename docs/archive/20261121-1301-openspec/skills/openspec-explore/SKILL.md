> **历史归档，非当前计划入口** · 原路径：`.agents/skills/openspec-explore/SKILL.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:.agents/skills/openspec-explore/SKILL.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

---
name: openspec-explore
description: 在本项目讨论可行性、澄清需求或调查设计选择时使用；不自行转成实施或新审批。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 按问题探索

从具名未知和真实调用者出发，按影响读相关设计/代码/官方资料。使用 `npm run --silent openspec -- list --json` 定位现有变更；无需为了讨论创建 change 或任务。

实质架构选择按 CDA，记录能改变选择的约束、事实、未知与最小验证方法；普通问题直接回答。探索不实施生产代码，不将弱依赖后续需求塞入本批承诺。

结论落在现有设计对应段落或当前答复。需要跟踪且尚未批准的需求记录到 Beads deferred 项，不建立平行调查报告。

唯一分工与门禁边界见[项目工具规则](../../../../development/documentation-standards.md#planning-tool-authority)；安装和命令见[开发环境](../../../../development/development-environment.md#planning-tools)。本文件只提供该操作入口，不增加阶段或报告。
