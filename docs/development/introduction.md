# 从代码可辨识的事实（introduction）

<a id="run-introduction"></a>

> **代号与快照范围**：当前条目引用[主计划语义代号](../plan/backend-root-execution-unification-plan.md#plan-semantic-identifiers)，不是统一 RUN 前缀下的旧 R 序号；下文原快照、历史批准与门禁保持原义，旧来源子号按映射定位。

> 当前快照：P-DIAG-NATIVE-001/批次01 I1→I3与V1已完成；4 KiB/16 KiB AVD的App/Shizuku debug与minified-release纵向全部PASS，2026-11-06。
> 唯一在办架构计划是 [P-RUN-X-001](../plan/backend-root-execution-unification-plan.md#p-run-x-001)；逐文件处置权威是[主计划 ledger](../plan/backend-root-execution-unification-ledger.tsv)；结构图唯一权威是 [UML 当前执行视图](full-process-uml.md#run-current-views)。
> 旧计划只从 docs/archive/ 读取，不再授权实现。
> 本批已执行的门只按§七逐项记录；Diagnose的AVD runtime matrix已齐，不把其外推为任何真攻击backend或真实设备证据。

## 零、当前产品边界

GhostLock 的产品成功不是 raw uid 0，而是 concrete backend 到达自己的 root execution point 后，派发公共层发布的同一份 root script，并由 typed result 证明用户 Payload 达成。Finish 是 Payload 之后的附加动作，backend cleanup 是独立层；三者不能互相冒充。

当前 registry 刻意没有 selectable production-root backend：

- CUSTOM 声明 PRODUCT_ROOT capability，但 selection policy 为 DISABLED，operation catalog 为空，readiness 为 NotReady。
- DIAGNOSE 在debug/release都注册，capability为DIAGNOSTIC_ONLY；即使native/JNA/shell纵向全部完成，产品成功投影仍为false。
- R5-S1曾写入neutral common product host spine，但后续用户已取消R5-S2/S3并要求断开全部真攻击backend。43499-Glued、43284-Glued、98260只保留source-only candidate；它们不在registry或APK native target graph，源码存在不等于selectable、supported或device proof。

## 一、Android/Kotlin 分层

| 层 | 当前 owner 与职责 | 禁止反向依赖 |
| --- | --- | --- |
| domain/backend | neutral BackendDescriptor：stable key、selection、abort、execution-domain、capability；availability facts | Android、Compose、resource ID、Flow factory、配置 schema、concrete backend、persistence |
| domain/run | FrozenRunInput、RunCapability、六层 RunReport、terminal 与 product-success projector | host implementation、日志推断 |
| domain/payload 与 domain/finish | 两份独立完整 data class、校验和用户语义 | backend root primitive、Android storage |
| data | 五个 main-process typed DataStore、selection/session adapter、Android run adapter、RunHost client/wire/host adapter、workspace、PFD staging、FD log connector与diagnostic report；旧 profile 迁移缝仅保留 `LegacyProfileConverter` 与 `ValueModel` | concrete backend membership、UI catalog、通用 HOCON/profile/route pipeline |
| backend/declarations 与 backend/backends | internal BackendRuntime/BackendRuntimeEntry、resource-free readiness、fresh one-shot Flow factory | UI、membership list、AIDL 暴露 |
| composition/backend | 每backend一个 BackendDefinition；唯一 immutable BackendRegistry 与 runtime/UI projection | mutable run state、第二份 key/membership 表 |
| core/runhost | platform-neutral RunHost protocol、request/snapshot model与one-shot state machine | Android、AIDL Parcelable、data、backend implementation、Compose |
| core/run | 一次冻结输入、RunAdmissionPolicy 与 application-lifetime LogHub | renderer/ViewModel lifetime ownership |
| core/flow | Flow.run 接受 BackendFlowInput 并返回 BackendRunFacts | product script generator |
| runhost 与 shizuku | `AppRunHostService` 与 `GhostlockUserService` 两个 framework entry；共用同一 AIDL 和 data/core owner | 第二份 membership、UI catalog、backend-private contract |
| UI/backend | BackendUi、BackendUiEntry、唯一 BackendUiCatalog、具名Android页面、多语言资源与项目图标 | data/backend/core implementation import、membership authority |
| UI其它层 | immutable UI state 与 action contract | data/backend/core implementation import |

`BackendDescriptor` 不持有 Flow、Context、资源、配置 schema、动态 readiness cache 或 persistence callback。只有 outer `BackendDefinition` 同时引用 descriptor、backend runtime 与 Android UI；每次 accepted run 必须由 `BackendRuntime` 创建新的 Flow。只有依赖系统内核偏移信息的backend参数由各自GLK profile v3表达；43284、Diagnose等非内核偏移型backend设置由各自具名typed authority表达，两类authority均禁止镜像双写。通用schema/renderer与Custom Lua→Compose映射均不存在。

当前 `data/` 只放置有现役 caller 的 Android adapter/repository，以及唯一旧 profile 迁移入口所需的 `LegacyProfileConverter`/`ValueModel`。从旧 43499 exec-glue 搬入、但只在自身内部互相引用的 HOCON loader、generic profile model/resolver/exporter、wire/route model、component placeholder 与 Lua document repository 已删除；内置 kernel profile 资产、extractor、冻结的 native backend source 和历史图不因本次 data 分层清理而改变。

## 二、Payload、Finish 与单一 root script

Payload 与 Finish 分别由自己的 repository 将完整 data class 直接写入 typed DataStore；内部没有 version wrapper、JCS 或 legacy codec。UI、Operational 与 AppSelection 使用另外三个独立 store。所有 store 只在 application 主进程构造，`:flowhost` 与 Shizuku host 只接收已经冻结的请求。

DataStore actor 是每个 aggregate 的唯一写顺序 owner。设置事件提交 actor-current transform；Payload preview 直接使用其完整保存事务返回的精确对象，并显式传入空 Finish。Run action 在回调边界向 Operational、Payload 与 Finish actor 各排入 cutoff snapshot，再只冻结这些 handle 返回的不可变对象；cutoff 后的编辑进入下一次 run。初始化先读取 UI、Operational、AppSelection，恢复 backend/executor 选择，再允许 `MainActivity.setContent`，因此首帧不使用临时默认主题。

RootScriptRenderer 是唯一整脚本 writer。发布过程固定为 host workspace 内写入、关闭、权限收口、同一 opened object 身份确认，再向 Flow 提供 published identity。backend 不生成 KernelSU、Payload 或 Finish 产品脚本，只负责 root primitive、exact artifact dispatch 与 attack-private cleanup facts。

结果由 typed root-result codec 与 RunReport 合成。产品成功只允许来自 PRODUCT_ROOT capability 与成功 terminal 的合法组合；raw uid 0、stdout、exit 0、callback event 或全局日志只能作为诊断事实。

## 三、typed RunHost 执行链

合同定义：[P-RUN-IPC-001：RunHost wire 与状态机](../plan/backend-root-execution-runhost-wire.md#p-run-ipc-001)；下列为本页原快照说明，不复制合同 authority。

1. UI 只提交 selected stable backend key。
2. AndroidRunSessionAdapter 从 captured descriptor projection 解析 key，并通过 runtime projection读取动态 availability；显示名称只来自BackendUiCatalog。
3. RunInputFreezer 一次冻结所有输入并创建 application-owned read-only source artifact。
4. AppRunHostClient 或 ShizukuRunHostClient 发送相同 typed request 与只读 PFD；request 不携带 capability。
5. RunHost 校验实际 Parcel 大小、结构、run identity、generation、semantic fingerprint、execution UID 与 SELinux domain。
6. `AppRunHostService` 或 `GhostlockUserService` 从 exact `BackendRuntimeEntry` 的 descriptor 投影 capability，dup PFD，stage 到 host-owned workspace，并由runtime创建fresh Flow。框架直接构造的两个host只允许调用outer registry的`runtime(key)`投影，不读取UI、definition或membership list。
7. Flow 返回 backend primitive、dispatch、cleanup 与 diagnostic facts；PRODUCT_ROOT carrier 还必须让公共脚本产生 Payload/Finish item facts。
8. host 发布 immutable terminal snapshot；callback 只作低延迟通知，client 同时轮询 getSnapshot 读取 durable authority。
9. client 先请求 requestClose；host 接受并进入 DESTROYED 后，App 才 unbind，Shizuku 才 remove UserService。

running cancel 只预留取消请求并保持 RUNNING。worker 必须先停止最后访问、完成 backend cleanup，再发布 CANCELLED terminal；这段窗口内 requestClose 固定拒绝。late callback 必须同时匹配 runId、generation、strict sequence 与 non-terminal state，否则丢弃。

App host workspace 位于 app-private no-backup root；Shizuku shell host 位于 shell-owned /data/local/tmp 固定 namespace。两者都拒绝 symlink、错误 owner 与未发布 executable identity。

## 四、native current tree

~~~text
app/src/main/cpp/
  CMakeLists.txt
  common/
  backends/CMakeLists.txt
  backends/diagnose/     current non-root shared library with one ordinary-C export
  backends/43499glued/   frozen future executable carrier candidate
  backends/43284glued/   frozen future executable/bootstrap candidate
  backends/98260/        frozen future candidate, default build disabled
tools/tests/
tools/avbcheck/
~~~

original 43499/43284 手写 JNI closure、glgeneral、generic access/native-handle/public victim 与旧 native log bridge 已退出 current graph。`backends/CMakeLists.txt` 仍不声明攻击backend option，只固定include非root的`diagnose` target；Gradle也不转发backend/M0开关、注册M0 task或打包攻击ELF。current CMake构建`gl_common` OBJECT target与`libghostlock_diagnose.so`，Android依赖固定为JNA 5.19.1 AAR。

DIAGNOSE的Flow在RunHost prepare阶段构造backend-private binding，binding无参构造器只执行`Native.load("ghostlock_diagnose", DiagnoseJnaLibrary::class.java)`。accepted start先把当前per-run workspace绑定为`GHOSTLOCK_HOME`，再进入`DiagnoseFlow : SequentialFlow`唯一的`diagnose-native-chain` physical step。该step只调用一次不带版本号的`gl_diagnose_run`：同一次native operation完成uid/pid/nonce round-trip、Native pipe日志、common固定位置handoff、Shell pipe日志与同步reap，并一次交付primitive和dispatch两项typed fact；Kotlin不持有shell process或script path，始终不执行Payload/Finish。

保留 candidate 内的攻击事务不得修改。当前没有首个真实carrier排期；重新接线必须由新的L级计划明确取代2026-11-05断开裁定。

## 五、日志与诊断

日志是 diagnostic sink，不是成功 authority。`domain/run`提供最小`LogConnector.collect(LogDraft)`、`Drained/Incomplete`、neutral `LogSink`与唯一`GLLOG2`逐行formatter。Kotlin producer、Native/Shell FD、App/Shizuku callback分别在core或transport边界适配，唯一application `LogHub`不认识PFD、AIDL、JNA或shell grammar；它为每次run建立独立connector scope，动态attach后先drain再close。current `LogHub`以raw与processed两个`Channel.UNLIMITED`、单processor及单dispatcher提交与分发，UI、private-file、可选MediaStore各有独立unlimited积压；source FIFO不重排，跨source唯一顺序是raw ingress接受顺序，source clock与transport sequence都不是全局排序键。`data/run`的文件与MediaStore worker各自独占stream，不逐行flush；App/Shizuku仍不各建exporter。

用户主动触发的 diagnostic report 仍由 AndroidDiagnosticReportBuilder 生成，并包含 typed run report。manual share、diagnostic log与automatic export复用同一逐行formatter及私有文件的已flush精确前缀；三者与 root-script result、产品成功判据彼此独立，export failure 不改变 RunReport 或 terminal。

## 六、source guard

tools/kotlin_architecture_d0.py 维护以下 source 规则：

- UI/domain/core 到 concrete backend 的禁止边；
- runtime reflection 禁止；
- backend 生成产品 root script 禁止；
- 旧 IFlowHost 与 IGhostlock UserService AIDL transaction 禁止；
- frozen candidate 之外的 manual JNI object marshalling 禁止；
- `RunCoordinator`、旧debugprobe key/type、公共`Flow.initialize`、第二日志hub、任意JNA path/reload、backend内`ProcessBuilder`与项目JNI export禁止；`Native.load`只允许出现在Diagnose binding的constant-name无参构造路径；
- 日志guard禁止第二个`LogConnector`合同、source-specific LogHub API、`(LogRecord)->Unit` direct ingress、累计有界Channel与按source clock排序；
- P-RUN-X-001/批次01/P-RUN-CORE-001/批次01 删除路径必须在 file-action manifest 中保持 ABSENT。

current architecture fixture已登记LogHub move、retired debugprobe与Diagnose/JNA current rows；门禁结果只按下一节的literal认定。

## 七、规范命令与本轮状态

~~~sh
./gradlew nativeTestHost --console=plain
./gradlew nativeBuildAndroid --console=plain
python3 tools/kotlin_architecture_d0.py verify --repo . --fixtures app/src/test/resources/architecture --section architecture
~~~

本批当前已观测：`nativeTestContractsHost`的Diagnose ABI 2/2 PASS；JNA distribution strict gate输出`JNA_DISTRIBUTION=PASS VERSION=5.19.1 ABI=arm64-v8a LICENSE=3 NOTICE=ABSENT`；`nativeBuildAndroid`同时输出debug/release `NATIVE_ARTIFACTS=PASS ... COUNT=2 ABI=arm64-v8a JNI=0 EXPORTS=1`；architecture输出`D0_VERIFY=PASS ... DIAGNOSE_NATIVE_BOUNDARY=0 UNCLASSIFIED=0`；debug/release unit XML实物均为34 files/102 tests/0 failure/error/skipped。`emulator-5556`（6.6.30、`PAGE_SIZE=4096`）与新建`GhostLock_6_6_16K`/`emulator-5558`（6.6.30、`PAGE_SIZE=16384`）的debug exact class gate都实际执行App `:flowhost`、direct JNA与Shizuku UserService三条纵向，各输出`OK (3 tests)`且`SKIPPED=0`；Shizuku运行在uid 2000/`u:r:shell:s0`。两个page-size AVD的minified-release production UI也已分别在App与Shizuku执行域显示各一次Native/Shell exact行、primitive/dispatch/cleanup三项SUCCESS、`DIAGNOSTIC_COMPLETE`与`Product success: NO`；logcat观测到Shizuku UserService加载`libjnidispatch.so`并输出project DSO round-trip行。这关闭了Diagnose V1，但只证明非root诊断纵向。

2026-11-06 用户裁定移除两个 app native artifact verifier 及其 CMake manifest 生产链；上段 `NATIVE_ARTIFACTS=PASS` 只保留为当时的历史证据。当前 `nativeBuildAndroid` 的语义为构建 debug/release 两个 variant 并执行 JNA distribution 检查，不再输出 ELF/ABI/JNI 精确核验结论。

P-RUN-HANDOFF-001/批次01完成后，Diagnose接口已收敛为与App同版发布的非版本化`gl_diagnose_run`；request/result不含version、size、reserved，不做运行时layout/header/size兼容检查，也不保留旧符号或结构分支。最终门为host contracts=`3/3`、native host=`31/0`、debug/release unit各`111/111`、architecture零违规、clean后双variant build/JNA distribution PASS；6.6 4 KiB与16 KiB AVD在刷新Shizuku server状态后，App/direct-JNA/Shizuku full exact class各输出`OK (3 tests)`。这些证据只覆盖非root diagnostic handoff纵向。

## 八、后续边界

- P-BACKEND-X-001/批次02：已按用户后续裁定关闭为real-backend disconnection，不再实施43499 carrier。
- P-DIAG-NATIVE-001/批次01：I1→I2→I3与V1已完成；4 KiB/16 KiB AVD的App/Shizuku debug与release runtime均已PASS，批次收口。
- P-BACKEND-NATIVE-001/批次01 / P-BACKEND-NATIVE-002/批次01：仍不自动排队；只有用户重新批准具名product-root backend后，才可另立L级计划引入carrier delta。
- extractor/KMP、profile v3 schema 与 LegacyProfileConverter 不由本计划顺手改动。
- current Class、Sequence、State 与 result projection 只维护在 docs/development/full-process-uml.md。
