## Why

现有活跃计划同时维护设计、批准和多处 checkbox，产生旧清单复活与进度漂移。用户选择 OpenSpec + Beads + CDA，要求先交付开发环境入口、固定工具版本、跑通一个现有流程/测试变更，再迁移有效活跃计划。

## What Changes

- 开发环境指南与 README / AGENTS 入口；官方来源、按任务安装与当前接线问题分开。
- OpenSpec 管变更设计入口，Beads 管任务状态/依赖，CDA 为作者做技术设计的方法；既有自检、非作者 critic、批准与真机门保持。
- 现有计划正文和批准记录保留原锚点；只迁移当前有效状态，不导入历史未勾选项，不复制其它仓库任务。
- 不改生产代码、业务合同、历史归档，不添加平行报告、rollback 产物或新的审批步骤。

## Capabilities

无产品行为合同变化；本变更 `skip_specs: true`。现行工程规则仍以 docs/development 对应规范为准，不复制到 specs。

## Impact

根 npm 配置、OpenSpec/Beads 原生配置与数据、相关 skill、现有规范和活跃计划入口。Beads 入口：`npm run --silent bd -- show gl-1w2 --json`；任务与证据只在该任务树维护。语义代号 `E-DOC-X-002`：开发环境与规划工具接入。
