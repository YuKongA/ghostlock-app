---
name: openspec-update-change
description: 在本项目修订现有设计、纳入用户纠正或整理计划结构时使用；不是执行代码或刷新上游生成器。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 修订设计变更

定位当前 change 和它引用的真实权威正文。按文档原大纲就地修正，不在末尾不断追加替代 section；保留既有批准和旧号映射。已批准范围发生实质改变时走原设计审查/批准条件。

`npm run --silent openspec -- status --change <name> --json` 与 `instructions <artifact> --change <name> --json` 用于定位工件；它们不证明内容已获批准。更新受影响 proposal/design/specs，不复制当前合同为第二份规范。

Beads 仅调整实际受影响的任务描述、依赖与待设计/阻断状态；保留既有运行证据，不能将 scope 整理写成 PASS。进度变化只写 Beads，不反写正文 checkbox。用户纠正同步执行入口，停止受影响工作。

本 skill 不修改生产代码。若用户要的是 `openspec update` CLI（升级生成的 skill），先审本项目适配差异，避免覆盖 Beads 单一状态入口。

唯一分工与门禁边界见[项目工具规则](../../../docs/development/documentation-standards.md#planning-tool-authority)；安装和命令见[开发环境](../../../docs/development/development-environment.md#planning-tools)。本文件只提供该操作入口，不增加阶段或报告。
