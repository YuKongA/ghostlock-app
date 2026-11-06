# 归档说明

- 原路径：`docs/plan/flow-process-launcher-annex-b4a.md`
- 归档日期：2026-11-04 22:24:59 -0500
- 归档原因：completed/current-fact plan superseded by root execution authority；用户于 2026-11-04 批准 Backend root execution D1–D9 与 R0→R1 收口，本文件不再作为活动实施 authority。
- 来源：`dev-nocpp` 当前工作树；正文原样保留。现行唯一后继为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# Flow 进程启动器计划 · 附录 B4a：host 注入参数与生产 Flow 接入（v1）

> 状态：**附录（2026-11-02）；归属主计划 `flow-process-launcher-plan.md` v5（已生效）§2.3/§2.4 的 B4a 细化**。未改变主计划 D1–D7；只补「生产 Flow 如何 context-free 运行」的契约。B4a 写码前由 design-critic 复核本附录。
> 背景：B1a/B2a/B2b 已证明 `:flowhost` 通道；B3a 已实现 Shizuku flow 分支（B3b 设备待 Shizuku）。生产 43499-Glued Flow 当前依赖 `Context`（profile assets、nativeLibraryDir、filesDir），不能直接在 host 进程复用。

## 1 决策：host 注入参数（采用方案 b）

主计划 §2.3 要求参数化；本附录把 host 运行时事实作为**保留前缀 `host.`** 注入 params，Flow 入口只读 params，不接收 `Context`：

| 保留键 | 类型 | 含义/来源 |
|---|---|---|
| `host.uid` / `host.pid` | Int | host 进程身份（`android.os.Process`） |
| `host.domain` | String | SELinux domain（读 `/proc/self/attr/current`，去 NUL/空白） |
| `host.run_dir` | String | host 为本 run 新建的目录（app: app filesDir 下；shell: `/data/local/tmp/ghostlock-flow/<runId>`），0700 |
| `host.native_lib_dir` | String | host 进程可见的 nativeLibraryDir（app host=applicationInfo.nativeLibraryDir；shell host=package context 可解析时；不可解析 ⇒ 具名失败） |
| `host.log_path` | String | run 日志文件路径（在 run_dir 下） |

规则：
- 调用方（UI/launcher）**不得**传 `host.*`：launcher 侧拒绝 `flow-params:reserved-key:<key>`；host 在 decode 后**覆盖**注入，调用方值不生效。
- host 在 prepare 时创建 run_dir（不存在/不可写 ⇒ `flow-host:run-dir:*`），终态后 best-effort 清理（证据先写 verify，路径留在 `evidence.host_run_dir`）。
- `FlowHostEnv` 接口**不增加** `Context`（主计划 §2.2 的 shell 无有效 app Context 约束不变）；需要 app 资源的 Flow 必须把这些资源放进 params（如 profile 字节）。 **B4b 例外（2026-11-02）：仅 43284Glued 使用 host-local `FlowHostEnv.context`（见 `annex-b4b.md` v2）；43499 永不使用。**

## 2 43499-Glued 迁移契约

- params（UI 侧由 dispatcher 构造，UI 进程有 Context）：`profile.conf`(String，源 HOCON)、`profile.bin`(ByteArray，GLK1)、`profile.release`(String)；由现有 `Glued43499ProfileCodec.resolve(context, profileReference)` 得到。
- entry：`@FlowEntry fun entry(env: FlowHostEnv): FlowResult`：
  1) 从 env.params 取三键（缺失 ⇒ `flow-entry:43499:params`）；
  2) 在 `host.run_dir` 写 `profile.conf`/`profile.bin`（0600）；
  3) `ProcessBuilder(host.native_lib_dir/lib43499-Glued.so, "--ghostlock-app-call")`，stdin=profile.bin，env=`GHOSTLOCK_HOME=host.run_dir`、`TMPDIR=host.run_dir`、`GHOSTLOCK_KSU_LOG=host.log_path`；
  4) stdout/stderr 逐行 → `env.log`；`process.waitFor()`；终态 evidence：`backend=43499-Glued`、`exit=<code>`、`profile=<release>`、`host_run_dir`；
  5) exit!=0 ⇒ `FlowResult.Failed("exec-exit:<n>")`；启动失败 ⇒ `flow-entry:43499:start:*`。
- 不得在 entry 内读 `BackendCve43499Glued` 全局/profileReference（host 进程无 UI 状态）；不得 fallback 到旧 runner。

## 3 迁移期单权威（B4a）

- `BackendSpec.flowSymbol: FlowSymbol?`（默认 null）；`flowParams(context): Map<String,Any?>`（默认空）供 dispatcher 构造参数。
- `runCapability = FLOW_RUN` 当 `runner != null || flowSymbol != null`。
- `BackendCve43499Glued`：`flowSymbol = FlowSymbol("com.ghostlock.app.backend.cve43499glued.Flow43499GluedEntry","entry")`，`runner = null`（旧路径不可达）；其它 backend（43284Glued/98260/原版）本批不动。
- dispatcher（`BackendRunGate`）：`spec.flowSymbol != null` ⇒ 新通道 `FlowLaunchParent.launchSymbol(context, executor, spec.flowSymbol, spec.flowParams(context), onLine)`；APP→`AppProcessFlowLauncher`、SHIZUKU→`ShizukuProcessFlowLauncher`；否则走既有 `local runner` 旧通道（迁移期，B5c 删）。
- UI 状态：launcher 返回 `BackendRunUiState`；`HandoffRequested`/`Finished`/`Refused` 原样映射，无二次判定。

## 4 门禁（B4a）

- JVM（无设备）：`FlowEntryParamsTest`（保留键拒绝/覆盖语义/预算）、`Glued43499ExecParamsTest`（参数编码/解码、缺键具名失败）。
- 编译：`:app:compileDebugKotlin`、`:app:testDebugUnitTest`（定向）、`:app:assembleDebug :app:assembleDebugAndroidTest`。
- 设备（B4a-b，A301SO）：**no-op fixture APK**（沿用 `docs/exec-glue/transaction-safe-exec-fixture/MODIFIED_FILE/lib43499-Glued.so` 标记 fixture；替换当前 APK 两个 Glued ELF 后重签）→ `Flow43499GluedHostSmokeTest` 断言：gate allowed、`Finished.Ok`、输出含 `fixture=ghostlock-exec-noop-v1`、host pid≠app pid、两次 run 新 host pid、`evidence.host_run_dir` 存在；**不运行真实漏洞 ELF**。真实攻击冷机门禁属 B4b，另行批准。
- 报告归档 `docs/analysis/device-gates/`；所有真机项单列已验证/未验证。

## 5 未决/风险

- shell host 的 `nativeLibDir` 可解析性未验证（B3b）；不可解析时 43499-Glued 在 Shizuku flavor 下须具名拒绝（不得回退 app flavor）。
- 替换 ELF 的 fixture APK 需重新签名，设备上会与生产 APK 同 versionCode 替换；门禁后恢复生产 APK（记录两者 sha256）。
- `host.*` 键注入后仍受 FlowHostParams 预算（≤256 KiB）约束；profile 文本+GLK1 远小于预算。
