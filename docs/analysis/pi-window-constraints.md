# PI 窗口约束（实证版）

> 分析类（≤8 KiB）。**自包含**。本文是 **PI 窗口内「能做什么 / 不能做什么」的唯一权威**：以 device-gate 实测记录为准，取代原先由 ADR 推下的理论条款（见 §4 沿革）。
> 配套：`docs/development/design-review-principles.md` **E4**（同批改为指向本文）、`docs/plan/MASTER-PLAN.md` §7 R-6、`docs/analysis/handoff-payload-verification.md`。

## 1. 证据链（A）

**结论（一句话）**：实测 PI 窗口死因**不是**内存分配，也不是间接分派，而是**窗口内发起的 `sched_setattr`（改 nice / SCHED_BATCH）进入了内核 PI 链**（`rt_mutex_adjust_pi`），在 waiter 已停泊的窗口里踩到被竞争/回收的内核对象 ⇒ Oops ⇒ panic。

### 1.1 两次独立 panic：调用栈逐帧相同

| 帧 | 符号 | 证据 |
|---|---|---|
| 0 | `Internal error: Oops: 0000000096000005 [#1] PREEMPT SMP`（level 1 translation fault） | `docs/archive/device-gates/20260922-2231-CPP12x-20260918-w1-panic.dmesg.log:11` |
| 1 | `pc : rt_mutex_adjust_prio_chain+0x1b0/0x14c0` | 同上 `:23` |
| 2 | `rt_mutex_adjust_prio_chain+0x1b0` → `rt_mutex_adjust_pi+0x174/0x318` | 同上 `:37-38` |
| 3 | `__sched_setscheduler+0xdf8/0x1144` | 同上 `:39` |
| 4 | `__do_sys_sched_setattr+0x244/0x3c8` ⇒ `el0_svc` | 同上 `:40` |
| 尾 | `Kernel panic - not syncing: Oops: Fatal exception` | 同文件末段 |

- **第二份独立记录**：`docs/archive/device-gates/20260922-2231-CPP12s-20260918-w1-panic.dmesg-ramoops-0.log`（同一栈；另见 `-ramoops-1.log`）。
- 两份记录**互相独立**（不同批次/不同 ramoops 槽），栈帧一致 ⇒ 机制性归因，而非单次偶发。

### 1.2 native 侧在**窗口内**发同一类 syscall

- `core/src/support/util.cpp:76-88`：`sched_setattr_tid()` 直接 `syscall(274, tid, &attr, 0)`，策略 `SCHED_BATCH`，**源码注释原文**：`nice change triggers PI walk (pi=true)`。
- 调用点 `core/src/race/threads.cpp:144`：在 **consumer 循环**内调用 —— 此时 waiter 已停泊（`FUTEX_WAIT_REQUEUE_PI` 已提交），**正处于 PI 窗口**。

⇒ 「窗口内不许做什么」不再是从 ADR 推下的原则，而是**有实测栈支撑的条款**。

### 1.3 窗口内**没有**堆分配（排除该归因）

- 实测：`core/src/race/**`、`core/src/backend/cve_2026_43499/route/**`、`core/src/backend/cve_2026_43499/spray.cpp` 中 `std::vector` / `std::string` / `new ` / `malloc` 命中 **0**；窗口内只使用栈上 `std::array` 与非拥有 `string_view`。
- 日志中**没有**任何以分配为致因的证据（栈只指向 `rt_mutex_adjust_pi` 链）。

⇒ 保留「禁分配」为**防御性条款**（防回归），但**不作为本次死因**归因。

### 1.4 归因纪律（与 `KERNEL-PANIC-01` 并存）

- `KERNEL-PANIC-01` 是既有的环境/时序 flake（同构建可 PASS/panic/PASS）。它约束的是**判定因果的方法**（同构建复现 + 冷机复跑），**不否定**本节的机制证据：栈帧一致 + native 侧确有同 syscall 的窗口内调用点。

## 2. 窗口内 syscall 白名单（B，允许）

