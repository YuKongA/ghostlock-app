# 由真实consumer触发的插件后继需求

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-2zi"></a>

## P-BACKEND-CORE-001：由真实 consumer 触发配置实现

<!-- migrated-source: 043fdf6a Beads gl-2zi description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
真实backend config consumer出现后细化；保留现行typed store和owner，不为无使用者的配置预建框架或迁移。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-core-001

<!-- migrated-source: design gl-2zi -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-7yv](../runtime-foundations/design.md#record-gl-7yv)；对应Beads只查执行/证据。
- OpenSpec [gl-sfg](../plugin-architecture/design.md#record-gl-sfg)；对应Beads只查执行/证据。

<a id="acceptance-gl-2zi"></a>

### 验收定义与原边界：gl-2zi

<!-- migrated-source: acceptance_criteria gl-2zi -->
真实backend config consumer出现后细化；保留现行typed store和owner，不为无使用者的配置预建框架或迁移。

批准/审核/实际结果：Beads `gl-2zi` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-9lz"></a>

## P-BACKEND-X-002：data-only profile provider / Document

<!-- migrated-source: 043fdf6a Beads gl-9lz description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
独立更新输入而不重装APK；在真实provider/Document consumer批移除B1 Diagnose假profile fixture，不现在把profile nullable；exact设计/批准与验证待做。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03

<!-- migrated-source: design gl-9lz -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-sfg](../plugin-architecture/design.md#record-gl-sfg)；对应Beads只查执行/证据。
- OpenSpec [gl-dek](../builtin-diagnose-plugin/design.md#record-gl-dek)；对应Beads只查执行/证据。

<a id="acceptance-gl-9lz"></a>

### 验收定义与原边界：gl-9lz

<!-- migrated-source: acceptance_criteria gl-9lz -->
独立更新输入而不重装APK；在真实provider/Document consumer批移除B1 Diagnose假profile fixture，不现在把profile nullable；exact设计/批准与验证待做。

批准/审核/实际结果：Beads `gl-9lz` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-tz3"></a>

## P-BACKEND-X-002：真实 Custom consumer 的公开能力合同

<!-- migrated-source: 043fdf6a Beads gl-tz3 description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
按原已认可目标设计公开能力目录与raw-memory语义、专用context；不借录入任务扩大B1批准，不复活旧filtered Custom。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03

<!-- migrated-source: design gl-tz3 -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-sfg](../plugin-architecture/design.md#record-gl-sfg)；对应Beads只查执行/证据。

<a id="acceptance-gl-tz3"></a>

### 验收定义与原边界：gl-tz3

<!-- migrated-source: acceptance_criteria gl-tz3 -->
按原已认可目标设计公开能力目录与raw-memory语义、专用context；不借录入任务扩大B1批准，不复活旧filtered Custom。

批准/审核/实际结果：Beads `gl-tz3` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-r8h"></a>

## P-BACKEND-X-002：外部 extractor Maven consumer

<!-- migrated-source: 043fdf6a Beads gl-r8h description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
独立库发布固定版本后在本仓设计consumer/provider/projector与report-only验证；K2/源码/发布在独立库，不复制任务或源码module；本仓consumer尚未授权。
原始来源（已归档/承接）：docs/analysis/kotlin-generic-kernel-extractor-plan.md

<!-- migrated-source: design gl-r8h -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-sfg](../plugin-architecture/design.md#record-gl-sfg)；对应Beads只查执行/证据。

### Kernel Symbol Extractor 计划迁移指针

> 2026-11-18 用户裁定：通用 extractor 改为独立 **Kernel Symbol Extractor** 库，发布到 Maven，再由 GhostLock 固定版本引用。原同仓 module 方案被取代。

计划正文的唯一权威已移动到：

- 当前本地权威：`/Users/nickji/Documents/Program/Project/KernelSymbolExtractor/docs/plan/kernel-symbol-extractor-plan.md`
- 计划中的 repository 路径（尚未 commit／push，当前不能当作已发布入口）：`https://github.com/NickJi2019/KernelSymbolExtractor/blob/main/docs/plan/kernel-symbol-extractor-plan.md`

GhostLock 本仓只保留 Maven consumer、backend/provider request、profile projector 与产品门禁责任；不得从本文件恢复 extractor 源码 module、Capstone CMake include、Git submodule/composite build、checked-in AAR/JAR 或旧 Rust fallback。独立库计划的设计批准、实现、发布与 GhostLock product cutover 仍分别经过各自 gate；本迁移不把任何 `NOT_RUN` 改成 `PASS`。

若GhostLock后续持久化library返回的report/expectation objects，真实性与完整性属于GhostLock自有格式合同：必须先验证自有authenticated envelope／record，再重建Java objects，再调用library semantic verifier，最后进入projector。library verifier不证明caller persistence authenticity，也不能发现对整个dynamic ledger的coherent rewrite。

独立库 K1 已交付 Java 对象合同和内部 Kotlin/JVM 引擎。2026-11-19 首发范围收敛到 `:api/:engine/:android`，桌面 facade 延后。当前构建、签名演练和隔离样例的证据，以及此前四坐标的历史结果，均由独立库权威计划维护。GhostLock 的 Maven consumer 仍按本仓独立批次批准。


<a id="acceptance-gl-r8h"></a>

### 验收定义与原边界：gl-r8h

<!-- migrated-source: acceptance_criteria gl-r8h -->
独立库发布固定版本后在本仓设计consumer/provider/projector与report-only验证；K2/源码/发布在独立库，不复制任务或源码module；本仓consumer尚未授权。

批准/审核/实际结果：Beads `gl-r8h` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-dt8"></a>

## P-BACKEND-X-002：external import/update 与 READY 后对象保护

<!-- migrated-source: 043fdf6a Beads gl-dt8 description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
未来有external import/update真实consumer且需要READY后热改catalog时，同批设计cutoff object pin/lease后才允许回收旧object；B1只有READY前GC，不预建安装器/信任UI。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03-batch-01

<!-- migrated-source: design gl-dt8 -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-sfg](../plugin-architecture/design.md#record-gl-sfg)；对应Beads只查执行/证据。
- OpenSpec [gl-dek](../builtin-diagnose-plugin/design.md#record-gl-dek)；对应Beads只查执行/证据。

<a id="acceptance-gl-dt8"></a>

### 验收定义与原边界：gl-dt8

<!-- migrated-source: acceptance_criteria gl-dt8 -->
未来有external import/update真实consumer且需要READY后热改catalog时，同批设计cutoff object pin/lease后才允许回收旧object；B1只有READY前GC，不预建安装器/信任UI。

批准/审核/实际结果：Beads `gl-dt8` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-2u2"></a>

## 真实 backend 候选：43284 保持断开

<!-- migrated-source: 043fdf6a Beads gl-2u2 description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
FREEZE_FUTURE_CANDIDATE：未排期，待用户重新批准新L级exact计划才接回registry/APK graph；保留真实目标设备门后才supported/selectable。对应批处理legacy native物理目录与有实际归属后的AES/HMAC；不复活旧43499 child。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03

<!-- migrated-source: design gl-2u2 -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L2337-L2346 -->
<a id="p-backend-native-001"></a>

<a id="p-backend-native-001-batch-01"></a>

<a id="p-run-x-001-r7"></a>

##### P-BACKEND-NATIVE-001/批次01 43284-Glued carrier（未来独立 L 子计划）

- native tree保持 `FREEZE_FUTURE_CANDIDATE`；独立 exact delta 只替换 bootstrap product hardcode→common script，root helper物理事务保留，同批完成不中断 root point。



<a id="acceptance-gl-2u2"></a>

### 验收定义与原边界：gl-2u2

<!-- migrated-source: acceptance_criteria gl-2u2 -->
FREEZE_FUTURE_CANDIDATE：未排期，待用户重新批准新L级exact计划才接回registry/APK graph；保留真实目标设备门后才supported/selectable。对应批处理legacy native物理目录与有实际归属后的AES/HMAC；不复活旧43499 child。

批准/审核/实际结果：Beads `gl-2u2` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-2ot"></a>

## 真实 backend 候选：98260 保持断开

<!-- migrated-source: 043fdf6a Beads gl-2ot description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
FREEZE_FUTURE_CANDIDATE：未排期，待用户重新批准新L级exact计划才接回registry/APK graph；保留真实目标设备门后才supported/selectable。对应批处理legacy native物理目录与有实际归属后的AES/HMAC；不复活旧43499 child。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03

<!-- migrated-source: design gl-2ot -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L2347-L2357 -->
<a id="p-backend-native-002"></a>

<a id="p-backend-native-002-batch-01"></a>

<a id="p-run-x-001-r8"></a>

##### P-BACKEND-NATIVE-002/批次01 98260 carrier（未来独立 L 子计划）

- native tree保持 `FREEZE_FUTURE_CANDIDATE`；独立 exact delta 只替换 KSU product hardcode→common script，self-reentry/stage write/restore/park留在 executable。
- 43499/43284 若未来重建 shared operation，按 P-DIAG-NATIVE-001/批次01 规则另立子计划；不复活旧 JNI。



<a id="acceptance-gl-2ot"></a>

### 验收定义与原边界：gl-2ot

<!-- migrated-source: acceptance_criteria gl-2ot -->
FREEZE_FUTURE_CANDIDATE：未排期，待用户重新批准新L级exact计划才接回registry/APK graph；保留真实目标设备门后才supported/selectable。对应批处理legacy native物理目录与有实际归属后的AES/HMAC；不复活旧43499 child。

批准/审核/实际结果：Beads `gl-2ot` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-m0f"></a>

## 真实 backend 候选：43499 保持断开

<!-- migrated-source: 043fdf6a Beads gl-m0f description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
FREEZE_FUTURE_CANDIDATE：未排期，待用户重新批准新L级exact计划才接回registry/APK graph；保留真实目标设备门后才supported/selectable。对应批处理legacy native物理目录与有实际归属后的AES/HMAC；不复活旧43499 child。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03

<!-- migrated-source: design gl-m0f -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-5r9](../runtime-foundations/design.md#record-gl-5r9)；对应Beads只查执行/证据。
- OpenSpec [gl-sfg](../plugin-architecture/design.md#record-gl-sfg)；对应Beads只查执行/证据。

<a id="acceptance-gl-m0f"></a>

### 验收定义与原边界：gl-m0f

<!-- migrated-source: acceptance_criteria gl-m0f -->
FREEZE_FUTURE_CANDIDATE：未排期，待用户重新批准新L级exact计划才接回registry/APK graph；保留真实目标设备门后才supported/selectable。对应批处理legacy native物理目录与有实际归属后的AES/HMAC；不复活旧43499 child。

批准/审核/实际结果：Beads `gl-m0f` notes；本段不维护实时勾选或新PASS。
