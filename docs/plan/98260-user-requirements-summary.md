# CVE-2026-98260 backend：用户需求整理（抽象功能 / 具象架构）

> 2026-10-28。来源：本会话用户消息（按时间归纳）。本件**只整理需求**，不代表已实现；已完成/差距见 §5。
> 权威序：`软件工程守则.md` > `AGENTS.md` > 设计原则/文档；需求冲突时以最新用户裁定为准（见 §0）。

## 0. 需求沿革与取代关系（重要）

| 早期要求 | 最新裁定（取代） |
|---|---|
| 98260 自己的 Shizuku UserService/AIDL | **Shizuku 启动器全 backend 共用**：唯一 `GhostlockUserService` + 公用 host/注册表/client，不允许 per-backend service/AIDL |
| 98260 自己的 victim launcher / finish 脚本 | victim 启动器上移 `core/victim` 公用；payload/finish 由项目 UI 统一生成，backend 只消费 |
| backend readiness 声明 Shizuku 状态/配置 | **backend 只声明 uid 需求，不自检**；UI 按「执行体种类 + Shizuku 授权二值」拒绝启动；不解析 uid |
| 43499 可迁 Shizuku 并跳过 W3 | **不迁移 43499、不改 W3**；攻击执行体二选一，App 支持 43499/43284，Shizuku 支持 98260 |
| 98260 payload 独立进程 | 目标：攻击在**选中执行体的 JVM 攻击线程**中由 Flow 入口启动（该迁移未完成，§5） |
| 静态 BackendSpec 列表 | **动态 BackendSpec**：运行时注册；未来每个 Lua 脚本一个 spec，可声明 ANY/SHELL 需求 |

## 1. 抽象功能需求（What / 原则）

- **A1 新增 backend 全流程**：只读研究现有架构与上游；L 级设计 + design-critic + 用户批准；实现 Kotlin backend（注册/Flow/UI）+ C payload/JNI（timer exploit 移植）+ profile/参数 + 测试/构建门禁；如实报告验证状态；未经要求不 commit/push。
- **A2 复用项目结构**：98260 必须适配公用 victim、log、payload、finish、flow runner 与 Shizuku 启动器；不为单 backend 自造平行基础设施。
- **A3 执行体唯一且由 UI 控制**：同一时刻只有一个攻击进程/执行体（App 或 Shizuku，二选一）；UI 选择决定启动位置；不允许两个攻击进程并存。
- **A4 攻击线程仍在 JVM**：攻击由 JVM 攻击线程执行（App 进程 JVM 或 Shizuku UserService JVM），不是另起 native 攻击载体；线程的**唯一入口是指定 Flow**。
- **A5 统一 symbol 指向选定 Flow**：lambda 不能跨 AIDL；App/Shizuku 的上一级都接收同一 **FlowSymbolInvocation(symbol + 可序列化 config)**；App=新起本地 JVM 线程解析执行；Shizuku=Shizuku 攻击线程解析执行同一 symbol；UI/main 不直接跑攻击、不直调 JNI。
- **A6 声明与检查分离**：backend **只声明**需求（尤其 uid/执行体需求），**不自检**；UI 按当前执行体检查并拒绝启动；用户经 Lua 等旁路绕过声明检查时不由 backend 兜底。
- **A7 授权是二值**：Shizuku 授权了就是可用、没授权就是不可用；UI **不读取/解析** Shizuku 实际 uid。
- **A8 动态 backend**：BackendSpec 需动态实现（运行时注册/注销）；lua 以后也有接口，每个 Lua 脚本可声明自己的需求（ANY 或仅 Shizuku）。
- **A9 payload/finish 统一 UI 生成**：项目 payload（root/cmd/lkm）与 finish 脚本由同一套 UI（`FinishConfig`/`FinishState`/`PayloadRequests`/`FinishScript`）生成；backend 不自建 finish 脚本/UI payload。
- **A10 payload 与 finish 由公用 victim 承担**：payload 由公用 victim helper 启动；finish 由 payload 内 rooted victim 执行；victim 启动器必须公用。
- **A11 安全与纪律**：触发后不 kill/timeout/reap payload；成功也 `must_reboot`；同 boot 单次 armed；未过真机 G0–Gx 不得标 supported/可用；debug-only profile/device-gate-pending 口径沿用。
- **A12 不迁移既有 backend**：43499/43284 保持 App 域语义与 W3；禁止“顺手迁移”。
- **A13 参数/偏移声明**：BackendSpec 必须声明 backend 需要的偏移/地址/尺寸/调优参数（只声明元数据，值仍由 profile 提供）；公共 ABI 项与 backend 私有项分开；Lua spec 可由脚本元数据声明自己的参数。

