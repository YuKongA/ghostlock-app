# Race 生命周期/取消加固 计划（2026-09-23，只读调查 + 设计）

> 面向 `B4`/`B3` 门禁中反复出现的 PI 链 panic（`rt_mutex_adjust_prio_chain` via `sched_setattr`）
> 与 `PiRace::run()` 无 deadline（`TODO(pi-timeout-01)`）。
> **本文件只做调查与设计，不含实现**；实现属攻击关键路径，须 `cmp_disasm` + 真机门禁。

## 现状与基线

- 分支 `very-not-stable-dev`；HEAD `a9d9cbf`；设备证据见
  `docs/analysis/device-gates/B4-20260924-multicast-w3-pi-panic-fail.md`。
- `PiRace` 生命周期（`race/pi_race.cpp`、`race/threads.cpp`）：
  - `reset()` 清状态并置 `route_status = ROUTE_RETRYABLE`；
  - `start_threads()` 依次启 consumer/owner/waiter；
  - `PiRace::run()`：等 `waiter_waiting && owner_started` → settle → `FUTEX_CMP_REQUEUE_PI` →
    `while (!route_done.load()) usleep(poll)`（**无 deadline**）→ `outcome_with_counters`；
  - `run_main_route_threads()`：`run()` 后 `request_stop()` + `join()`。
- 现有 `TODO(pi-timeout-01)`（`threads.cpp:184-188`）已写明：该等待无 deadline；route 在竞态窗口卡住时
  进程永久停驻、被破坏的 PI 链永不 disarm；建议从 `profile::TargetProfile.execution` 取上界，超时映射到
  `ROUTE_DIRTY_FAILURE` 而非无限循环。
- 触发 panic 的两处 `sched_setattr/sched_setscheduler`：
  - `threads.cpp:134` consumer 提 nice（`support::sched_setattr_tid`，6.1 compact 用 `(calls%19)+1`）；
  - `multicast_waiter_route.cpp:40` multicast `SYS_sched_setscheduler`（SCHED_NORMAL↔BATCH）。
  两者都会走内核 `__sched_setscheduler → rt_mutex_adjust_pi → rt_mutex_adjust_prio_chain`。当 PI 链因漏洞
  原语处于暂态损坏时，该调用触发 Oops（pstore：`rt_mutex_adjust_prio_chain+0x984`，`WnR=1`）。
- 结论：panic 是 **PI 原语的固有暂态风险**（race 窗口内链被破坏），并非某批前端/配置代码引入；
  但“无 deadline 的等待 + 永不到达的 stop/join”会放大后果（进程停驻、状态不回收）。

## 目标与范围

### 目标

1. 为 race 等待引入**有界终止**与**明确终态**，避免永久停驻。
2. 终止路径上，**每个参与者先停止访问共享 PI/race 状态**，再回收/转移资源；不得因过早退出制造悬空。
3. 超时/失败有明确的可诊断结果，交由上层决定重试或停止。

### 非目标 / 明确警告

- **“给 `route_done` 加超时”本身不是修复**：超时后若不证明参与者已停止访问 PI/race 状态，
  释放或复用会制造新的悬空状态；本设计把“停止访问 → 回收”作为验收前提。
- 不改 PI 原语算法、payload、W1/W2/W3 时序与内存布局（除经单独批准）。
- 不在 PI 竞争窗口内引入间接调用；不新增可变全局。

## 关键问题（调查结论）

1. **不可中断等待**：`PiRace::run()` 轮询 `route_done`，超时后主控可离开，但 waiter/owner/consumer
   可能仍阻塞在 `futex`（`FUTEX_LOCK_PI`/`FUTEX_WAIT`）或自旋中；`join()` 会继续挂。
2. **停止机制**：`request_stop()` 只置 `consumer_stop/owner_stop` 并清 `consumer_go`；waiter 是否响应
   停止需核实（waiter 未持有 stop 标志）。因此“置标志 + join”在当前实现并不保证可终止。
