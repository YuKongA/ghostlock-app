---
name: openspec-sync-specs
description: 仅在已完成并验证的行为合同变更需要同步到 OpenSpec 主规范时使用；纯工具/状态迁移不需要此步骤。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 同步已验证的行为合同

仅在所属行为变更已经完成并满足原批准与适用门禁时，将 change 的 delta specs 合入 OpenSpec 主 specs，保持 requirement/scenario身份及唯一权威；当前设计和验收定义不留两份并行维护。

未实现或未过真机门的 B1 等增量仍在所属 change，不因用户认可设计、Beads decision.closed、validate成功而提前发布为主规范。纯工具/状态迁移不补造产品合同。

全工程治理规范留在现有专属文档；不把教材、AGENTS或工程规范复制成一套新spec。记录实际同步结果到原Beads任务，设计历史按原归档方式保留。

唯一分工见[文档规范](../../../../docs/development/documentation-standards.md#planning-tool-authority)，安装/命令见[开发环境](../../../../docs/development/development-environment.md#planning-tools)。本skill只导航该职责，不增加阶段或报告。