> **2026-11-02 沿革（执行通道被取代）**：A4（攻击在 JVM 线程）、A5 与 §2.1（`symbol=backend key`；App=`LocalThreadFlowExecutor`；Shizuku=pending）已被 [flow-process-launcher-plan.md](flow-process-launcher-plan.md) 取代（攻击一律启动新进程；UI 持久化 flow reflection symbol+参数；App `:flowhost` / Shizuku 进程启动器反射执行；symbol 只能指向 Flow 函数）。A1/A2/A3/A6/A7/A8–A13 继续有效。

## 2. 具象架构要求（How / 组件与契约）

### 2.1 执行体与 Flow 入口

- `ExecutorPreference.mode ∈ {App, Shizuku}` 是 UI 选择；攻击执行体二选一。
- 唯一启动链：
  ```text
  UI startAttack
    → BackendRunDispatcher.dispatch
    → FlowLaunchParent.launch(context, executorState, FlowSymbolInvocation(symbol, config))
        ├─ APP      : LocalThreadFlowExecutor → FlowSymbolResolver → Backends.byKey(symbol) → spec.runner(Flow)
        └─ SHIZUKU  : ShizukuHostFlowExecutor（同一 symbol 交给 host JVM；R2 待接；禁止静默回退 App）
  ```
- backend 不出现第二入口；不得从 UI/其他层直接调用 JNI/攻击原语。
- `FlowSymbolInvocation` 必须携带稳定 `symbol`（内置=backend key；Lua=`lua:<scriptId>`）与可序列化 `config`；**不传 lambda**；App/Shizuku 实现同一 `FlowLaunchExecutor` 接口（多态）。

### 2.2 需求声明与 UI 门禁

- `BackendSpec.uidRequirement ∈ {ANY_UID, APP_UID, SHELL_UID_2000}`：
  - 默认 `ANY_UID`（lua/43499/43284；App/Shizuku 攻击线程均可）；
  - 98260 = `SHELL_UID_2000`。
- UI 侧只解析 **执行体种类 + Shizuku 授权二值**（`pingBinder` + `checkSelfPermission`），不读 uid；
  - 声明不匹配 → `backend_run_uid_requirement`；
  - Shizuku 未授权/未运行 → `backend_run_executor_unavailable`；
  - backend/Flow 不自检执行体/uid。
- 98260 readiness 只声明**输入事实**（profile/payload/store/boot_id），不自检 Shizuku/uid。

### 2.3 动态 BackendSpec / Lua

- `BackendSpec.key`：稳定唯一键（内置默认 `cve`；动态 Lua 用 `lua:<scriptId>`）。
- `Backends`：Compose 可观察动态注册表（register/unregister/byKey/resetToBuiltins）；内置 43284/98260/43499/Custom 固定不可注销。
- `BackendSelection`：按稳定 key 选择（index 为兼容派生）；动态增删不丢选择。
- `BackendFlowRunner.key: String`（runner 身份动态化）。
- Lua 未来：脚本元数据 → 一个 BackendSpec（key/cve/title/rows/uidRequirement/runner）→ 注册；需求解析属 UI/Lua 加载层，不属 backend 自检。

### 2.4 公用组件（所有 backend 共享）

- **Shizuku 启动器**：唯一 `GhostlockUserService`（UserService 进程 JVM）+ 公用 `IGhostlockUserService`/`IGhostlockCallback` AIDL + `ShizukuBackendHost`/`ShizukuBackendHosts` 注册表 + `ShizukuBackendClient`；98260 仅 `Cve98260ShizukuHost`；无 per-backend AIDL/service。
- **victim**：`core/victim` 类型/能力 + `VictimSession` + 公用 `VictimScriptLauncher`；98260 只提供启动脚本内容/pid 探针。
- **log**：`LogFile`/`gl_log`/`log` 管线；host 日志 fd（attach/detach）；payload 行 NATIVE，fallback SHELL。
- **finish/payload**：UI `FinishState.toRootScript` → `FinishScriptStore` → `FinishHandoff`；backend 只消费脚本字节并做自己的验收证据合同；backend 不自建 finish 脚本。
- **flow runner/状态**：`BackendSpec.runner` 单一权威；`BackendRunDispatcher` 只调 runner；`BackendRunUiState` 中性四态；每次 run 新建 Flow。

