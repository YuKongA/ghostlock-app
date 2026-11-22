# 日志v3、LogConnector与自动导出验收

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:tools/engineering/.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-s7q"></a>

## P-LOG-X-001 / P-LOG-IO-001：v3双流、独立sink与LogConnector

<!-- migrated-source: 043fdf6a Beads gl-s7q description -->
2026-11-21迁入的共享设计记录，不是新的执行任务。closed仅表示该记录收录完毕，不表示业务实现、新批准或门禁通过。
现行v3加LogConnector，旧v2.1 journal/GLLOG1/旧容量拓扑不再现行；真实设备与最终automatic export验收仍见gl-f47。积压OOM/host死亡/慢sink拖累的实测重访条件完整保留。

<!-- migrated-source: design gl-s7q -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L1622-L1741 -->
###### 2026-11-06 用户裁定：v3 双流＋独立 sink 积压（当前唯一实施候选）

用户明确采用全流式双流模型，进一步要求未处理流和已处理/分发流尽最快速度推进，慢 sink 的积压归各 sink 自己；`RunReport` 的日志展示只是一组普通 Info 行，用户的全局阈值高于 Info 时不显示也不导出这些行。此裁定定点取代 v2.1 的 64 KiB/15-slot chunk、staging/finalized journal、bounded viewport、terminal后二次 MediaStore copy、恢复状态机及其 45 行旧 candidate ledger；旧 design-critic 的零阻断结论不自动批准 v3。

```text
Kotlin / Native / Shell -> RunHost typed onLog -> raw Channel.UNLIMITED
    -> one LogHub processor: validate, threshold, monotonic commit order, encode once
    -> processed Channel.UNLIMITED
    -> one nonblocking dispatcher
         |-> UI sink Channel.UNLIMITED -> full append-only rows + keyed LazyColumn
         |-> private file sink Channel.UNLIMITED -> buffered append stream
         `-> optional MediaStore sink Channel.UNLIMITED -> buffered append stream
