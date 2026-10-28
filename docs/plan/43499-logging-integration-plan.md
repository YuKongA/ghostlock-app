# 43499 日志接入 L 设计（UI 运行界面 + logcat + FileSink）

- 状态：**L 设计 v1.1；task-101 已实现 provisional（真机 UNVERIFIED）**；未 commit；`perf.enabled=0`；**不碰 98260**。
- **task-103 合规修正**：UI 仅 `Info+`；Info 只里程碑无参数细节；Debug 改「首次进入+汇总」；Warn/Error 用 `log.warn/error(msg, reason)`；静态 gate/双构建见 approval。
- 目标：让 43499 生产路径**实时**把事实行送到 UI RunPage、logcat、FileSink（共用 `com.ghostlock.app.log`），不改控制流/retry/timeout/成功语义；敏感值脱敏。
- 现状（Lead 已核）：通用日志系统存在（`log.log` 门面/logcat/FileSink；native `gl_log` + `NativeLogBridge.attachFd43499`），但 **43499 生产路径零 `log.*`、`Cve43499FlowRunner` 忽略 `onLine`、Flow 无 `FlowProgress`**；UI 只在结束后显示 evidence；logcat 仅测试 harness TAG；native jni/core 零日志。

## 0. 结论摘要

0. **现状答复（用户问题）**：设计时 43499 生产路径**未接入**通用日志（task-101 已实现 seam/emit/静态 gate；真机 UNVERIFIED）；UI RunPage/logcat 当前看不到 43499 运行行（UI 只在结束后显示 evidence；logcat 仅测试 harness TAG）。接入后：UI 实时行 + logcat tag `Ghostlock43499` + FileSink；native 行 `GhostLockNative`；见 §3–§7。
1. **方案**：仿 98260，在 `Flow43499Request` **append-only** 增加 `progress: FlowProgress = FlowProgress.NONE`；`Cve43499FlowRunner` 构造 `FlowProgress { line -> onLine(line) }` 注入；`RunPage` 已有 `onLine→TerminalEntry` 通路（`RunPage.kt:234/508`），**UI 无需改结构**。
2. **双向 sink**：每个事件只调用一次统一 helper `Cve43499RunLog.emit`：`progress.onLine(UI 行)` + `log.invoke(level, "Ghostlock43499", 行)`（FileSink+logcat）；同一事件不重复发两条、不与 native 行重复。
3. **每阶段行**（§4）：W1/W1b/W2/W2b/W3/victim/finish/close/fail-stop 的 Info 里程碑、Warn/Error（恰好一个理由）、分支 Debug（每分支恰好一条）；**PI 窗口禁一切日志**；单行 ≤512B ASCII；地址/原始 perf/脚本体/密钥不外泄。
4. **Native**：`gl_log` fd 已在 App 启动注册（`GhostLockApp.kt:37`→`NativeLogBridge.attach`→`attachFd43499`）；43499 Native 当前零调用——设计只允许 **JNI 入口/返回边界**少量 `info/warn/error`，不得进 actor/carrier/page/perf/victim 热路径，PI 窗口禁，限频；tag 用 `GhostLockNative`（logcat）。
5. **验证**：fake FlowProgress/假 LogSink 单测 + RunPage 结构测试 + 真机 logcat/FileSink 断言（E1/E7 未跑⇒UNVERIFIED）；示例/命令见 §7。

## 1. 现状证据（file:line）

