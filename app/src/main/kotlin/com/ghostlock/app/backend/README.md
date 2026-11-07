# backend 目录、R6 Diagnose 与单一 LogHub

> 唯一在办架构计划：docs/plan/backend-root-execution-unification-plan.md。
> 本文描述 current tree；历史 FlowHost、动态注册表、Glued Kotlin connector 与反射入口不再是实现权威。
> R4 已清除 caller-zero SessionPhase；automatic export 的删除结论已被 2026-11-05 用户纠正。R4B 保留产品能力并把 I/O 简化为 neutral sink + `data/run` single worker；R4C 再以 `LogHub` 收敛所有日志来源、排序、过滤与 sink fan-out，并把 `RunReport` 组合归还 Flow。

## 1. 当前布局与 owner

app/src/main/kotlin/com/ghostlock/app/
  domain/backend/       neutral immutable BackendSpec、配置 schema、参数要求与动态 availability facts
  domain/run/           FrozenRunInput、typed LogRecord、六层 RunReport 与产品成功 projector
  backend/declarations/ internal BackendPlugin：spec + readinessProbe + flowFactory
  backend/registry/     immutable BackendRegistry；same-APK membership 唯一权威
  backend/transport/    typed IRunHost 客户端、状态机、App/Shizuku transport、通用 process carrier
  backend/backends/
    diagnose/           release-visible DIAGNOSTIC_ONLY SequentialFlow + JNA binding
    lua/                受限 operation catalog frontend；当前 disabled
  core/flow/            统一 Flow.run(BackendFlowInput) -> RunReport；拥有BackendRunFacts到最终report的组合；无反射
  core/run/             单次 input freeze 与application-lifetime LogHub单一日志中枢
  data/run/             host-owned workspace 与只读 PFD staging
  data/backend/         neutral catalog/selection/config/run adapters
  shizuku/              与 App host 相同 IRunHost schema 的 UserService 实现

BackendSpec 只保存 immutable declaration，不保存 Flow、Android Context、资源、readiness cache 或 persistence callback。BackendPlugin 只在 backend/registry/transport 内部可见；UI、domain、profile 和 AIDL 只看到 stable key 与 neutral DTO。

## 2. 当前 membership

BackendRegistry 的顺序固定为：

1. CUSTOM：PRODUCT_ROOT capability，但 selectionPolicy=DISABLED，operation catalog 为空且 readiness=NotReady；
2. DIAGNOSE：debug/release 都注册，selectionPolicy=SELECTABLE，runCapability=DIAGNOSTIC_ONLY；它不执行 Payload/Finish。

R3 允许 selectable production backend 数量为 0。43499-Glued、43284-Glued 与 98260 的保留 native candidate 不进入 registry，也不进入APK CMake target graph；Gradle不存在可重新打开它们的backend property、M0 staging task或marker开关。重新接线必须由新的L级计划明确取代2026-11-05断开裁定，而不是把源码存在解释为PRODUCT_ROOT plugin。

Registry 没有 register、unregister、reset、digest、reflection symbol 或 compatibility wrapper。key lookup 是精确匹配；每次已接受 run 都由 flowFactory 创建新的 one-shot Flow。

## 3. 配置与选择权威

- BackendSessionStore 只持有当前 application process 的 selected stable key。
- backend 配置的唯一声明 authority 是 BackendConfigurationSpec，持久化 authority 固定为 GLK profile v3。
- AndroidBackendConfigurationAdapter 当前只投影 static declaration 与 resource-free availability；在 profile-v3 editor port 接入前，mutation 明确返回 UNSUPPORTED。
- SharedPreferences backend config store 已退出 current tree；Payload 与 Finish 仍由各自 version-three canonical document authority 持有。
- release catalog 没有 selectable backend 时，Home 显示 Not Ready 及具名原因，Run gate 保持关闭。

## 4. 统一执行链

