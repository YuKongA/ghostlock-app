# 旧R4C前置设计（历史，不是现行合同）

## 已完成前置设计（容量/formatter旧拓扑已被v3取代）
<a id="p-log-x-001"></a>

<a id="p-log-x-001-batch-01"></a>

<a id="p-run-x-001-r4c"></a>

### P-LOG-X-001/批次01 单一日志中枢与 Flow 结果归属修复（取代 P-RUN-X-001/批次01/P-LOG-IO-001/批次02 的双轨展示，保留 P-LOG-IO-001/批次02 sink 拓扑）

#### 沿革、动机与取代边界

- 2026-11-05 用户先后裁定：日志系统必须是“一个日志中枢统一收集日志源，按时间排序，再统一分发到各个 sink”；`RunReport` 属于 Flow 结果，不是第二个日志源/子系统；控制方向只能是 `RunHostService` / `GhostlockUserService -> Flow.run(input)`，Flow 不调用任何 service。用户随后以“紧急修日志系统”批准本批立即实施。
- 活跃计划核对：不新建平行计划。本节直接并入唯一 active plan，取代 P-RUN-X-001/批次01 中“typed report 单独渲染 + event rows”及 P-LOG-IO-001/批次02 中 coordinator 将所有 RunHost event 硬投影为 Kotlin/Debug 的部分。P-LOG-IO-001/批次02 已简化的“一个 bounded channel + 一个 Android I/O worker + GLLOG1 唯一 byte contract”保持不变。
- 紧急修复不恢复任何真攻击 backend，不改 profile/root-script/attack 协议，不增加 queue、drainer、monitor、executor 或第二份 report state machine。

#### 唯一目标结构

```text
Flow / RunHost / application
        |
        | RunLogRecord(origin, level, message, hubElapsedMs)
        v
RunCoordinator = 唯一应用日志中枢
  - 单锁内验证、过滤、accept/commit sequence、canonical append
  - 不推断产品成功；RunReport 只作 typed terminal authority
        |
        +--> StateFlow entries --> Material3 / Miuix 同一 terminal renderer
        +--> RunLogDocumentFormatter --> manual share
        +--> RunLogSink -----------> automatic MediaStore export
        +--> diagnostics -----------> 同一 canonical snapshot
```

`RunHostService` 与 `GhostlockUserService` 仍是 Flow 的 caller/transport owner；它们只准备不可变输入、调用 `Flow.run(input)`、转发 Flow 返回的 `RunReport`。Flow 仅依赖注入的 `RunLogEmitter`、纯数据 `FlowReportContext`与一个中立 `FlowTerminalClaim` port；不依赖 Binder、Android service、Compose 或 sink。

唯一 terminal/cancel 线性化点保留在现有 RunHost lifecycle owner：`FlowTerminalClaim.claim()` 在同一 state-machine lock 内执行 `RUNNING -> TERMINAL` 并返回 `cancelledBeforeClaim`；它只是 Flow input 的中立函数 port，不暴露/传入 service 对象。`FlowReportContext` 固定 `runCapability`、PRODUCT_ROOT 时的 `RootScriptExecutionPlan`、已有 `RootScriptArtifact` 与 result identity；Flow 得到 backend facts 后先 claim，再用 claim 的 cancellation 结果一次组合最终 `RunReport`。App/Shizuku 使用同一 input/完成函数，两个 service 不再自行组合 report。Flow 未捕获异常也由同一 Flow completion helper 产生 internal-failure facts、执行同一 claim 并组合 report，不复制第二状态机。

#### 最小合同与行为差异

