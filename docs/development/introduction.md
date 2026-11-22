# 从代码可辨识的事实（introduction）

<a id="run-introduction"></a>

> **代号与快照范围**：当前条目引用主计划语义代号（[gl-4tt设计](../../openspec/changes/runtime-foundations/design.md#record-gl-4tt)，`plan-semantic-identifiers`），不是统一 RUN 前缀下的旧 R 序号；下文原快照、历史批准与门禁保持原义，旧来源子号按映射定位。

> 当前快照：P-RUN-X-001/批次03已于2026-11-17完成shared Host去重；`BXP3-B1` builtin Diagnose plugin source已经进入当前工作树，完整V1、AVD双域、process-death与具名ARM64真机门仍未关闭。P-DIAG-NATIVE-001/批次01的旧same-APK 4 KiB/16 KiB App/Shizuku纵向证据继续只作历史事实，不能外推为当前pluginized路径PASS。
> 有效设计在OpenSpec、原批准与进度在Beads；提交依赖先经过已完成的Allure报告门`gl-8s7`，再由runtime source baseline恢复`gl-dpd`补齐三个已批准但未形成Git前驱的完成态，之后由当前B1 exact scope/矩阵`gl-dek`消费，最后进入重叠构建闭环`gl-4on`；B1父设计仍为`gl-sfg`。旧统一ledger只保留历史，不再是当前逐文件范围authority；结构图见[当前状态全局UML](full-process-uml.md#global-current-view)。
> 旧计划只从 docs/archive/ 读取，不再授权实现。
> 本批已执行的门只按§七逐项记录；未运行的AVD、Shizuku和真机门保持`NOT_RUN`，不把旧Diagnose矩阵或当前host/build结果外推为pluginized runtime、真攻击backend或真实设备证据。

## 零、当前产品边界

GhostLock 的产品成功不是 raw uid 0，而是 concrete backend 到达自己的 root execution point 后，派发公共层发布的同一份 root script，并由 typed result 证明用户 Payload 达成。Finish 是 Payload 之后的附加动作，backend cleanup 是独立层；三者不能互相冒充。

当前catalog刻意没有selectable production-root backend：

- 唯一installed package是APK builtin、ARM64、单BACKEND component的DIAGNOSE；它在debug/release都投影为`DIAGNOSTIC_ONLY`，即使native/JNA/shell纵向完成，产品成功仍为false。
- CUSTOM不再以disabled placeholder占据membership；provider、external import、真实攻击backend与额外ABI均未实现。
- R5-S1曾写入neutral common product host spine，但后续用户已取消R5-S2/S3并要求断开全部真攻击backend。43499-Glued、43284-Glued、98260只保留source-only candidate；它们不在plugin package、catalog或APK active native graph，源码存在不等于selectable、supported或device proof。

## 一、Android/Kotlin 分层

| 层 | 当前 owner 与职责 | 禁止反向依赖 |
| --- | --- | --- |
| domain/backend | neutral BackendDescriptor：stable key、selection、abort、execution-domain、capability；availability facts | Android、Compose、resource ID、Flow factory、配置 schema、concrete backend、persistence |
| domain/run | FrozenRunInput、RunCapability、六层 RunReport、terminal 与 product-success projector | host implementation、日志推断 |
| domain/payload 与 domain/finish | 两份独立完整 data class、校验和用户语义 | backend root primitive、Android storage |
| data | 五个main-process typed DataStore、catalog object store/seeder/verifier/source、selection/session adapter、Android input/dispatch/presentation/sink adapter、physical root-script owner、RunHost client/Binder endpoint/preparation adapter、workspace、PFD staging、plugin loader、FD log connector与diagnostic report；旧profile迁移缝仅保留`LegacyProfileConverter`与`ValueModel` | UI membership、第二份host lifecycle、通用HOCON/profile/route pipeline |
| composition/plugin | application-scoped catalog bootstrap与唯一`INITIALIZING/READY/FAILED` StateFlow | Android path、plugin classload、第二份catalog |
| composition/backend | 每个READY generation只产生一次descriptor + exact `PluginObjectRef`投影 | runtime factory、UI membership、可变package handle |
| core/runhost | platform-neutral RunHost protocol、request/snapshot model、one-shot state machine与唯一shared controller | Android、AIDL Parcelable、data、composition、backend implementation、Compose |
| core/run | RunAdmissionPolicy、actor cutoff、dispatch/sink ports与application `RunSessionCoordinator` | Android、data、AIDL、PFD、File、renderer/ViewModel lifetime ownership |
| core/log | application-lifetime `LogHub`与Kotlin connector | Android、PFD、AIDL、source-specific transport |
| core/flow | Flow.run 接受 BackendFlowInput 并返回 BackendRunFacts | product script generator |
| runhost | `runhost/appuid/AppUidRunHostService`（package app UID/domain）与`runhost/shizukushell/ShizukuShellRunHostService`（shell UID/domain）两个thin framework entry；只委托同一endpoint/controller并在authorized destroy后物理退出 | lifecycle/candidate/callback/snapshot业务owner、catalog、UI、backend-private contract |
| UI/backend | `BackendPresentationRegistry`、唯一`BackendUiCatalog`、具名Android页面、多语言资源与项目图标 | data/backend/core implementation import、membership authority |
| UI其它层 | immutable UI state 与 action contract | data/backend/core implementation import |

`BackendDescriptor`不持有Flow、Context、资源、配置schema、插件class、PFD/path或persistence callback。authenticated manifest是descriptor事实来源；`BackendCatalogProjection`从同一catalog record同时产生descriptor与exact object ref，`BackendPresentationRegistry`只能为已存在key补Host UI。每次accepted run由RunHost loader创建新的plugin entry/run并包成generic `PluginBackendFlow`。只有依赖系统内核偏移信息的backend参数才由各自GLK profile v3表达；其它backend设置等待具名typed consumer。通用schema/renderer与Custom Lua→Compose映射均不存在。

当前 `data/` 只放置有现役 caller 的 Android adapter/repository，以及唯一旧 profile 迁移入口所需的 `LegacyProfileConverter`/`ValueModel`。从旧 43499 exec-glue 搬入、但只在自身内部互相引用的 HOCON loader、generic profile model/resolver/exporter、wire/route model、component placeholder 与 Lua document repository 已删除；内置 kernel profile 资产、extractor、冻结的 native backend source 和历史图不因本次 data 分层清理而改变。

## 二、Payload、Finish 与单一 root script

Payload 与 Finish 分别由自己的 repository 将完整 data class 直接写入 typed DataStore；内部没有 version wrapper、JCS 或 legacy codec。UI、Operational 与 AppSelection 使用另外三个独立 store。所有 store 只在 application 主进程构造，`:flowhost` 与 Shizuku host 只接收已经冻结的请求。

DataStore actor 是每个 aggregate 的唯一写顺序 owner。设置事件提交 actor-current transform；Payload preview 直接使用其完整保存事务返回的精确对象，并显式传入空 Finish。Run action 在回调边界向 Operational、Payload 与 Finish actor 各排入 cutoff snapshot，再只冻结这些 handle 返回的不可变对象；cutoff 后的编辑进入下一次 run。初始化先读取 UI、Operational、AppSelection，恢复 backend/executor 选择，再允许 `MainActivity.setContent`，因此首帧不使用临时默认主题。

`RootScriptRenderer`是唯一整脚本文本writer；`data/rootscript/RootScriptArtifactPublisher`独占workspace内的exclusive write、fsync、权限收口、atomic move与published identity。`ProductRunPreparer`把同一plan/artifact绑定为neutral `RootScriptResultReader`，Flow只在terminal claim后且dispatch成功时读一次。backend不生成KernelSU、Payload或Finish产品脚本，只负责root primitive、exact artifact dispatch与attack-private cleanup facts。

结果由 typed root-result codec 与 RunReport 合成。产品成功只允许来自 PRODUCT_ROOT capability 与成功 terminal 的合法组合；raw uid 0、stdout、exit 0、callback event 或全局日志只能作为诊断事实。

## 三、typed RunHost 执行链

合同定义：[P-RUN-IPC-001：RunHost wire 与状态机](../../openspec/specs/runhost-wire/spec.md#p-run-ipc-001)；下列为本页原快照说明，不复制合同 authority。

1. UI只提交selected stable backend key。
2. `RunSessionCoordinator`只读一次terminal READY `BackendCatalogView`，按key取得同一projection的descriptor与exact `PluginObjectRef`，再把privilege snapshot固定为admission与dispatch共同target；显示名称只来自Host presentation。
3. coordinator同步捕获Operational/Payload/Finish actor cutoff；`AndroidRunInputFreezer`按exact object ref打开read-only plugin archive master PFD，并与其它输入一起组装typed request lease。
4. coordinator取得未打开storage sinks后调用LogHub；拒绝/pre-commit失败会abort sinks并关闭request lease，accepted commit完整可见后才启动已固定的App或Shizuku client。request只携带archive PFD与semantic content root，不携带catalog、path或重复manifest identity。
5. thin entry把调用交给`RunHostBinderEndpoint`。endpoint独占实际Parcel预算、AIDL/model转换、callback wire，并在每个prepare出口关闭decode PFD；wire codec先验证archive为read-only ordinary file，再产生validated duplicate holder。
6. `AndroidRunHostPreparationAdapter`把ordinary PFD一次性转移给staging、把archive PFD一次性转移给loader；descriptor acquisition成功后才创建role-specific workspace。loader复制并关闭archive，重验ZIP、manifest、全部files hash、semantic root、DEX/ELF、API/SDK/ABI/domain/capability，再通过filtered parent和`DelegateLastClassLoader`构造plugin entry/run及generic `PluginBackendFlow`。
7. `PluginRunResourceOwner`在首次class lookup前标记process taint，登记唯一run handle并管理TCCL/idempotent close。完整candidate返回后，唯一controller在同一mutex临界区原子提交state、candidate与PREPARED snapshot；entry不读取catalog、APK asset、UI或backend-private contract。
8. plugin Flow返回backend primitive、dispatch、cleanup与diagnostic facts；PRODUCT_ROOT公共路径仍由pure plan mapper、physical publisher与bound result reader组合Payload/Finish item facts。
9. host发布immutable terminal snapshot；callback只作低延迟通知，client同时轮询`getSnapshot`读取durable authority。terminal或prepare rejection后，client先drain日志，再依次`requestClose → destroy → unbind`并等待business binder death；超时返回transport failure并quarantine旧binder，迟到death才解除。

running cancel 只预留取消请求并保持RUNNING，同时把safe `Flow.stop()`登记为同一quiescence epoch的物理owner。worker必须先停止最后访问、完成backend cleanup并drain connector，再发布CANCELLED terminal；terminal claim前requestClose固定拒绝，claim后close仍等待materializer/worker/safe-stop/workspace-delete全部静止。late callback必须同时匹配runId、generation、strict sequence与non-terminal state，否则丢弃。

App host workspace位于app-private no-backup root；Shizuku shell host位于shell-owned `/data/local/tmp`随机run目录。两者都拒绝symlink、错误owner与未发布identity；execution lease必须先结束，workspace才可按retention判据删除。App entry在authorized destroy后执行`killProcess`，Shizuku entry执行`System.exit(0)`；unbind或service stop都不是进程死亡证据。

## 四、native current tree

~~~text
plugin/packages/diagnose/
  src/main/kotlin/       plugin-owned entry、run、JNA binding与pipe collector
  src/main/cpp/          current non-root shared library with one ordinary-C export
  src/main/plugin/       manifest source与普通license entries
plugin/native-source/common/
  common/                package/host-contract test共用的source-only C authority
app/src/main/cpp/
  backends/43499glued/   frozen future executable carrier candidate
  backends/43284glued/   frozen future executable/bootstrap candidate
  backends/98260/        frozen future candidate, default build disabled
tools/tests/
tools/avbcheck/
~~~

original 43499/43284手写JNI closure、glgeneral、generic access/native-handle/public victim与旧native log bridge已退出current graph。`:app`没有`externalNativeBuild`、JNA runtime dependency或Diagnose DSO packaging；`:plugin:packages:diagnose`是唯一Diagnose Kotlin/JNA/native/package producer，普通APK `lib/`不包含`libghostlock_diagnose.so`或`libjnidispatch.so`。

RunHost loader从verified package按数字顺序装载DEX，并以package native search path解析plugin-private `libjnidispatch.so`与`libghostlock_diagnose.so`。`DiagnoseRun`在package内直接创建binding；binding设置JNA classloader选项后调用不带版本号的`gl_diagnose_run`。同一次native operation完成uid/pid/nonce round-trip、Native pipe日志、common固定位置handoff、Shell pipe日志与同步reap，并一次交付primitive和dispatch两项typed fact；Host不import JNA、不调用`System.load*`/`dlopen`、不持有mapping/pointer/native handle。

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
- `RunCoordinator`、旧debugprobe key/type、公共`Flow.initialize`、第二日志hub、Host JNA/native owner、static `BackendRegistry` runtime与App→Diagnose implementation edge禁止；
- shared plugin API不得依赖Kotlin/Android/JNA；plugin不得依赖Host implementation或携带shared API副本；catalog writer、presentation projection与RunHost loader保持唯一；
- 日志guard禁止第二个`LogConnector`合同、source-specific LogHub API、`(LogRecord)->Unit` direct ingress、累计有界Channel与按source clock排序；`LogHub`/`KotlinLogConnector`唯一owner固定在`core/log`；
- core边界guard禁止`core/**`引用Android、data、generated AIDL、`ParcelFileDescriptor`或`java.io.File`，并拒绝旧`AndroidRunSessionAdapter`与`FlowRequest`残留；
- RunHost owner guard冻结唯一shared controller/quiescence机制、两个thin entry、endpoint/PFD owner、preparation/loader与client teardown的职责分离，并拒绝catalog/UI/backend-private owner回流到framework entry；
- P-RUN-X-001/批次01/P-RUN-CORE-001/批次01 删除路径必须在 file-action manifest 中保持 ABSENT。

current architecture fixture已登记`core/log`、focused Android run adapters、physical root-script owners、plugin API/core/package、catalog/projection/loader、retired static registry与Host-owned Diagnose/JNA rows；门禁结果只按下一节的literal认定。

## 七、规范命令与本轮状态

~~~sh
./gradlew nativeTestHost --console=plain
./gradlew nativeBuildAndroid --console=plain
./gradlew :app:verifyDebugUnitTestXml :app:verifyReleaseUnitTestXml --console=plain
python3 tools/kotlin_architecture_d0.py verify --repo . --fixtures app/src/test/resources/architecture --section all
~~~

BXP3-B1当前source证据（2026-11-21，工作树未提交）：plugin API/core/Diagnose debug+release unit均通过；App debug/release unit各`232/232`且failed/broken/unknown/skipped全0；`nativeTestHost`为`31/31`；architecture为`D0_VERIFY=PASS`、`ARCH_VIOLATIONS=0`、`RUNHOST_OWNER_BOUNDARY=0`、`PLUGIN_OWNER_BOUNDARY=0`、`UNCLASSIFIED=0`；`.glplugin.zip` semantic root=`fbd50adff210976b1d6b182c2d1db73685bb4650a2f52f9193ec394bf42db1fb`，debug/release APK boundary均PASS；`nativeBuildAndroid`以及debug/release/App AndroidTest APK assembly均成功。AGP固定为9.1.0，但compileSdk 37.2高于该AGP已测试到的36.1，只认当前树实测，不宣称官方完整支持。AVD、Shizuku runtime、真实kernel PFD/Binder跨进程和具名ARM64真机仍为`NOT_RUN`；这些source/build结果不关闭BXP3-B1 V1/V2/V3。

历史P-DIAG-NATIVE-001批次曾观测：`nativeTestContractsHost`的Diagnose ABI 2/2 PASS；JNA distribution strict gate输出`JNA_DISTRIBUTION=PASS VERSION=5.19.1 ABI=arm64-v8a LICENSE=3 NOTICE=ABSENT`；`nativeBuildAndroid`同时输出debug/release `NATIVE_ARTIFACTS=PASS ... COUNT=2 ABI=arm64-v8a JNI=0 EXPORTS=1`；architecture输出`D0_VERIFY=PASS ... DIAGNOSE_NATIVE_BOUNDARY=0 UNCLASSIFIED=0`；debug/release unit XML实物均为34 files/102 tests/0 failure/error/skipped。`emulator-5556`（6.6.30、`PAGE_SIZE=4096`）与新建`GhostLock_6_6_16K`/`emulator-5558`（6.6.30、`PAGE_SIZE=16384`）的debug exact class gate都实际执行旧same-APK App `:flowhost`、direct JNA与Shizuku UserService三条纵向，各输出`OK (3 tests)`且`SKIPPED=0`。该证据只证明当时的非root诊断纵向，不能关闭当前pluginized V1。

2026-11-06及后续用户裁定分别移除两个旧App native artifact verifier、对应CMake manifest生产链与`verifyJnaDistribution`任务；上段`NATIVE_ARTIFACTS=PASS`和`JNA_DISTRIBUTION=PASS`只保留为当时的历史证据。当前`nativeBuildAndroid`构建唯一Diagnose package producer及debug/release App，并执行`.glplugin.zip`与两个APK owner boundary；它不恢复旧ELF/ABI/JNI统计或JNA distribution inventory结论。

P-RUN-CORE-001/批次02完成后，current tree已使用pure `RunSessionCoordinator`、fixed-target `RunDispatchLease`、Android data adapters和single-open `RootScriptResultReader`取代mixed session/root-script physical owner。source critic提出的accepted-before-body取消、descriptor/path metadata替换与sink provider降级覆盖三个阻断均已关闭，最终PASS且阻断/重要均为0。最终门为architecture=`PASS/CORE_OUTER_DEPENDENCY=0/RETIRED_RUN_OWNER=0`、debug/release unit各`155/155`且四桶全0、native host=`31/31`、双variant build通过、31个FREEZE文件相对`4996d25c` zero-diff；6.6 AVD `emulator-5554` exact matrix为`OK (23 tests)`且无skip，真实non-root设备`b4541958`的physical I/O为`OK (4 tests)`且无skip。该证据关闭本架构批，不接回真实攻击backend；真实carrier/PRODUCT_ROOT仍为`NOT_RUN`。

P-RUN-X-001/批次03完成后，App与Shizuku只保留framework差异，统一委托`RunHostBinderEndpoint → RunHostController / AndroidRunHostPreparationAdapter`。source critic的KTH-SRC-01/02/03全部关闭，最终PASS且阻断/重要/建议均为0。current门为architecture=`PASS/RUNHOST_OWNER_BOUNDARY=0/FILE_ACTION_ROWS=604`、debug/release unit各`200/200`且failure/skipped/unknown全0、native host=`31/31`、双variant build与androidTest compile通过、70个FREEZE零漂移；6.6 AVD同树APK运行App/Shizuku smoke及lifecycle/cancel/host-death五个exact class，返回`OK (5 tests)`、exit=0。真实process-kill、carrier、root与PRODUCT_ROOT仍为`NOT_RUN`。

P-RUN-HANDOFF-001/批次01完成后，Diagnose接口已收敛为与App同版发布的非版本化`gl_diagnose_run`；request/result不含version、size、reserved，不做运行时layout/header/size兼容检查，也不保留旧符号或结构分支。最终门为host contracts=`3/3`、native host=`31/0`、debug/release unit各`111/111`、architecture零违规、clean后双variant build/JNA distribution PASS；6.6 4 KiB与16 KiB AVD在刷新Shizuku server状态后，App/direct-JNA/Shizuku full exact class各输出`OK (3 tests)`。这些证据只覆盖非root diagnostic handoff纵向。

## 八、后续边界

- P-BACKEND-X-001/批次02：已按用户后续裁定关闭为real-backend disconnection，不再实施43499 carrier。
- P-DIAG-NATIVE-001/批次01：I1→I2→I3与V1已完成；4 KiB/16 KiB AVD的App/Shizuku debug与release runtime均已PASS，批次收口。
- P-BACKEND-X-002/BXP3-B1：builtin Diagnose plugin source与host/build门正在`gl-dek`收口；V1、AVD双域/process death与ARM64真机任务仍open/blocked，provider/import/Custom/额外ABI不在本批。
- P-BACKEND-NATIVE-001/批次01 / P-BACKEND-NATIVE-002/批次01：仍不自动排队；只有用户重新批准具名product-root backend后，才可另立L级计划引入carrier delta。
- extractor/KMP、profile v3 schema 与 LegacyProfileConverter 不由本计划顺手改动。
- current Class、Sequence、State 与 result projection 只维护在 docs/development/full-process-uml.md。
