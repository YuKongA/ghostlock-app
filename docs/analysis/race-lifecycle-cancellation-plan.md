# Race 生命周期/取消加固 计划（2026-09-23，只读调查 + 设计，评审修订 R1）

> 面向 `B3`/`B4` 门禁中观测到的 PI 链 panic 与 `PiRace::run()` 无 deadline（`TODO(pi-timeout-01)`）。
> **本文件只做调查与设计，不含实现**；实现属攻击关键路径（`cmp_disasm` + 真机门禁）。
> 本版本按评审 R1 修订：修正基线提交、收敛因果措辞、拆分“超时/停止/disarm/回收”、
> 删除 S2、S3 更名、列出安全检查点与不可中断点。

## 现状与基线

- 分支 `very-not-stable-dev`；**设计提交 `bbae6d2`**（调查提交 `a9d9cbf`）；代码基线仍以攻击路径未变前的
  Batch 3.1 候选为准。
- 设备证据见 `docs/analysis/device-gates/B4-20260924-multicast-w3-pi-panic-fail.md`：运行 A（CPU `5/6`）
  在 `W2` route 中断、pstore 有 PI 链 Oops；运行 B（CPU `0/1`）完整 PASS。**CPU 对不同、日志不含候选 SHA**
  → 非受控对照；门禁记“非确定”。
- 因果措辞（收敛）：`pstore` 栈明确经过 `__do_sys_sched_setattr`，**优先指向 `race/threads.cpp:134` 的
  `sched_setattr_tid`**；`multicast_waiter_route.cpp:40` 的 `sched_setscheduler` 是另一条候选路径，
  当前这份栈**没有直接证明**它是触发点。观测**与 PI 链风险一致**，但**根因及各因素贡献未确定**，
  不表述为“固有风险”，也不断言“非本批引入”。

## 关键：超时检测 / 线程停止 / PI 链 disarm / 状态回收 必须分开定义

“超时后返回”若不同时定义 stop、disarm 与回收，会把永久等待换成更危险的并发访问。四者分别定义：

1. **超时检测**：判定某个等待越界（deadline）。
2. **取消意图**：通知参与者应当停止（标志），但**标志不等于可取消**。
3. **PI 链 disarm**：清理由漏洞原语留下的 dangling PI 状态（multicast 的 ghost disarm 是**必做步骤**）。
4. **状态回收**：仅在确认无访问者后清理/复用共享状态（futex、`request` 指针、session）。

## 根本限制与终态语义

- deadline 只表示“**检测到越界**”，**不证明**阻塞中的参与者已停止，也**不证明** PI 状态已解除或资源可回收。
- 因此本设计**不承诺“有界结束”或“安全恢复”**；在获得证明之前，终态只能表达为**停止继续执行（halt）**，
  不得宣称“清理完成 / 资源已回收 / 可安全复用”。
- 终态升级条件（须**同时**满足才可改称“已清理 / 可重试”）：参与者已停止访问 PI/race 状态、`waiter` 的
  ghost disarm 已执行、`futex`/`request`/session 已无访问者。任一未证，终态保持 **halt**。

## 安全检查点与不可中断点（逐个列出）

| 参与者 | 等待/阻塞点 | 有界？ | 停止检查 | 退出前的必做动作 |
|---|---|---|---|---|
| `waiter_thread`（`threads.cpp:18`） | `owner_started` 自旋（27） | 否 | 无 | — |
| | `FUTEX_WAIT_REQUEUE_PI`（48，timeout 来自 `race_route_wait_ms`） | 是 | 有（timed 返回） | 返回后**必须**执行 `controller.execute` 与 `route_needs_ghost_disarm` 的 ghost disarm（56-67），**不得**因取消跳过 disarm |
| | `route_done.store` + `chain_futex UNLOCK_PI`（68-69） | — | — | 是 disarm 与上报的责任者 |
| | `owner_chain_done` 等待（70） | 否 | 无 | — |
| `owner_thread`（75） | `FUTEX_LOCK_PI(target_futex)`（78） | **否（无超时）** | 无 | 停止时若已持有则 `UNLOCK_PI`（84-88/93-94） |
| | `waiter_ready` 自旋（81） | 否 | 有 `owner_stop` | — |
| | `chain_futex LOCK_PI`（90） | 否 | 无 | — |
| `consumer_thread`（98） | `sched_setattr_tid`（134） | 否（syscall） | 循环有 `consumer_stop`，但 syscall 内不可取消 | — |
| `PiRace::run()`（171） | `route_done` 轮询（189） | **否** | 无 | 超时须定义终态（见 D3） |
| `run_main_route_threads`（203） | `join()`（219） | 否 | 无 | 当前忽略 join 错误并无条件清 `request` |

结论：`waiter` 的 disarm 不可跳过；`owner`/`chain_futex` 的 PI futex 无超时，是**不可中断 syscall 边界**；
`consumer` 可能停在 `sched_setattr`；`run` 的 `route_done`、`owner_chain_done` 无 deadline。

## 决策（评审 R1 结论）

- **D1 = profile 配置**：新增独立 `execution.race.route_done_timeout_ms`（不复用 `race_route_wait_ms`，
  后者已控制 waiter 的 `FUTEX_WAIT_REQUEUE_PI`）。**deadline 覆盖范围须写明**：至少 `route_done`
  轮询与 `owner_chain_done` 等待；`owner` 的无超时 PI futex 需单独策略（见 D2）。