1. 删除 `RunEventKind` / `RunEventStage` / `RunEventDraft` 这条“进度事件再转日志”双重表达；producer 只发 `RunLogDraft(origin, level, message)`，RunHost wire 只传 `RunHostLogRecord`，通过真实 Binder 边界后投影为唯一 canonical `RunLogRecord`。旧 `RunLogEntry` 同批删除，不与 record 并存。
2. 唯一全局顺序 authority 是 `RunCoordinator` 单锁内的 acceptance/commit list order。RunHost 的 per-run `sequence` 只用于拒绝重复/逆序 callback，`elapsedRealtimeMs` 只作已验证诊断元数据；中枢在 accept 时统一计算 `hubElapsedMs`。不按源时间回溯重排，禁止 reorder buffer/第二 queue。UI、手动导出与自动导出均使用这同一 list order。
3. `Flow.run` 直接返回 `RunReport`。PRODUCT_ROOT 使用现有 `RunReportComposer` 与 root-script result codec；DIAGNOSTIC_ONLY 在同一 Flow 完成边界组合。两个 host 不再复制 report 组合分支。
4. Flow 只返回 typed `RunReport`，不发 report-summary 日志。`RunCoordinator` 在它的同一 lock 内、terminal commit 前一次性把 report 派生为普通 log records 并统一分发。`RunSessionState.report` 继续保留为结果/lifecycle authority，GLLOG1 structured trailer 继续保留，但 UI 删除 `RunReportRows` 的特殊无前缀渲染。threshold 只过滤 records，永远不删除 typed report authority。
5. 唯一中枢根据 frozen `TerminalLogLevel` 在所有 sink 之前统一过滤；旧 `app/log` Kotlin singleton/file sink 不再作第二个活跃中枢。
6. terminal 默认颜色固定为 Verbose=深灰、Debug=浅灰、Info=白、Warn=黄、Error=红。message 可含有界 ANSI SGR 前景色覆盖；reset 回到该级别默认颜色，非法/不支持序列不得破坏 Compose 渲染或伪造级别前缀。
7. 导出保留原始受验证 SGR，GLLOG1 级别/源前缀仍是语义 authority；日志失败永远不修改 `RunReport` 或 `isProductSuccess`。

#### RunHost log wire 与 ANSI exact contract

`RunHostLogRecord.aidl` 字段顺序固定为：`runId:String`、`generation:Long`、`sequence:Long`、`elapsedRealtimeMs:Long`、`origin:Int`、`level:Int`、`message:String`。`IRunHostCallback` 只保留 `onLog(record)` 与 `onTerminal(snapshot)`。snapshot 的诊断计数重命名为 `logSequence` / `droppedLogs`。

- origin wire：`KOTLIN=1`、`NATIVE=2`、`SHELL=3`；level wire：`VERBOSE=1`、`DEBUG=2`、`INFO=3`、`WARN=4`、`ERROR=5`，禁止使用 enum ordinal。
- `runId/generation` 复用现有 exact grammar；sequence 从 1 严格递增，上限 256；message 上限 8 KiB US-ASCII；record parcel 上限 12 KiB。host 侧 validation、budget、parcel 或 callback delivery 失败时 drop 该 record 并增加 host-owned `droppedLogs`；client 对 identity、sequence、timestamp、enum、message 或 parcel 的独立复验失败只在本地忽略，不反向改写 immutable host snapshot，也不改 terminal。
- plain 字符只允许 `0x20..0x7e`。唯一允许的 ESC 形式为 SGR：`ESC [` + 1..4 个十进制参数（每个 1..3 位，`;` 分隔）+ `m`，整个 sequence 最多 16 bytes。允许参数集只有 `0`、`30..37`、`39`、`90..97`；其它 ESC、C0（包括 NUL/CR/LF/TAB）、C1、空参数、超长、未终止或不支持参数全部拒绝。
- Compose 纯 parser 只解释上述集合；`0/39` 回到该 record 级别默认色。前缀在 message 之外单独渲染，所以 message 永远不能伪造前缀颜色。绕过 wire 直接传给 renderer 的 malformed sequence 也只被去除控制序列并安全显示文本，不执行任何其它 ANSI 动作。

#### Exact source ledger（P-LOG-X-001/批次01 唯一写入范围）