1. UI 只提交 selected backend key。
2. BackendRunSessionAdapter 重新解析同一 captured registry 与动态 readiness。
3. RunInputFreezer 一次冻结 backend key、profile-v3 snapshot、Payload 与 Finish，并发布 application-owned read-only source artifact。
4. AppRunHostClient 或 ShizukuRunHostClient 发送相同 typed RunHostRequest；request 不携带 capability。
5. RunHost 依次执行 actual Parcel budget、structural validation、state/identity/fingerprint preflight、exact plugin lookup、capability shape validation、实际 UID/SELinux execution-domain validation、availability probe、PFD duplication 与 host-owned staging。
6. RunHost 从命中的 immutable BackendSpec 投影 RunCapability，并在 prepare 中调用 flowFactory；DIAGNOSE 此时构造 `DiagnoseNativeBinding` 并只按常量名加载项目 DSO。两端都先在锁外完成Flow、PreparedRun与PREPARED snapshot候选，再在host lock内把`lifecycle.prepare()`和`prepared/snapshot`发布组成一个线性点；load失败直接映射为prepare `IO_FAILURE`、删除workspace且状态保持NEW，不伪造Flow report或日志。accepted start在同一个worker failure boundary内先把当前workspace root绑定为`GHOSTLOCK_HOME`，再调用Flow；prepare loser与未启动candidate不写该进程环境。
7. Flow 在中立 `FlowTerminalClaim` 取得唯一 terminal/cancellation 线性化结果后组合并返回 `RunReport`；RunHost 只把其映射为 immutable terminal snapshot。
8. LogHub 持有物理 run；ViewModel observer 消失只 detach。显式 abort 通过 typed cancel 传播，不直接 kill host。
9. terminal callback 只作低延迟通知；client 同时轮询 durable typed snapshot，因此 callback 丢失不会丢 terminal authority。client 得到 terminal 后先请求 requestClose；只有 host 进入 DESTROYED，App 才 unbind、Shizuku 才 remove UserService。

App service 与 Shizuku UserService 共用 app/src/main/aidl/com/ghostlock/app/run 下的 IRunHost/IRunHostCallback 以及同一 request/snapshot stable integer schema。旧 IFlowHost、IGhostlockUserService、Bundle 参数和 FlowSymbol 已退出 current tree。

## 5. 结果与日志边界

RunReport 是结果 authority。产品成功唯一条件为：

- runCapability=PRODUCT_ROOT；
- terminal=SUCCESS。

Diagnose 即使得到 diagnosticOutcome=PASS 且 terminal=DIAGNOSTIC_COMPLETE，isProductSuccess 仍固定为 false。raw uid 0、process exit 0、stdout、callback event、全局日志与单独 primitive success 都不能升级产品成功。

RunReport 是 Flow 返回的 typed terminal authority，但不是独立日志子系统。`LogHub` 在 terminal commit 内把 capability、diagnostic、primitive、dispatch、Payload、Finish、cleanup、terminal 与 product-success 派生为带 origin/level 前缀的普通 canonical records；Run 页面、手动分享、自动导出与诊断报告都消费同一 commit order。callback 只传 bounded `RunHostLogRecord(origin, level, message)`，日志文本不参与 report 或产品成功计算。

## 6. 资源与生命周期

