# Flow 进程启动器计划 · 附录 B4b v2：host-local Context 与 43284 runtime 生命周期

> 状态：**v2（2026-11-02），已按 design-critic task-147（G8：2 阻断/4 重要）修订**；归属主计划 v5 的 B4b 细化。主计划 §2.2 已加指针（B4b amendment）。
> 授权沿革：用户 goal「实现新进程攻击机制」（2026-11-02）授权实现新进程通道；本附录只对 **43284Glued** 引入 `FlowHostEnv.context`（host-local）作为 §2.2 的例外，未改 D1–D7 其余项。回退点：撤销 context 字段与 43284 entry，恢复 `Backend43284Glued.runner`（B5c 前仍可回退）；43499 永远 context-free。
> 依据：主计划 v5、`annex-b4a`、B4a 设备报告 `device-gates/20261102-b4a-43499-flowhost-fixture.md`、ADR-0022、A11。

## A1 Context 契约（host-local）

- `FlowHostEnv` 追加 `val context: Context?`：**host 进程本地对象**，不由 launcher 跨 Binder 传入、不持久化、不写日志、不写文件；只传给 registry 校验后的 Flow 入口，且入口不得 close/invalidate。
- app `:flowhost`（`FlowHostService.environment()`）：`context = this`（有效 app Context）。
- Shizuku host（`GhostlockUserService.flowEnvironment()`）：`context = userServiceContext`（Shizuku package context；API<13/不可用时 null）。
- 43284 entry：`env.context ?: FlowResult.Failed("flow-host:context-required")`，不 fallback；43499 entry 不读 context（保持 context-free）。
- 测试：43284 null-context 具名失败且不创建 runtime；43499 在 null context 下正常解码/执行路径不依赖 context；context 不进入日志/evidence。

## A2 43284 runtime 生命周期（A11 资源保全）

- 正常路径：`AndroidIpSecRuntime.create(context)` → exec ELF → 有界等待（ELF 已退出）→ `runtime.close()` → evidence `ipsec=runtime-closed` → `FlowResult.Ok`（或 exec-exit 语义）。
- close 失败：evidence `ipsec=runtime-close-failed`，`FlowResult.Failed("flow-entry:43284:ipsec-close:<detail>")`。
- **超时/取消（ELF 可能仍存活，A11 不 kill）**：**不得 close runtime**；evidence `ipsec=runtime-leaked`、`host_keepalive=true`；返回 `Failed("flow-entry:43284:timeout|cancelled")`。
- host 终端处理：verify 含 `host_keepalive=true` ⇒ **跳过** `stopSelf()`/`killProcess()`/run_dir 清理（host 进程保留以持有 runtime 与日志目录；只完成 terminal 回调）；正常终态行为不变。
- evidence 只允许 `ipsec=created|closed|close-failed|leaked` 状态（正常终态 `runtime-closed`）；超时/取消不得写 `ipsec=ok`。
- KDoc/报告明确：所有未关闭路径是 **A11 有意保留**，不是资源泄漏 bug；B4b-b fixture 不触发该路径，B5 真实攻击门禁需覆盖。

## A3 HostProcessExec 共享（43499 行为不变）

- 抽 `backend/core/HostProcessExec.kt`：有界 `waitFor` 轮询 + `env.isCancelled()` + `redirectErrorStream(true)` + 先起读线程 + stdin 独立 writer 后 close + env 注入；超时/取消**不 kill/destroy**（A11）；返回具名结果。
- `Flow43499GluedEntry` 改用该工具后必须保持：默认 timeout 600s/clamp 1h、不 destroy、daemon reader/writer、join 上限、evidence 键（backend/exit/profile/host_run_dir）与失败前缀不变。
- 回归：B4a JVM（FlowEntryParamsTest/Glued43499ExecParamsTest/BackendFlowSelectionTest/FlowHostProtocolTest/core.flow.*）全绿；Lead 重跑 B4a fixture 设备门禁（B4a 报告回归）。
- `BackendFlowSelectionTest`：43499/43284 均 `runner==null && flowSymbol!=null && FLOW_RUN`；其它 backend 旧 runner 不受影响。

## A4 B4b-b 设备门禁（fixture no-op，不跑真实 ELF）

- fixture APK：替换两个 Glued ELF 为 `fixture=ghostlock-exec-noop-v1` 且保留 `resources.arsc` Stored，重签；记录 fixture/生产 APK 双 sha256，门禁后恢复生产 APK 并跑 B2/B4a 回归。
- `Flow43284GluedHostSmokeTest` 断言：gate allowed、Executed、`Finished.Ok`、输出含 `fixture=ghostlock-exec-noop-v1`、`backend=43284Glued`、**`argv_count=8`**（4 个 IPsec option/value 对存在）、`evidence["ipsec"]=="runtime-closed"`、host pid≠app、两次 run 新 host pid、host_run_dir 非空。
- 记录 `:flowhost` 进程内 `AndroidIpSecRuntime.create/close` 成功/失败（IpSecManager 真实权限）；失败即门禁 FAIL，不跳过。
- Shizuku flavor：`userServiceContext` 的 IpSec 可用性属 B3b；不可用 ⇒ `flow-host:context-required`/`exec:ipsec:setup:*` fail-closed，不 fallback。

## A5 未验证/风险

- `:flowhost` 内 IpSecManager 权限（待 B4b-b 实测）；fixture 的 no-op ELF 不消费 SA/transform，真实 ELF 的资源时序属 B5 真实攻击门。
- 超时/取消 + `host_keepalive` 的 host 进程回收策略（保留进程）需 B5 门禁验证；未覆盖前不得用于真实攻击。
- 43284 Shizuku flavor 需 B3b 环境（A301SO 无 Shizuku）。
- `HostProcessExec` 抽取后 B4a 设备回归必须 PASS，否则回退抽取。