| action | exact files | 责任 |
|---|---|---|
| ADD | `domain/run/RunLogRecord.kt`; `aidl/.../RunHostLogRecord.aidl`; `backend/transport/RunHostLogCursor.kt`; `ui/screen/run/AnsiRunLogText.kt`; `test/.../ui/screen/run/AnsiRunLogTextTest.kt` | 最小 producer/canonical/wire record、真实 Binder cursor、纯 ANSI renderer/test。 |
| DELETE | `domain/run/RunEvent.kt`; `aidl/.../RunHostEvent.aidl`; `backend/transport/RunHostEventCursor.kt`; `core/flow/FlowProgress.kt`; `log/FileSink.kt`; `log/LogFile.kt`; `log/LogOrigin.kt`; `log/log.kt`; `core/run/RunReportComposer.kt` | 删除 event-as-log、未使用 progress wrapper、第二 Kotlin hub/file owner；report composer 迁入 Flow owner。 |
| ADD/MOVE | `core/flow/RunReportComposer.kt` | 保留算法不变，改由 Flow package 拥有。 |
| MODIFY domain/core | `domain/run/RunSessionState.kt`; `domain/run/RunLogDocumentFormatter.kt`; `core/flow/{Flow.kt,BackendFlowInput.kt,SequentialFlow.kt,FlowResult.kt}`; `core/run/RunCoordinator.kt` | canonical record/级别过滤、Flow report context+terminal claim、单中枢派生/分发。 |
| MODIFY producers | `backend/backends/debugprobe/DebugProbeFlow.kt`; `backend/backends/lua/FlowLuaImpl.kt` | 直接发 log draft，返回 Flow-owned report；清除 proc NUL/其它非法控制字符。 |
| MODIFY transport | `aidl/.../{IRunHostCallback,RunHostSnapshot}.aidl`; `backend/transport/{RunHostProtocol,RunHostSnapshot,RunHostService,RunHostProductPipeline,AppRunHostClient,ShizukuRunHostClient}.kt`; `shizuku/GhostlockUserService.kt`; `data/backend/BackendRunSessionAdapter.kt` | 两个 host/client 同构；唯一 claim；host 只调用/转运。 |
| MODIFY app/data | `GhostLockApp.kt`; `AppContainer.kt`; `data/settings/SharedPreferencesOperationalPreferencesAdapter.kt`; `data/diagnostics/AndroidDiagnosticReportBuilder.kt` | 断开旧 singleton/file；threshold 由 run context 冻结；diagnostics 读 coordinator canonical bytes。本批不增 logcat sink；现有 Android system logcat capture 仅是外部诊断输入，不是 GhostLock 日志 owner。 |
| MODIFY UI | `ui/screen/run/{RunUiState,RunPage}.kt`; `ui/viewmodel/RunViewModel.kt`; `res/values/strings.xml`; `res/values-zh-rCN/strings.xml` | 删除第二 report renderer/不再读 report labels，两皮肤共用 ANSI-aware record 渲染。 |
| MODIFY tests | `test/.../core/flow/FlowRequestTest.kt`; `test/.../core/run/RunCoordinatorLogSinkTest.kt`; `test/.../domain/run/RunLogDocumentFormatterTest.kt`; `test/.../backend/transport/{RunHostProtocolTest,RunHostStateMachineTest,RunHostSnapshotContractTest,RunHostProductPipelineTest,RunHostSharedWireTest}.kt`; `androidTest/.../backend/transport/{RunHostTestFixtures,AppRunHostSmokeTest,ShizukuRunHostSmokeTest,RunHostCancellationTest,RunHostHostDeathTest,RunHostLifecycleTest}.kt`; `androidTest/.../architecture/UiBaselineFixtures.kt`; `androidTest/.../data/run/RunCoordinatorMediaStoreIntegrationTest.kt` | 迁移现有判据并补顺序、threshold、origin/level、SGR、Flow ownership、App/Shizuku parity。 |
| MODIFY guard/docs | `tools/kotlin_architecture_d0.py`; `test/resources/architecture/*` 中由该 generator 命中的 current rows；`docs/plan/{backend-root-execution-unification-ledger.tsv,backend-root-execution-runhost-wire.md,backend-root-execution-unification-plan.md}`; `docs/development/full-process-uml.md`; `docs/plan/{MASTER-PLAN.md,REMAINING-PLAN.md,ROADMAP.md}` | 删除旧拓扑 authority，固定唯一新结构与 current pointer。 |