### 2.5 包结构

- 98260 代码按层：`backend/cve98260/**`（flow/profile/markers/finish/payload/host/client/service）；app 包根不得再有 `com/ghostlock/app/cve98260` 散包；`data/cve98260` 属 data 层。
- 不允许 98260 专用 Shizuku service/AIDL/victim launcher 文件存在。

### 2.6 98260 后端具象约束

- **参数声明**：`BackendSpec.parameterSchema`（`BackendParameterSchema`）；98260 声明 30 个 profile 项 + 7 个待提升的 fake-cred 布局偏移（`COMPILE_CONSTANT`/`required=false`）；公共 ABI 项标 `COMMON_KERNEL_ABI`。

- profile：26 个数值 + 4 个字符串段跨端单一权威（`params.h` ↔ Kotlin 对拍），fail-closed；每构建内核事实由 profile 提供，禁止硬编码。
- exploit artifact（静态 payload）≠ UI finish payload；由公用 victim 启动，父子进程（victim/killer）按攻击原语需要创建。
- finish 由 rooted victim 执行 UI 生成的脚本；98260 仅保留 backend 验收证据（脚本 sha/exit/uid/ksud；SELinux 仅在 UI `restoreSelinux` 时要求）。
- 安全：不 kill/timeout/reap payload；park 持页；`must_reboot` 唯一权威 `BootSafetyStore`。

## 3. 明确非目标 / 不做

- 不迁移 43499 到 Shizuku、不跳过/删改 W3；43284 保持 App 域现状。
- 不允许两个攻击进程并存。
- 不把执行体/uid 检查放进 backend/Flow；不读 uid；不追 Lua 旁路。
- 不自建 per-backend Shizuku service/AIDL/victim launcher/finish 脚本。
- 未过真机不宣称 supported/可用；不 commit/push（除非用户明确要求）。

## 4. 验收口径（需求级）

1. UI 选 App：lua/43499/43284 可由 App 攻击线程启动；98260 被 UI 具名拒绝。
2. UI 选 Shizuku 且已授权：98260 可启动（目标：Flow 在 Shizuku JVM 攻击线程执行）；lua/43499/43284 也可由 Shizuku 攻击线程启动（ANY 需求）。
3. UI 选 Shizuku 未授权：所有 backend 都被 `executor_unavailable` 拒绝。
4. 所有启动都经唯一 Flow 入口；无旁路攻击调用。
5. 动态注册的 Lua spec 能声明 ANY/SHELL 并被 UI 按同一规则检查。
6. 未过真机门禁的 backend/路径标 pending，不伪造证据。

## 5. 当前实现差距（截至 2026-10-28）

| 需求 | 状态 |
|---|---|
| 公用 Shizuku host/AIDL/client | 已实现（AIDL 编译绿；真机 pending） |
| 公用 victim launcher / payload 由 victim 启动 / finish 由 rooted victim 执行 | 已实现（离线；真机 pending） |
| UI 按「执行体种类+授权二值」检查 uid 需求；backend 不自检；不读 uid | 已实现（本轮） |
| FlowLaunchParent + lambda + App 新 JVM 线程 | 已实现（App 路径） |
| Shizuku host JVM 攻击线程 + Flow 在 host 执行 | **未实现**（`ShizukuHostFlowExecutor` 具名 pending；R2） |
| 动态 BackendSpec 注册表/key/选择 | 已实现（无 Lua 加载器） |
| Lua 脚本 → 动态 spec + 需求声明 | **未实现**（接口/注册表就位） |
| 98260 attack 完全在选中执行体 JVM 内（取消独立 payload 父进程） | **未实现**（当前独立 static payload；需 R2 设计/真机） |
| 43499/43284 由 Shizuku 攻击线程启动（ANY 需求） | 需求已声明；Shizuku host Flow 未接，暂不可用 |
| 离线全量门禁 | 被并发外部写者的在途 43499 文件/重复构建产物阻塞，未跑通 |

## 6. 指针

- R1 计划/进度：`docs/plan/98260-project-structure-adaptation-plan.md`、`docs/analysis/98260-r1-progress-20261028.md`
- 动态 backend：`docs/analysis/98260-dynamic-backends-20261028.md`
- 43499 旧 conf → 98260 复用分类：`docs/plan/98260-43499-conf-reuse-classification.md`
- 执行结构：`docs/plan/execution-architecture.md` §10
- 审查记录：`docs/analysis/architecture-review-log.md`