| 项 | 位置 | 事实 |
|---|---|---|
| 公共日志门面 | `app/src/main/kotlin/com/ghostlock/app/log/log.kt:1-199`（级别/环/`format`/`invoke`/`shell`；`DEFAULT_TAG="Ghostlock"`）；`LogcatSink:189-199`；`FileSink`/`LogFile.kt`（fd 追加/轮转） | 已有 `log.*` + logcat + FileSink；native 行写同一 fd |
| Native 日志 | `app/src/main/cpp/common/gl_log.h:65-103`（`gl_log_attach_fd`/阈值/`info/warn/error/debug/verbose` 宏，tag `GhostLockNative`）；`common/gl_log.c:26`；`very_legacy/jni_attach.c:1-20`（`attachFd43499`）；`GhostLockApp.kt:37` 启动 attach `LogFile.fd` | fd 注册已存在；43499 `jni/core` **零 gl_log 调用** |
| 43499 runner/Flow | `ui/backend/BackendFlowRunner.kt:50-71`（43499 runner 忽略 `onLine`）；`Flow43499Request.kt:29-40`（无 progress）；`Flow43499Impl.kt:243-275/400-407`（steps）；`Flow:531/760/829/1221/1398/1452/1489/1625/1662/1782`（W1/W1b/W2/W3/victim/probe/finish/close/acceptance/failStop） | 无 progress/日志；仅 FlowResult/evidence |
| UI 通路 | `RunPage.kt:234/508`（`onLine→TerminalEntry(KOTLIN, Info)`）；`BackendRunGate.kt:140-157`（dispatch 透传 onLine） | **UI 已就绪**，缺上游发射 |
| 98260 参照 | `FlowProgress.kt:11-20`；`Cve98260RequestFactory.kt:93-98`（progressSink）；`Flow98260Impl.kt:36-140`（stage 行）；`BackendFlowRunner.kt:82-101`（`FlowProgress{onLine}`） | 可复制的 append-only seam |
| PI 约束 | `Flow43499Impl.kt:266`（execute=唯一 PI 窗口）；`actor.c` 窗口内禁日志（既有注释） | 日志只能发生在事务返回之后 |

## 2. 目标/非目标

**目标**：43499 生产路径每个阶段的关键事实实时可见：UI RunPage 行、logcat（tag `Ghostlock43499`）、FileSink（`files/ghostlock-run.log`）；级别遵循 `log` 门面规则/AGENTS §12（Info=成功里程碑、Warn=缺失可回退、Error=无回退、Debug=分支恰好一条、Verbose=行级默认关）；V/≤512B/ASCII；敏感值脱敏；失败日志不改变 Flow 语义。

**非目标**：不改 wire/ABI/DTO/FlowResult/evidence 语义；不改 retry/timeout/cancel/成功判定；不加外部依赖；不接 98260 文件；不把 Native 变成日志状态机；不承诺 release 显示 debug 行（按 `BuildConfig.DEBUG` 阈值）。

## 3. seam 设计（append-only）

| 层 | 设计 |
|---|---|
| `Flow43499Request` | **append-only** `val progress: FlowProgress = FlowProgress.NONE`（新字段；旧构造/测试默认 NONE；不改其它字段语义） |
| `Cve43499RequestFactory.build` | 新增 `progress: FlowProgress = FlowProgress.NONE` 参数（append-only），构造 request 时写入；缺省无行为变化 |
| `Cve43499FlowRunner.run` | `val progress = FlowProgress { line -> onLine(line) }`；`Cve43499RequestFactory.build(..., progress=progress)`；异常仍按既有前缀 Refused（日志失败不得改变） |
| `Flow43499Impl` | 各 step 函数用统一 `Cve43499RunLog.emit(run, level, stage, message, evidence?)`；**不新增步骤/不读 progress 做控制流** |
| `Cve43499RunLog`（新 helper） | `emit` 一次事件→①`progress.onLine`；②`log.invoke`；**两者分别 `runCatching`（互不吞）**；emit 前置校验 ASCII/无控制字符/≤512B（超限截断或丢弃并计数）；**唯一发射点**；调用点白名单=仅事务/verify/close/fail-stop 返回后；**禁 executeActor 内部/Actor 三线程/waiter/carrier hot path** |
| UI | RunPage 已把 onLine 追加 TerminalEntry；无需改；日志行加 `[43499]` 前缀便于区分 |
| 去重 | 同一事件只 emit 一次（UI+log 各一份，不同 sink）；Native 行只走 gl_log，不回调 onLine；不用 `log.*` 再写 UI 行 |