3. **清理顺序**：`join()` 后 `reset()` 置 `target_futex/wait_futex = 0`；若线程仍在访问这些 futex，
   重置即悬空。清理必须晚于“确认无访问者”。

## 候选方案（待评审，不实现）

- **S1 有界等待 + 可证明的停止**：由 profile 提供 wait 上界；超时置 dirty 终态，并让所有等待点变为
  有界（轮询带 deadline），保证 waiter/owner/consumer 都能在有限时间内退出循环；再 `request_stop()` +
  `join()`，然后才 `reset()`。代价：触及 waiter/owner/consumer 循环形状 → 改攻击函数机器码。
- **S2 超时后放弃 join（detach + dirty）**：超时后不 join，标记 `ROUTE_DIRTY_FAILURE` 并把线程
  detach，进程继续/退出；避免挂死，但保留“线程仍在访问 PI/race 状态”的悬空风险，只作为最后兜底，
  且必须保证该进程随后不再复用这些 futex（不复用即无新悬空）。
- **S3 独立看门狗**：watchdog 在超时后直接终止进程（让内核回收），只在“进程中止可接受”时使用。

推荐方向：**S1 为主，S2 为兜底**；S2/S3 需明确“不再复用共享状态”的上层保证。

## 待决点（需评审）

- **D1 超时来源**：新增 `execution.race.route_done_timeout_ms`（profile）还是 runtime option？
- **D2 停止语义**：是否为 waiter 也引入 stop 标志并核实其等待可中断（S1）？还是接受 S2 兜底？
- **D3 dirty 终态**：`ROUTE_DIRTY_FAILURE` 的上层处理是“重试一次”还是“停止并报错”？是否需要区分
  “可重试的超时”与“链可能已损坏的 panic 前置状态”？
- **D4 验证策略**：改攻击函数机器码后如何建立新反汇编基线（`do_one_write`/`run_main_route_threads` 等）；
  真机门禁如何固定 CPU 对、冷机、多次以统计。
- **D5 与 Batch 4/5 的关系**：在组件所有权接口稳定前不宣称生命周期已闭环；新增组件不得据此声称
  “取消路径已验证”。

## 影响文件（拟，待 D1–D3 定）

| 文件 | 动作 |
|---|---|
| `src/core/race/threads.cpp`（`PiRace::run`、`run_main_route_threads`） | 有界等待 + 明确终态 + 保证可停止后再 join |
| `src/core/race/pi_race.{h,cpp}` | 停止标志/超时状态；清理顺序与 reset 前置条件 |
| `src/core/profile/model.h`/`binary.*` + Kotlin DTO | 若 D1 选 profile 字段，新增超时键（跨层契约） |
| `src/core/session/exploit_session.hpp` | 更新 race 终结点为“已闭环”（待实现后） |
| `src/core/tests/**` | 生命周期/取消/清理测试；超时路径模拟 |

## 验证矩阵（拟）

| 项 | 命令 | 预期 |
|---|---|---|
| host 单测 | `make -C src native-host-tests` | 有界等待、超时→dirty、join 前无访问者 |
| 反汇编 | `cmp_disasm <new-baseline> build/native/ghostlock` | 差异经逐条复核（本批预期会改形状，需重建基线） |
| 构建/静态 | `make -B -C src ghostlock`、`lint-tidy` | 零告警、0 findings |
| 真机门禁 | 固定 CPU 对、冷机、KernelSU 未加载、multicast、多次 | panic 率与终态行为；归档 |

## 明确保留

- 漏洞原语算法、payload、W1/W2/W3、内存布局（除本专项明确批准）。
- 不新增 PI 窗口内间接调用、不新增可变全局、不引入虚基类 provider。

## 进度

- [x] 只读调查 `PiRace` 生命周期、`run_main_route_threads` 停止/join 顺序、`sched_setattr` 触发点与
      pstore 证据；确认 `TODO(pi-timeout-01)` 已存在。
- [x] 产出本设计（含“超时≠修复”的警告与 S1/S2/S3 候选）。
- [ ] 评审 D1–D5。
- [ ] 实现（另批，按攻击关键路径门槛）。
