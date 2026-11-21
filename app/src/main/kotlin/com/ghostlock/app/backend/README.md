# backend 目录、Diagnose 与单一 LogHub

> 唯一在办架构计划：docs/plan/backend-root-execution-unification-plan.md。
> 本文描述 current tree；历史 FlowHost、动态注册表、Glued Kotlin connector 与反射入口不再是实现权威。
> R4 已清除 caller-zero SessionPhase；automatic export 的删除结论已被 2026-11-05 用户纠正。R4B 保留产品能力并把 I/O 简化为 neutral sink + `data/run` single worker；R4C 再以 `LogHub` 收敛所有日志来源、排序、过滤与 sink fan-out，并把 `RunReport` 组合归还 Flow。

## 1. 当前布局与 owner

app/src/main/kotlin/com/ghostlock/app/
  domain/backend/       neutral immutable BackendDescriptor 与动态 availability facts
  domain/run/           FrozenRunInput、typed LogRecord、六层 RunReport 与产品成功 projector
  backend/declarations/ internal BackendRuntime + BackendRuntimeEntry，只暴露 readiness 与 fresh Flow
  composition/backend/  每个后端一个 BackendDefinition；immutable BackendRegistry 是 same-APK membership 唯一权威
  ui/backend/           BackendUi、BackendUiEntry、唯一 BackendUiCatalog 与具名 Android 页面
  backend/backends/
    diagnose/           release-visible DIAGNOSTIC_ONLY SequentialFlow + JNA binding
    lua/                受限 operation catalog frontend；当前 disabled
  core/flow/            统一 Flow.run(BackendFlowInput) -> RunReport；通过RootScriptResultReader读取产品结果；无反射
  core/log/             application-lifetime LogHub与Kotlin日志connector
  core/run/             run admission、actor cutoff、dispatch/sink port与RunSessionCoordinator
  core/runhost/         platform-neutral RunHost 合同、request/snapshot model、状态机与唯一 shared controller
  data/rootscript/      脚本/result物理发布、同对象identity与一次有界result读取
  data/log/             通用 FD 日志 connector
  data/run/             Android input freezer、presentation/sink adapter、host workspace与只读PFD staging
  data/runhost/         fixed-target dispatch、App/Shizuku client、AIDL codec/Binder endpoint与Android宿主准备adapter
  data/backend/         selection/session store adapter；不拥有 UI catalog 或后端 membership
  runhost/              仅保留 App `:flowhost` thin framework entrypoint
  shizuku/              仅保留同一IRunHost schema的thin UserService framework entrypoint

`BackendDescriptor` 只保存 stable key、selection、abort、execution-domain 与 capability，不保存 Flow、Android Context、资源、配置 schema、readiness cache 或 persistence callback。`BackendDefinition` 是唯一可以同时引用 descriptor、backend-owned runtime 与 Android-owned UI 的 outer composition root；inner caller 只取得 `BackendRuntimeEntry` 或 `BackendUiEntry` 投影。

## 2. 当前 membership

`composition/backend/BackendRegistry` 的唯一 definition list 顺序固定为：

1. CUSTOM：PRODUCT_ROOT capability，但 selectionPolicy=DISABLED，operation catalog 为空且 readiness=NotReady；
2. DIAGNOSE：debug/release 都注册，selectionPolicy=SELECTABLE，runCapability=DIAGNOSTIC_ONLY；它不执行 Payload/Finish。

R3 允许 selectable production backend 数量为 0。43499-Glued、43284-Glued 与 98260 的保留 native candidate 不进入 registry，也不进入APK CMake target graph；Gradle不存在可重新打开它们的backend property、M0 staging task或marker开关。重新接线必须由新的L级计划明确取代2026-11-05断开裁定，而不是把源码存在解释为PRODUCT_ROOT plugin。

Registry 没有 register、unregister、reset、digest、reflection symbol 或 compatibility wrapper。key lookup 是精确匹配；每次已接受 run 都由对应 `BackendRuntime.createFlow()` 创建新的 one-shot Flow。AppContainer 从同一 snapshot 取得 descriptor/UI 投影；只有框架直接构造的 `runhost/appuid/AppUidRunHostService` 与 `runhost/shizukushell/ShizukuShellRunHostService` 可以直接调用 `BackendRegistry.runtime(key)`，且它们不能读取 UI、definition 或 registry list。

## 3. 配置与选择权威

- BackendSessionStore 只持有当前 application process 的 selected stable key。
- 当前没有需要 Compose 配置的真实 backend；通用 schema、通用 renderer 与 unsupported mutation adapter 已删除。
- 只有需要系统内核偏移的 backend 才由自己的 GLK profile 文件表达低层参数；其它 backend 使用自己的具名 typed settings authority。本批不预建通用 repository、shared-reference 或树状编辑器，具体 backend 出现真实字段与 consumer 时再做纵向设计。
- `BackendUi` 只拥有普通 Android 多语言资源、项目 `IconPair` 与具名 Compose 信息/设置页面；它不把 UI 节点、资源 ID 或图标写入 domain/profile/wire。
- Custom 当前只显示 disabled placeholder；未来 custom 包固定走 ZIP + Lua + WebUI，而不是 Lua→Compose 映射。
- 旧 backend config store 已退出 current tree；Payload 与 Finish 由各自独立的完整 data class typed DataStore 持有，运行前只消费同一 actor 返回的 cutoff 值。
- release catalog 没有 selectable backend 时，Home 显示 Not Ready 及具名原因，Run gate 保持关闭。

