# 剩余计划（dev-nocpp · 2026-11-05）

> **代号范围**：当前项使用[主计划语义代号](backend-root-execution-unification-plan.md#plan-semantic-identifiers)；历史批准、纠错记录和 R5-S1 等来源子号保留兼容，不改变本页产品状态。

> 本文件只投影尚未完成的产品项、gate与已裁定的断开状态；完整语义、逐文件scope与判据以[backend-root-execution-unification-plan.md](backend-root-execution-unification-plan.md)为唯一authority。当前没有自动排队的真实backend child。

## 未完成产品项：[P-LOG-X-001/批次01：单一日志中枢](backend-root-execution-unification-plan.md#p-log-x-001-batch-01)（source/host gates PASS，current AVD runtime pending）

- [x] `RunCoordinator` 成为唯一日志中枢；Kotlin/Native/Shell/RunHost records 在同一 lock 内按 acceptance commit order 排序、统一 threshold、统一 fan-out。
- [x] `Flow.run` 在中立 terminal claim 后返回最终 `RunReport`；App/Shizuku RunHost service 只调用 Flow 与转运，不再各自组合 report。
- [x] event-as-log、独立 `RunReportRows` 与 legacy `app/log` singleton/file owner 已删除；Material3/Miuix 共用五级默认色与有界 ANSI foreground renderer。
- [x] current exact unit gate PASS：`UNIT_TEST_XML=PASS FILES=33 TESTS=96 FAILURES=0 ERRORS=0 SKIPPED=0`；architecture inventory PASS 且 `UNCLASSIFIED=0`。
- [x] current P-LOG-X-001/批次01 androidTest compile、native host与Android zero-target gates已按 current tree通过；clean 后 CMake 外部 manifest 需用 AGP build-model 参数重新 configure 的 producer dependency另记 build debt，不改变 gate 的最终 PASS 输出。
- [ ] current P-LOG-X-001/批次01 AVD UI/runtime smoke仍须按 current tree取得证据后关闭；P-LOG-IO-001/批次02历史 AVD 与用户提供的旧 Shizuku 观察不代替本项。

- [x] P-LOG-IO-001/批次02明确取代P-LOG-IO-001/批次01 runtime topology；P-LOG-IO-001/批次01恢复的settings、GLLOG1、canonical/manual-share与partial-file产品语义继续保留。
- [x] design-critic已关闭v0.3与P-LOG-IO-001/批次02全部阻断；single-channel/single-worker sink已删除opening、double queue、sequence/drainer、timeout与completion monitor。
- [x] domain contract、Android worker、RunCoordinator、composition/caller、四组有效test source、architecture inventory与UML已迁移到P-LOG-IO-001/批次02；无真实双client路径的标签式parity test已按“严防过度设计”删除，本项只表示source交付。

### P-LOG-IO-001/批次01 被取代历史（不再作为 current runtime architecture）

- [x] 用户纠正 automatic export：它是未完成能力，不是删除项。
- [x] 在唯一活动计划、exact ledger 与 UML 中恢复产品责任，并保留 R4 已发生删除的沿革。
- [x] design-critic round 4 已关闭 run-scoped owner、逐行 flush、typed formatter、failure semantics、producer ordering 与 exact file rows；阻断/重要/建议均为0。
- [x] 用户以“批准R4A和R5”批准R4A具体结构；已同批接通 settings → RunCoordinator → MediaStore exporter，没有恢复无caller UI。
- [x] manual share与automatic export已共用唯一GLLOG1 formatter；四组有效test source与architecture current fixture已登记，RunHost client的sink零ownership改由source guard锁定。该 checkbox 只表示 source 交付，不表示 automatic export 产品项完成。
- [x] R4A-S5 已关闭abort与backend同步返回竞争窗：active slot在`finally`结束前不释放，`NonCancellable`幂等终结保证`Aborted` trailer与single close；该回归先进入R4B历史93项基线，现继续包含在R4C current 96项unit exact PASS中。
- [x] R4A-S6 已关闭export worker退出与producer enqueue竞争窗：closed channel不再伪报overflow，worker failure不再留下可接收但无consumer的command channel；R4B sink exact instrumentation 7项已覆盖normal close、failure与late-write拒绝并PASS。
- [x] R4A-S7 的opening/claim topology已由R4B整体删除，current single-channel sink以blocked-open finish/drain测试取代；对应exact instrumentation已PASS，不保留旧duplicate-await判据。
- [x] R4A-S8曾把新增abort unit method投影为168→169；R4B source复评后的历史基线为32个test source、93个`@Test`。R4C 新增 wire-value、ANSI renderer 与 central threshold/origin 判据后，current tree为33个XML suite、96项 test，`expectedTests=96`且 exact gate重跑PASS。
- [x] R4A-S9 已移除无收益的本地BufferedOutputStream并修正failure文件合同：成功文件仍逐行完整，失败文件只保证canonical byte prefix，provider故障可留下不完整最后一行；partial-write exact instrumentation已PASS。
- [x] source architecture verifier已经两轮失败定位与定点debug后PASS；`RUNTIME_REFLECTION=0`、`UNCLASSIFIED=0`、`REMOVE_RESIDUE=0`、`KEEP_DRIFT=0`。
- [x] Gradle debug compile/unit XML exact gate已按 R4C current tree通过：`./gradlew clean --console=plain && ./gradlew :app:testDebugUnitTest :app:verifyDebugUnitTestXml --no-build-cache --rerun-tasks --console=plain`，exit=0，literal=`UNIT_TEST_XML=PASS FILES=33 TESTS=96 FAILURES=0 ERRORS=0 SKIPPED=0`。普通缓存命令曾从污染的Gradle资源缓存恢复带空格的duplicate packaged resources；clean + no-build-cache/rerun排除source问题并在fresh outputs通过。
- [x] native host=`29/0`、Android zero-target build=`COUNT=0 ABI=none JNI=0`、androidTest compile、sink=`7/0/0/0`、settings=`4/0/0/0`、App RunHost=`1/0/0/0`与AVD真实MediaStore automatic/manual bytes=`1/0/0/0`均已通过。
- [x] 用户提供Shizuku AVD production runtime证据：`uid=2000`、`domain=u:r:shell:s0\0`、Diagnostic PASS、primitive SUCCESS、script/payload/finish SKIPPED、cleanup SUCCESS、terminal DIAGNOSTIC_COMPLETE、product success NO；命令/exit未提供，因此只按用户观察的runtime evidence记录，不补造自动化结果。
- [ ] 真实设备门与最终产品验收仍为NOT_RUN。
- [ ] automatic export 通过上述 gates 并获最终验收后，才可从 UNFINISHED 改为完成；禁止以删除旧 `LogExport.kt` object 冒充能力完成。

## 随后：偿还 P-RUN-IO-001/批次01 至 P-LOG-X-001/批次01（原批次范围） evidence debt

- [x] R2-H1 Android发布纠错已在`GhostLock_6_6`完成：kernel 6.6.30，Payload Save成功，App未崩溃，published script/result mode与无temp残留符合合同；只关闭SELinux hard-link故障，不代表下项完整矩阵通过。
- [ ] 执行 P-RUN-IO-001/批次01 root-script fault matrix 与 ordinary-file identity/staging matrix。
- [ ] 执行 P-RUN-X-001/批次01 Kotlin compile、unit、typed App/Shizuku transport 与 AVD lifecycle matrix。
- [x] 执行 P-RUN-CORE-001/批次01/P-LOG-IO-001/批次02 architecture source guard；exact command exit=0，literal=`D0_VERIFY=PASS ... ARCH_VIOLATIONS=0 ... OLD_AIDL_TRANSACTION=0 MANUAL_JNI_OBJECT_MARSHALLING=0 ... SUPERSEDED_RUN_LOG_TOPOLOGY=0 RUNHOST_CLIENT_LOG_SINK_OWNER=0 ... UNCLASSIFIED=0`。
- [ ] 把失败保留为失败并回到 debug；未执行项只记 NOT_RUN，不用 source implementation 冒充 PASS。
- [ ] 在 release 或未来重新批准的首个真实 carrier 变成 selectable 前关闭上述欠账；当前所有真攻击 backend 均已断开。

## Source implementation 已完成

1. E-DOC-X-001/批次01/P-BACKEND-X-001/批次01：authority 收口与 disabled/original backend closure 删除。
2. P-RUN-IO-001/批次01：Payload/Finish v3 repository、唯一 renderer、published artifact 与 typed result codec。
3. P-RUN-X-001/批次01：neutral registry/plugin/Flow、typed RunHost、App/Shizuku clients、workspace/staging、六层 RunReport 与 UI projection。
4. P-RUN-CORE-001/批次01 已完成部分：caller-zero SessionPhase、source guard 与非导出 pointer。
5. P-LOG-IO-001/批次02：single-channel/single-worker automatic-export source与first-failure callback。
6. P-LOG-X-001/批次01：唯一日志中枢、Flow-owned RunReport、typed log wire、统一 ANSI-aware UI与current docs pointer。

automatic export 不在“已完成删除”清单；P-LOG-IO-001/批次02 负责唯一 I/O sink，P-LOG-X-001/批次01 负责其上游唯一日志中枢，当前产品状态仍为 UNFINISHED。

## 真攻击 backend 断开收口

- [x] R5-S2 native carrier与R5-S3 Disabled plugin实施已取消；旧 R5 carrier child（已终止）只保留沿革。
- [x] Kotlin registry中真实攻击backend数量为0。
- [x] Gradle不再接受backend/M0开关或运行43284 M0 source staging/build task。
- [x] app CMake backend assembly不再声明option或include 43499-Glued、43284-Glued、98260 chunk。
- [x] native artifact verifier把header-only manifest解释为断开权威，并要求merged native output实际为空；不再以“至少一个target”反向强迫接回攻击ELF。
- [x] 旧native tree保持source-only，不修改其攻击、handoff、waiter/race、restore或park实现。

P-DIAG-NATIVE-001/批次01 / P-BACKEND-NATIVE-001/批次01 / P-BACKEND-NATIVE-002/批次01不再是自动排队的future child。未来若用户重新批准任一真实backend，必须新建L级计划并明确取代本次断开裁定。