## 4. 每阶段行清单（Info/Warn/Error/Debug；PI 窗口禁日志）

| 阶段 | 事件/级别 | 行模板（脱敏） | 来源/备注 |
|---|---|---|---|
| run start | Info 1 | `[43499] run start runId=<8hex>` | `runId` 截断；Flow 入口 |
| W1 placement | Info 1 | `[43499] w1 placement rounds=<n>` | Prepared/facts |
| W1 landing | Info 1 | `[43499] w1 land reclaimSends=<n>` | Landed |
| W1 actor execute | Info 1（**事务返回后**） | `[43499] w1 actor consumerSuccess=<n> prim=<kind>` | 事务返回后；PI 窗口内禁 |
| W1 失败（placement/landing/actor-execute） | **Warn/Error 各分支恰好 1** | `w1 <stage> failed reason=<token> attempt=<n>` | clean failure=Warn；retained=Error；不重试日志轰炸 |
| W1 verify | Info/Warn 1 | `w1 verify=<VERIFIED/UNSUPPORTED/UNVERIFIED> reason=<token>` | verifier；UNSUPPORTED=Warn 1 |
| W1 chain close | Info/Warn 1 | `w1 close retained=<bool> term=<token>` | close 回执；retained=Warn |
| W1b | Info 1 + Debug 1 | `w1b state=<repaired/skipped> writes=<n>`；`w1b attempt i/n`（Debug） | 分支恰好一条 Debug；失败 Warn/Error |
| W2 precheck/perf | Info 1 + Debug 1 | `w2 precheck=<ok/fail> passes=<n> votes=<n> candidates=<n>`；地址只打 `addr=masked`（不显原始值） | target source/evidence；失败 Warn/Error |
| W2 cred | Info 1 + Debug 1 | `w2 cred attempt i/n result=<routed/failed> repair=<mode?>` | 每次 attempt Debug 一条；成功 Info |
| W2 最终失败/clean/retained | **Warn/Error 1** | `w2 failed category=<clean/retained/route> reason=<token>` | 最终失败 Warn；retained=Error |
| W1b/W2 clean-failure/retained 分支 | **Warn/Error 1** | `w1b/w2 branch=<clean-failure/retained> reason=<token>` | 每分支恰好 1；retained=Error |
| W2b fast-repair | Info 1 / Warn 1 | `w2b mode=<prebuilt/full-fallback/none> prebuildUs=<n> repairUs=<n>`；fallback=Warn（panic 窗口不改善） | G05 证据；retained⇒Error |
| W3 hard gate | Info 1 / Warn/Error 1 | `w3 gate=<PASS/FAIL> uid=<0\|n> seccomp=<0\|n> leaf=<EXACT/+8\|none>`；FAIL 带 `reasonCategory` | 不打印地址；coldReboot=Warn |
| victim | Info 1 + Debug 1 | `victim start pid=<masked> rooted=<bool>`；`victim detach/park`（Debug） | 不打印完整 pid/uid |
| victim start 失败 | **Warn/Error 1** | `victim start failed reason=<token>` | 失败恰好 1；不回显路径全量 |
| finish | Info 1 / Warn/Error 1 | `finish source=victim exit=<n> timedOut=<bool> sha=<12hex>` | 脚本体/输出原文不打印 |
| close | Info 1 / Warn 1 | `close termination=<token> released=<bool> retained=<bool> parkedRooted=<bool>` | root detach 非泄漏 |
| fail-stop | **Error 1** | `fail-stop stage=<stage> reason=<token> coldRebootRequired=<bool>` | G01；先 evidence/日志再终止；`enabled=0` 也记 |
| 分支 Debug | Debug 1/分支 | 重试/退避/UNVERIFIED 分支各一条，含计数不含地址/原始数据 | 每分支恰好一条 |

