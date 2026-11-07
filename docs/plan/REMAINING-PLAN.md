# 剩余计划（dev-nocpp · 2026-11-07）

> **代号范围**：当前项使用[主计划语义代号](backend-root-execution-unification-plan.md#plan-semantic-identifiers)；历史批准、纠错记录和 R5-S1 等来源子号保留兼容，不改变本页产品状态。

> 本文件只投影尚未完成的产品项、gate与已裁定的断开状态；完整语义、逐文件scope与判据以[backend-root-execution-unification-plan.md](backend-root-execution-unification-plan.md)为唯一authority。当前没有自动排队的真实backend child。

## 已完成本批：[P-RUN-HANDOFF-001/批次01：common 固定位置 handoff](backend-root-execution-unification-plan.md#p-run-handoff-001)

- [x] HND-D0/D1/D2：唯一active plan、固定位置/marker、same-opened fd、单次native chain、multi-fact owner与用户批准均已关闭；JNA同版发布接口按用户纠正删除version、size、reserved、layout/header检查及旧兼容分支。
- [x] HND-I1：common `run_handoff()`、product/diagnostic publisher、两端accepted-start `GHOSTLOCK_HOME`、Flow multi-fact、唯一`gl_diagnose_run`、双pipe binding、单step Diagnose、source guard、exact ledger与current UML已接通；真实攻击backend零改动且保持断开。
- [x] HND-V1：`nativeTestContractsHost`=`3/3`、Flow/Diagnose/root-script聚焦unit=`19/19`、debug/release exact unit各`111/111`、`nativeTestHost`=`31/0`、architecture=`ARCH_VIOLATIONS=0`、clean后`nativeBuildAndroid`双variant/JNA distribution PASS；6.6 4 KiB/16 KiB AVD的App/direct-JNA/Shizuku full exact class各为`OK (3 tests)`且无skip。

## 已完成本批：[P-LOG-X-001/批次02：全流式双流与独立 sink 积压](backend-root-execution-unification-plan.md#p-log-x-001-batch-02)

- [x] 用户选定raw/processed双流尽快处理、各sink自行积压、`RunReport`摘要只作为普通Info日志且服从全局阈值；未实施的v2.1 chunk/journal/viewport/final-copy candidate已失效。
- [x] v3 exact ledger=56行（含6个文件更名、9个额外caller/夹具）；单processor、单dispatcher、UI/private-file/可选MediaStore的独立unlimited sink queue、GLLOG2无隐藏typed report trailer与active share barrier已冻结。
- [x] v3 design-critic两轮关闭V3-01..03，最终阻断=0；UI采用append-only ArrayList+O(1)只读prefix snapshot，旧run/clear/barrier以同一FIFO control union终结。
- [x] LS-D4：用户明确批准v3实施并要求收敛非必要RunLog*命名；定点复评新增架构阻断=0，56行ledger已纳入更名与额外caller/夹具。
- [x] LS-I1：56行exact scope源码已完成，包含日志专用`RunLog*`更名、双流/独立sink、GLLOG2、完整UI快照与文件前缀分享；旧累计256/512、whole-document ByteArray及逐行flush不再属于current source。
- [x] LS-V1：debug/release unit各104/104、architecture、native host 30/0、>10 MiB Android端MediaStore逐字节一致与App/Shizuku 4 KiB AVD smoke均PASS；按用户修订，双 variant build/JNA distribution 取代已删除的 native artifact verifier 并PASS。
- [x] LS-I2：用户免评审直接授权的统一sink更正已落地；UI/private-file/MediaStore由factory列表创建并由binding列表按role分发，UML与caller已同步。
- [x] LS-V2：更正后104项双variant unit、architecture、native host与4 KiB AVD 6+1+1项PASS；修订后的`nativeBuildAndroid`双 variant build/JNA distribution 门PASS。
- [x] LS-I3：用户再次简化为预构造的存储sink直接传入`LogHub.start`，UI在接受后补入同一binding列表；拒绝run会abort且worker未做平台I/O。LS-I2的factory注入形态已被取代。
- [x] LS-V3：最终树debug/release各104项unit、native host 30项、architecture与4 KiB AVD storage/UI 7项及App/Shizuku Diagnose各1项PASS；修订后的`nativeBuildAndroid`双 variant build/JNA distribution 门PASS。原ELF/ABI/JNI verifier已删除而非PASS。

## 已由后继证据关闭：[P-LOG-X-001/批次01：单一日志中枢](backend-root-execution-unification-plan.md#p-log-x-001-batch-01)

- [x] `LogHub` 成为唯一日志中枢；Kotlin/Native/Shell/RunHost records 在同一 lock 内按 acceptance commit order 排序、统一 threshold、统一 fan-out。
- [x] `Flow.run` 在中立 terminal claim 后返回最终 `RunReport`；App/Shizuku RunHost service 只调用 Flow 与转运，不再各自组合 report。
- [x] event-as-log、独立 `RunReportRows` 与 legacy `app/log` singleton/file owner 已删除；Material3/Miuix 共用五级默认色与有界 ANSI foreground renderer。
- [x] current exact unit gate PASS：`UNIT_TEST_XML=PASS FILES=33 TESTS=96 FAILURES=0 ERRORS=0 SKIPPED=0`；architecture inventory PASS 且 `UNCLASSIFIED=0`。
- [x] current P-LOG-X-001/批次01 androidTest compile、native host与Android zero-target gates已按 current tree通过；clean 后 CMake 外部 manifest 需用 AGP build-model 参数重新 configure 的 producer dependency另记 build debt，不改变 gate 的最终 PASS 输出。
- [x] 2026-11-07账本收口：P-LOG-X-001/批次02 LS-V3已在current tree通过4 KiB AVD storage/UI 7项及App/Shizuku各1项；P-RUN-HANDOFF-001/批次01 HND-V1又在4 KiB/16 KiB AVD通过App/direct-JNA/Shizuku full exact class各3项。两组后继证据共同覆盖批次01的current LogHub wire/UI/runtime欠账；这只关闭旧批次证据债，不代表automatic export真实设备门或最终产品验收完成。

- [x] P-LOG-IO-001/批次02明确取代P-LOG-IO-001/批次01 runtime topology；P-LOG-IO-001/批次01恢复的settings、GLLOG1、canonical/manual-share与partial-file产品语义继续保留。
- [x] design-critic已关闭v0.3与P-LOG-IO-001/批次02全部阻断；single-channel/single-worker sink已删除opening、double queue、sequence/drainer、timeout与completion monitor。
- [x] domain contract、Android worker、LogHub、composition/caller、四组有效test source、architecture inventory与UML已迁移到P-LOG-IO-001/批次02；无真实双client路径的标签式parity test已按“严防过度设计”删除，本项只表示source交付。

### P-LOG-IO-001/批次01 被取代历史（不再作为 current runtime architecture）

- [x] 用户纠正 automatic export：它是未完成能力，不是删除项。
- [x] 在唯一活动计划、exact ledger 与 UML 中恢复产品责任，并保留 R4 已发生删除的沿革。
- [x] design-critic round 4 已关闭 run-scoped owner、逐行 flush、typed formatter、failure semantics、producer ordering 与 exact file rows；阻断/重要/建议均为0。
- [x] 用户以“批准R4A和R5”批准R4A具体结构；已同批接通 settings → LogHub → MediaStore exporter，没有恢复无caller UI。
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
- [x] 执行 P-RUN-IO-001/批次01 root-script fault matrix 与 ordinary-file identity/staging matrix；host=`31/31`、6.6 AVD=`3/3`、architecture=`D0_VERIFY=PASS ARCH_VIOLATIONS=0`，三个命令均exit 0；真实carrier设备门仍单独保持`NOT_RUN`。
- [x] P-RUN-X-001/批次01 Kotlin compile、debug/release exact unit各`111/111`、typed App/Shizuku正常transport/runtime已由P-RUN-HANDOFF-001/批次01 current-tree证据覆盖；4 KiB/16 KiB AVD的App/direct-JNA/Shizuku full exact class各为`OK (3 tests)`。
- [ ] 补齐P-RUN-X-001/批次01专属`RunHostLifecycleTest`、`RunHostCancellationTest`、`RunHostHostDeathTest` exact AVD证据；正常Diagnose/Handoff纵向不替代生命周期、取消与host-death判据。
- [x] 执行 P-RUN-CORE-001/批次01/P-LOG-IO-001/批次02 architecture source guard；exact command exit=0，literal=`D0_VERIFY=PASS ... ARCH_VIOLATIONS=0 ... OLD_AIDL_TRANSACTION=0 MANUAL_JNI_OBJECT_MARSHALLING=0 ... SUPERSEDED_RUN_LOG_TOPOLOGY=0 RUNHOST_CLIENT_LOG_SINK_OWNER=0 ... UNCLASSIFIED=0`。
- 证据纪律：失败保留为失败并回到debug；未执行项只记`NOT_RUN`，不用source implementation冒充PASS。本句是执行规则，不作为开发交付checkbox。
- [ ] 在 release 或未来重新批准的首个真实 carrier 变成 selectable 前关闭上述欠账；当前所有真攻击 backend 均已断开。

## Source implementation 已完成

1. E-DOC-X-001/批次01/P-BACKEND-X-001/批次01：authority 收口与 disabled/original backend closure 删除。
2. P-RUN-IO-001/批次01：Payload/Finish v3 repository、唯一 renderer、published artifact 与 typed result codec。
3. P-RUN-X-001/批次01：neutral registry/plugin/Flow、typed RunHost、App/Shizuku clients、workspace/staging、六层 RunReport 与 UI projection。
4. P-RUN-CORE-001/批次01 source已完成：caller-zero SessionPhase、source guard 与非导出 pointer；日志产品验收由后继P-LOG批次承接，不再把本批写成partial source。
5. P-LOG-IO-001/批次02：single-channel/single-worker automatic-export source与first-failure callback。
6. P-LOG-X-001/批次01：唯一日志中枢、Flow-owned RunReport、typed log wire、统一 ANSI-aware UI与current docs pointer；旧current-tree AVD欠账已由批次02与Handoff后继证据关闭。

automatic export 不在“已完成删除”清单；P-LOG-IO-001/批次02 负责唯一 I/O sink，P-LOG-X-001/批次01 负责其上游唯一日志中枢，当前产品状态仍为 UNFINISHED。

## 已完成：[P-DIAG-NATIVE-001/批次01：Diagnose native/JNA/shell纵向](backend-root-execution-unification-plan.md#p-diag-native-001-batch-01)

- [x] R6-I1：固定JNA 5.19.1与许可资产，加入真实`libghostlock_diagnose.so`、ordinary-C ABI、host contract test及debug/release双variant artifact verifier。
- [x] R6-I2：以release-visible `DIAGNOSE`替换`DEBUG-PROBE`，完成constructor-load candidate的原子PREPARED发布、`SequentialFlow`两step、Native/Shell日志与handoff shell全链路；6.6/4 KiB AVD上的App production adapter→LogHub纵向已输出`OK (1 test)`。
- [x] R6-I3：完成`LogHub`机械改名、current docs/UML与三份进度投影的唯一authority收口。
- [x] R6-V1：debug/release 34 files/102 tests、native host/Android均已PASS；6.6/4 KiB与6.6/16 KiB AVD的debug exact class gate都以`OK (3 tests)`/`SKIPPED=0`覆盖App/direct-JNA/Shizuku，两个page size下的App与Shizuku minified-release production UI也均已PASS。AVD Diagnose PASS只证明诊断纵向，不证明root或CVE。

P-DIAG-NATIVE-001/批次01是已完成的非攻击诊断批；P-BACKEND-NATIVE-001/批次01与P-BACKEND-NATIVE-002/批次01仍未排期，真攻击backend保持断开。

## 真攻击 backend 断开收口

- [x] R5-S2 native carrier与R5-S3 Disabled plugin实施已取消；旧 R5 carrier child（已终止）只保留沿革。
- [x] Kotlin registry中真实攻击backend数量为0。
- [x] Gradle不再接受backend/M0开关或运行43284 M0 source staging/build task。
- [x] app CMake backend assembly不再声明option或include 43499-Glued、43284-Glued、98260 chunk。
- [x] native artifact verifier把header-only manifest解释为断开权威，并要求merged native output实际为空；不再以“至少一个target”反向强迫接回攻击ELF。
- [x] 旧native tree保持source-only，不修改其攻击、handoff、waiter/race、restore或park实现。

P-BACKEND-NATIVE-001/批次01 / P-BACKEND-NATIVE-002/批次01不再是自动排队的future child。未来若用户重新批准任一真实攻击backend，必须新建L级计划并明确取代本次断开裁定；已完成的P-DIAG-NATIVE-001/批次01仅覆盖非root诊断链。
