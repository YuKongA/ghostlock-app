# Flow 进程启动器计划（L · v5 · 已生效）

> 状态：**已生效（2026-11-02）**：v4 经 design-critic task-140 判「可进用户批准 gate（阻断 0）」；用户以 goal「实现新进程攻击机制」下达实现指令，D1–D7 按推荐值生效；执行通道取代/指针动作按 §7 执行（exec-glue-plan 已更新）。B1 起实现，B2+ 待设备门禁。
> 2026-11-03 指针更新：Clean Architecture 纵切把 UI ownership 改为 RunViewModel + application-scoped RunCoordinator + BackendRunSessionAdapter；本文出现的 BackendRunViewModel/旧 presentation wiring 只作当时实施清单，当前权威类图见 `docs/development/full-process-uml.md` §11.9–11.10。Flow/launcher/host 的新进程与资源语义不因此改变。
> 需求来源（用户 2026-11-02 原话）：
> 「现在有新架构：攻击一律启动新进程：1.UI选定指定的后端，保存指定的flow入口的reflection symbol和参数；
> 2.将reflection symbol和参数传入到进程启动器（包含一般app进程和shizuku进程）3.进程启动器调用对应的symbol执行。
> 注意：reflection symbol只能指向flow的函数」
> 依据：`docs/development/软件工程守则.md` > `AGENTS.md` > ADR-0022 / `execution-architecture.md` / 本计划。
> 取代关系（**批准后生效**）：取代 `98260-user-requirements-summary.md` A4（攻击在 JVM 线程）/A5 与 §2.1（symbol=backend key；App=LocalThread；Shizuku=pending）、`98260-backend-plan-delta-userservice.md` §2 D2′ 的「UserService 只承载 host 原语、由 app 进程 Flow 编排」、`exec-glue-plan.md` §3/§5 的「App-owned Flow + 共用 UserService 只启动/观测 ELF」执行通道、`execution-architecture.md` 中「Flow 只在 app 进程执行」的事实。
> **不变**：ADR-0022 决策 1/2/4/5/6（Flow 唯一 run 权威、close 权威、JNI 边界、Lua cleanup；决策 3 的 FlowResult 语义在 D5 默认 B 下也不变）；A3/A6/A7（执行体二选一、声明与检查分离、授权二值）；**A12（原版 43499/43284 保持 App 域语义与 W3，不迁移）**；**A13（BackendSpec 参数声明）**；A9/A10/A11（payload/finish/victim/reboot：触发后不 kill/timeout）；profile 体系；不 commit。

## 1 目标与范围

一次 backend run 的编排（Flow）本身必须运行在“选中的执行体进程”里，而不是 UI 进程：
1. UI 选定 backend 后持久化该 backend 的 **flow 入口 reflection symbol** 与参数；
2. 启动器把 symbol+参数送到新进程（一般 app 进程 / Shizuku 进程，二选一）；
3. 新进程按 symbol 反射调用 flow 入口；**symbol 只能指向 flow 的函数**（§2.4 硬约束）。

**范围**：只迁 `43499-Glued`、`43284Glued` 及后续新 backend（98260/Lua）。**原版 43499/43284 不迁移、不加 flowSymbol、保持不可 selectable**（A12；当前 `BackendSpec.kt:231` 也只允许两个 Glued）。原版文件不改语义。

## 2 架构

```text
UI 选 backend（BackendSpec 声明 flowSymbol + paramsSchema）
  → 持久化 {backendKey, flowSymbol, params, executorPreference}（§2.6）
  → BackendRunGate（不变）→ BackendRunDispatcher
  → FlowLaunchParent.launch(context, executorState, FlowLaunchRequest(backendKey, symbol, params, payload?, logFd?))
       ├─ AppProcessFlowLauncher     → :flowhost 新进程（app uid）
       └─ ShizukuProcessFlowLauncher → Shizuku 新进程（uid 2000 / u:r:shell:s0）
  → 目标进程 FlowHost：registry 校验 symbol（§2.4）→ 参数白名单解码 → 反射调用 Flow 入口 → Flow.run（ADR-0022）
  → 终态先回传（state+verify+callback），再回收进程（launcher unbind / host self-kill）
```

