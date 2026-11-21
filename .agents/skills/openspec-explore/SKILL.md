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

唯一分工与门禁边界见[项目工具规则](../../../docs/development/documentation-standards.md#planning-tool-authority)；安装和命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本文件只提供该操作入口，不增加阶段或报告。