## 4. 统一执行链

1. UI 只提交 selected backend key。
2. `RunSessionCoordinator` 从启动期 captured descriptor projection 解析key，读取动态readiness，并一次固定admission与dispatch共用的`AttackExecutorKind`；显示名称只由UI catalog提供。
3. coordinator同步捕获Operational、Payload与Finish actor cutoff；`AndroidRunHostDispatchAdapter`再调用`AndroidRunInputFreezer`固定输入、组装AIDL request并关闭master PFD，所得`RunDispatchLease`只拥有request duplicates。
4. LogHub接受前由`AndroidLogSinkFactory`创建未打开的storage sinks；拒绝或pre-commit失败会abort sinks并关闭lease。accepted commit完整可见后才启动lease，随后只调用已固定的App或Shizuku client。
5. RunHost 依次执行 actual Parcel budget、structural validation、state/identity/fingerprint preflight、exact runtime projection lookup、capability shape validation、实际 UID/SELinux execution-domain validation、availability probe、PFD duplication 与 host-owned staging。
6. 两个thin framework entry各自只做一次`BackendRegistry.runtime(key)`窄投影，并把六个business method委托给同一`RunHostBinderEndpoint`。endpoint独占actual Parcel budget、AIDL转换与callback wire；`AndroidRunHostPreparationAdapter`独占role/live context、domain/readiness、PFD/workspace/staging/publication与fresh Flow构造；platform-neutral `RunHostController`独占唯一lock、state machine、candidate、callback/snapshot、Flow worker、日志计数及quiescence barrier。candidate在锁外完整物化后，controller在同一临界区原子提交`lifecycle.prepare + prepared + snapshot`；load失败映射为prepare `IO_FAILURE`、删除workspace且状态保持NEW。accepted start先绑定`GHOSTLOCK_HOME`再运行Flow；prepare loser与未启动candidate不写该进程环境。
7. PRODUCT_ROOT由`ProductRunPreparer`调用pure plan mapper和`RootScriptArtifactPublisher`，再绑定一个`AndroidRootScriptResultReader`；Flow先取得唯一terminal claim，dispatch成功时才通过neutral `RootScriptResultReader`读取一次GLR1并组合`RunReport`。Diagnose不读产品result。
8. LogHub 持有物理 run；ViewModel observer 消失只 detach。显式 abort 通过 typed cancel 传播，不直接 kill host。
9. terminal callback 只作低延迟通知；client 同时轮询 durable typed snapshot，因此 callback 丢失不会丢 terminal authority。client 得到 terminal 后先请求 requestClose；只有 host 进入 DESTROYED，App 才 unbind、Shizuku 才 remove UserService。

App service 与 Shizuku UserService 共用 app/src/main/aidl/com/ghostlock/app/run 下的 IRunHost/IRunHostCallback 以及同一 request/snapshot stable integer schema。旧 IFlowHost、IShizukuShellRunHostService、Bundle 参数和 FlowSymbol 已退出 current tree。

## 5. 结果与日志边界

RunReport 是结果 authority。产品成功唯一条件为：

- runCapability=PRODUCT_ROOT；
- terminal=SUCCESS。

Diagnose 即使得到 diagnosticOutcome=PASS 且 terminal=DIAGNOSTIC_COMPLETE，isProductSuccess 仍固定为 false。raw uid 0、process exit 0、stdout、callback event、全局日志与单独 primitive success 都不能升级产品成功。

RunReport 是 Flow 返回的 typed terminal authority，但不是独立日志子系统。Kotlin producer、Native/Shell pipe 与 App/Shizuku callback 分别由 `KotlinLogConnector`、`FdLogConnector`、`RunHostLogConnector` 适配成 `LogDraft`；唯一 application `LogHub` 只认 `LogConnector`，以 raw ingress 接受顺序分配全局 `commitId`。source clock 和 RunHost sequence 只用于诊断/来源完整性，不参与跨来源排序。connector 先 drain，再关闭 reader，最后才提交普通 Info 的 RunReport 摘要与 End；`Incomplete` 只追加普通 Warn，不能改写 typed result。

## 6. 资源与生命周期

