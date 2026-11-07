# ROADMAP：Backend root execution 排期视图

> 2026-11-04 用户批准 D1–D9 与 R0→R1。进度 authority 是 [MASTER-PLAN.md](MASTER-PLAN.md)，完整设计是 [backend-root-execution-unification-plan.md](backend-root-execution-unification-plan.md)，逐文件 scope 是 [backend-root-execution-unification-ledger.tsv](backend-root-execution-unification-ledger.tsv)。

> **本页代号范围**：决策/批次使用[主计划语义代号](backend-root-execution-unification-plan.md#plan-semantic-identifiers)；图内 R0/R1 等变量仅是 Mermaid 节点名，不是人读主代号。原顺序、状态和 gate 保留。

## 批次顺序

~~~mermaid
flowchart LR
  R0["E-DOC-X-001/批次01 authority 收口<br/>完成"] --> R1["P-BACKEND-X-001/批次01 disabled closure<br/>完成"]
  R1 --> R2["P-RUN-IO-001/批次01 source + host/AVD matrix<br/>PASS / device NOT_RUN"]
  R2 --> R3["P-RUN-X-001/批次01 source + compile/unit/正常transport<br/>PASS / lifecycle专项待补"]
  R3 --> R4["P-RUN-CORE-001/批次01 source<br/>完成"]
  R4 --> R4B["P-LOG-IO-001/批次02 automatic export sink<br/>source complete"]
  R4B --> R4C["P-LOG-X-001/批次01 single log hub<br/>后继批次证据已关闭旧AVD欠账"]
  R4C --> R5["P-BACKEND-X-001/批次02 real backends<br/>DISCONNECTED"]
  R5 --> R6["P-DIAG-NATIVE-001/批次01 DIAGNOSE<br/>完成"]
  R6 --> R4D["P-LOG-X-001/批次02 dual stream + sink backlog<br/>revised build/AVD gate PASS"]
  R4D --> HND["P-RUN-HANDOFF-001/批次01 common handoff<br/>HND-I1/V1 COMPLETE"]
  HND --> E["release前偿还 RunHost lifecycle专项、真实设备与最终验收欠账"]
  R5 -. 新批准L计划 .-> F["future backend<br/>not scheduled"]
~~~

## 状态

| 批次 | 状态 | 当前事实 | 下一条件 |
|---|---|---|---|
| [E-DOC-X-001/批次01](backend-root-execution-unification-plan.md#e-doc-x-001-batch-01) | 完成 | 旧 authority 归档，pointer/index 收口 | 无 |
| [P-BACKEND-X-001/批次01](backend-root-execution-unification-plan.md#p-backend-x-001-batch-01) | 完成 | disabled/original closure 删除并留当批门禁记录 | 无 |
| [P-RUN-IO-001/批次01](backend-root-execution-unification-plan.md#p-run-io-001-batch-01) | source + host/AVD matrix PASS / device NOT_RUN | typed documents、renderer、artifact/result source；host fault matrix `31/31`、6.6 AVD staging/identity `3/3` | 真实carrier设备证据 |
| [P-RUN-X-001/批次01](backend-root-execution-unification-plan.md#p-run-x-001-batch-01) | source、compile/unit与正常App/Shizuku runtime PASS / lifecycle专项待补 | neutral execution spine与typed RunHost已落地；current debug/release unit各`111/111`，4 KiB/16 KiB AVD App/direct-JNA/Shizuku full exact class各`OK (3 tests)` | `RunHostLifecycleTest`、`RunHostCancellationTest`、`RunHostHostDeathTest` exact AVD证据；真实carrier设备门 |
| [P-RUN-CORE-001/批次01](backend-root-execution-unification-plan.md#p-run-core-001-batch-01) | source complete | SessionPhase删除、source guard与非导出docs pointer已落地；automatic-export产品责任由P-LOG批次承接 | 无独立source工作；随P-LOG产品门验收 |
| [P-LOG-IO-001/批次01](backend-root-execution-unification-plan.md#p-log-io-001-batch-01)→[P-LOG-IO-001/批次02](backend-root-execution-unification-plan.md#p-log-io-001-batch-02) | source + current AVD evidence PASS / device-final NOT_RUN | 保留settings、formatter、canonical/manual-share语义；current全流式sink与>10 MiB、storage/UI、App/Shizuku AVD证据均已通过 | 真实设备门与最终产品验收 |
| [P-LOG-X-001/批次01](backend-root-execution-unification-plan.md#p-log-x-001-batch-01) | **CLOSED BY SUCCESSOR EVIDENCE** | one LogHub hub、Flow-owned RunReport、typed log wire与统一ANSI renderer已落地；批次02 LS-V3和Handoff HND-V1共同覆盖current 4 KiB/16 KiB AVD wire/UI/runtime | 无独立旧批次门；automatic export产品状态仍由P-LOG-IO-001/批次02的真实设备/最终验收决定 |
| [P-LOG-X-001/批次02](backend-root-execution-unification-plan.md#p-log-x-001-batch-02) | **REVISED GATE PASS** | v3 LS-I1、统一binding LS-I2与直接sink注入 LS-I3已实施；data adapter预构造存储sink、LogHub接受后补UI sink。用户删除两个native artifact verifier与专用CMake manifest后，`nativeBuildAndroid`双variant构建/JNA distribution PASS；debug/release unit各104/104、architecture、native host 30/0、>10 MiB MediaStore与4 KiB AVD storage/UI 7项、App/Shizuku各1项均PASS；LS-V1/V2/V3关闭 | 真实设备门与最终产品验收；不把构建PASS写成ELF/ABI/JNI精确核验 |
| [P-BACKEND-X-001/批次02](backend-root-execution-unification-plan.md#p-backend-x-001-batch-02) | **DISCONNECTED / CLOSED** | S2/S3取消；registry与APK native graph均无真实攻击backend；S1 neutral source无真实caller | 无；重新接线必须由新的L级计划明确取代断开裁定 |
| [P-DIAG-NATIVE-001/批次01](backend-root-execution-unification-plan.md#p-diag-native-001-batch-01) | **COMPLETE** | JNA 5.19.1、release-visible DIAGNOSE、真实Diagnose DSO/C ABI、constructor binding、SequentialFlow、Native/Shell日志与LogHub source已落地；dependency/host ABI/双variant artifact/debug+release 102 unit/focused architecture已PASS；6.6/4 KiB与6.6/16 KiB AVD debug exact class均为`OK (3 tests)`/`SKIPPED=0`，两个page size的App/Shizuku minified-release production UI均全链路PASS | 无；Diagnose仍为`DIAGNOSTIC_ONLY` |
| [P-RUN-HANDOFF-001/批次01](backend-root-execution-unification-plan.md#p-run-handoff-001) | **COMPLETE** | 固定位置common `run_handoff()`、accepted-start home binding、单次不带version/size/reserved与旧兼容分支的`gl_diagnose_run`、Flow multi-fact与current UML已落地；host contracts `3/3`、聚焦unit `19/19`、debug/release unit各`111/111`、native host `31/0`、architecture、双variant build/JNA distribution均PASS；4 KiB/16 KiB App/direct-JNA/Shizuku各`OK (3 tests)` | 无；只证明diagnostic handoff infrastructure，不证明真攻击或root产品成功 |
| [P-BACKEND-NATIVE-001/批次01](backend-root-execution-unification-plan.md#p-backend-native-001-batch-01) / [P-BACKEND-NATIVE-002/批次01](backend-root-execution-unification-plan.md#p-backend-native-002-batch-01) | not scheduled | 43284-Glued与98260仍source-only冻结，未接registry/APK graph | 用户重新批准具名真实backend后另立L级计划 |

## 当前阻断与边界

- 当前production-root backend catalog为0；`DIAGNOSE`在debug/release均为`DIAGNOSTIC_ONLY`，只报告diagnostic outcome且产品成功恒为false。
- P-LOG-X-001/批次01的旧current-tree AVD欠账已由批次02 LS-V3与Handoff HND-V1后继证据关闭；这不关闭automatic export真实设备门或最终产品验收。
- P-LOG-X-001/批次02 v3 source、日志专用命名与UI/file/MediaStore统一sink列表已完成；旧累计256/512限制已从current source移除，debug/release 104项unit与4 KiB AVD 10 MiB MediaStore/App/Shizuku门已PASS；用户删除不必要的native artifact verifier后，修订的双variant构建/JNA distribution普通门PASS。
- P-RUN-HANDOFF-001/批次01的source、host/unit/architecture、双variant build及4 KiB/16 KiB exact AVD门均已完成；结果只关闭common handoff/Diagnose纵向，不替代真实backend目标设备攻击门。
- P-RUN-X-001/批次01的正常App/Shizuku transport/runtime已有current证据；生命周期、取消与host-death三个专属exact AVD判据仍为`NOT_RUN`。
- AVD 只证明 transport、binding 与 ordinary staging，不替代目标设备攻击门。
- 43499-Glued、43284-Glued 与 98260 的攻击算法、offset、waiter/race、self-reentry、restore 与 park 继续冻结。
- P-BACKEND-X-001/批次02旧production授权已被后续断开裁定取代；不存在下一carrier批，也不存在selectable、Ready或supported真实backend。
