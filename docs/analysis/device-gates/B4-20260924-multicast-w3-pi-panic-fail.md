# B4 真机门禁：frontend contract / known-id decode（multicast_waiter，direct）— FAIL

对应候选 `build/native/ghostlock`（Batch 4，提交 `b0069bd`）SHA-256
`fcbc2191693b3a9eda41e8f07e5bf9958b210c90ffba89a6bda34439f5f5613d`。

## 设备与入口

- 型号 A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：App 进程直接执行 native（direct）；`Comm: libghostlock.so`。
- KernelSU：未加载（重启后 `su` 不可用）；冷机；route = multicast_waiter。

## 结果

连续多次在 PI route 阶段 panic：

- `20260924-090931/ghostlock-direct-0.log.txt`：`W3-0: leaf dir` route success 后，
  `W3: TIF_SECCOMP` `heap spray start` 后中断。
- `20260924-085457/ghostlock-direct-0.log.txt`：`W2: cred` route `waiting route_done` 后中断。
- `20260924-084136`（direct-1）、`20260924-084127`、`20260924-084004`：日志为空（panic 早期，
  与既往 direct 全 NUL 现象一致）。

pstore `/sys/fs/pstore/dmesg-ramoops-0`（2026-09-24 08:55，对应 `085457`）：

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

## 结论

- **门禁 FAIL（本候选）**，不能沿用旧 PASS 覆盖。
- **不是已证实的 Batch 4 源码回归**：Batch 4 未改 W3、PI race 或其资源清理；`cmp_disasm` 对
  Batch 3.1 候选 `5dcd8ddd…` 为 7 函数 IDENTICAL + `do_one_write` 既有 LAYOUT-SHIFT，RESULT PASS。
- 与 `B3-20260923-multicast-direct-pass.md` 记录的 panic **同族**（`rt_mutex_adjust_prio_chain` via
  `sched_setattr`），属既有的间歇性 PI 链风险；本次连续发生，升级为本候选的明确失败。
- 根因待新日志，并应放入独立的“生命周期/取消”批次处理（race 无 deadline 仍未闭环）。

## 日志

- 设备：`Download/GhostLock/20260924-090931`、`-085457`、`-084136`、`-084127`、`-084004`。
- 内核：`/sys/fs/pstore/dmesg-ramoops-0`（08:55）。
