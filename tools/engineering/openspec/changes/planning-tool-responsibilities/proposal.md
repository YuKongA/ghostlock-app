## Why

E-DOC-X-002/批次04。2026-11-21用户纠正：迁移对象是旧纯Markdown计划，而不是废除OpenSpec。此前043fdf6a将设计正文全部移入Beads并卸载OpenSpec，工具分工超出用户原意。

## What Changes

恢复OpenSpec proposal/design/specs作为设计、范围、验收规范的唯一正文；Beads只管理执行任务、依赖、进度、批准/运行证据并引用设计；CDA负责设计方法。旧docs/plan和此前历史归档保持退役。

## Capabilities

本次仅纠正管理职责与迁移载体，不新增产品行为合同，skip_specs=true；既有公共wire合同迁入主spec，B1已批准增量作为原设计投影保留，不扩大批准。

## Impact

只改OpenSpec/npm工具配置、现有AGENTS/skills/配套规范和Beads内容指针，不改生产代码。原52条记录的ID、状态、依赖、批准与证据保持；验证后主动提交。本批任务与实际验证证据：Beads `gl-8yr`。