- application source artifact：`AndroidRunInputFreezer`创建，`AndroidFrozenRunInputLease`在request assembly成功或失败后关闭master并删除仅由它拥有的private source；
- request PFD：`RunDispatchLease`持有，RunHost prepare先dup；LogHub拒绝/pre-commit失败或accepted execute结束后幂等关闭；
- duplicated PFD：RunHost staging owner，成功或失败均关闭；
- staged artifact/workspace：RunHost owner；App host 使用 app-private no-backup 目录，Shizuku shell host 使用 shell-owned /data/local/tmp 固定 namespace；两者均拒绝 symlink/错误 owner，只在 terminal retention decision 或 accepted requestClose 后删除；
- Flow：一个 accepted run 独占一个实例，不跨 generation 重用；
- Diagnose binding：构造器只保存constant-name JNA mapping；Kotlin class与项目DSO随同一App发布，只映射不带version/size/reserved字段的`gl_diagnose_run`，不保留旧接口兼容或运行时layout/header检查。每次native call独占Native/Shell两组pipe，C++依次借用并dup对应write fd；返回后binding关闭最后write端，两组`FdLogConnector`无累计限额地流式drain到EOF，单条仍服从8 KiB wire边界；DSO按process lifetime驻留，不伪造unload handle；
- Diagnose handoff：Flow不持有shell process或自由script path。DSO在同一次`gl_diagnose_run`中调用common `run_handoff()`；helper只从`GHOSTLOCK_HOME/root-script/root-script.sh`定位并在same-opened fd验证marker，以`posix_spawn`执行固定shell并同步wait/reap。Shell stdout/stderr继承当前Shell日志fd，helper不建立第二套串行器或取消状态；
- running cancel：controller在线性化点预留取消并为safe `Flow.stop()`登记同一in-flight owner，状态保持RUNNING；worker停止最后访问、drain日志并完成cleanup后才发布CANCELLED terminal。terminal前requestClose固定拒绝；terminal后仍等待materializer/worker/safe-stop/workspace-delete同一epoch静止，framework destroy才获授权；
- callback：`RunHostLogConnector`只在相同runId+generation、source sequence连续且terminal marker一致时返回`Drained`；gap、duplicate、inversion、peer death或marker缺失返回`Incomplete`并保留已接收前缀。跨来源顺序只由LogHub raw ingress接受顺序决定；
- 当前无真实 process carrier 消费者；旧 caller-zero carrier 已删除，未来只能随首个真实 carrier 的具名后端设计重建；
- Shizuku UserService：每次 run 使用唯一 tag 与 process suffix，typed close gate 接受后才 remove。

## 7. 后续边界

- 当前没有真实 PRODUCT_ROOT backend接线批；若未来重新批准，必须由新的L级计划接线，不得把Diagnose、Lua空catalog或保留native tree直接改成supported。
- JNA 5.19.1、同版发布的直接C mapping、constant-name loader与 `libghostlock_diagnose.so` 只服务当前具名DIAGNOSE consumer；该接口不设版本协商、size探测或旧兼容层，禁止把它扩成动态DSO registry、自由path loader或攻击backend旁路。
- Payload/Finish script 的生成与结果解析属于 common root-script authority；backend 只提供 root primitive、dispatch 与 attack-private cleanup facts。
- docs/development/full-process-uml.md 保存 current architecture 唯一 Class、Sequence 与 State 图；本文件只解释 owner 与判据，不复制 wire 完整数值表。

## 8. R4 residue guard

- old IFlowHost、IFlowHostCallback、IGhostlockCallback 与 IShizukuShellRunHostService 不得重新出现在 source、manifest、ProGuard 或 build logic；
- frozen 43284-Glued、43499-Glued 与 98260 candidate 之外禁止 manual JNI object marshalling；当前具名DIAGNOSE只允许backend-private JNA ordinary-C mapping；
- Settings 的日志职责包括 level、terminal font、timestamp 以及 R4B 保留的 automatic-export flag/destination；UI、preference 与真实 sink 必须同批接通，不能保留无 caller 开关；
- automatic export由application `LogHub`统一提交：source经`LogConnector`动态attach/detach，raw与processed两个unlimited流、一个processor和一个非阻塞dispatcher；UI、private-file与可选MediaStore各自持有unlimited积压，文件worker缓冲写入。`LogSink`与`LogSinkBinding`位于`domain/run`；Android data adapter按run创建未打开的存储sink并直接传给`LogHub.start`，LogHub接受后才补入UI sink，拒绝时abort存储sink且不产生文件或MediaStore I/O。manual share与diagnostic复制私有文件的已flush精确前缀，不创建完整文档ByteArray。`RunReport`的九条摘要作为普通Info日志受全局阈值过滤，typed结果仍是产品成功authority；
- current architecture fixture继续如实记录旧 `log/LogExport.kt=DELETE/ABSENT/R4`，并登记`core/log`唯一LogHub、Android freezer/dispatch/presentation、physical root-script owner以及shared RunHost三组件；既有`runhost-owner-boundary`拒绝controller的Android/AIDL/data/composition反向边、entry业务owner回流、endpoint生命周期/workspace owner、adapter callback/snapshot owner、第二quiescence机制与非两处`runtime(key)`访问。inventory同步本身不构成App/Shizuku runtime PASS。
