# B3 真机门禁：NativeOrchestrator 组件目录化（multicast_waiter，direct）— PASS

对应代码：Batch 3 工作树（`NativeOrchestrator` = `route/orchestrator.hpp` + `route/component_catalog.hpp`），
基于提交 `6dc288e`（Batch 2 v3 wire）。

## 设备与入口

- 型号 A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：App 进程直接执行 native（direct），`uid=10528 untrusted_app`，`attr=u:r:untrusted_app:s0`，
  `enforce=unreadable` → 代码按 enforcing 处理并执行 W1。
- KernelSU：**未加载**（本次 native 日志无 `KernelSU ready`；门禁要求的干净启动条件）。
- 固定 CPU 对：`main=3 consumer=4`（`selected_cpus` 与运行标签一致）。
- route / middleware：`multicast_waiter` 单组合。

## 结果

- 多次 `[route] route_done status=0 clean=1/1 step=0 errno=0 calls=1 success=1`；
  其间一次 `status=2 clean=1/1 step=61 errno=99`（重试后继续），无 `panic`/`Oops`。
- `child is root!`；`[T+30069ms] exploit complete`。
- 本机 `/sys/fs/pstore` 无 `dmesg-ramoops-*`（无 panic 现场），仅有旧的 `pmsg-ramoops-0`。

## 日志

- 设备：`Download/GhostLock/20260923-224923/ghostlock-direct-0.log.txt`
- 同目录 `kernel-info.txt`（uname/cmdline：`rcu_nocbs=0-7`，8 核）。

## 变更说明

- 本批新增 header-only `NativeOrchestrator` 与组件 catalog（frontend/backend/middleware ID、可用性、
  selection 校验）；`main.cpp` 经其选择后交给既有 `ExploitProcedure`；`route_policy.hpp` 注释标明为
  middleware catalog；`session/exploit_session.hpp` 文档化组件所有权（字段不变）。
- 攻击路径不变：`cmp_disasm` 对 Batch 3 前基线为 7 函数 IDENTICAL + `do_one_write` 既有 LAYOUT-SHIFT，
  RESULT PASS。

## 附：同批次早期非确定观察（不作为本 gate 失败）

- 同一工作树在更早的 direct 两次运行出现 panic，pstore `dmesg-ramoops-0`：
  `pc = rt_mutex_adjust_prio_chain` via `rt_mutex_adjust_pi` → `__sched_setscheduler` →
  `__do_sys_sched_setattr`（`Comm: libghostlock.so`）。这是 exploit PI/rt_mutex 原语的非确定风险
  （`KERNEL-PANIC-01` 类），与 Batch 1–3 无因果证据（攻击函数机器码未变、同构建有成功记录、
  KSU 激活下 direct 亦成功）。
- 一次 Shizuku 失败为 log-and-continue：W1b `SYSCHK(sched_setaffinity): Invalid argument` +
  `SYSCHK(open): Permission denied`，随后 `native exited code=255`（非 panic）。
