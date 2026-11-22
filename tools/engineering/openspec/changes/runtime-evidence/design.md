# 现行运行合同的待执行证据与条件性欠账

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:tools/engineering/.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-uat"></a>

## P-RUN-X-001：真实 process-kill 证据

<!-- migrated-source: 043fdf6a Beads gl-uat description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
NOT_RUN：保留原真实process-kill门；已完成lifecycle/cancel/host-death exact类不重开，不用它们替代真实进程终止。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-run-x-001-batch-03

<!-- migrated-source: design gl-uat -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-oaq](../runhost-foundations/design.md#record-gl-oaq)；对应Beads只查执行/证据。
- OpenSpec [gl-dek](../builtin-diagnose-plugin/design.md#record-gl-dek)；对应Beads只查执行/证据。

<a id="acceptance-gl-uat"></a>

### 验收定义与原边界：gl-uat

<!-- migrated-source: acceptance_criteria gl-uat -->
NOT_RUN：保留原真实process-kill门；已完成lifecycle/cancel/host-death exact类不重开，不用它们替代真实进程终止。

批准/审核/实际结果：Beads `gl-uat` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-32v"></a>

## P-DIAG-NATIVE-001：后续改动后的最终树 regression

<!-- migrated-source: 043fdf6a Beads gl-32v description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
NOT_RUN：后续package/producer改变最终路径时重跑适用RunHost最终树矩阵，历史PASS只作基线，不阻断已经完成结构批。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-diag-native-001-batch-02

<!-- migrated-source: design gl-32v -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-oaq](../runhost-foundations/design.md#record-gl-oaq)；对应Beads只查执行/证据。
- OpenSpec [gl-cdy](../diagnose-stress-design/design.md#record-gl-cdy)；对应Beads只查执行/证据。

<a id="acceptance-gl-32v"></a>

### 验收定义与原边界：gl-32v

<!-- migrated-source: acceptance_criteria gl-32v -->
NOT_RUN：后续package/producer改变最终路径时重跑适用RunHost最终树矩阵，历史PASS只作基线，不阻断已经完成结构批。

批准/审核/实际结果：Beads `gl-32v` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-68z"></a>

## detached Payload retained artifact 回收

<!-- migrated-source: 043fdf6a Beads gl-68z description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
首个detached Payload进入production catalog时，由对应carrier批补takeover ack、owner转移、重启扫描与最终回收；当前不承诺后台GC。 未触发，不扩大当前承诺。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#active-technical-debt

<!-- migrated-source: design gl-68z -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-2qz](../runtime-foundations/design.md#record-gl-2qz)；对应Beads只查执行/证据。

#### 原触发与偿还责任（主计划基线L2456）
| detached Payload retained artifact 回收 | `data/run` + `domain/payload` | P-RUN-IO-001/批次01/P-RUN-X-001/批次01 只支持显式 `retainedArtifacts` 与 terminal retention，不承诺后台自动 GC | 第一个 detached Payload 进入 production catalog | 对应首个 carrier 子计划；证明 takeover ack、owner 转移、重启后扫描与最终回收 |

<a id="acceptance-gl-68z"></a>

### 验收定义与原边界：gl-68z

<!-- migrated-source: acceptance_criteria gl-68z -->
首个detached Payload进入production catalog时，由对应carrier批补takeover ack、owner转移、重启扫描与最终回收；当前不承诺后台GC。 未触发，不扩大当前承诺。

批准/审核/实际结果：Beads `gl-68z` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-51h"></a>

## GLR1 result protocol 演进

<!-- migrated-source: 043fdf6a Beads gl-51h description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
第二种result producer或字段不能表达已批准success fact时，再做codec/schema/version/golden/旧版只读/malformed matrix。 未触发，不扩大当前承诺。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#active-technical-debt

<!-- migrated-source: design gl-51h -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-4tt](../runtime-foundations/design.md#record-gl-4tt)；对应Beads只查执行/证据。

#### 原触发与偿还责任（主计划基线L2457）
| root result protocol evolution | `core/rootscript` | `GLR1` 只含稳定 ID/整数状态，unknown field fail-closed | 第二种 result producer 或现有字段无法表达一个 approved success fact | 单独 codec/schema 批；版本分派、golden、旧版只读策略与 malformed matrix 全绿 |

<a id="acceptance-gl-51h"></a>

### 验收定义与原边界：gl-51h

<!-- migrated-source: acceptance_criteria gl-51h -->
第二种result producer或字段不能表达已批准success fact时，再做codec/schema/version/golden/旧版只读/malformed matrix。 未触发，不扩大当前承诺。

批准/审核/实际结果：Beads `gl-51h` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-v2i"></a>

## native-backed Flow 构造期 load 重访

<!-- migrated-source: 043fdf6a Beads gl-v2i description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
第二native-backed Flow、需typed load failure或取消阻止load时重访；保留用户取消当前决定critic复评的裁定，不自动加公共initialize。 未触发，不扩大当前承诺。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#active-technical-debt

<!-- migrated-source: design gl-v2i -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-yhz](../diagnose-handoff-foundations/design.md#record-gl-yhz)；对应Beads只查执行/证据。

#### 原触发与偿还责任（主计划基线L2459）
| native-backed Flow 构造器提前 load | Diagnose backend owner | 用户知情强制选择prepare-time constructor load并取消本决定的critic复评；代价是start前已load、RUNNING取消不能阻止load、load失败只有RunHost `IO_FAILURE`且无RunReport/Flow log；构造器不得创建fd/process/thread | 出现第二个native-backed Flow、产品要求load失败进入typed RunReport/日志、或要求取消能阻止load | 触发后的首个L级native binding计划；比较constructor convention与显式统一lifecycle后一次偿还，不在P-DIAG-NATIVE-001/批次01内增公共Flow.initialize |

<a id="acceptance-gl-v2i"></a>

### 验收定义与原边界：gl-v2i

<!-- migrated-source: acceptance_criteria gl-v2i -->
第二native-backed Flow、需typed load failure或取消阻止load时重访；保留用户取消当前决定critic复评的裁定，不自动加公共initialize。 未触发，不扩大当前承诺。

批准/审核/实际结果：Beads `gl-v2i` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-mdl"></a>

## operation/C symbol 单一 authority

<!-- migrated-source: 043fdf6a Beads gl-mdl description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
第三operation、第二DSO或symbol/layout第二处重复时，再统一header/export/CMake/JNA/test生成。 未触发，不扩大当前承诺。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#active-technical-debt

<!-- migrated-source: design gl-mdl -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-yhz](../diagnose-handoff-foundations/design.md#record-gl-yhz)；对应Beads只查执行/证据。

#### 原触发与偿还责任（主计划基线L2460）
| operation catalog / C symbol authority codegen | `backend/registry` + `backend/native-binding` | 第一个真实 operation 可由一份手工审查的 generated-input authority驱动；禁止多处手抄 | 出现第3个operation、第二个DSO，或symbol/layout信息出现第2处重复 | 新JNA计划或其直接后继ADR；header/export/CMake/JNA/test由一处生成并做stale gate |

<a id="acceptance-gl-mdl"></a>

### 验收定义与原边界：gl-mdl

<!-- migrated-source: acceptance_criteria gl-mdl -->
第三operation、第二DSO或symbol/layout第二处重复时，再统一header/export/CMake/JNA/test生成。 未触发，不扩大当前承诺。

批准/审核/实际结果：Beads `gl-mdl` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-ftu"></a>

## parked/held carrier UNKNOWN 恢复

<!-- migrated-source: 043fdf6a Beads gl-ftu description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
后继已批准backend留下parked child/zombie/held kernel state时同批定义冷机恢复、重启识别、原始证据、人工处置与设备门。 未触发，不扩大当前承诺。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#active-technical-debt

<!-- migrated-source: design gl-ftu -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-k6e](../runtime-foundations/design.md#record-gl-k6e)；对应Beads只查执行/证据。

#### 原触发与偿还责任（主计划基线L2461）
| parked/held carrier 的 UNKNOWN 恢复 | `backend/transport` + 对应 backend | P-RUN-X-001/批次01只保留durable terminal snapshot与`REBOOT_REQUIRED/UNKNOWN`，不自动kill/retry；当前无真实carrier | 未来重新批准的production backend可留下parked child/zombie/held kernel state | 对应新L级计划；冷机恢复、重启识别、证据保留与人工处置路径进入device gate |

<a id="acceptance-gl-ftu"></a>

### 验收定义与原边界：gl-ftu

<!-- migrated-source: acceptance_criteria gl-ftu -->
后继已批准backend留下parked child/zombie/held kernel state时同批定义冷机恢复、重启识别、原始证据、人工处置与设备门。 未触发，不扩大当前承诺。

批准/审核/实际结果：Beads `gl-ftu` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-dcu"></a>

## preview workspace 生命周期回收

<!-- migrated-source: 043fdf6a Beads gl-dcu description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
首次重复/连续preview或页面关闭清理时同批确定owner，证明旧workspace回收不删除当前可见preview。 未触发，不扩大当前承诺。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#active-technical-debt

<!-- migrated-source: design gl-dcu -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-zgp](../datastore-runtime/design.md#record-gl-zgp)；对应Beads只查执行/证据。

#### 原触发与偿还责任（主计划基线L2466）
| preview workspace 未回收 | `data/run PreviewRootScriptPublisher` | P-SETTINGS-IO-001只把preview收口为durable Payload-only读取，不改变current每次创建UUID workspace且不回收的行为 | 首次实现重复/连续preview生命周期，或页面关闭清理 | 触发后的同一批；定义唯一owner并证明旧workspace在不删除当前可见preview的前提下被清理 |

<a id="acceptance-gl-dcu"></a>

### 验收定义与原边界：gl-dcu

<!-- migrated-source: acceptance_criteria gl-dcu -->
首次重复/连续preview或页面关闭清理时同批确定owner，证明旧workspace回收不删除当前可见preview。 未触发，不扩大当前承诺。

批准/审核/实际结果：Beads `gl-dcu` notes；本段不维护实时勾选或新PASS。
