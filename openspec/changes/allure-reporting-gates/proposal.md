## Why

2026-11-15 用户批准把现有 Android 单测与 CTest 的手写统计／报告层替换为 Allure Report 3 薄适配，并明确保留真实 runner 失败、跳过语义与真机门禁。该批已经实施并验证，但旧计划迁入 OpenSpec／Beads 时遗漏，源码至今与三个更早完成却未形成独立提交的 runtime foundation 批次、`gl-dek`、`gl-4on` 叠加在同一未提交工作树。若继续把它们视为无主基线，任何提交都会夹带另一批或留下不能独立验证的中间树。

本 change 恢复既有 `E-BUILD-X-002/批次01` 的设计与执行入口，并以 `E-BUILD-X-002/批次02` 恢复 `P-RUN-CORE-001/批次02`、`P-RUN-X-001/批次03`、`P-RUN-X-001/批次04` 已批准且已验证、但从未成为独立提交前驱的净 source baseline；同时定义四棵提交候选的唯一 owner。不重新设计 Allure 或 runtime 产品合同、不新增测试阶段，也不把历史运行结果改写为当前 PASS。原 Allure 批准来源为 [2026-11-15 对话](codex://threads/01a1aeeb-f4e4-7740-824a-743f4b878f92)。

## What Changes

- 恢复 Allure 报告门的完整现行设计、逐 hunk owner、失败语义、非目标和验收方法。
- 把提交依赖固定为 `E-BUILD-X-002/批次01 → E-BUILD-X-002/批次02 → gl-dek → gl-4on`；每个候选必须从前一棵已验证树增量构造，不能用当前叠加树代替。
- `E-BUILD-X-002/批次02` 只恢复三个既有 runtime 批次完成态的净 source、test、guard 与 current-view 文档字节；旧任务仍保持 closed，不重跑设计、不恢复已归档 Markdown 计划，也不吸收 B1 plugin source。
- 将语义属于 Allure 基线的 `prepareAllure` 缺失输出重建判据和 `node_modules` 清理边界归回本批；`gl-4on`只消费并验证，不重复拥有。
- 将 Diagnose package 整文件及其直接 host-contract 接线归 `gl-dek`；`gl-4on`保留构建闭环清理、CI 和当前事实文档，不把验证过的前置实现据为本批源码。

本 change 不改变已批准的产品运行时、plugin API、manifest、RunHost、provider/import/Custom、设备支持或真机判据；批次02只是把既有 runtime 完成态变成 B1 可消费的独立前驱，不恢复固定测试数量，也不另建报告入口。

## Capabilities

迁移既有已批准工程设计并修复提交 owner；无新增产品行为 delta，`skip_specs=true`。Allure 门的工程行为与验收只在同目录 `design.md` 维护，不提前同步为产品公共 spec。

## Impact

- 直接受影响：根／App Gradle 测试门、`tools/allure_report.py`、两份工具回归、三个既有runtime批次的净source／test／guard与current-view恢复、工程规范及四个提交候选的依赖／owner。
- `gl-dek` 与 `gl-4on` 的产品目标、最终树和原批准不变；只修复此前未定义的 runtime 前置基线、B1 两个漏列 support 文件与提交边界。
- Beads `gl-8s7` 保存批次01状态；批次02使用独立 Beads 任务保存候选、依赖和实际证据。本 proposal 不复制实时进度。