### 2.1 一般 app 进程启动器（唯一方案）
- manifest 新增 `FlowHostService`：`android:process=":flowhost"`、`exported=false`、`stopWithTask=false`；
- 新 AIDL `IFlowHost`（app 进程内 Binder）：`prepare(symbol, Bundle params, byte[] payload, ParcelFileDescriptor? logFd)` / `start()` / `getState()` / `readLog(offset,max)` / `verify()` / `cancel()` / `setCallback(IFlowHostCallback)` / `destroy()=16777114`；
- 每次 run 新进程：终态回报后 `stopSelf()`+`killProcess(ownPid)`；下次 bind 时 AMS 重新 fork；B2 以 pid/旧进程消失/启动时间验证；
- 生命周期：launcher 以 `BIND_AUTO_CREATE` 绑定；task 移除/服务被杀/bind 超时/同 UID 进程复用均按 §2.5 给具名终态，且不得把复用进程当新 run（B2 断言 bind 超时、被杀、task 移除、pid 变化）。
- **排除** app 侧 `exec app_process + CLASSPATH` 备选（SELinux/exec 未验证且与“攻击一律新进程”目标重叠）：只记技术债 `TD-APP-PROCESS-EXEC`，不在本计划评估。

### 2.2 Shizuku 进程启动器
- 复用唯一 `GhostlockUserService`（Shizuku `bindUserService` 在 uid 2000、`u:r:shell:s0` app_process 进程实例化，app 包类可反射）：AIDL 由 per-backend host 改为与 §2.1 **同构的通用 FlowHost**（同一方法集/稳定码），host 只执行 flow symbol；
- 每次 run 绑定新实例：`UserServiceArgs.daemon(false)` + 唯一 `tag`/`processNameSuffix`（现状固定 `"shell"`，本批改 run 唯一值并按 Shizuku 约束校验） + 终态 `destroy()=16777114` 后 unbind；不以常驻 host 复用；门禁验证 service 实例、pid、进程启动时间均变化（B3）；
- shell 域限制：`GhostlockUserService` 的 package context **非有效 app Context**（`GhostlockUserService.kt:43-47`）。需 Context/资产/nativeLibraryDir 的 flow：43499 参数化（context-free）；43284 用 host 本地 `FlowHostEnv.context`（B4b 见 `annex-b4b`；Shizuku 侧可为 null ⇒ `flow-host:context-required`。

### 2.3 参数/结果传输
- `FlowLaunchRequest`：`backendKey`、`symbol`、`params: Bundle`、可选 `payload: ByteArray`、可选 `logFd`；
- **类型白名单**：`String/Int/Long/Boolean/Float/Double/ByteArray` 与有限 `String` 列表；禁止任意 Parcelable/Binder/IBinder/FileDescriptor 数组/嵌套 Bundle（B1 仅实现白名单类型，新增类型须改 spec schema + 测试）；
- **预算**：params 序列化 ≤256 KiB、payload ≤512 KiB、params+payload+fd 总量 ≤1 MiB−64 KiB（AIDL binder 预算）；键数 ≤32、单 String ≤8 KiB；超限具名 `flow-params:too-large`；B1/B2 负例：总量/键数/单串超限 + 持久化 schema/迁移/清理；
- 进度：host 写 log fd（无则 logcat），格式按 AGENTS §12；oneway `onLine/onTerminal/onDropped`；
- 终态：`getState()`+`verify()`（UTF-8 key=value，沿用现有 host 证据风格）；launcher 映射到 `BackendRunUiState`；
- **handoff/reboot**：见 §2.5（D5 默认不改 `FlowResult`）。