实施时若发现新的 production caller，先补本 ledger 并重过 critic；不在 source 中顺手扩范围。

#### 实施批次与 checkbox

- [x] R4C-S0：核对唯一 active plan，记录用户裁定、取代边界、不变项与最小结构。
- [x] R4C-S1：design-critic 只读审查两轮完成；第一轮 R4C-01..05 共 5 个阻断，修订顺序 authority、Flow terminal claim/report context、coordinator-only report projection、exact wire/ANSI 与 source ledger 后，同岗复评 5 项全部 CLOSED，阻断=0，可进入 production source。
- [x] R4C-S2：用最小 typed log record/emitter 取代 event-as-log domain/AIDL/client/host 链，两个 RunHost 保持同构。
- [x] R4C-S3：令 Flow 在中立 terminal claim 后返回最终 `RunReport`，product/diagnostic report 组合归 Flow；两个 service 只调用与转运，report-summary 只由 coordinator 在 terminal commit 内派生。
- [x] R4C-S4：以现有 `RunCoordinator` 充当唯一日志中枢，统一排序/过滤/分发；断开旧 Kotlin `app/log` 中枢与 legacy file sink。
- [x] R4C-S5：删除 report 特殊 UI 通道，Material3/Miuix 共用一个 ANSI-aware terminal renderer，并修正五级默认色。
- [x] R4C-S6：同步 unit/instrumentation fixtures、architecture guard、RunHost wire 文档与 `full-process-uml.md`；不增加为未实现 backend 预留的抽象。
- [x] R4C-S7：执行 current-tree Kotlin/unit、architecture、native host/Android 门，全量读 XML；真攻击 backend 仍断开，因此不运行攻击真机门。


历史审批/实际证据见 Beads [gl-s7q](../../logging-runtime/design.md#record-gl-s7q) notes（原文保留，不作为实时状态副本）。
#### 验收矩阵

| 输入 | 必须观测结果 |
|---|---|
| Kotlin/Native/Shell 各一条 V/D/I/W/E record，并发乱序到达 | coordinator 以单锁提交顺序产生一份 entries；UI/manual/automatic 的记录顺序、origin、level、body 一致；无第二 queue |
| threshold=Info，输入 V/D/I/W/E | V/D 在任何 sink 之前同时被过滤，I/W/E 在 UI 与两种导出中一致 |
| Info 内嵌绿色 SGR + reset + 普通文本 | 前缀与未覆盖文本为 Info 白色，仅指定 span 为绿色；malformed/unsupported SGR 安全退化，不崩溃 |
| Debug Probe diagnostic report | 全部 report summary 都是带 origin/level 前缀的普通日志行；UI 没有独立 `RunReportRows`；typed `RunReport` 仍为 `DIAGNOSTIC_COMPLETE` 且 product success=false |
| PRODUCT_ROOT `primitive=SUCCESS` 但 Payload 失败 | 日志可正确呈现成功/失败色，但 `RunReport.isProductSuccess` 仍为false |
| legacy global logger/file sink symbol scan | production caller=0；diagnostics 只读 coordinator canonical snapshot，不读第二个 log file |

#### 明确保留与不修改

- 保留 P-LOG-IO-001/批次02 `RunLogDocumentFormatter`、`RunLogSinkFactory/RunLogSink`、Android single-channel/single-worker MediaStore lifecycle、manual/automatic GLLOG1 byte 同源与 first-failure 语义。
- 保留 `RunSessionState.report` 作为结构化结果/lifecycle authority；删除的只是它作为第二套日志展示。
- 保留 current Debug Probe/Lua Flow 与所有真攻击 backend 断开状态；不改 root-script schema、Payload/Finish 成功判据、MediaStore path/pref key。
- 不创建 rollback/baseline/diff/verification fixture；恢复来源仍只是用户所有的 Git 历史与 Time Machine。

<a id="p-run-x-001-r4c-review"></a>


历史审批/实际证据见 Beads [gl-s7q](../../logging-runtime/design.md#record-gl-s7q) notes（原文保留，不作为实时状态副本）。
