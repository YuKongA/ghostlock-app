# Diagnose与common handoff基线

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-yhz"></a>

## P-DIAG-NATIVE-001：已完成JNA诊断纵向

<!-- migrated-source: 043fdf6a Beads gl-yhz description -->
2026-11-21迁入的共享设计记录，不是新的执行任务。closed仅表示该记录收录完毕，不表示业务实现、新批准或门禁通过。
已完成基线，保留当轮原批准与门禁。旧同APK loader/Host native/Flow入口由B1具名delta替代；不恢复用户已取消的公共initialize/构造器critic复评。

<!-- migrated-source: design gl-yhz -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L1830-L2027 -->
<a id="p-diag-native-001"></a>

<a id="p-diag-native-001-batch-01"></a>

<a id="p-run-x-001-r6"></a>

##### P-DIAG-NATIVE-001/批次01 Diagnose：首个真实 JNA/shared-library 纵向（已完成）

> **当前接口纠正**：本节保留初版沿革；其中 `*_v1`、version、size、reserved、layout/header 运行时检查及 Flow 侧独立 shell step 已由 P-RUN-HANDOFF-001/批次01（Beads [gl-obq](design.md#record-gl-obq)，定位 `p-run-handoff-001`） 明确取代，不得恢复为兼容层或 current gate。当前唯一接口是与 App 同版发布的非版本化 `gl_diagnose_run`。

###### P-DIAG-NATIVE-001/批次01.1 用户裁定、取代关系与边界

2026-11-05，用户明确要求把现有 debug-only `DebugProbe` 拓宽为正式 `Diagnose` backend：debug 与 release 都可用；它必须包含实际 C++ 代码与实际 `.so`，并由同一条 Flow 实际贯通 native、JNA、shared-library load、Native/Shell 日志及 handoff shell。该裁定使本节成为 R6 唯一候选设计，并产生一个当前真实 JNA consumer；§9.7“不得为不存在的 consumer 建 placeholder DSO”继续有效，但不再阻止本 Diagnose 纵向。

本批仍保持 `RunCapability.DIAGNOSTIC_ONLY`。它不取得 root、不执行 Payload/Finish、不接回 43499-Glued/43284-Glued/98260，也不把 native 调用、shell exit 0 或 `DIAGNOSTIC_COMPLETE` 投影成产品成功。旧 `DEBUG-PROBE` key、debug-only registry 分支与只读 Kotlin snapshot fixture由单一 `DIAGNOSE` key/实现取代，不保留双 key 或兼容 alias。

2026-11-05 后续裁定固定日志中枢命名为`LogHub`、`DiagnoseFlow`继承`SequentialFlow`。用户在获知公共`Flow.initialize`会扩散到Flow/Lua/两个RunHost，而构造器加载会发生在prepare、早于start之后，进一步裁定采用代码约定：native-backed Flow在构造器内构造backend-private binding，由binding构造器加载native library；同时明确本决定不再提交design-critic复评。两个RunHost都在prepare事务中执行`plugin.createFlow()`，所以本方案有意接受“prepare即load”：load失败由prepare异常边界返回`RunHostCode.IO_FAILURE`，不会提交PREPARED、不会形成`RunReport`或Flow日志；prepare成功但从未start时，只有process-lifetime DSO mapping，不创建调用期fd/process/thread。2026-11-06 实施检查发现旧顺序会先提交PREPARED再构造Flow，故本批同步修正为：锁外完成Flow/PreparedRun候选，随后在host lock内执行`lifecycle.prepare()`并原子发布`prepared + snapshot`；这是已批准构造器加载合同的必要闭包，不是新增生命周期。

###### P-DIAG-NATIVE-001/批次01.2 直接方案、`LogHub` 与构造器加载边界

具名 native caller 只有 `DiagnoseFlow`。直接方案只有 `DiagnoseJnaLibrary` 与 `DiagnoseNativeBinding` 两个 backend-private Kotlin 类型：前者只描述 JNA ordinary-C mapping，不含 singleton、`INSTANCE`、owner 或日志；后者是 concrete owner，构造时按常量名执行 `Native.load("ghostlock_diagnose", DiagnoseJnaLibrary::class.java)`，接收本次 `RunLogEmitter`，管理本次 native call 的 pipe/fd 与严格解码。DSO/process lifetime 不伪装成可 unload 资源；`DiagnoseNativeBinding.close()` 只终结它实际拥有的调用期资源。

`Flow`公共接口保持`run(input)`与`stop()`，不增加initialize phase；`FlowLuaImpl`保持不动。`BackendDiagnose.flowFactory`调用`DiagnoseFlow()`，而`DiagnoseFlow`主构造器立即创建唯一`DiagnoseNativeBinding()`；binding构造器只执行常量名`Native.load("ghostlock_diagnose", DiagnoseJnaLibrary::class.java)`并保存immutable mapping，不创建fd/process/thread、不接收`RunLogEmitter`。JNA/link/load throwable直接离开Flow构造器，由两个RunHost prepare catch映射为`IO_FAILURE`并删除本次workspace。两个RunHost只改同一处提交顺序：先完整构造本地`candidate`，再在既有host lock内把`lifecycle.prepare()`与`prepared/snapshot`发布合成一个临界区；`start()`观察到PREPARED后取得的`prepared`因此必非空。

Native日志注入不依赖构造器参数：第一项primitive step调用`binding.runProbe(logs = input.logs, nonce = ...)`，由binding在该方法内创建pipe、解码exact Native record并投递`RunLogEmitter`。因此binding同时管理native调用期生命周期与日志source adapter，但prepare-time load本身没有Flow emitter，也不伪造一条load日志。为保持unit test不触发真实Android loader，binding只增加internal构造入口接收一个已映射的`DiagnoseJnaLibrary`；production无参构造器仍是唯一`Native.load` literal，Flow不接收library name/path/loader lambda。

`DiagnoseFlow : SequentialFlow` 只声明两项有物理意义的 step：`diagnose-native` 对应 `BACKEND_PRIMITIVE`，`diagnose-shell-handoff` 对应 `ROOT_SCRIPT_DISPATCH`；它覆盖 `cleanup()` 终结 binding/process，覆盖新签名`diagnosticOutcome(runs, cleanupFact)`从两项step与唯一cleanup fact得出PASS/FAIL/UNKNOWN，并覆盖`stop()`仅设置取消事实、销毁当前active shell process。`SequentialFlow.assembleFacts()`只计算一次cleanup fact并把同一个值同时交给hook和最终`BackendRunFacts`，禁止DiagnoseFlow缓存第二份cleanup状态。shell process仍归Flow，不塞进native binding。

应用侧把现有 `core/run/RunCoordinator.kt` 与类名机械改为 `core/run/LogHub.kt` / `LogHub`，并同步 caller/test名称；其已批准的 accepted-run slot、canonical document、threshold、单锁 total order、report-to-record projection 与 sink fan-out 责任不变，不拆出第二 coordinator/session/interface。`LogHub` 是唯一应用日志中枢；RunHost sequence只拒绝重复/逆序传输，pipe与shell decoder只是 source adapter，不排序、不持久化、不 fan-out。P-LOG-X-001/批次01 的既有历史记录继续保留当时名称，但 P-DIAG-NATIVE-001/批次01 current source、UML、KDoc 与 current pointers不得再把 `RunCoordinator` 写成日志中枢。

此前 §9.8 为尚不存在的 Shizuku JNA consumer预设了“PFD staging + boot path”分支。本批仅验证更直接的同 APK 常量名候选：JNA AAR 与 `libghostlock_diagnose.so` 均由 AGP 合入 APK；binding不接收 Context、profile、环境变量或用户路径。Android JNA预期从 APK native-library set解析 `libjnidispatch.so`，随后按常量名解析项目 DSO；这只是待 runtime gate证实的候选，不在 source gate前写成已证明事实。App `:flowhost` 与 Shizuku UserService 两个实际 Flow execution domain必须分别在 debug/minified release AVD证明两次加载与 round-trip。任一域失败即该域 Diagnose FAIL并停批回设计；禁止临时 fallback、PFD staging或扩 RunHost wire。

本批不增加公共 `NativePort` interface、opaque handle、JNA callback、reader/collector thread、第二 queue、executor、动态 DSO registry、任意 path loader、host publish lock或第二日志中枢。native与shell各只产出一条远小于pipe容量的受控记录，因此同一 Flow在生产者返回后顺序 drain 即可，没必要为当前 caller制造并发机制。

###### P-DIAG-NATIVE-001/批次01.3 唯一控制流与日志流

```mermaid
flowchart TD
  UI[Run UI selects DIAGNOSE] --> HUB[LogHub accepts one run and owns canonical log]
  HUB --> CLIENT[App or Shizuku RunHost client]
  CLIENT --> HOST[RunHost prepare calls plugin.createFlow]
  HOST --> FLOWCTOR[construct DiagnoseFlow]
  FLOWCTOR --> BIND[construct DiagnoseNativeBinding]
  BIND --> LOAD[Native.load constant ghostlock_diagnose]
  LOAD --> CANDIDATE[complete PreparedRun candidate]
  CANDIDATE --> COMMIT[host lock: lifecycle prepare plus prepared/snapshot publish]
  COMMIT --> PREPARED[RunHost prepared]
  PREPARED --> FLOW[accepted start calls DiagnoseFlow.run via SequentialFlow]
  FLOW --> KLOG[Kotlin milestone log]
  FLOW --> JNA[DiagnoseJnaLibrary mapping]
  JNA --> DISPATCH[libjnidispatch.so from the same APK]
  DISPATCH --> DSO[libghostlock_diagnose.so]
  DSO --> CPP[extern C wrapper to C++ implementation]
  CPP --> NPIPE[exactly one gl_log record to borrowed pipe fd]
  NPIPE --> NINGRESS[bounded drain and strict decode as origin Native]
  FLOW --> SH[/system/bin/sh diagnostic-noop.sh]
  SH --> SINGRESS[bounded merged-output drain as origin Shell]
  KLOG --> EMIT[input.logs]
  NINGRESS --> EMIT
  SINGRESS --> EMIT
  EMIT --> PUB[existing RunHost sequence and callback]
  PUB --> HUB
  FLOW --> REPORT[DIAGNOSTIC_ONLY RunReport]
  REPORT --> HOST
  HOST --> HUB
```

顺序固定为：prepare staging → `plugin.createFlow()` → `DiagnoseFlow()` → `DiagnoseNativeBinding()` → constant-name `Native.load` → 完整构造本地`PreparedRun candidate` → host lock内`lifecycle.prepare()`与`prepared/snapshot`原子发布 → prepared；accepted start后才进入`SequentialFlow.run(input)` → Kotlin start → JNA/DSO round-trip → 关闭所有native write端 → 同一Flow bounded drain到EOF → shell handoff → 关闭shell stdin → wait/reap → bounded drain merged output到EOF → cleanup → `input.complete(facts)`。constructor load失败时prepare直接返回`RunHostCode.IO_FAILURE`并删除workspace，lifecycle仍为NEW，run与日志链均不启动；prepare成功后第一项物理step失败才进入typed diagnostic facts。

Native 与 Shell 共用现有 `[V|D|I|W|E/<elapsed>ms] body` grammar，但本纵向把输入冻结得更窄：native输出必须且只能是 ASCII `"[I/<0..9223372036854775807>ms] diagnose:native:roundtrip-ok\n"`，shell输出必须且只能是 `"[I/0ms] diagnose:shell:handoff-ok\n"`。每个stream完整字节数≤768（含最终LF），正文1..511字节；只允许一个最终LF，禁止CR/NUL/第二行/尾随字节。decoder用769字节上限的byte buffer持续drain到EOF，禁止无界`readLine()`；missing、extra、partial、overflow、malformed均在drain完毕后映射为`UNKNOWN/MALFORMED_RESULT/NONE/0`。source adapter显式注入origin并丢弃source elapsed；RunHost拥有transport sequence，`LogHub`在单锁commit时拥有唯一canonical order/time。日志解析/投递失败不得升级任何执行fact或PRODUCT_ROOT语义。

###### P-DIAG-NATIVE-001/批次01.4 C ABI、JNA mapping 与资源所有权

唯一 exported operation 为：

```c
int32_t gl_diagnose_run_v1(
    const gl_diagnose_request_v1 *request,
    gl_diagnose_result_v1 *result
) noexcept;
```

函数返回值是唯一status authority；result中不再重复status。header使用`extern "C"`、固定宽度整数与显式version/size/reserved；C++ wrapper为`noexcept`，不让异常、STL对象或allocator ownership跨ABI。

| struct | 字段顺序 | arm64 contract |
|---|---|---|
| `gl_diagnose_request_v1` | `uint32_t abi_version; uint32_t struct_size; uint64_t nonce; int32_t log_fd; uint32_t reserved;` | size=24、align=8、offset=`0/4/8/16/20` |
| `gl_diagnose_result_v1` | `uint32_t abi_version; uint32_t struct_size; int32_t uid; int32_t pid; uint64_t echoed_nonce; uint32_t reserved0; uint32_t reserved1;` | size=32、align=8、offset=`0/4/8/12/16/24/28` |

status固定为：`0 OK`、`1 NULL_REQUEST`、`2 NULL_RESULT`、`3 REQUEST_VERSION`、`4 REQUEST_SIZE`、`5 REQUEST_RESERVED`、`6 RESULT_VERSION`、`7 RESULT_SIZE`、`8 RESULT_RESERVED`、`9 LOG_ATTACH`、`10 INTERNAL_EXCEPTION`；unknown positive/negative status均保留原始32-bit值进入`ExitKind.NATIVE_STATUS`。caller先把两个struct全量zero，写入version/size、nonce与fd，再显式调用JNA `write()`；native按null→request header/reserved→result header/reserved顺序验证，失败时不修改result payload。成功时才写uid/pid/echoed_nonce，所有出口都detach native duplicate fd；catch-all把异常变为`INTERNAL_EXCEPTION`且不跨ABI。Kotlin在返回后先取function `Int`，再调用result `read()`；function status与result payload不存在第二authority。

JNA `Structure` 只存在于 backend-private `jna` package并显式`@FieldOrder`、`ByReference`；mapping方法返回`Int`，不使用JNA callback、String/native-owned allocation或跨调用pointer。Flow只接收binding返回的Kotlin value。`nonce`按unsigned raw bits逐位回显，contract正例固定覆盖`Long.MIN_VALUE/-1/0/1/Long.MAX_VALUE`；`uid/pid`必须与同一RunHost进程的Kotlin观测相同，reserved必须为0。native status非0、version/size/layout/echo不一致均为typed diagnostic failure。

资源顺序固定如下：

1. `DiagnoseFlow()`在prepare内构造一个`DiagnoseNativeBinding()`；binding无参构造器只完成constant-name load与immutable mapping保存，不持有log emitter或调用期资源。load失败发生在任何PREPARED提交前，抛回RunHost prepare并映射`RunHostCode.IO_FAILURE`，workspace删除、lifecycle保持NEW且没有`backendPrimitive` fact；load成功后Flow进入完整本地candidate，只有host lock内`lifecycle.prepare()==OK`时才与`prepared/snapshot`一起发布。并发同identity loser得到`ALREADY_APPLIED`并删除自己的workspace；其binding只有process-lifetime mapping，没有调用期资源。mapping随本次one-shot Flow存活，DSO本体随process存活。
2. `DiagnoseNativeBinding.runProbe()`创建pipe并独占read/write两端；JNA调用只借用原write fd。C++用现有`gl_log_attach_fd()`复制fd，写exact一条短记录，并在所有出口用`gl_log_attach_fd(-1, ...)`关闭自己的duplicate；native不得在返回后保存pointer/fd或启动线程。
3. JNA返回后binding关闭原write端，同一调用协程以≤769字节buffer把read端drain到EOF并关闭；没有reader thread、join或第二queue。exact一条记录小于pipe容量是该顺序无死锁的显式前提。
4. `DiagnoseFlow`核对host-published diagnostic script identity后只启动`ProcessBuilder("/system/bin/sh", scriptPath).redirectErrorStream(true)`；立即关闭stdin。process一创建即写入atomic active reference，随后立刻复查取消，关闭“start成功但stop先看到null”的竞态。Flow独占process与merged stream，输出由受控脚本冻结为一条短记录，因此先wait/reap、再bounded drain不会填满pipe。正常等待deadline=`5,000 ms`；超时依次执行`destroy()`→等待`1,000 ms`→仍存活则`destroyForcibly()`→等待`1,000 ms`。只有观察到终结并完成`waitFor`后才CAS清空active reference；仍存活则保留给cleanup继续终结并把dispatch记为`FAILED/TIMEOUT/NONE/0`。`stop()`只destroy当前active process，Flow仍是唯一reap owner。
5. `cleanup()`以non-cancellable顺序关闭残留fd/stream；对仍active process执行`destroyForcibly()`并再等待`1,000 ms`，观察到终结并reap后才CAS清空；随后调用binding `close()`。JNA DSO按process lifetime驻留，不建unload/handle状态机。任何不能证明终结的owner只改变cleanup为`FAILED/CLEANUP_INCOMPLETE/NONE/0`，不清空仍live的owner引用、不伪造成功。

同步`Native.load`与native operation都不能被Kotlin coroutine强行中断。load发生于prepare、早于可取消的RUNNING状态，只做linker工作；用户取消不能阻止已开始的constructor load，这是本裁定接受的成本。operation不得loop、sleep、wait、spawn thread或保留borrow。accepted start后在native call前、call返回并完成fd drain后各检查一次取消；若取消发生在native call中，则等待这个有界call返回，drain/close后跳过shell并完成cleanup。shell阶段的取消由`stop()` destroy active process解除阻塞，之后仍由Flow按上述deadline reap。

###### P-DIAG-NATIVE-001/批次01.5 结果语义

所有表项均写全`state/reason/exitKind/exitValue`：

constructor load属于RunHost prepare，不属于下表的Flow facts：成功才产生prepared Flow；失败为`RunHostCode.IO_FAILURE`、workspace删除、run未开始、无`RunReport`且无Flow log。不得把prepare failure伪造成`backendPrimitive`。

| 事实 | PASS值 | 失败值 |
|---|---|---|
| `backendPrimitive` | 已prepare的binding完成C ABI、round-trip与exact native ingress：`SUCCESS/NONE/NONE/0` | call/pipe为`FAILED/IO_FAILURE/NONE/0`；layout/echo/header为`FAILED/INTERNAL_CONTRACT_FAILURE/NONE/0`；malformed record为`UNKNOWN/MALFORMED_RESULT/NONE/0`；function status非0为`FAILED/REJECTED/NATIVE_STATUS/<raw int32>` |
| `rootScriptDispatch` | 非root diagnostic shell退出0且exact Shell ingress完成：`SUCCESS/NONE/PROCESS_EXIT/0` | 未到达为`SKIPPED/NOT_STARTED/NONE/0`；process start为`FAILED/IO_FAILURE/NONE/0`；deadline耗尽为`FAILED/TIMEOUT/NONE/0`；非0为`FAILED/PROCESS_EXIT_NONZERO/PROCESS_EXIT/<exit>`；identity不匹配为`FAILED/ARTIFACT_IDENTITY_MISMATCH/NONE/0`；malformed record为`UNKNOWN/MALFORMED_RESULT/NONE/0` |
| `backendCleanup` | 所有fd/stream/process终结：`SUCCESS/NONE/NONE/0` | 无法证明终结为`FAILED/CLEANUP_INCOMPLETE/NONE/0` |
| `diagnosticOutcome` | 三层均成功时`PASS` | typed `FAILED`或cleanup failure为`FAIL`；任一stage为`UNKNOWN`、取消或终结不确定为`UNKNOWN` |
| terminal/product | `DIAGNOSTIC_COMPLETE` / `false` | `FAIL`仍为`DIAGNOSTIC_COMPLETE/false`；malformed/取消等`UNKNOWN`分别为`UNKNOWN/false`或`CANCELLED/false`；任何日志文本不得升级结果 |

取消/失败矩阵固定如下，不允许靠异常覆盖已取得fact：

| 时间点 | primitive | dispatch | cleanup | outcome / terminal |
|---|---|---|---|---|
| prepare/constructor load失败 | 无Flow fact | 无Flow fact | 无调用期资源；RunHost删除workspace | prepare=`IO_FAILURE`，无RunReport/terminal/log |
| prepare成功后、start前cancel | Flow不进入`run()`；binding已load但无调用期资源 | Flow不进入`run()` | host按既有PREPARED取消路径终结 | 既有RunHost cancelled snapshot；无Flow RunReport |
| native record malformed/overflow/missing/extra | `UNKNOWN/MALFORMED_RESULT/NONE/0` | `SKIPPED/NOT_STARTED/NONE/0` | 按真实cleanup | `UNKNOWN / UNKNOWN` |
| native call前cancel | `CANCELLED/CANCELLED/NONE/0`，不call | `CANCELLED/CANCELLED/NONE/0` | 按真实cleanup | `UNKNOWN / CANCELLED` |
| native call中cancel | call返回、drain、close后保留已完成的native success/failure；不启动shell | `CANCELLED/CANCELLED/NONE/0` | 按真实cleanup | `UNKNOWN / CANCELLED` |
| native成功后、shell前cancel | `SUCCESS/NONE/NONE/0` | `CANCELLED/CANCELLED/NONE/0` | 按真实cleanup | `UNKNOWN / CANCELLED` |
| shell record malformed/overflow/missing/extra | `SUCCESS/NONE/NONE/0` | `UNKNOWN/MALFORMED_RESULT/NONE/0` | 按真实cleanup | `UNKNOWN / UNKNOWN` |
| shell start/wait中cancel | `SUCCESS/NONE/NONE/0` | destroy/reap后`CANCELLED/CANCELLED/NONE/0` | 按真实cleanup | `UNKNOWN / CANCELLED` |
| shell已完成、terminal claim前cancel | 保留两个已完成stage fact | 保留两个已完成stage fact | 按真实cleanup | terminal claim以现有single authority裁定；cancel胜出则`UNKNOWN / CANCELLED` |
| cleanup失败 | 保留此前stage fact | 保留此前stage fact | `FAILED/CLEANUP_INCOMPLETE/NONE/0` | 非取消为`FAIL / DIAGNOSTIC_COMPLETE`；取消为`UNKNOWN / CANCELLED` |

###### P-DIAG-NATIVE-001/批次01.6 Exact source ledger

| action | exact files | 责任 |
|---|---|---|
| ADD | `app/src/main/cpp/backends/diagnose/CMakeLists.txt`; `app/src/main/cpp/backends/diagnose/api/diagnose_api.h`; `app/src/main/cpp/backends/diagnose/diagnose.cpp` | C++实现、ordinary C ABI、`gl_common`日志与单一shared target。 |
| MODIFY | `app/src/main/cpp/backends/CMakeLists.txt`; `app/src/main/cpp/CMakeLists.txt` | 始终包含唯一非攻击 Diagnose DSO；manifest同时服务debug/release且列出ordinary exports。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/backend/backends/debugprobe/BackendDebugProbe.kt`; `app/src/main/kotlin/com/ghostlock/app/backend/backends/debugprobe/DebugProbeFlow.kt` | 删除debug-only名称与Kotlin-only fixture，不保留alias。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/backend/backends/diagnose/BackendDiagnose.kt`; `app/src/main/kotlin/com/ghostlock/app/backend/backends/diagnose/DiagnoseFlow.kt`; `app/src/main/kotlin/com/ghostlock/app/backend/backends/diagnose/jna/DiagnoseJnaLibrary.kt`; `app/src/main/kotlin/com/ghostlock/app/backend/backends/diagnose/jna/DiagnoseNativeBinding.kt` | release可见spec、`SequentialFlow`两step、纯JNA mapping与constructor-load/lifecycle/log adapter。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/core/flow/SequentialFlow.kt` | `diagnosticOutcome(runs, cleanupFact)`消费唯一cleanup fact；公共Flow接口、Lua与两个RunHost不改。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/backend/registry/BackendRegistry.kt`; `app/src/main/kotlin/com/ghostlock/app/data/run/RootArtifactStagingAdapter.kt` | `DIAGNOSE`无build-type分支；diagnostic artifact只输出一条exact Shell record后退出。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostService.kt`; `app/src/main/kotlin/com/ghostlock/app/shizuku/GhostlockUserService.kt` | 两端先完成constructor-load candidate，再在host lock内原子提交lifecycle与`prepared/snapshot`；factory/load失败不得残留PREPARED。 |
| MOVE_REWRITE | `app/src/main/kotlin/com/ghostlock/app/core/run/RunCoordinator.kt` → `app/src/main/kotlin/com/ghostlock/app/core/run/LogHub.kt`; `app/src/test/kotlin/com/ghostlock/app/core/run/RunCoordinatorLogSinkTest.kt` → `app/src/test/kotlin/com/ghostlock/app/core/run/LogHubSinkTest.kt`; `app/src/androidTest/kotlin/com/ghostlock/app/data/run/RunCoordinatorMediaStoreIntegrationTest.kt` → `app/src/androidTest/kotlin/com/ghostlock/app/data/run/LogHubMediaStoreIntegrationTest.kt` | 机械命名收口，责任/锁/队列/ordering不变；无第二coordinator。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/AppContainer.kt`; `app/src/main/kotlin/com/ghostlock/app/data/backend/BackendRunSessionAdapter.kt`; `app/src/main/kotlin/com/ghostlock/app/domain/run/RunLogRecord.kt`; `app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostLogCursor.kt` | caller、字段、KDoc统一指向`LogHub`。 |
| MODIFY | `app/build.gradle.kts`; `app/proguard-rules.pro`; `build.gradle.kts` | pinned `net.java.dev.jna:jna:5.19.1@aar`、release keep、debug/release merged-native union verifier、release XML task与root native gate。 |
| ADD | `gradle/verification-metadata.xml`; `app/src/main/assets/third_party/jna/LICENSE`; `app/src/main/assets/third_party/jna/AL2.0`; `app/src/main/assets/third_party/jna/LGPL2.1`; `app/src/main/assets/third_party/jna/INVENTORY.txt` | strict dependency checksums；记录coordinate/upstream/Apache-2.0选择，并逐字打包JNA `5.19.1` source tag的LICENSE、AL2.0与LGPL2.1。Maven AAR实物只含classes、Android资源与各ABI `libjnidispatch.so`，不含许可正文或NOTICE；不创建空NOTICE。 |
| ADD/MODIFY native tests | `tools/tests/diagnose_abi_test.cpp`; `tools/tests/CMakeLists.txt`; `tools/tests/native-test-disposition.tsv` | host C ABI的status/layout/init/mutation/nonce矩阵进入`nativeTestHost`且disposition exact。 |
| ADD/MODIFY Kotlin tests | `app/src/test/kotlin/com/ghostlock/app/backend/backends/diagnose/DiagnoseFlowTest.kt`; `app/src/test/kotlin/com/ghostlock/app/backend/registry/BackendRegistryTest.kt`; `app/src/test/kotlin/com/ghostlock/app/backend/transport/{RunHostProtocolTest,RunHostRequestValidationTest,RunHostSharedWireTest,RunHostStateMachineTest}.kt`; `app/src/androidTest/kotlin/com/ghostlock/app/backend/transport/{RunHostTestFixtures,AppRunHostSmokeTest,ShizukuRunHostSmokeTest}.kt`; `app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/diagnose/DiagnoseJnaSmokeTest.kt`; `app/src/androidTest/kotlin/com/ghostlock/app/architecture/UiBaselineFixtures.kt` | constructor-load/prepare failure、prepare/start交错原子发布、两step/cancel/cleanup、registry、旧key零残留、C/JNA layout、App/Shizuku/runtime/release/log/handoff纵向判据。 |
| MODIFY docs/guards | `README.md`; `README_ZH.md`; `app/src/main/kotlin/com/ghostlock/app/backend/README.md`; `tools/kotlin_architecture_d0.py`; `app/src/test/resources/architecture/kotlin-file-actions-v1.tsv`; `docs/development/introduction.md`; `docs/development/full-process-uml.md`; `docs/plan/backend-root-execution-runhost-wire.md`; `docs/plan/backend-root-execution-unification-ledger.tsv`; `docs/plan/backend-root-execution-unification-plan.md`; `docs/plan/MASTER-PLAN.md`; `docs/plan/REMAINING-PLAN.md`; `docs/plan/ROADMAP.md`; `docs/plan/developer-defined-tasks.md` | current图/名称/membership改为LogHub+Diagnose并保持根README双语；开发者清单只允许在实际完成后切换既有三个Diagnose checkbox，不改文字/增条目。 |

发现新production caller或必须扩RunHost wire时先回到本节改ledger并重过critic；不得在实施中顺手加入library staging、公共native抽象或第二日志通路。

###### P-DIAG-NATIVE-001/批次01.7 验证矩阵

> **2026-11-08 current-tree取代说明**：下表的`verifyJnaDistribution`、旧native artifact verifier及其literal只记录P-DIAG-NATIVE-001/批次01当时的历史准出。开发者已裁定这些额外检查不必要并从current Gradle task graph删除；当前普通Android门仅为`nativeBuildAndroid`直接构建debug/release两variant，不再要求或声称JNA distribution、ELF、ABI、JNI精确核验。

| 层 | 命令/输入 | 必须literal证明 |
|---|---|---|
| dependency/license | 分三次执行：`./gradlew --dependency-verification=strict :app:dependencies --configuration debugRuntimeClasspath --console=plain`；`./gradlew --dependency-verification=strict :app:dependencies --configuration releaseRuntimeClasspath --console=plain`；`./gradlew --dependency-verification=strict :app:verifyJnaDistribution --console=plain` | 两classpath各自只解析`net.java.dev.jna:jna:5.19.1@aar`；metadata checksum匹配；POM声明`LGPL-2.1-or-later`/`Apache-2.0`双许可；AAR含arm64`libjnidispatch.so`且不含LICENSE/LGPL2.1/NOTICE正文；APK assets与JNA `5.19.1` source tag逐字包含LICENSE/AL2.0/LGPL2.1；literal=`JNA_DISTRIBUTION=PASS VERSION=5.19.1 ABI=arm64-v8a LICENSE=3 NOTICE=ABSENT`。 |
| Kotlin/unit/architecture | `./gradlew :app:testDebugUnitTest :app:testReleaseUnitTest :app:verifyDebugUnitTestXml :app:verifyReleaseUnitTestXml --console=plain`；`python3 tools/kotlin_architecture_d0.py verify --repo "$PWD" --fixtures app/src/test/resources/architecture` | XML四桶全量0 failure/error/skipped；`DEBUG-PROBE`、BuildConfig gate、`RunCoordinator` current symbol、JNI export、第二hub、自由path loader、公共`Flow.initialize`命中0；`Native.load`只命中`DiagnoseNativeBinding` production无参构造路径；literal含`D0_VERIFY=PASS ... UNCLASSIFIED=0`。 |
| Native host | `./gradlew nativeTestHost --console=plain` | `diagnose_abi_test`进入CTest/disposition；null/version/size/reserved/status/result-unchanged、size/align/offset与五个raw-bit nonce全绿；literal=`NATIVE_TEST_HOST=PASS ... FAILED=0`。 |
| Native debug/release | `./gradlew nativeBuildAndroid :app:assembleDebug :app:assembleRelease --console=plain` | root task实际依赖两个variant verifier；merged union精确含`libghostlock_diagnose.so`与AAR `libjnidispatch.so`；arm64 ELF；project DSO ordinary export exact且无`Java_*`/意外mangled；两DSO 16KiB alignment；literal分别含`NATIVE_ARTIFACTS=PASS VARIANT=debug COUNT=2`与`... VARIANT=release COUNT=2`。 |
| C/JNA contract | arm64 instrumentation以`Long.MIN_VALUE/-1/0/1/Long.MAX_VALUE` nonce及wrong null/version/size/reserved输入 | JNA/C size/align/offset exact；合法nonce raw bits、uid、pid exact；非法输入返回固定status、result payload不变、fd/pointer不保留；prepare构造Flow时load exact一次，`run()`不重复load。 |
| App `:flowhost` debug AVD | `./gradlew :app:assembleDebug :app:assembleDebugAndroidTest --console=plain`；`adb -s SERIAL install -r -t <debug.apk>`；`adb -s SERIAL install -r -t <androidTest.apk>`；`adb -s SERIAL shell am instrument -w -e class com.ghostlock.app.backend.backends.diagnose.DiagnoseJnaSmokeTest#appFlowHostRoundTrip com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner` | `Tests run: 1, Failures: 0`、instrumentation exit=0；Kotlin/Native/Shell三prefix进入同一LogHub；native/shell exact行各1；primitive/dispatch/cleanup=`SUCCESS`；terminal=`DIAGNOSTIC_COMPLETE`；product=false。 |
| Shizuku debug AVD | 同一debug/test APK执行`adb -s SERIAL shell am instrument -w -e class com.ghostlock.app.backend.backends.diagnose.DiagnoseJnaSmokeTest#shizukuRoundTrip com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner` | `Tests run: 1, Failures: 0`、instrumentation exit=0；uid=2000/shell domain；constant-name候选在真实UserService成功；两DSO实际参与round-trip；无sequence inversion/drop；结果非产品root。 |
| App/Shizuku minified release AVD | `./gradlew :app:assembleRelease --console=plain`；`adb -s SERIAL install -r -t <release.apk>`；从production UI分别选择App与Shizuku执行域运行唯一`DIAGNOSE`并导出同一LogHub文档 | install=`Success`；release UI中DIAGNOSE可选；两域各出现一次Native/Shell exact行与完整typed report；project/JNA mapping未被P-BACKEND-NATIVE-002/批次01裁剪；product success=`NO`。记录`getconf PAGE_SIZE=16384`、variant、execution domain、`libjnidispatch`角色与project DSO load/round-trip证据；任一缺失为NOT PASS。 |
| failure/cancel | 两个RunHost均在Flow factory/load后才进入host-lock commit；source guard与prepare/start交错测试锁定顺序；binding internal mapped-library构造只服务host unit；decoder/process seam注入malformed/overflow native/shell bytes、native status非0、shell exit非0、各RUNNING取消时间点与cleanup failure | constructor load throwable→`IO_FAILURE`、不提交PREPARED、不进入RunReport/log、workspace删除且后续start不能进入RUNNING；成功commit后任何start观察到的`prepared`非空；其余exact映射到§P-DIAG-NATIVE-001/批次01.5四元组；drain到EOF后判错；无live fd/process/thread；无fallback/重试/第二路径；日志故障不创建产品success。production loader不增加missing/wrong DSO runtime seam或自由library name。 |

AVD只证明Diagnose机制与release包装，不证明任何CVE、root primitive或目标设备supported。真实攻击backend仍须自己的冷机真机门。

2026-11-06首次minified-release production UI运行得到`RunHost rejected: IO_FAILURE`；同次logcat只观测到`libjnidispatch.so`加载，未观测到project DSO round-trip。根因是本批最初只keep `com.sun.jna.Native`与项目mapping，R8仍可改写JNA的reflection/JNI surface。按JNA上游Android ProGuard规则保留`com.sun.jna.*`及其派生mapping类型后，release重新构建、安装并从production UI运行成功，页面逐项显示`diagnose:native:roundtrip-ok`、`diagnose:shell:handoff-ok`、primitive/dispatch/cleanup SUCCESS、`DIAGNOSTIC_COMPLETE`和`Product success: NO`。

同一天在`emulator-5556`（6.6.30、`PAGE_SIZE=4096`）启动真实Shizuku server并授权GhostLock后，debug exact class gate实际执行`shizukuRoundTrip`、`directJnaLayoutAndNonceRoundTrip`与`appFlowHostRoundTrip`，instrumentation输出`OK (3 tests)`、exit=0且`SKIPPED=0`。随后的minified-release production UI在Shizuku执行域也显示同一Native/Shell exact行与完整typed report；logcat证明UserService进程由uid 2000/`u:r:shell:s0`的`shizuku_server`启动、加载`libjnidispatch.so`并输出`GhostLockNative: diagnose:native:roundtrip-ok`。因此4 KiB App/Shizuku debug与release纵向均为PASS；该证据不得外推为16 KiB PASS。

随后安装Android 15 `google_apis_ps16k/arm64-v8a` system image并新建`GhostLock_6_6_16K` AVD。`emulator-5558`实测`PAGE_SIZE=16384`、kernel=`6.6.30-android15-8-gdd9c02ccfe27-ab11987101`。同一debug exact class gate输出`OK (3 tests)`、exit=0且`SKIPPED=0`；minified-release production UI先后以App与Shizuku执行域运行Diagnose，两次均显示Native/Shell exact行、primitive/dispatch/cleanup SUCCESS、`DIAGNOSTIC_COMPLETE`与`Product success: NO`。Shizuku次logcat同时显示UserService加载`libjnidispatch.so`与`GhostLockNative: diagnose:native:roundtrip-ok`，活体domain为uid 2000/`u:r:shell:s0`。因此16 KiB App/Shizuku debug/release runtime也为PASS，V1关闭。

###### P-DIAG-NATIVE-001/批次01.8 进度

- 原已完成步骤、批准与运行证据：见 Beads `gl-yhz` notes 的迁移前记录；本设计不维护进度checkbox。

<a id="p-run-x-001-r6-review"></a>


历史审批/实际证据见 Beads [gl-yhz](design.md#record-gl-yhz) notes（原文保留，不作为实时状态副本）。


<a id="acceptance-gl-yhz"></a>

### 验收定义与原边界：gl-yhz

<!-- migrated-source: acceptance_criteria gl-yhz -->
保留原文各行为判据、当轮证据和未关闭设备门。此decision收录完成不关闭任何关联业务任务。

批准/审核/实际结果：Beads `gl-yhz` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-obq"></a>

## P-RUN-HANDOFF-001：common固定脚本handoff

<!-- migrated-source: 043fdf6a Beads gl-obq description -->
2026-11-21迁入的共享设计记录，不是新的执行任务。closed仅表示该记录收录完毕，不表示业务实现、新批准或门禁通过。
已完成公共handoff语义、API、magic、typed facts、owner与原证据；B1仅改变native归属，不替换该行为合同。

<!-- migrated-source: design gl-obq -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L2255-L2336 -->
<a id="p-run-handoff-001"></a>

##### P-RUN-HANDOFF-001/批次01：common 无参数脚本 handoff（已完成）

###### P-RUN-HANDOFF-001/批次01.1 用户裁定与取代关系

2026-11-06，用户纠正现有 Diagnose handoff：common `run_handoff` 不接收 `scriptPath`，脚本由统一发布者写入固定位置并带固定 magic，helper 自行定位、验证并执行；helper 不检查 uid，真实产品调用者仍必须是已经取得目标权限的攻击进程或其 victim 子进程，Diagnose 只用非 root 调用验证失败语义与全链路，不得外推为产品 root。用户随后进一步纠正：Flow 不保留独立 `diagnose-shell-handoff` physical step；真实攻击通常必须在仍持有提权上下文的同一次 native operation 内立即完成 handoff，Diagnose 应当复现该边界，而不是由 Kotlin 在第二步重新启动 shell。

本节只取代 P-DIAG-NATIVE-001/批次01 中“`DiagnoseFlow` 直接以 `ProcessBuilder` 接收自由 `scriptPath`、Kotlin 持有 shell process/timeout/cancel”的 current handoff 部分；已经完成的 JNA constant-name load、native round-trip、`SequentialFlow`、LogHub 与两端 RunHost 合同继续有效。历史段落保留沿革，不再作为 handoff current authority。本节并入同一 backend root execution 主计划，不建立第二份 active plan。

###### P-RUN-HANDOFF-001/批次01.2 固定定位、固定 magic 与 common API

固定位置不是一个跨 SELinux 域共享的设备绝对路径，而是唯一运行目录 authority 下的固定相对位置：

```text
GHOSTLOCK_HOME/root-script/root-script.sh
```

App `:flowhost` 与 Shizuku UserService 各自仍拥有 fresh per-run workspace；被接受的 run 在进入 `Flow.run` 前把自己的 `workspace.root.absolutePath` 写为该进程当前 `GHOSTLOCK_HOME`。同一进程不得重叠运行两个 Flow；后续 service 实例只能在前一 handoff child 已同步 reap 且 run 已 terminal 后覆盖此值。prepare loser、未被接受的 candidate 与预览路径均不得写环境。`Os.setenv` 与 `Flow.run` 位于同一 RunHost failure boundary；设置失败必须经唯一 terminal claim 形成失败报告，不得留下 RUNNING 无 terminal。

脚本固定以以下两个 ASCII 行开头：

```text
#!/system/bin/sh
# GHOSTLOCK_HANDOFF_V1
```

product `RootScriptRenderer` 与 diagnostic publisher 使用同一 marker 文本；现有 `root-result.glr1` 的 `GLR1` 是结果协议 magic，不替代脚本类型 marker。common 只提供：

```c
int32_t run_handoff(void);
```

`run_handoff` 只读 `GHOSTLOCK_HOME`，不接收 path、uid、计划、Payload、Finish 或日志对象；不扫描目录，不回退 `TMPDIR`、`/data/local/tmp` 或旧 `.ghostlock_root.sh`，不检查 uid。它用 `open/openat(..., O_NOFOLLOW)` 逐级打开 home、`root-script` 与 `root-script.sh`，在同一 script fd 上验证普通文件和 exact header；随后以 `posix_spawn` file-actions 把该 fd 复制到 child 固定 fd，并执行 `/system/bin/sh /proc/self/fd/<fd>`，避免验证后由 shell 重开路径。helper 不建立 queue、executor、callback、线程、取消协议或第二套 Payload/Finish 串行器；它只同步等待并 reap 自己创建的 shell child，child 自然继承调用进程的 uid/gid/capability/SELinux 上下文。

返回合同冻结为：`0`＝观察到 shell child exit 0；`1..126` 与 `128..255`＝观察到对应 child exit；`127`＝`GL_HANDOFF_CHILD_EXIT_127_AMBIGUOUS`，只记录观察值，不宣称能区分脚本自身、shell 或 Android/Bionic child setup/exec 失败；独立负值分别表示 `HOME_MISSING`、`HOME_INVALID`、`OPEN_HOME`、`OPEN_DIRECTORY`、`OPEN_SCRIPT`、`SCRIPT_TYPE`、`SCRIPT_MAGIC`、`SPAWN_CALL`、`WAIT`、`CHILD_SIGNALED` 与 `INTERNAL`。signal 终止不得伪装成 `128 + signal`，父侧 `posix_spawn` 返回错误不得伪装成脚本 exit。

###### P-RUN-HANDOFF-001/批次01.3 Diagnose 单次 native chain、typed facts 与日志所有权

`DiagnoseFlow : SequentialFlow` 只声明一个有物理意义的 `diagnose-native-chain` step。该 step 只调用一次 `DiagnoseNativeBinding.runChain()`；binding 只调用一次 ordinary-C `gl_diagnose_run(request, result)`。同一次 native operation 内固定执行：验证两个 borrowed log fd → 取得 uid/pid/nonce → Native record → detach Native fd → attach Shell fd → common `run_handoff()` → detach Shell fd → 一次提交 result。Flow 中不再存在 `diagnose-shell-handoff` step，Kotlin 不直接 `ProcessBuilder` shell，也不把 `scriptPath` 传入 native。

2026-11-06用户在实施开始后裁定JNA与project DSO始终随同一版本App发布，不保留旧兼容；因此本批删除request/result中的version、size与reserved字段，删除Kotlin运行时layout/header/size检查，并把唯一符号定为非版本化`gl_diagnose_run`。request只含`nonce`、`nativeLogFd`、`shellLogFd`；result只含`uid`、`pid`、`echoedNonce`、`handoffStatus`。caller仍把`handoffStatus`初始化为`INT32_MIN`，其稳定含义是`HANDOFF_NOT_RUN`，native在调用helper前只验证该执行语义sentinel。function return仍是唯一call status authority，与phase fact不重复：function status非OK时result payload全部不可采信，binding映射primitive failure/unknown与dispatch `SKIPPED/NOT_STARTED`；只有实际进入`run_handoff()`后，native才允许把local handoff return写入`handoffStatus`，helper失败仍是call OK加真实dispatch failure。所有fd与identity前置验证都发生在helper之前；helper返回后只进行无throw字段赋值、detach与return，不留下“已执行但仍标NOT_RUN”的窗口。

binding为一次call创建两条独立短pipe。native只在Native attach窗口向native pipe写一条record；common只能通过现有`gl_log_export_fd()`只读借用Shell attach窗口的fd，并只把它通过`posix_spawn` file-actions `dup2`为shell stdout/stderr，common不close、不保存、不写日志、不调用LogHub。JNA返回后binding分别关闭两条pipe的最后write end并bounded drain到EOF，严格解析后分别以`RunLogOrigin.Native`与`RunLogOrigin.Shell`各投递一次。因此日志仍只有`source fd -> binding decoder -> RunLogEmitter -> RunHost callback -> LogHub -> sinks`一条authority；某一record malformed只降低对应phase fact，不覆盖另一phase的真实结果。

一次physical native operation必须仍能向`RunReport`交付彼此独立的`backendPrimitive`与`rootScriptDispatch`，不能把raw primitive success与产品dispatch混成一个布尔值。为此`FlowStep`把单一`stage`字段最小替换为非空`produces: Set<FlowStage>`，`StepResult`与`FlowStepRun`携带typed `FlowStepFacts(primitive, scriptDispatch)`；facts必须exact覆盖该step声明的producer且不得写未声明fact。`SequentialFlow`在执行前验证整个plan内每个`FlowStage`最多一个producer，重复writer直接合同失败；全局无人生产的fact才是`SKIPPED`。执行前取消把本step全部producer记为`CANCELLED`；unexpected exception把全部producer记为`UNKNOWN/INTERNAL_CONTRACT_FAILURE`；backend已知失败必须显式返回两项fact，例如primitive失败且helper未调用时dispatch=`SKIPPED/NOT_STARTED`。Diagnose唯一step声明`{BACKEND_PRIMITIVE, ROOT_SCRIPT_DISPATCH}`、attempts=1、timeout=0；未来多个native函数仍可由多个互不重叠producer的Flow step顺序组装。

同步 JNA handoff 开始后不增加跨线程 native cancellation 状态；`DIAGNOSE` 的 `AbortPolicy` 因此由 `SAFE_DURING_RUN` 改为 `BEFORE_START_ONLY`。start 前取消仍由 RunHost 拒绝进入 Flow；start 后 helper 必须等待自己的受控短脚本终结并 reap。diagnostic script必须包含固定marker，并用一个受控非零子命令验证shell对失败exit的捕获，再只在捕获值正确时输出唯一`diagnose:shell:handoff-ok` record与exit 0。

###### P-RUN-HANDOFF-001/批次01.4 实施边界与验证

| 类别 | exact scope | 判据 |
|---|---|---|
| common native | 新增 `app/src/main/cpp/common/run_handoff.{h,c}`；修改 `app/src/main/cpp/common/CMakeLists.txt` | 无 path 参数、无 uid gate、无 fallback/scan；same-opened-fd magic 验证与 spawn/wait 返回合同进入 host CTest。 |
| publisher | 修改 `RootScriptRenderer.kt`、`RootArtifactStagingAdapter.kt` 及其 tests | product/diagnostic 都发布固定 `root-script/root-script.sh` 与 exact marker；现有 artifact identity/result authority 保持。 |
| RunHost | 修改 `RunHostService.kt`、`GhostlockUserService.kt` 及状态测试 | 只有 accepted running candidate 设置 `GHOSTLOCK_HOME`；setenv failure 取得 terminal；并发 prepare loser 不写；顺序 run 只能在前一 terminal 后覆盖。 |
| core Flow facts | 修改 `FlowStep.kt`、`StepResult.kt`、`FlowStepRun.kt`、`SequentialFlow.kt` 与 focused tests | `produces`保留计划期fact owner；每项fact最多一个producer；一次physical step可exact交付primitive+dispatch，不增加backend-private缓存或伪projection step。 |
| Diagnose native/JNA/Flow | 修改 Diagnose C ABI/implementation/CMake、JNA mapping/binding、`DiagnoseFlow.kt`、`BackendDiagnose.kt` 与 focused tests | 唯一JNA/native chain内部调用common handoff并返回两项phase fact；`diagnose-shell-handoff` step、Kotlin `ProcessBuilder`、active shell process与自由path seam命中0；Native/Shell独立pipe与origin各一次。 |
| docs/guards | 修改 exact ledger、architecture guard、UML 与 current pointers | 唯一图显示 RunHost home binding -> JNA wrapper -> common locator -> shell fd execution -> LogHub；不恢复任何真实攻击 backend。 |

source gate 至少执行 current exact Kotlin unit/architecture gate、`nativeTestHost`、debug/release Android native build，并在 4 KiB 与 16 KiB AVD 的 App/Shizuku execution domain 各证明：`run_handoff()` 无 path 参数实际到达；magic/缺失 home/错误 magic/脚本非零/exit 127/signaled 返回映射 exact；正常 diagnostic 仍是 primitive/dispatch/cleanup `SUCCESS`、terminal `DIAGNOSTIC_COMPLETE`、product success `NO`。这些结果只证明 handoff infrastructure，不证明 CVE、root primitive 或目标设备 supported。

###### P-RUN-HANDOFF-001/批次01.5 进度

- 原已完成步骤、批准与运行证据：见 Beads `gl-obq` notes 的迁移前记录；本设计不维护进度checkbox。


历史审批/实际证据见 Beads [gl-obq](design.md#record-gl-obq) notes（原文保留，不作为实时状态副本）。


<a id="acceptance-gl-obq"></a>

### 验收定义与原边界：gl-obq

<!-- migrated-source: acceptance_criteria gl-obq -->
保留原文各行为判据、当轮证据和未关闭设备门。此decision收录完成不关闭任何关联业务任务。

批准/审核/实际结果：Beads `gl-obq` notes；本段不维护实时勾选或新PASS。
