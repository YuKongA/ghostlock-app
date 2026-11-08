# ROADMAP：Backend root execution 排期视图

> 2026-11-04 用户批准 D1–D9 与 R0→R1。进度 authority 是 [MASTER-PLAN.md](MASTER-PLAN.md)，完整设计是 [backend-root-execution-unification-plan.md](backend-root-execution-unification-plan.md)，逐文件 scope 是 [backend-root-execution-unification-ledger.tsv](backend-root-execution-unification-ledger.tsv)。

> **本页代号范围**：决策/批次使用[主计划语义代号](backend-root-execution-unification-plan.md#plan-semantic-identifiers)；图内 R0/R1 等变量仅是 Mermaid 节点名，不是人读主代号。原顺序、状态和 gate 保留。

## 批次顺序

~~~mermaid
flowchart LR
  R0["E-DOC-X-001/批次01 authority 收口<br/>完成"] --> R1["P-BACKEND-X-001/批次01 disabled closure<br/>完成"]
  R1 --> R2["P-RUN-IO-001/批次01 source + host/AVD matrix<br/>PASS / device NOT_RUN"]
  R2 --> R3["P-RUN-X-001/批次01 source + 正常transport<br/>PASS / lifecycle专项待补"]
  R3 --> R4["P-RUN-CORE-001/批次01 source<br/>完成"]
  R4 --> R4B["P-LOG-IO-001/批次02 export sink<br/>source + AVD PASS"]
  R4B --> R4C["P-LOG-X-001/批次01-02 single hub + dual stream<br/>PASS"]
  R4C --> R5["P-BACKEND-X-001/批次02 real backends<br/>DISCONNECTED"]
  R5 --> R6["P-DIAG-NATIVE-001/批次01 + HANDOFF<br/>完成"]
  R6 --> ICON["P-UI-ICON-001/批次01 project Icons<br/>developer accepted"]
  ICON --> BD["P-BACKEND-CORE-001/批次01 Definition/Descriptor/UI<br/>complete"]
  BD --> LC["P-LOG-X-001/批次03 LogConnector/order<br/>complete"]
  LC --> DS["P-SETTINGS-IO-001/批次01 typed DataStore<br/>source + ordinary gates PASS / Android runtime pending"]
  DS --> KT["P-RUN-X-001/批次02 Kotlin owner/package<br/>complete"]
  KT --> CLEAN["core/data → Host去重 → backend → UI/root composition<br/>rolling detail / not authorized"]
  CLEAN --> AS["P-DIAG-NATIVE-001/批次02 producer churn<br/>rebase after package cleanup"]
  AS --> E["最终树：RunHost lifecycle exact AVD<br/>automatic export真实设备/最终验收"]
  ICON -. later requirement .-> ICON2["P-UI-ICON-002 full upstream catalog<br/>design pending / not in 001"]
  R5 -. 新批准L计划 .-> F["future real backend<br/>not scheduled"]
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
| [P-UI-ICON-001/批次01](backend-root-execution-unification-plan.md#p-ui-icon-001-batch-01) | **DEVELOPER ACCEPTED** | 88个current pair、30个caller/175条import收口；architecture、unit、androidTest compile、双variant assemble与host native通过；开发者目视两皮肤无显示错误并关闭IC-V1 | 无图标批工作；不必要的JNA distribution task已由后续开发者裁定删除 |
| [P-UI-ICON-002](backend-root-execution-unification-plan.md#p-ui-icon-002) | **REQUIREMENT RECORDED / DESIGN PENDING** | 覆盖Material 1.7.8与Miuix 0.9.4每一个导出icon，不只current usage | 单独冻结生成、style/weight、配对与成本合同；不夹入001 |
| [P-BACKEND-CORE-001/批次01](backend-root-execution-unification-plan.md#p-backend-core-001-batch-01) | **COMPLETE** | 75行source/caller/用户纠正闭包已落地；outer BackendDefinition/唯一registry、BackendDescriptor、runtime/UI投影与具名UI完成，旧catalog/carrier/generic DSL清零；architecture、unit、host native、6.6 AVD UI通过；删除JNA distribution task后clean与`nativeBuildAndroid`通过 | 无；BD-I3继续等待真实配置consumer |
| [P-LOG-X-001/批次03](backend-root-execution-unification-plan.md#p-log-x-001-batch-03) | **COMPLETE** | LogConnector统一source、Drained/Incomplete、stop-write→drain→close、动态attach/detach、source FIFO、raw acceptance全局commit与unlimited累计流均已落地；debug/release各120项unit、architecture、host 31项、双variant build、>10 MiB MediaStore/两皮肤UI 7项及App/Shizuku exact connector路径通过 | 无；不外推为P-DIAG-NATIVE-001压力编排、真攻击backend或root成功 |
| [P-SETTINGS-IO-001/批次01](backend-root-execution-unification-plan.md#p-settings-io-001) | **SOURCE + ORDINARY GATES PASS / ANDROID RUNTIME PENDING** | 五个完整Kotlin data class typed DataStore、无迁移、主进程owner、幂等首帧、accepted transaction、Payload-only preview exact handle与run cutoff已落地；debug/release JVM unit各130项、architecture、host native 31项和双variant build通过 | 在获准AVD执行Android instrumentation exact class后关闭DS-V1；不拿真实设备替代 |
| [P-RUN-X-001/批次02](backend-root-execution-unification-plan.md#p-run-x-001-batch-02) | **COMPLETE** | RunHost contract/client/wire/host/framework owner、Android session adapter与admission policy已归位；旧package/类名与caller-zero carrier清零。architecture、debug/release各128项unit、androidTest compile、native 31/31、双variant build、AIDL zero-diff及6.6 AVD App/Shizuku exact smoke通过 | 不外推为lifecycle全矩阵、真实设备、carrier、root或产品成功；后续批仍需独立细化/审核/批准 |
| [P-DIAG-NATIVE-001/批次02](backend-root-execution-unification-plan.md#p-diag-native-001-batch-02) | **PRODUCER CORE RETAINED / SNAPSHOT SLICE REOPENED** | opaque native group、partial-start owner、A→B churn、>10 MiB与drain后handoff候选保留；旧通用GLK/JCS配置与51行scope不再授权，当前未预建无consumer的Diagnose store | P-DIAG同批设计Diagnose具名typed DataStore、per-run snapshot、跨进程承载与exact ledger，重新复审并获用户批准；production保持NOT_RUN |
| [P-BACKEND-NATIVE-001/批次01](backend-root-execution-unification-plan.md#p-backend-native-001-batch-01) / [P-BACKEND-NATIVE-002/批次01](backend-root-execution-unification-plan.md#p-backend-native-002-batch-01) | not scheduled | 43284-Glued与98260仍source-only冻结，未接registry/APK graph | 用户重新批准具名真实backend后另立L级计划 |

## 当前阻断与边界

- 当前production-root backend catalog为0；`DIAGNOSE`在debug/release均为 `DIAGNOSTIC_ONLY`，产品成功恒为false。
- 当前顺序为已关闭Icons/Backend/Log → P-SETTINGS source与普通门完成 → P-RUN-X-001/批次02 RunHost owner/package → core/data与run coordinator拆分 → Host去重 → backend/UI/root composition收口 → 重新冻结Diagnose typed store/snapshot/producer scope → lifecycle exact AVD → automatic-export真实设备/最终验收。DataStore Android runtime、Icons 002与真实carrier门继续保留原触发，不因结构计划改写为PASS。
- 旧semantic menu graph、67-row scope、固定4×8192/第二同步symbol、37-row scope及对应critic PASS只保留历史，不再授权实施。
- 真实carrier设备门（三.2）从当前候选批排除并保持 `NOT_RUN`；触发器是用户重新批准具名carrier且新的L级计划接回registry/APK graph。
- P-LOG-X-001/批次01-02的单Hub、双流、unlimited sink queue、>10 MiB与current AVD证据继续有效；批次03只改source connector和排序边界。
- P-RUN-HANDOFF-001/批次01的source、host/unit/architecture、双variant build及4 KiB/16 KiB exact AVD门均已完成；结果只证明diagnostic handoff infrastructure。
- P-RUN-X-001/批次01正常App/Shizuku transport/runtime已有current证据；lifecycle、cancel与host-death三个专属exact AVD判据仍为 `NOT_RUN`。
- AVD只证明transport、binding与ordinary staging，不替代目标设备攻击门；43499-Glued、43284-Glued与98260的攻击算法、offset、waiter/race、self-reentry、restore与park继续冻结。