- **D2 = 分阶段取消意图，但不承诺 syscall 可取消**：
  - 定义“取消意图”标志与检查点，而非直接“waiter stop flag”；
  - `waiter`：timed futex 返回后仍**必须完成 route 与 ghost disarm**，再据此决定终态；
  - `owner`/`chain_futex` 的 PI futex 无超时 → 属不可中断边界，须给出超时后的处置（不得假装可取消）；
  - `consumer` 可能停在 `sched_setattr`，同样不可承诺即时取消。
- **D3 = armed/状态不明一律 terminal-stop**：一旦 PI 窗口已进入或状态不明，**停止本次运行**——
  不重试、不切 route、不继续 W2/W3 chain rounds。只有能证明“尚未 armed、参与者已 join、资源 clean”
  的失败才可重试。**必须完整传播状态**：`run_main_route_threads()` 现把 `RouteStatus` 压成 `bool`
  （`threads.cpp:203`、`return status.code == ROUTE_OK`），`retry_write_stage()` 在 `!routed` 时 `continue`
  （`exploit_procedure.cpp:92-98`）；仅设置 `ROUTE_DIRTY_FAILURE` 不构成终止语义，需要让 dirty 一路上报并
  在此停止。
- **D4 = 不可变旧基线 + 同条件门禁**：使用**改动前已确认的精确二进制**（如 `/private/tmp/ghostlock-batch3-base`）
  作 `cmp_disasm` 基线，不用新实现重置基线来消除差异。除 8 个攻击函数外，还须反汇编核对本批触碰的
  **等待、join、disarm、reset 及资源准备/回收顺序**。真机固定同一 CPU 对与启动条件；**单次 PASS 不关闭风险**。
- **D5 = 独立共享 race 生命周期批次**：该路径被所有 middleware 共享，不只属于某个 frontend/backend，
  所有调用它的 middleware 都要覆盖验证。新组件不得据此宣称取消已验证；但不必把它们的接口设计塞进本批。

## 方案边界

- **主方案 S1（deadline 检测 + 取消意图 + halt 终态）**：由 profile 提供 deadline；超时置 dirty 并
  **halt（停止继续执行）**。`waiter` 的 disarm 能否完成、`owner` 的不可中断 futex 如何处置、join 失败
  如何上报，均须显式定义；**不得**在未证明“参与者已停止 / PI 已 disarm / 资源无访问者”时宣称清理完成。
  只有“未 armed 且 clean（全部前提已证）”才允许重试。
- ~~**S2（超时后 detach 并继续运行）——不批准**~~：`start_threads()` 把 `WriteRequest` 以**借用指针**交给
  线程（`pi_race.cpp:37` 保存 `request`），调用者的 request 是栈对象；detach 后调用者可能返回/重试/复用
  session，线程仍访问旧对象与 futex。且 `join()` 忽略 join 错误并无条件清空 `request`，**不能**作为
  “线程已停”的证明。
- **S3 = fail-stop / 紧急隔离（保留但更名）**：终止进程**不等于**证明内核 PI 状态已 disarm；它**不允许
  恢复执行**，也**不承诺**内核状态安全回收。仅作为不可恢复情况的紧急终止，不作为生命周期证明。

## 影响文件（拟，待实现）

| 文件 | 动作 |
|---|---|
| `src/core/race/threads.cpp`（`PiRace::run`、`run_main_route_threads`、waiter/owner/consumer） | deadline、取消意图检查点、必做 disarm、join 错误处理、完整状态传播 |
| `src/core/race/pi_race.{h,cpp}` | 取消/超时状态；清理顺序与 `reset()` 前置条件 |
| `src/core/route/exploit_procedure.cpp`（`retry_write_stage`） | dirty/terminal 时不重试 |
| `src/core/profile/model.h`、`binary.*` + Kotlin DTO | `execution.race.route_done_timeout_ms`（跨层契约） |
| `src/core/session/exploit_session.hpp` | 实现后把 race 终结点标记为已闭环 |
| `src/core/tests/**` | 生命周期/取消/clean 前无访问者；超时→terminal-stop 路径 |

## 验证矩阵（拟）

| 项 | 命令 | 预期 |
|---|---|---|
| host 单测 | `make -C src native-host-tests` | 有界等待、超时→terminal-stop、clean 前无访问者、dirty 传播 |
| 反汇编 | `cmp_disasm <改动前精确基线> build/native/ghostlock` | 8 函数 + 本批触碰的等待/join/disarm/reset/回收顺序经逐条核对 |
| 构建/静态 | `make -B -C src ghostlock`、`lint-tidy` | 零告警、0 findings |
| 真机门禁 | 固定 CPU 对、冷机、KernelSU 未加载、multicast、多次 | 每次结果归档；单次 PASS 不关闭风险 |

## 明确保留

- 漏洞原语算法、payload、W1/W2/W3、内存布局（除本专项明确批准）。
- 不新增 PI 窗口内间接调用、不新增可变全局、不引入虚基类 provider。

## 进度

- [x] 只读调查 `PiRace` 生命周期、waiter/owner/consumer 等待点、`run_main_route_threads` 停止/join、
      `retry_write_stage` 重试，确认 `TODO(pi-timeout-01)`。
- [x] 产出设计（R1）：四者分离、检查点清单、D1–D5 结论、S2 删除、S3 更名。
- [x] 依评审结论补充“根本限制与终态语义”：deadline ≠ 停止；未证明即 **halt**，不宣称“有界结束/安全恢复/
  清理完成”。
- [ ] 评审批准 R1 后的 D1–D5 收敛版本。
- [ ] 实现（另批，按攻击关键路径门槛）。