```

这里的两个主流分别是 raw 与 processed；dispatcher 只做同一 immutable processed event 的非阻塞 `trySend` fan-out，不等待任何 sink `write/flush/close`，也不运行第三套 formatter/排序。不能用零缓冲 `SharedFlow.emit()`：它会等待最慢订阅者，违背用户要求。各 sink worker 是各自 I/O/展示资源的 owner；积压队列属于各 sink，不设人为记录数/累计字节上限。`Channel.UNLIMITED` 仍受进程实际内存约束，持续慢于 producer 的 sink 会在自身队列积压；这项成本是用户在获知 raw 无界积压与最慢 sink 代价后选定的不同权衡，不把它写成固定容量或吞吐保证。技术债 owner=`P-LOG-X-001/批次02`；若 >10 MiB gate出现OOM、应用/host因积压死亡或慢sink阻塞其它sink，下一批必须以实测trace重新裁定容量/磁盘spool，而非悄悄加隐藏阈值；本批不得把这种故障写成产品成功。

1. **接入与顺序控制**：保留现有 RunHost 的 runId/generation、单 record 8 KiB、单 parcel 12 KiB、source sequence递增与非法record诊断；删除 `MAX_QUEUED_LOGS=256` 及 producer/client/snapshot的累计条数上限。唯一raw FIFO事件联合为`Start(token, immutable SinkSet)`、`Record(token, validated record)`、`UiClear(token)`、`FileBarrier(token, completion)`、`End(token, phase)`；所有操作，包括clear/share/terminal，都经同一raw→processed→dispatcher顺序，没有第二控制队列。`SinkSet`仅含本次run的UI、private-file、可选MediaStore producer handles，不是registry；不把 Binder callback、native fd 或 typed report对象当作 sink。
2. **处理与跨run**：唯一 processor以 raw channel 的接收线性化顺序分配 commitId和 elapsed、应用冻结的 level阈值、调用一次 formatter 得到 immutable record row，再 `trySend` 到 processed channel。整个步骤不等待 sink，也不按不同进程的 source timestamp重排。LogHub必须在释放旧active run槽之前把旧run全部Info摘要及`End`按序写入raw；下一run的`Start`只能随后入队。processed dispatcher在匹配token的`Start..End`区间按 FIFO 把同一个event引用`trySend`到该run的SinkSet；`End` fan-out后立即detach该集合，但旧worker自行排空自己的queue，新`Start`必须在processed FIFO中旧`End`之后生效。每个sink发布、first-failure warning与UI state更新都带token，旧run迟到worker不能污染新run；同一encoded bytes不按sink重复复制。
3. **`RunReport`**：Flow继续拥有 typed `RunReport`，供 terminal、产品成功及非日志的执行结果读取；它不能改成仅靠文本重解析。LogHub在收到 Flow 完成事实后把既有 capability/diagnostic/primitive/dispatch/Payload/Finish/cleanup/terminal/product-success 摘要转成**九条普通 Kotlin/Info raw records**，按完全相同的 threshold、commitId、formatter和fan-out处理。没有独立 report renderer、report bypass或 report 专用 sink event；阈值为 Warn/Error 时，这九条 Info 在 UI、private file、MediaStore 与manual/diagnostic日志中都不存在。terminal phase与typed `RunReport` 本身仍独立于日志阈值。
4. **文件格式**：旧 GLLOG1 的 typed report trailer不能在 Info 被过滤后偷偷保留完整 report。新写入的日志使用下述唯一`GLLOG2`物理语法，`LogDocumentFormatter`独占所有row literal/escape规则；已有GLLOG1文件只按首行识别为历史版本，当前app不解析、不迁移，也不混用GLLOG2 formatter。File与MediaStore接收相同ordered encoded rows，各自写自己的buffered stream；不再构造完整文档`ByteArray`或terminal后二次copy。
5. **sink 生命周期与failure**：run接受时先创建 UI 与private-file sink，按 frozen setting决定是否创建 MediaStore sink，再投递 Start。每个sink按自己的 queue FIFO消费；失败只锁存本sink首次故障、取消并丢弃未消费积压，逐一完成其中等待的Barrier/End completion为失败，其他sink和两个主流继续。dispatcher无法投递Barrier时立即完成其completion为失败；成功/失败/取消/关闭都exactly-once完成，share/diagnostic不会无限等一个已死sink。processor或dispatcher意外终止时停止接受并关闭关联channels/等待者，不留下继续积压而没有consumer的流。End使每个sink写剩余记录、session/end、一次flush、一次close；LogHub/Flow terminal发布不等待最慢sink。输出是streaming prefix；`terminal_received`只表示该sink收到End，不证明close成功或已持久化。
6. **UI增量存储**：UI sink保留本次run全部已处理行，不设viewport淘汰。一个app-scoped append-only `ArrayList<LogRecord>`由UI sink独占写入，按批`addAll`；它只向`RunSessionState`发布普通只读view `LogRowsSnapshot(backingRows, fixedSize, token, revision)`，snapshot固定旧prefix长度、只能按index读取，不暴露可变集合。`LogRowsSnapshot`的identity/equality/hashCode只使用token/revision/size等O(1)标量，不递归比较或打印完整backingRows。旧snapshot的prefix从不修改，`UiClear`在UI sink FIFO位置切换到新的backing list，因此旧snapshot仍稳定；读写通过同一个轻量锁保护。`RunViewModel`只透传snapshot，不对完整历史`map`；两个renderer的keyed `LazyColumn.items(count)`按index读取，只转换/解析可见行。append摊销O(1)/record，snapshot O(1)，visible get O(1)，没有每条`entries + record`或全量重映射。页面重建直接订阅最新snapshot，无incremental handshake漏行。`UiClear(token)`在UI sink FIFO内清除其前全部rows；clear后的新row正常出现，private/MediaStore不清。UI queue与历史使用相应内存，不作固定容量承诺。
7. **分享/诊断**：private-file sink始终启用。活动期请求`FileBarrier(token, completion)`，file sink在此前row写完后flush并完成`{stablePath, exactByteLength}`；文件位于app-private稳定路径，本批不rename、不rotate、不在本次run可能的reader前删除。分享/诊断只复制这个长度的前缀到自己的目标，后续append不会进入本次快照，copy使用`use`在取消/错误时关闭文件handle。terminal后异步等待file sink处理End，再分享/复制文件；这些等待只发生在发起操作的coroutine，绝不阻塞processor、dispatcher、Flow或其它sink。sink失败时所有未完成barrier请求显式失败。不需要staging/rename/refcount/recovery状态机；进程死亡可能留下partial文件。
8. **导出**：自动导出只在本次run启用时订阅processed stream，直接写MediaStore buffered output，不读取private file。慢provider仅让MediaStore sink queue积压，不拖慢UI/file或processor。保留已有失败独立于产品结果的原则；失败文件允许是不完整prefix，不将其解释为攻击失败。无额外 logcat sink、数据库、Paging、PFD/batch wire、第二日志中枢或测试专用spam backend。

**当前验收**：用正常run路径发送16,384条640-byte ASCII记录（大于10 MiB）并确认全部合法记录和唯一terminal按顺序进入各启用sink；再以100,000短记录证明snapshot构造、StateFlow发布与ViewModel投影不复制旧prefix，ArrayList整体扩容搬迁总量为O(N)、每条append摊销O(1)，可见行只按index读取。Warn阈值下九条Info report行在所有日志sink均为零而typed `RunReport`与产品成功值不变；阻塞MediaStore fake时，processor/dispatcher可继续把terminal投递给UI/file并完成两者，不等待MediaStore；再检查旧run End排队后立即新run Start、UiClear前积压行不复现、手动Barrier成功/失败/取消exactly-once、文件/media成功时byte identity、每个sink故障隔离、进程死亡partial与close失败不伪称成功。App与Shizuku真实Diagnose smoke分别证明执行域链路，不能代替10 MiB端到端transport证明。性能以无逐行flush、无O(n²) UI append与慢sink不阻塞其它sink的可复现测试为判据，不引入任意吞吐数字或隐藏容量上限。

`GLLOG2` exact grammar（UTF-8、每个物理row恰好一个结尾LF，字段顺序固定）：

```text
GLLOG2
meta\trun_id=<runId>\tgeneration=<generation>\tbackend=<backendKey>
entry\tcommit_id=<positive Long>\torigin=<Kotlin|Native|Shell>\tlevel=<Verbose|Debug|Info|Warn|Error>\telapsed_ms=<nonnegative Long or ->\tmessage=<escaped body>
session\tphase=<RunSessionPhase.name>
end\tterminal_received=true
```

上述代码框每个显示行实际都以单个LF结束，显示的`\t`表示文件中的TAB而不是反斜杠+t；真实文件按该顺序写入header、零或多条entry、terminal时的session/end，最后也有LF且不插入空行。`runId`/`backendKey`与message沿用唯一formatter的反斜杠、CR、LF、TAB转义（分别为`\\`、`\r`、`\n`、`\t`），允许既有受验证SGR原字节；其他内容不被重新解析。未接受run的手动分享沿用`run_id=-/generation=0/backend=-`及两条既有greeting的相同formatter，greeting的commitId固定为1、2且后续idle条目递增，避免让原有idle分享失效。活动期Barrier prefix可以没有session/end；close失败即使已经写入`terminal_received`也仍只是failed/partial sink产物，绝不把marker当持久化证明。formatter/unit、private sink与MediaStore parity都用同一逐字节authority。

**exact source scope**：用户批准后，同批将存活的 `RunLogLevel/Origin/Draft/Record/Emitter`、`RunLogSink*`、`RunLogFormatConfig/DocumentIdentity/DocumentFormatter`、`RunLogMediaStoreGateway` 机械更名为相应 `Log*`；保留有运行域语义的 `RunHost*`、`RunSession*`、`RunReport`、`RunUi*`、`RunCancellationView`。4个日志源码文件和2个同名测试文件随符号更名，额外8个caller已加入ledger；定点只读复评（2026-11-06）无新增架构阻断。只按本计划同名 batch 的 v3 ledger 行实施：`LogSink.kt`、`LogDocumentFormatter.kt`、`LogRecord.kt`、`RunSessionState.kt`、`LogHub.kt`、`AndroidLogSinkFactory.kt`、`BackendRunSessionAdapter.kt`、`AndroidDiagnosticReportBuilder.kt`、`RunSessionPort.kt`、`RunBackendUseCase.kt`、`AppContainer.kt`、`RunHostProtocol.kt`、`RunHostLogCursor.kt`、`RunHostSnapshot.kt`、`RunHostService.kt`、`GhostlockUserService.kt`、`RunViewModel.kt`、`RunUiState.kt`、`RunPage.kt`，以及 ledger逐路径列出的现有测试、architecture fixture、wire/UML和当前进度文档。发现额外caller先补ledger并定点复审；本批不触碰真实攻击 backend。

**v3 design-critic沿革**：`2026-11-06 v3首轮 | 总判=仍需修订 | 阻断=3`；V3-01=跨run旧worker/clear/Barrier终结，V3-02=UI全量List复制，V3-03=GLLOG2字节语法。`2026-11-06 v3定点复评 | V3-01..03 CLOSED | 总判=可进入用户最终确认及production source gate | 阻断=0 | 重要=1`；V3-PERF-01要求把“旧row零复制”限定于snapshot/StateFlow/ViewModel并使用O(1)标量identity，本节已定点修订。此评审只有设计证据，不包含Gradle、AVD或>10 MiB运行PASS。

- 原已完成步骤、批准与运行证据：见 Beads `gl-s7q` notes 的迁移前记录；本设计不维护进度checkbox。

**2026-11-06 统一 sink 定点更正（用户直接授权立即实施且明确免评审）**：上一实现的单个`LogSinkFactory`打包`file/mediaStore`，UI由`LogHub`单独构造，导致生命周期和fan-out仍硬编码三个字段；这与“各sink独立积压、统一分发”的模型不一致。本更正只改当前批次已列出的`LogSink.kt`、`LogHub.kt`、`AndroidLogSinkFactory.kt`、`AppContainer.kt`、三个相应单元/设备测试、Diagnose smoke、§11.14 UML及本计划/进度投影，不修改GLLOG2字节语法、RunHost wire、Flow/Report语义、真实backend或构建系统。`LogHub`构造器改为`List<LogSinkFactory>`；每个factory代表UI/private-file/MediaStore之一并在run接受时至多创建一个sink。UI也经factory创建，三者以带role的同一binding列表由dispatcher分发Start/Entry/End；`UiClear`只到UI、`FileBarrier`只到private-file。private-file的completion仍由具名role取得而非第二日志通道，MediaStore关闭则不创建sink；单sink失败不阻塞其它worker。验证沿用104项debug/release unit、architecture、10 MiB MediaStore/两皮肤UI/App-Shizuku AVD gate，并补核factory列表缺失/重复role、active barrier和慢sink隔离。免评审决定仅覆盖本次sink结构更正，不放宽LS-V1的既有未关闭门。

- 原已完成步骤、批准与运行证据：见 Beads `gl-s7q` notes 的迁移前记录；本设计不维护进度checkbox。

**2026-11-06 用户二次简化裁定（LS-I3，直接实施）**：LS-I2把统一分发误推成LogHub构造器必须持有factory列表，增加了`LogSinkFactory`/`LogSinkContext`及factory-role校验。实际每次run的file/MediaStore由Android data adapter依据冻结的`LogSinkRequest`创建好，再作为`List<LogSinkBinding>`直接传给`LogHub.start`；LogHub仅在接受run后创建与自身StateFlow相连的UI sink，并将它加入同一列表。LogHub构造器只接收formatter，不保留factory；分发、End、UiClear、FileBarrier仍走原双流/role规则。`start`拒绝时必须abort预构造的外部sink，Android worker在收到Start前不得打开文件/MediaStore，以免拒绝run产生副作用；接受后单sink故障仍只报普通Warn，不改变Flow或产品结果。范围限于本批已列出的LogSink/LogHub/Android sink/BackendRunSessionAdapter/AppContainer、对应unit/AndroidTest、backend README、§11.14 UML与本计划进度投影；GLLOG2/RunHost/Native/真实backend均不动。验证覆盖拒绝run零I/O与abort、active barrier、慢MediaStore、>10 MiB parity、双皮肤及App/Shizuku AVD。此裁定取代LS-I2的**构造器factory**形态，但保留其**统一binding列表分发**结果。

- 原已完成步骤、批准与运行证据：见 Beads `gl-s7q` notes 的迁移前记录；本设计不维护进度checkbox。

**2026-11-06 用户对 LS 准出更正**：`verifyDebugNativeArtifacts`、`verifyReleaseNativeArtifacts` 被裁定为不必要检查，直接删除 Gradle task/class、CMake target manifest 生成及 AGP 参数，不以空 manifest 或跳过标志伪造 PASS；`nativeBuildAndroid` 改为实际构建 debug/release 两个 variant 并保留现有 JNA distribution gate。历史 `NATIVE_ARTIFACTS=PASS` 与 manifest 缺失失败均保留为发生过的证据，不再作为当前 LS-V1/V2/V3 的待修 producer 工作。LS 准出继续要求已有 debug/release 104项 unit XML、native host 30项、architecture、>10 MiB 私有文件与 MediaStore parity、双皮肤 UI 以及 App/Shizuku Diagnose AVD 证据；本次 build-logic 变更后重新执行双 variant 构建与适用 source gate，不把 assemble 叫作 ELF/ABI/JNI 精确核验。范围仅 app/root Gradle KTS、app CMake manifest 尾段、AGENTS 当前普通门、§11.8 UML、introduction 当前指针及主/剩余/roadmap 进度投影；不修改 Diagnose JNA ABI、日志流或真实攻击 backend。

**LS-I3本轮证据（2026-11-06）**：`./gradlew :app:compileDebugKotlin :app:compileDebugAndroidTestKotlin --console=plain` exit=0；`./gradlew :app:testDebugUnitTest :app:testReleaseUnitTest :app:verifyDebugUnitTestXml :app:verifyReleaseUnitTestXml nativeTestHost --console=plain` exit=0，debug/release各`UNIT_TEST_XML=PASS FILES=34 TESTS=104 FAILURES=0 ERRORS=0 SKIPPED=0`、native=`NATIVE_TEST_HOST=PASS TOTAL=30 FAILED=0`；architecture verify exit=0、`D0_VERIFY=PASS ARCH_VIOLATIONS=0 ... UNCLASSIFIED=0`；debug/test APK构建与安装均exit=0。首次AVD storage测试是测试迁移遗漏MediaStore `Start`事件，literal=`FAILURES!!! Tests run: 7, Failures: 2`（`adb`自身exit=0）；补齐测试Start后重建、重装，最终storage/MediaStore/两皮肤UI exact class=`OK (7 tests)`，App和Shizuku Diagnose exact class分别=`OK (1 test)`，三者`adb` exit=0。新增测试核对预构造并abort的Android sink未创建私有目录、未调用MediaStore insert。`nativeBuildAndroid`的manifest producer缺口未修改，LS-V3普通门保持未关闭；本轮没有真攻击backend和root成功主张。

**统一sink更正的最终树验证（2026-11-06）**：clean后`./gradlew :app:assembleDebug :app:assembleDebugAndroidTest --console=plain`=`BUILD SUCCESSFUL in 7s`、exit=0；`./gradlew :app:testDebugUnitTest :app:testReleaseUnitTest :app:verifyDebugUnitTestXml :app:verifyReleaseUnitTestXml nativeTestHost --console=plain`=`NATIVE_TEST_HOST=PASS TOTAL=30 FAILED=0`且debug/release各`UNIT_TEST_XML=PASS FILES=34 TESTS=104 FAILURES=0 ERRORS=0 SKIPPED=0`、exit=0；architecture verify=`D0_VERIFY=PASS ... ARCH_VIOLATIONS=0 ... UNCLASSIFIED=0`、exit=0。AVD重新安装本次debug/test APK，storage/MediaStore/两皮肤UI exact class=`OK (6 tests)`，App/Shizuku Diagnose exact class各=`OK (1 test)`，三命令均exit=0。现有unit同时拒绝缺失/重复role，证明`UiClear`/`FileBarrier`不会泄漏至MediaStore。`nativeBuildAndroid`的两个manifest路径在本次clean+assemble后仍缺失；未动该build producer，原失败门继续开放。


历史审批/实际证据见 Beads [gl-s7q](design.md#record-gl-s7q) notes（原文保留，不作为实时状态副本）。

**历史普通门失败（已按用户裁定退役）**：`./gradlew nativeTestHost nativeBuildAndroid verifyDebugUnitTestXml verifyReleaseUnitTestXml --console=plain`及重试`./gradlew nativeBuildAndroid --console=plain`均exit≠0，Gradle `VerifyNativeArtifactsTask.manifestFile`输入`build/app/native-artifacts/{debug,release}/manifest.tsv`不存在。CMake的`file(GENERATE)`只在configure时产出该路径，而AGP的`app/.cxx` configure cache在`./gradlew clean`后仍在、`build/`下manifest已被清除；单独`externalNativeBuildDebug/Release --rerun-tasks`不会恢复该输入。此段仅保留当时失败事实；本次用户已删除这两个 verifier 与仅服务它们的 manifest，不再排队修 producer，也不把历史失败改写为 PASS。

**LS 最终收口证据（2026-11-06）**：旧任务在本轮 `./gradlew clean --console=plain` exit=0 后，`./gradlew nativeBuildAndroid --console=plain` exit=0、`JNA_DISTRIBUTION=PASS VERSION=5.19.1 ABI=arm64-v8a LICENSE=3 NOTICE=ABSENT`、`:app:assembleDebug`/`:app:assembleRelease`/`:nativeBuildAndroid` 均实际执行、`BUILD SUCCESSFUL in 31s`；构建任务图不再出现 `verifyDebugNativeArtifacts` 或 `verifyReleaseNativeArtifacts`。`./gradlew :app:testDebugUnitTest :app:testReleaseUnitTest :app:verifyDebugUnitTestXml :app:verifyReleaseUnitTestXml nativeTestHost :app:assembleDebugAndroidTest --console=plain` exit=0，debug/release 各 `UNIT_TEST_XML=PASS FILES=34 TESTS=104 FAILURES=0 ERRORS=0 SKIPPED=0`、`NATIVE_TEST_HOST=PASS TOTAL=30 FAILED=0`。architecture verifier exit=0、`D0_VERIFY=PASS ARCH_VIOLATIONS=0 ... UNCLASSIFIED=0`。本次 debug/test APK 重新安装到 `emulator-5556` 均为 `Success`；storage/MediaStore/两皮肤 UI exact class=`OK (7 tests)`，App 与 Shizuku Diagnose exact class各=`OK (1 test)`，三次 adb 命令 exit=0。新的 build gate 只证双版 APK 构建与 JNA distribution，不证被删除 verifier 的 ELF/ABI/JNI 精确集合；LS-V1/V2/V3 按用户修订准出关闭，真攻击 backend 和产品 root 仍未接通。

<a id="p-log-x-001-batch-03"></a>

##### P-LOG-X-001/批次03 LogConnector、动态 source 与全局顺序（已完成）

###### 取代与保留

本批只重构日志 ingress，不改已经完成的双流/sink 结果：仍只有一个应用 `LogHub`，raw 与 processed 使用 `Channel.UNLIMITED`，每个 sink 有独立 unlimited queue；`RunReport` 摘要仍只是普通 Info 行并服从统一阈值，typed report 仍是产品结果 authority。旧的 Flow/RunHost client 直接调用 `(LogRecord) -> Unit` 入口被统一 connector 取代。

###### 最小 connector 合同

```kotlin
sealed interface LogConnectorEnd {
    data object Drained : LogConnectorEnd
    data class Incomplete(val reason: String) : LogConnectorEnd
}

