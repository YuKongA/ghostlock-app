---
name: openspec-apply-change
description: 在本项目按现有 OpenSpec 设计执行或继续任务时使用；读取设计，任务状态只操作 Beads。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 按设计执行任务

从当前请求确定 change，运行 `npm run --silent openspec -- instructions apply --change <name> --json`。读取所列工件及本批实际相关的原批准/合同，不遍历全套历史。

从 proposal 的 Beads 入口执行 `npm run --silent bd -- show <id> --json`；按需用 `ready --json` 或 `blocked --json`。核实当前授权与原门禁后，再对选定任务执行 `update <id> --claim`；claim 不代表文件写入锁或批准。其它人的任务不抢占。

只实施已授权差异，按改动面验证。任务中记录实际命令、输入、输出/退出码及仍为 NOT_RUN 的门；满足原关闭条件后 `close <id> --reason <实际结论>`。无需改 OpenSpec checkbox，`taskTrackingConfigured:false` 是本项目正常配置，不能据零条 Markdown tasks 宣称全部完成。

作者自检、普通代码审查、独立设计审查和提交按既有触发条件执行；不增加最终 critic PASS。

唯一分工与门禁边界见[项目工具规则](../../../docs/development/documentation-standards.md#planning-tool-authority)；安装和命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本文件只提供该操作入口，不增加阶段或报告。
