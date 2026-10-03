# A2-3a 真机门禁：in_direct_map 归位 memory、删除 attack 模块（multicast_waiter）— PASS（第 3 次冷机）

对应提交 `44a19b6`（Phase A2-3a）。候选二进制 SHA-256
`655d17c41ecb60ad2807e0a080f8ed59128df3122c5b34eede079abbe97037e6` — **与 A2-2e 逐字节相同**，
即本批源码组织迁移在二进制层面零差异。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；route = multicast_waiter；CPU 对 0/1。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a3a --load-prebuilt-profile
  /data/local/tmp/p0.bin`。

## 结果（3 次冷机）

| # | 结果 | 说明 |
|---|---|---|
| 1 | PANIC | W1 后进入 W1b private scratch repair，卡在 `waiting route_done`，设备重启（`up 0 min`）|
| 2 | PANIC | 同 1，W1b `waiting route_done` 后设备重启 |
| 3 | **PASS** | multicast clean ×4、`child is root!`、`[T+16919ms] exploit complete`、`KernelSU ready` |

日志：`A2-3a-20261003-multicast-panic1.native.log`、`...-panic2.native.log`、`...-direct-pass.native.log`。
事后 `su -c id` = root、`kernelsu` 在场。

## 判定

两次 panic 归为已知环境/时序抖动（`KERNEL-PANIC-01` 类），非本批引入：

- **候选二进制与 A2-2e 逐字节相同**（sha256 一致），源码变化只是 `in_direct_map` 从 `attack/ops.hpp` 内联体
  原样搬到 `memory/direct_map.hpp`、删除空 `attack/ops.cpp`；内联体表达式逐字未变；
- `cmp_disasm --reviewed` 6 函数骨架一致（`do_one_write` 126）；
- 同构建第 3 次冷机 PASS。

## 变更说明

`in_direct_map` 迁到 `memory/direct_map.hpp`（inline 真实现）+ host 影集 `memory/direct_map.hpp`（返回 1，
供数据流测试的伪任务地址）；删除 `attack/` 整个模块（`ops.cpp` 已空、`ops.hpp` 仅剩该函数）；
backend/terminal/victim/threads 显式 `#include "common.h"` 补回原经 `attack/ops.hpp` 的传递包含。