interface LogConnector : AutoCloseable {
    suspend fun collect(emit: suspend (LogDraft) -> Unit): LogConnectorEnd
}
```

- `LogDraft` 是尚未全局提交的来源记录；只有LogHub single processor创建 `LogRecord`、`commitId` 与run-relative `elapsedMs`。`LogConnectorEnd` 只报告日志source是否完整drain，不进入 `RunReport` 或产品成功。
- LogHub拥有独立run-scoped `CoroutineScope(SupervisorJob + background dispatcher)`。attach为connector启动一个collect job；**正常detach不先调用 `close()`**，而由producer owner先停止新写入并形成该source的完成条件，LogHub/forwarder等待collect返回 `Drained/Incomplete`，随后才调用 `close()` 释放reader/transport资源并移除job。`close()` 是终结释放，不是正常stop请求；只有run abort/coroutine cancel才允许取消collect并提前close，此时结果视为partial/incomplete。
- `KotlinLogConnector` 的producer/write-side owner是Flow或producer coroutine group；owner调用implementation-private `finishInput()` 关闭内部channel写端，collect排空channel后返回 `Drained`。producer throw/cancel先关闭写端并保存reason，collect排空已接受draft后返回 `Incomplete`。
- `FdLogConnector` 的write-side owner是native binding或shell child；正常顺序固定为stop producer→join thread/reap child→关闭write fd→reader读至EOF并处理完整尾行→collect返回→关闭read fd。reader不得为normal detach主动关闭read fd；EOF前peer/error、残缺尾行或read失败返回 `Incomplete`。RunHost forwarder可动态attach/detach多个FD connector，但它不是第二hub，只给每条转发记录分配source-private transport sequence。
- `RunHostLogConnector` 的远端producer owner是App/Shizuku RunHost，应用侧connector只读typed transport。RunHost必须在全部本地Kotlin/FD source drain后发送连续final sequence与terminal-delivery marker；应用collect仅在序列连续且marker到达时返回 `Drained`。typed terminal snapshot仍由callback/poller独立取得，但poller看到terminal不能立即关闭transport：client现有唯一completion deadline负责等待terminal snapshot与日志marker两者；deadline届满、gap/inversion/duplicate、Binder peer death或lifecycle关闭transport而无marker时，connector排空已接收prefix后返回 `Incomplete`。LogHub不另建第二deadline。
- LogHub不认识FD/PFD、AIDL、JNA、shell grammar或具体backend；connector/forwarder负责边界解析与origin/level校验。`Incomplete`或connector throw只提交一条普通Warn/Error draft并继续关闭该source，不改变攻击流程或产品成功。
- reader、raw、processed与sink queue都不设累计记录/字节上限；实际进程资源是唯一物理边界。每行仍服从现有单条wire/parcel/ASCII规则，不把“不限累计”解释成单条消息无限大。

###### 顺序 authority

1. 每个 connector 必须保留自身 source FIFO；同一 native FD、Kotlin channel 或 RunHost transport 内不重排。
2. RunHost transport sequence 只在 `RunHostLogConnector` 内检测 drop、duplicate、inversion 与 terminal delivery，不参与不同 source 的排序，也不成为 UI/file commit ID。
3. 多 connector 的全局顺序以 draft 被 raw ingress 接受的先后为唯一 authority；single processor 按该顺序分配连续 `commitId`。
4. source Unix timestamp、native clock、transport arrival timestamp只作为可选元数据展示/诊断；不回溯重排，不增加 watermark、reorder window 或跨时钟校准。
5. 结束run时，producer owner先停止新写入并关闭write side/发送terminal marker；connector collect先drain并返回end状态，随后owner关闭reader资源。single processor再提交剩余draft、任何Incomplete warning、普通Info `RunReport` 行与End；dispatcher把同一processed order交给所有sink。慢sink只积压自己的queue，不能拖慢connector、processor或其它sink。

###### 批次边界与验收

本批修改 `LogHub`、日志 source contract、App/Shizuku client/RunHost forwarder、Diagnose现有短日志 adapter、相应 unit/AVD fixtures、architecture guard 与唯一日志 UML；不加入 async stress producer、不改变 JNA ABI、不接回真攻击 backend。

验收必须证明：动态attach/detach前缀不丢不重；Kotlin与RunHost两source并发时各自FIFO、global `commitId`连续且只按raw acceptance；Unix时间倒退不导致重排；Kotlin channel partial buffer、FD read suspend/EOF/残缺尾行、transport gap/duplicate/inversion、Binder peer death、close与collect并发均得到唯一Drained/Incomplete结果；terminal snapshot早于callback marker时client仍等待marker，唯一completion deadline届满才把已接收prefix终结为Incomplete；单connector异常不终止其它connector；>10 MiB累计日志经UI/private/MediaStore启用sink保持同一committed order；正常结束必须stop writer→drain collect→close reader→report/End；Info阈值过滤report行时typed `RunReport`不变。

- 原已完成步骤、批准与运行证据：见 Beads `gl-s7q` notes 的迁移前记录；本设计不维护进度checkbox。


历史审批/实际证据见 Beads [gl-s7q](design.md#record-gl-s7q) notes（原文保留，不作为实时状态副本）。



#### 沿用的 Android MediaStore 边界（按 v3 执行）

以下迁自原 R4B/§15.3 中仍有效的平台行为，不重建旧拓扑，不增加验收门。来源：f882e670:docs/plan/backend-root-execution-unification-plan.md，R4B Current contract/runtime 与 §15.3；后继 v3/LogConnector（本文上段）和 typed DataStore（gl-zgp）覆盖旧实现细节。

1. **admission 与平台 I/O 隔离**：sink 创建/接入不得在 run admission、coordinator/LogHub 锁或 producer 路径内执行或等待 MediaStore insert/open/write/flush/close。被拒绝或 already-running 的启动不创建导出资源、不替换当前 run 日志 owner；正常 terminal/abort 与 active slot 释放不等待 provider。
2. **URI/stream 故障清理**：insert 失败没有 URI 可删；insert 成功、open 失败时清理同一孤儿 URI 一次。正常 End 在 open 阻塞期间已入队，open 最终成功后按 v3 FIFO 处理已接受前缀及 End，关闭 stream 一次。此正常 End 路径不覆盖 sink 已失败的情况：失败后仍按 v3 丢弃未消费积压、失败完成等待者。write/flush 后 close 再失败保留首个失败，不重试 close、不产生第二次导出失败通知。provider 已暴露字节只保证有序输出前缀，末行允许不完整；永久阻塞的真实边界仍见 gl-8dj，不伪造 bounded 终结。
3. **结果与诊断分离**：自动导出失败对该 sink 最多发一次 presentation warning，不修改 typed RunReport 或 root/Payload/Finish/cleanup/terminal/product-success 事实；旧 run 迟到通知不污染新 run。
4. **平台可见性/命名**：异常或进程死亡后已写前缀允许可见，不用 IS_PENDING 隐藏 partial；同秒不同 run 的 display name 按 runId/generation 区分。保留实际 text/plain 对应 .log.txt 命名；真实 MediaStore 验收只清理测试自己创建的唯一 URI。
5. **路径与选择器**：新安装默认启用自动导出，目录为 Download/ghostlock-log；当前值 false 不因归一化变 true。合法 Downloads-relative path 保持，普通相对目录补 Download/，重复 slash 归一；空串和 . / .. traversal 回默认。picker 接受 primary:Download 及合法子目录，拒绝 volume root、Pictures、dot traversal；拒绝时原选择不变。持久化只按 gl-zgp 的 typed DataStore，不恢复旧 SharedPreferences key 或迁移分支。

旧 capacity=512/256 events、QUEUE_FULL 固定容量、同一 coordinator lock 中 append/format/enqueue、全系统单 channel/worker、out-of-band finish、GLLOG1/report trailer、canonical document ByteArray、失败后继续 drain、固定 expectedTests/XML 统计和旧类名/fixture membership 均不随本段恢复为合同。历史 PASS/NOT_RUN 只绑定当轮输入；当前未关闭设备/最终验收仍由 gl-f47 承担。


<a id="acceptance-gl-s7q"></a>

### 验收定义与原边界：gl-s7q

<!-- migrated-source: acceptance_criteria gl-s7q -->
保留原文各行为判据、当轮证据和未关闭设备门。此decision收录完成不关闭任何关联业务任务。

沿用原自动导出验收样例：admission reject/already-running；insert/open/write/flush/close故障；正常End-before-open；write/flush后close再失败；旧token迟到；路径normalization/picker拒绝；真实MediaStore partial、唯一命名、成功输出byte identity及只删除自身URI。按现行v3/LogConnector解释生命周期/顺序，不恢复旧容量/工具细节。真实设备和最终产品验收仍是gl-f47的原未关闭门，NOT_RUN。

批准/审核/实际结果：Beads `gl-s7q` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-f47"></a>

## P-LOG-IO-001：automatic export 真机与最终产品验收

<!-- migrated-source: 043fdf6a Beads gl-f47 description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
UNFINISHED / NOT_RUN：保留既有source/AVD证据；真实设备automatic-export与最终验收按原判据，不把清理旧实现当能力删除。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-log-io-001

<!-- migrated-source: design gl-f47 -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-s7q](design.md#record-gl-s7q)；对应Beads只查执行/证据。

<a id="acceptance-gl-f47"></a>

### 验收定义与原边界：gl-f47

<!-- migrated-source: acceptance_criteria gl-f47 -->
UNFINISHED / NOT_RUN：保留既有source/AVD证据；真实设备automatic-export与最终验收按原判据，不把清理旧实现当能力删除。

批准/审核/实际结果：Beads `gl-f47` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-8dj"></a>

## MediaStore provider 调用不可中断

<!-- migrated-source: 043fdf6a Beads gl-8dj description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
观察到永久阻塞，或获批导出终结有时限需求时，设计可证明取消或进程隔离并补URI/FD设备证据；不用timeout/monitor伪造终结。 未触发，不扩大当前承诺。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#active-technical-debt

<!-- migrated-source: design gl-8dj -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-s7q](design.md#record-gl-s7q)；对应Beads只查执行/证据。

#### 原触发与偿还责任（主计划基线L2465）
| MediaStore provider调用不可中断 | `data/run` | 单worker可在insert/open永久阻塞；finish关闭channel但cleanup/callback只能等provider返回或进程死亡；不增timeout/monitor伪造完成 | 观察到provider永久阻塞，或产品批准“导出终结必须有时限”的新需求 | 独立Android平台设计；只接受可证明取消provider调用或隔离进程的方案，并补URI/FD设备证据 |

<a id="acceptance-gl-8dj"></a>

### 验收定义与原边界：gl-8dj

<!-- migrated-source: acceptance_criteria gl-8dj -->
观察到永久阻塞，或获批导出终结有时限需求时，设计可证明取消或进程隔离并补URI/FD设备证据；不用timeout/monitor伪造终结。 未触发，不扩大当前承诺。

批准/审核/实际结果：Beads `gl-8dj` notes；本段不维护实时勾选或新PASS。