### 2.4 reflection symbol 契约（硬约束）
- 语法：`<fqcn>#<methodName>`，正则 `^[A-Za-z_][\w.$]*#[A-Za-z_]\w*$`；必须在 `FlowEntryRegistry` 登记（registry=唯一 allowlist，由 `BackendSpec` 生成，Lua 只能引用预登记项，脚本不得自带类名）；
- 目标类必须实现 `com.ghostlock.app.core.flow.Flow<*>`；目标方法必须：`@FlowEntry`（定义写死 `@Target(AnnotationTarget.FUNCTION)` + **`@Retention(RUNTIME)`**）、**`declaringClass == 目标类`**、`getDeclaredMethod(name, FlowHostEnv::class.java)` 精确匹配、**非 static**（拒绝 Java static/`@JvmStatic`）、非 synthetic/非 bridge、非 private、返回 `FlowResult`；实例化仅允许 Kotlin `object`（`INSTANCE`）或可访问无参构造类；**拒绝 companion/继承/重载**；
- host 侧顺序：regex → `Class.forName(name,false,FlowEntry::class.java.classLoader)` → `Flow.isAssignableFrom` → `getDeclaredMethod` → 签名/修饰符校验 → `@FlowEntry`；失败 ⇒ `flow-symbol:*`，**绝不调用其它方法/构造器**；
- launcher 侧：dispatcher 只发送 `BackendSpec.flowSymbol`（持久化值必须与 spec 一致，否则清空并 NotReady）；不接受 UI 自由输入；
- 负例矩阵（JVM 单测）：非 Flow 类、错误注解、static/@JvmStatic、companion、继承、overload、private、bridge/synthetic、**suspend/Continuation 签名、默认参数、协变返回（返回类型必须精确 `FlowResult`，非 isAssignable）、inner class、泛型方法、`@Target(FUNCTION)` 缺失**、未登记 symbol、classloader 不可见（Shizuku 门禁）；正向：所有 selectable spec 的 symbol 可解析；registry 与 spec 集合双向一致。

### 2.5 生命周期与终态权威
| 事件 | 终态权威/结果 | 攻击子进程(A11) | 资源/进程 |
|---|---|---|---|
| 正常 | host 写 TERMINAL+verify → onTerminal → launcher 读后 unbind | Flow 自收尾 | host 自杀；归还 log fd |
| 取消 | `cancel()`→Flow.stop()；终态 `Cancelled` | **不 kill**（A11） | 同上 |
| host crash | launcher Binder death ⇒ `flow-host:death`（Unknown，不伪成功/失败） | **不 kill**；不重试 | 系统回收；不复用 |
| UI 进程死亡 | host 见 callback Binder death ⇒ 停编排、证据写 log fd 后自杀 | 不 kill | 子进程 detached |
| reboot（无 handoff） | Unknown（`flow-host:reboot-before-terminal`），不得判成功 | 按 Finish | 设备重启回收 |
| handoff（D5=B） | Flow 在 spawn/reboot 前给具名 handoff 证据；launcher 映射 `BackendRunUiState.HandoffRequested`（B7；ADR-0022 不动），**之后不等待/不读 exit/不写状态**（exec-glue:18/27/53-55） | Finish 链决定 | 资源交后继 |
- one-shot：host 只接受一次 `start()`（重复 `ERR_STATE`）；每次 run 新进程（§2.1/§2.2）；
- 执行体二选一、失败不静默回退（A3/A7）；
- 具名失败前缀：`flow-launcher:{app|shizuku}:*`、`flow-host:*`、`flow-symbol:*`、`flow-params:*`、`flow-terminal:*`。

### 2.6 UI 持久化语义
- `BackendSessionStore` 持久化：`state_schema_version`、`selected_backend_key`、`flow_symbol`、`params`（Bundle 编码）、`executor_preference`（已有）；
- 键/迁移：schema 不符或 symbol 与 spec 不一致 ⇒ 清空 symbol+params 并 NotReady（具名 reason）；不可 selectable ⇒ 清空；params 权威=`BackendConfigStore` schema 键（profile reference 只是其中一个参数，不成为第二权威）；
- 保存时机：选择 backend、修改参数、选择执行体时立即持久化；run 只用持久化+已校验值构造请求；
- v0→v1：缺省从 spec 默认值生成 + 迁移测试。