| 类别 | 允许 | 证据（raw 行号） |
|---|---|---|
| **futex（PI 三类）** | `FUTEX_LOCK_PI` / `FUTEX_WAIT_REQUEUE_PI` / `FUTEX_UNLOCK_PI` | `core/src/race/threads.cpp:37`、`:61`、`:75`、`:79`、`:88`、`:96` |
| 时钟 | `clock_gettime` | `core/src/race/**` 3 处（超时计算） |
| 睡眠 | `usleep`（有界、非 PI 路径） | `core/src/race/**` 8 处 |
| 既有 route 的 socket 族 | `socket` / `setsockopt` / `send` | socket 4 处、setsockopt 4 处、send 1 处（route 目录） |
| 既有 route 的 mmap 族 | `mmap` / `memfd_create` | mmap 2 处、memfd 2 处（route 目录） |
| 既有 route 的等待/管道 | `pselect` / `pipe` / `fcntl` / `dup2` | pselect 18 处、pipe 4 处、fcntl 3 处、dup2 5 处（route 目录） |

**白名单的解释规则（重要）**：白名单是**穷举式**的 —— 不在表内的 syscall 若要进入窗口，必须**先补本节 + 一条 device-gate 记录**（或在评审中证明其**不可能**进入 `rt_mutex_adjust_pi` / PI 链）。

## 3. 明确禁止（C）

1. **禁 `sched_setattr`**（任何 policy / nice）——**实测死因**：`__do_sys_sched_setattr` → `__sched_setscheduler` → `rt_mutex_adjust_pi` → `rt_mutex_adjust_prio_chain`（§1.1 栈）。
2. **禁 `setpriority` / `nice` 及任何会进入 `rt_mutex_adjust_pi` 的调用**：它们与 (1) 同类（都改任务优先级/权重并触发 PI 调整）。**新增此类调用 = 设计评审否决项**。
3. **禁堆分配**（防御性保留；§1.3 已排除为本次致因，但分配可能间接进入 PI 链或引入不确定性）。
4. **禁间接分派**（虚接口/函数指针）：窗口内只允许具体类型 + 静态调用。
5. **禁首次触碰新页**（首次 `mmap` 后写入、缺页异常路径）。

**可执行判据（黑盒/白盒 + 极端值）**：

- 白盒：窗口 TU 的文件级扫描（禁 include 清单）+ `nm` 无 `_ZTV*`（见 `docs/analysis/handoff-payload-queue-schema.md` §4）；
- 白盒：静态检查窗口内出现 `SYS_sched_setattr` / `setpriority` / `nice` 的**任何**调用点 ⇒ 失败；
- 黑盒：真机门禁（冷机、固定 CPU 对、单 route）—— 窗口内引入被禁调用时，`dmesg` 栈应复现 §1.1 形态；
- 极端值：waiter 存活期最长/最短（nice 轮转边界 `(calls%19)+1`，`threads.cpp:141-144`）各跑一遍。

## 4. 与旧条款的关系（D，沿革）

- **旧 E4（2026-10-07 版）**：「**PI 窗口内禁间接分派/分配/首次触碰新页**；窗口外允许虚接口」——依据 ADR-0001:183、ADR-0004:188-189、`route_lifecycle.hpp:9-11`。
- **问题**：该表述是从 ADR **推下**的理论条款，**覆盖不到实测死因**（`sched_setattr` 既不是分配，也不是间接分派）。
- **本文件**：以 device-gate 记录（§1）为准，把「窗口内可调用面」写成**白名单 + 禁项**；E4 同批改写为指向本文（保留原措辞一行作沿革）。

## 5. 未决 / 待证（E）

| # | 未决项 | 现状 | 处置 |
|---|---|---|---|
| U-1 | `CPP12y` / `CPP17` 批次是否同栈 | **未逐一核** | 需要时按 §1.1 的 grep 方法补核 |
| U-2 | route 侧是否有**其它**路径间接触发 PI 链 | **未核** | 白名单落定后，对 route 目录全量 syscall 面做一次分类核对 |
| U-3 | `gettid` / `nanosleep` 等零命中项是否允许 | 当前**未用**（0 命中） | 若要用，按 §2 解释规则补证 |
| U-4 | 白名单的机器化守卫（静态扫描） | 判据已写（§3），**尚未接线** | 归 B0.1 批次（层表/扫描同批） |