- application source artifact：RunInputFreezer 创建并在 request 完成或 assembly 失败后释放；
- request PFD：application client 持有，RunHost prepare 先 dup；run adapter finally 关闭；
- duplicated PFD：RunHost staging owner，成功或失败均关闭；
- staged artifact/workspace：RunHost owner；App host 使用 app-private no-backup 目录，Shizuku shell host 使用 shell-owned /data/local/tmp 固定 namespace；两者均拒绝 symlink/错误 owner，只在 terminal retention decision 或 accepted requestClose 后删除；
- Flow：一个 accepted run 独占一个实例，不跨 generation 重用；
- Diagnose binding：构造器只保存constant-name JNA mapping；Kotlin class与项目DSO随同一App发布，只映射不带version/size/reserved字段的`gl_diagnose_run`，不保留旧接口兼容或运行时layout/header检查。每次native call独占Native/Shell两组pipe，C++依次借用并dup对应write fd；返回后binding关闭最后write端，再分别以769-byte cap drain到EOF；DSO按process lifetime驻留，不伪造unload handle；
- Diagnose handoff：Flow不持有shell process或自由script path。DSO在同一次`gl_diagnose_run`中调用common `run_handoff()`；helper只从`GHOSTLOCK_HOME/root-script/root-script.sh`定位并在same-opened fd验证marker，以`posix_spawn`执行固定shell并同步wait/reap。Shell stdout/stderr继承当前Shell日志fd，helper不建立第二套串行器或取消状态；
- running cancel：state machine 先预留取消但保持 RUNNING；worker 停止最后访问并完成 cleanup 后才发布 CANCELLED terminal，期间 requestClose 固定拒绝；
- callback：只在相同 runId+generation 且 source sequence 严格递增时接收；terminal 后 log、unknown origin/level、非法 ASCII/SGR 与超 budget record 均丢弃，不能改变当前 report；跨来源顺序只由 LogHub lock 内 canonical commit order决定；
- process carrier：只接受 host-owned directory 内与已发布 inode/digest identity 相符的 executable；stdout/stderr 由独立 collector 持续 drain，只保留 bounded diagnostic prefix；
- Shizuku UserService：每次 run 使用唯一 tag 与 process suffix，typed close gate 接受后才 remove。

## 7. 后续边界

- 当前没有真实 PRODUCT_ROOT backend接线批；若未来重新批准，必须由新的L级计划接线，不得把Diagnose、Lua空catalog或保留native tree直接改成supported。
- JNA 5.19.1、同版发布的直接C mapping、constant-name loader与 `libghostlock_diagnose.so` 只服务当前具名DIAGNOSE consumer；该接口不设版本协商、size探测或旧兼容层，禁止把它扩成动态DSO registry、自由path loader或攻击backend旁路。
- Payload/Finish script 的生成与结果解析属于 common root-script authority；backend 只提供 root primitive、dispatch 与 attack-private cleanup facts。
- docs/development/full-process-uml.md 保存 current architecture 唯一 Class、Sequence 与 State 图；本文件只解释 owner 与判据，不复制 wire 完整数值表。

## 8. R4 residue guard

- old IFlowHost、IFlowHostCallback、IGhostlockCallback 与 IGhostlockUserService 不得重新出现在 source、manifest、ProGuard 或 build logic；
- frozen 43284-Glued、43499-Glued 与 98260 candidate 之外禁止 manual JNI object marshalling；当前具名DIAGNOSE只允许backend-private JNA ordinary-C mapping；
- Settings 的日志职责包括 level、terminal font、timestamp 以及 R4B 保留的 automatic-export flag/destination；UI、preference 与真实 sink 必须同批接通，不能保留无 caller 开关；
- automatic export由application `LogHub`统一提交：raw与processed两个unlimited流、一个processor和一个非阻塞dispatcher；UI、private-file与可选MediaStore各自持有unlimited积压，文件worker缓冲写入。`LogSink`与`LogSinkBinding`位于`domain/run`；Android data adapter按run创建未打开的存储sink并直接传给`LogHub.start`，LogHub接受后才补入UI sink，拒绝时abort存储sink且不产生文件或MediaStore I/O。manual share与diagnostic复制私有文件的已flush精确前缀，不创建完整文档ByteArray。`RunReport`的九条摘要作为普通Info日志受全局阈值过滤，typed结果仍是产品成功authority；
- current architecture fixture继续如实记录旧 `log/LogExport.kt=DELETE/ABSENT/R4`，并登记LogHub move、retired debugprobe与Diagnose/JNA current rows；inventory同步本身不构成App/Shizuku runtime PASS。