## 3 安全不变量（硬约束）
1. symbol 只能指向登记过的 Flow 函数（§2.4 全套校验）；host 解码 fail-closed；
2. 参数类型/键/大小白名单（§2.3）；每次 run 新进程（真机验证）；
3. 执行体由 UI 选，launcher 只启一个进程、不 fallback、不并发两攻击进程；
4. Flow 资源在 host 进程内终结（进程终止只是兜底）；host Binder `exported=false`；
5. 原版 43499/43284 不改语义、不迁移（A12）；payload/finish/victim 不变（A9/A10/A11）。

## 4 文件级变更（B1 冻结前草案）
新增：`core/flow/FlowSymbol.kt`、`FlowEntry.kt`（`@FlowEntry`/`FlowHostEnv`/结果映射）、`FlowEntryRegistry.kt`；`backend/core/FlowHostService.kt`；`aidl/com/ghostlock/app/flow/IFlowHost.aidl`、`IFlowHostCallback.aidl`；`backend/core/AppProcessFlowLauncher.kt`、`ShizukuProcessFlowLauncher.kt`。
修改：`FlowLaunchParent.kt`（进程启动器；删除 LocalThread/Shizuku pending）、`BackendFlowRunner.kt`/`BackendRunGate.kt`/`BackendRunViewModel.kt`（dispatcher 改 flowSymbol）、`BackendRunUiState.kt`/`BackendRunGate.displayLines`/`RunPage.kt`/`BackendFlowRunner.kt`/`FlowLaunchParent.launch`（新增 `HandoffRequested` + **全部穷举点** + 不报成功测试）、`BackendSpec.kt`（新增 `flowSymbol`；B5 后删 `runner`）、两个 Glued backend/flow（加 `@FlowEntry fun entry(env)`）、`GhostlockUserService.kt`/`ShizukuBackendClient.kt`/`shizuku/host/*`（按 D3 收缩为 FlowHost）、`AndroidManifest.xml`、`full-process-uml.md`、计划指针（§7）。
不变：`app/src/main/cpp/**`（native 不动；攻击子进程仍由 Flow 内既有 launcher 负责）、profile、原版 backend。

## 5 批次与门禁（单变量、每批先冻结文件清单）
| 批次 | 内容 | 判据 |
|---|---|---|
| B0 | 定稿：修 v3、critic 复核、用户裁定 D1–D7、§7 指针 | 阻断=0；用户批准 |
| B1a | FlowSymbol/FlowEntry/registry + params codec + JVM 负例/预算矩阵（B1b=Bundle wire+持久化 schema 归入 B2） | 已交付 2026-11-02：42 tests/0/0；registry↔spec 接线已交付（G9） |
| B2 | `:flowhost` App 进程 + launcher + fixture + `HandoffRequested` | **2026-11-02 A301SO PASS 4/4**（`20261102-flowhost-a301so-smoke.md`）；取消 G10；task 移除待补 |
| B3 | Shizuku FlowHost（UserService 泛化）+ 同一 fixture | **B3a 已交付**（AIDL 12–18/service/client/launcher；51 tests）；B3b 已 PASS（2026-11-02） |
| B4a | 只迁 43499-Glued：flowSymbol + entry；runner=null（单权威） | 已交付 2026-11-02：63 tests；fixture 门禁 PASS（`20261102-b4a-43499-flowhost-fixture.md`）；B4-real 待授权 |
| B4b | 只迁 43284Glued（同上） | **已交付 2026-11-02**：70 tests；fixture 门禁 PASS（`20261102-b4b-43284-flowhost-fixture.md`）；G11 keepalive/G12 超时 PASS |
| B5a | 98260 迁移 | **不适用当前 build**（98260 不可选）；重新启用时再迁移；旧 host 仅 98260 legacy 保留 |
| B5b | Lua 迁移 | **不适用**（Lua 不可选）；动态 symbol 注册 gap 见 annex-b5-closeout |
| B5c | 删除旧 launcher/host + UML/approval/quota/指针收口（B5c-1/2 已完成；B5c-3 收口语 `annex-b5-closeout`） | 全量单测 470/0/0；debug/AndroidTest/Release PASS；`git diff --check`；仓外备份（/tmp） |
- 迁移期**单执行权威**：每批先置该 backend `runner=null`（旧路径不可达）再启用 flowSymbol；不允许 feature flag 双轨/并发；测试断言旧 runner 不存在或不可调用；B5a/B5b/B5c 各自只含一个 backend 或一个删除动作。
- 攻击路径改动一律真机门禁并归档 `docs/analysis/device-gates/`；所有真机项在报告单列「已验证/未验证」；不 commit/push。