**硬规则**：**PI 窗口内（`executeActor` 内部及 Actor 三线程/waiter/carrier 触发热路径）绝不调用 any log/progress**；日志只能在事务返回、verify、close、失败路径等窗口外；**emit 调用点白名单/静态断言见 §8**；每行 ASCII、≤512B、单次 write；Info 每阶段最多 1 条，Warn/Error 恰好 1 个理由，Debug 每分支恰好一条，Verbose 默认关。

## 5. Kotlin tag/sink/脱敏

- Kotlin tag：`const val TAG = "Ghostlock43499"`；全部 `log.invoke(level, TAG, line)`（FileSink+logcat 同一 tag）。
- 级别：默认 `LogLevel.Info`（release 同）；debug 构建可用 Debug；建议 `log.configure(minLevel = if (BuildConfig.DEBUG) Debug else Info)` 由现有全局配置负责，本设计不另建配置。
- 脱敏：**不打印**内核/用户地址数值、perf raw/寄存器、脚本/输出原文、runId 全长、pid/uid 完整值、密钥/路径敏感段；只打印计数、布尔、枚举、时间、`<12hex>` SHA 前 12 位；地址以 `masked` 占位（或省略）。
- onLine 去重：UI 行只来自 `progress.onLine`；logcat/File 行只来自 `log.*`；同一 emit 调两者一次，不得在调用点重复发送；Native 行只进 gl_log，不回 UI/不重复 `log.*`。

## 6. Native 边界（JNI 边界/机制，不加逻辑）

| 项 | 设计 |
|---|---|
| 已具备 | `GhostLockApp.kt:37` 启动 `NativeLogBridge.attach(LogFile.fd)`→`attachFd43499`→`gl_log_attach_fd`；`gl_log` tag `GhostLockNative`、FileSink 同一 fd |
| 允许（I4 白名单） | **仅 JNI 入口/参数校验/返回**：`preparePlacement/landPage/prepareActor/executeActor/close*`、victim spawn/send/receive；只打印 **stage token/opcode/result code/count/布尔**；每入口 ≤2 行、失败才 Error；地址/path/pid/uid/rawSize **不打印或 mask**；ASCII/≤512B；无状态机/无分支业务 |
| 禁止 | actor/waiter/owner/consumer、page/carrier 触发热路径、perf ring 拷贝循环、victim helper 命令循环、PI 窗口内任何 gl_log；不解析/不格式化业务数据（只 `%d/%u/%s` 机械值/枚举 token） |
| 限频 | 同一入口每调用最多 1 条 Info；失败路径 1 条 Warn/Error；release 由 `gl_log` 阈值 Info 控制（debug attach 传 DEBUG）；不打印地址/raw/字节 |
| 验证 | `adb logcat -s GhostLockNative:V`；**新 gate `verify_no_log_in_pi`**：静态扫描禁 actor/page/carrier/perf/victim loop 内 `gl_log`/`__android_log_print`（PI 窗口/Actor 三线程 hot path）；E4/E7 logcat 含「无 PI 行」验证项；host `verify_no_fork_imports`/NDK 零告警不变 |

## 7. UI 期望示例 + logcat/FileSink 验证

**UI RunPage（终端行，`[43499]` 前缀）**：
```text
[43499] run start runId=3f9a1c2e
[43499] w1 placement rounds=1
[43499] w1 land reclaimSends=32
[43499] w1 actor consumerSuccess=7 prim=multicast
[43499] w1 verify=VERIFIED reason=-
[43499] w2 precheck=ok passes=2 votes=96 candidates=384
[43499] w2 cred attempt 1/2 result=routed
[43499] w3 gate=PASS uid=0 seccomp=0 leaf=EXACT
[43499] finish source=victim exit=0 timedOut=false sha=9c1e…
[43499] close termination=clean released=true retained=false parkedRooted=true
```
**logcat/FileSink 验证命令**：
```sh
adb logcat -c
adb logcat -s Ghostlock43499:V GhostLockNative:V   # 运行中实时；Kotlin/native 两 tag
adb logcat -d -s Ghostlock43499:V | grep -c "\[43499\]"   # 行数与 UI 应一致（同 emit）
adb shell run-as com.ghostlock.app cat files/ghostlock-run.log | tail -n 40
adb logcat -d -s Ghostlock43499:V | grep "w3 gate"      # literal 验证
```

