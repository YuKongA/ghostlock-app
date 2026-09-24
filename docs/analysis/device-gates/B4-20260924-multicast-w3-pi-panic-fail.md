# B4 真机门禁：frontend contract / known-id decode（multicast_waiter，direct）— 非确定（PASS 与 panic 混合）

对应候选 `build/native/ghostlock`（Batch 4，提交 `b0069bd`）SHA-256
`fcbc2191693b3a9eda41e8f07e5bf9958b210c90ffba89a6bda34439f5f5613d`。
**注意**：设备日志本身不记录候选 SHA，候选一致性依赖外部构建/安装记录。

## 设备与入口

- 型号 A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：direct（`Comm: libghostlock.so`）；route = multicast_waiter；KernelSU 初始未加载。
- 条件提醒：本页两次可读运行**使用了不同的 CPU 对**，不是固定 CPU 对的受控对照。

## 运行证据（拆分，避免混用）

| 运行 | 目录 | CPU 对 | 结果 |
|---|---|---|---|
| A | `20260924-085457/ghostlock-direct-0.log.txt` | `main=5 consumer=6` | 日志止于 `W2: cred` route 的 `waiting route_done` |
| B | `20260924-090931/ghostlock-direct-0.log.txt` | `main=0 consumer=1` | **完整 PASS**：W2/W3 route `route_done success=1` → `[T+30360ms] exploit complete` → `handoff: child=... sent=1` → `KernelSU ready` |
| C | `20260924-084136`（direct-1）、`-084127`、`-084004` | 未知 | 日志为空/NUL；阶段未知，**不作为阶段证据** |

运行 A 对应的 pstore `/sys/fs/pstore/dmesg-ramoops-0`（2026-09-24 08:55）：

```
FSC = 0x0f: level 3 permission fault   (WnR = 1)
pc : rt_mutex_adjust_prio_chain+0x984/0x14c0
Call trace:
  rt_mutex_adjust_prio_chain
  rt_mutex_adjust_pi
  __sched_setscheduler
  __do_sys_sched_setattr / __arm64_sys_sched_setattr
Comm: libghostlock.so
```

## 判定

- 同一候选记录下观察到不同结果（A 中断、B PASS），但**两次运行的 CPU 对不同（`5/6` vs `0/1`），并非固定 CPU 对的受控 PASS/panic 对照**；日志也不含候选 SHA。
- 现象与既有非确定风险一致（PI 链 `rt_mutex_adjust_prio_chain` via `sched_setattr`），但**尚未完成同条件复现**，因此不能断言“确认 `KERNEL-PANIC-01`”，也不能归因 Batch 4 源码回归（未改 W3/PI/清理，`cmp_disasm` PASS）。
- 门禁结果：**非确定**（无稳定 PASS）；**Batch 4 不算完成**。
- 后续：固定 CPU 对、同构建、冷机、多次复跑统计；并在独立“生命周期/取消”批次处理 race 无 deadline 与 PI 链可终止性之前，不宣称稳定支持。

## 日志

- 设备：`Download/GhostLock/20260924-085457`（A）、`-090931`（B）、`-084136`/`-084127`/`-084004`（C）。
- 内核：`/sys/fs/pstore/dmesg-ramoops-0`（08:55，运行 A）。
