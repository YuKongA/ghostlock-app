---
name: ghostlock-design-critic
description: 在 GhostLock 项目中执行已触发的非作者只读设计/规划审查，或对实质设计修订、仍需独立判断的 finding 做定点复审。不用于作者自检、普通代码/测试审查、TSV 逐格核对、Lead 按原指定条件机械关闭或仅进度/证据/格式更新。
---

# GhostLock 独立设计审查

本 skill 只导航既有 critic 审查；细则以所引规范为准。链接相对本 skill 目录解析。

## 执行入口

- 触发和角色按 [AGENTS §7](../../../AGENTS.md#agents-review)：非作者、只读，不编辑、不跑门禁。参与创作的成员将任务交给既有独立成员，不自行切换身份出独立结论。
- 按 [§3 职责边界](../../../docs/development/design-review-principles.md#review-task-boundaries) 识别本次任务；普通代码/测试审查与机械核对继续原职责，不因交付、台账同步或请求“最终 PASS”启用本 skill。设计验收方法及实质差异仍由原 critic 判断。
- 首轮读取并执行 [§3.1 阶段与阻断](../../../docs/development/design-review-principles.md#review-stage-blockers)、[§3.2 输入与增量读取](../../../docs/development/design-review-principles.md#review-incremental)、[§3.3 输出与关闭](../../../docs/development/design-review-principles.md#review-output)；需判断的复审按 §3.2–§3.3 处理原 finding 和受影响差异，输入身份与停写直达 [G4](../../../docs/development/design-review-principles.md#review-g4)。
- 结论返回 Lead，由 Lead 复用原记录位置；不另交 skill 报告或完成状态。当前任务已完成且输入未变的审查不因加载 skill 重做。
- 从本批 Beads spec_id 只读对应 OpenSpec proposal/design/specs；原批准、critic finding 与实际证据按原 Beads ID 定点读取（`npm run --silent bd -- show <id> --json --brief-deps`）。设计正文不从 Beads 取副本。按[字段分工](../../../docs/development/documentation-standards.md#planning-tool-authority)区分工件齐备、批准与验收，不默认展开全部依赖、TSV或历史。

## 有具体疑点时直达

| 疑点 | 只读对应规则 |
|---|---|
| 本批必要合同说明、注释中的约束或变通理由 | [代码修改与注释判据](../../../docs/development/engineering-standards.md#engineering-code-editing) |
| 当前承诺与侦查、合理但弱依赖的后续需求混在一起 | [本批范围](../../../docs/development/design-review-principles.md#review-scope) |
| 调用者用法是否支持拟议接口/机制，而非 reviewer 优选 | [调用者、职责与方案取舍](../../../docs/development/design-review-principles.md#review-design-choice) |
| 已有具体范围/依赖疑点涉及 test/TSV/manifest；不默认逐格审核全表 | [合同与消费者](../../../docs/development/design-review-principles.md#review-contract-boundary) |
| 设计中的验收方法是否能证明目标，而非普通测试实现审查 | [D1 验证覆盖](../../../docs/development/design-review-principles.md#review-rule-d1) |
| 可观察/可重放证据是否足以关闭，或低层结果被当作产品成功 | [行为证据](../../../docs/development/engineering-standards.md#engineering-evidence)、[证据关闭](../../../docs/development/design-review-principles.md#review-evidence-closure) |
| 攻击关键路径、设备依赖行为或端到端成功 | [真机门禁](../../../docs/development/engineering-standards.md#engineering-device-gate) |