## 8. 测试/门禁

| 类 | 内容 |
|---|---|
| fake FlowProgress | fake request `progress` 收集行；断言 W1/W1b/W2/W2b/W3/victim/finish/close/fail-stop 关键行存在、顺序正确、每阶段 Info≤1、Warn/Error 理由=1、无 PI 窗口行（用 fake 生命周期在 execute 前置标记断言无日志） |
| fake LogSink | `log.install(fakeSink)`；断言 tag=`Ghostlock43499`、级别、≤512B/ASCII、脱敏（无地址/raw/脚本体）、UI 与 log 行一致（同事件）；**fake progress/sink 抛异常互不影响（I3）** |
| 静态 gate（I1/I4） | `verify_43499_log_sites`：emit 调用点白名单/禁 PI 窗口调用；`verify_no_log_in_pi`：Native 禁 actor/page/carrier/perf/victim loop 内 gl_log/android_log；扫描 Native 日志参数禁地址/指针/path/raw；负例造错→gate FAIL 后撤回 |
| 失败负例（I2） | 重试计数（attempt 行数=w1Attempts）、retained 分支 Error、`failStop.enabled=0` 仍发 fail-stop Error 且 StepResult 不变 |
| UI 结构 | RunPage 既有 `onLine→TerminalEntry` 不改；测试 `BackendRunDispatcher`/runner 把 progress 行传给 onLine（fake factory/launcher） |
| 离线 | compileDebugKotlin/UnitTestKotlin 过滤、scanner r2、ABI/JNI 符号/zero-fork/diff-check 不变；无外部依赖 |
| 真机 | logcat tag 行、FileSink 行、UI 实时显示、release(Info only)/debug(Debug) 差异；E1/E7 未跑⇒**UNVERIFIED**，不标 supported |

## 9. 风险/CBN

| 选项 | 说明 | 结论 |
|---|---|---|
| **A. FlowProgress + log facade（推荐）** | append-only 注入；同一 emit 双 sink；UI 无需改 | 采纳 |
| B. 仅 `log.*`，UI tail 读 FileSink | 无 FlowProgress，UI 依赖文件轮询/延迟 | 否决（非实时、重复/乱序） |
| C. Native 为主 | 无 Kotlin 阶段信息，PI 风险 | 否决 |
| D. 只保留结束 evidence | 当下现状，用户问题 | 否决 |

**风险**：①PI 时序（PI 窗口内误 log 会破坏覆盖栈/时延；硬规则+测试）；②敏感值泄漏（地址/raw/脚本）；③重复/乱序（emit 唯一；单线程 Flow 顺序；UI channel trySend）；④release/debug 差异（minLevel）；⑤日志量/FileSink 增长（Info 每阶段 1 行；轮转策略既有）；⑥日志异常影响 Flow（helper `runCatching` + 失败不改变 StepResult）；⑦UI 主线程（RunPage channel 已异步）；⑧Native attach 失败回退（现有 `NativeLogBridge.attach` 返回 no-op 行，不影响攻击）。

## 10. UNVERIFIED/不做

**UNVERIFIED**：43499 生产路径当前零日志/无 progress（source-only）；FlowProgress 字段/发射未实现；UI 实时行未真机验证；logcat/FileSink 行未跑；release/debug 差异未验证；E1/E7 未跑；Native gl_log 调用行未实现/未测；PI 窗口禁日志仅设计约束。
**不做**：不写实现、不改生产代码/ABI/wire/控制流/retry/timeout/成功语义；不加外部依赖；不碰 98260 文件；不把 Native 变日志状态机；不 commit、不标 supported；`perf.enabled=0` 保持。
