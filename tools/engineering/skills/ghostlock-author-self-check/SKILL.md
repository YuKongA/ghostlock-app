---
name: ghostlock-author-self-check
description: 在 GhostLock 项目中，设计或代码作者正式送审、交付前检查自己的方案或最终 diff，或按用户要求做作者自检。用于提前发现现有合同、事实通路和本批范围问题；不充当独立 design-critic，也不触发额外审批。
---

# GhostLock 作者自检

本 skill 只导航既有作者自检；细则以所引规范为准。链接相对本 skill 目录解析，不依赖 shell 当前目录。

## 执行入口

读取并执行 [§3.0 作者自检](../../../../docs/development/design-review-principles.md#review-author-self-check)，直接处理原交付；仅检查任务只报告，修正与验证限本批已有授权。当前任务已按该条完成且输入未变时，复用结果，不因加载 skill 再做一轮。

需要独立审查时，按 [AGENTS §7](../../../../AGENTS.md#agents-review) 委派非作者，而非由作者切换身份再自审。

按[改动级别](../../../../docs/development/engineering-standards.md#engineering-change-levels)选择现成输入：有设计时从具名 OpenSpec change 或 Beads spec_id 读取相关正文，批准/证据读原 Beads；普通 S/M 没有 change/spec_id 时直接检查用户请求、M 级简短说明、最终 diff 与相关合同，**不为自检补建计划、任务或报告**。新 L 或实质差异的独立审查仍按 §7，CDA 的作者分析不替代它；已批准实施不因此重开设计。[正文与证据分工](../../../../docs/development/documentation-standards.md#planning-tool-authority)仍是唯一规则。

## 有具体疑点时直达

| 疑点 | 只读对应规则 |
|---|---|
| 本批代码编辑、注释取舍或以注释代替约束 | [代码修改与注释判据](../../../../docs/development/engineering-standards.md#engineering-code-editing) |
| 从调用者用法推导接口/机制，或把尚未接通的能力当死代码 | [调用者、职责与方案取舍](../../../../docs/development/design-review-principles.md#review-design-choice) |
| 同批计划混入后续需求，或最新要求改变承诺 | [本批范围](../../../../docs/development/design-review-principles.md#review-scope) |
| 行为合同与实现细节难分，或 test/TSV 问题被当作阻断 | [合同与消费者](../../../../docs/development/design-review-principles.md#review-contract-boundary)、[阶段与影响](../../../../docs/development/design-review-principles.md#review-stage-blockers) |
| 用例应断言什么，是否误锁实现 | [D1 验证覆盖](../../../../docs/development/design-review-principles.md#review-rule-d1) |
| 选择验证、记录可观察/可重放证据及其证明边界 | [验证分级](../../../../docs/development/engineering-standards.md#engineering-verification-levels)、[行为证据](../../../../docs/development/engineering-standards.md#engineering-evidence) |
| 攻击关键路径、设备依赖行为或端到端成功 | [真机门禁](../../../../docs/development/engineering-standards.md#engineering-device-gate) |