## 6 已裁定（2026-11-02 goal 指令；按推荐值生效）
- **D1** 一般 app 进程启动方式：**A=manifest `:flowhost` Service（唯一方案）**；app_process 备选已排除（TD）。
- **D2** flow 入口形态：**A=每 Flow 加 `@FlowEntry fun entry(FlowHostEnv)` + registry（推荐）**；B=反射 `Flow.run`（需 request 工厂，拒绝）。
- **D3** Shizuku 侧：**A=per-backend host 整体替换为通用 FlowHost（推荐）**；B=双轨（拒绝，违反单执行权威）。
- **D4** 参数载体：**A=Bundle+payload+log fd，§2.3 类型/大小白名单（推荐）**；B=JSON/文件（拒绝，第二序列化权威）。
- **D5** handoff 终态：**B=不改 `FlowResult`（Ok/Failed）；handoff 作为具名证据由 launcher 映射**新增的 **`BackendRunUiState.HandoffRequested`（推荐，ADR-0022 不动；B7/§4 已列 UI 状态文件与穷举分支）**；A=新增 `FlowResult.Handoff`（须同批 ADR-0022 amendment + 沿革记录）。
- **D6** 迁移顺序：**A=B4a 43499-Glued → B4b 43284Glued → B5a 98260 → B5b Lua → B5c 删除旧通道（推荐，每批单变量）**；B=一次全迁（拒绝）。（2026-11-02 实际：B5a/B5b 因 backend 不可选不适用；B5c-1/2 已执行。）
- **D7** A12 范围确认：**Glued 变体（新 stable ID）不属于 A12 的“原版既有 backend”，可迁移；原版 43499/43284 不动（推荐）**；若用户认为 Glued 也受 A12 约束，则 B4 需知情强制并记录沿革。

## 7 批准生效同批动作（B0；避免两份活动计划）
1. `exec-glue-plan.md` 头部：标「执行通道 §3/§5 被本计划取代（指针+日期）；payload/finish/root handoff/logger 证据继续有效」；不再自称唯一；
2. `execution-architecture.md`：Flow-app-only 事实标历史并指向本计划；ADR-0022 不改（D5=A 除外）；
3. `98260-user-requirements-summary.md`：A4/A5/§2.1 加沿革注记；
4. `full-process-uml.md` 更新流程/序列图；plan 索引/approval 表补本计划行；复核单一权威、链接与判据完整性；
5. 旧 launcher/host 删除清单与回退（file:line）记录在 B4a/B5c。

## 8 现状差距（2026-11-02）
- 机制层已交付（G9–G13/B3b PASS/后端注册表统一）；剩余：B4-real 待授权、release 攻击流程、keepalive watchdog、Lua/动态注册技术债；详见 `annex-b5-closeout` 与 `backend-registry-unification`。
- backend 目录整理完成（统一 `backend/backends/` + `@UnfinishedBackend`；mismatch=0；438 tests；`backend-directory-cleanup`）。
