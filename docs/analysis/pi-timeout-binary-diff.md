# PI-TIMEOUT-01 二进制差异分析（反汇编级）

- 工具：NDK `llvm-nm --print-size --demangle` + `llvm-objdump -d --no-show-raw-insn`，对比前做地址/标签归一化
- 对象：
  - **A** = `6bd2291acca483861664d0d945c098e45e7dfadf26a9680fa5b75d110b6c08c6`：含 PI-TIMEOUT-01（panic 版本；由 `bd9a57f` worktree 重建，哈希与原构建一致）
  - **B** = `a5b2d151343f38229c61726056434c2cbaadc3e7372a574a6c72c3f10b44ba4c`：完全回退后的基线（隔离复跑 PASS 版本）

## 结论摘要

| 维度 | 结果 |
|---|---|
| 符号集合 | 完全相同（995 个） |
| 函数体逐指令对比（归一化地址） | **388 个完全一致；0 个同大小函数体变化** |
| 函数大小变化 | **仅 1 个：`run_main_route_threads` +228B** |
| 攻击关键函数 | `waiter_thread`、`owner_thread`、`consumer_thread`、`do_one_write`、`do_kernel5_fake_lock_route`、`do_pselect_fake_lock_route`、`run_exploit`、`multicast_waiter_worker`、`multicast_owner_worker`、KernelSnitch 各 pass —— **逐指令一致** |
| `.text` | A 比 B 大 416B（228B 为真实新增，其余为段对齐） |
| 数据段 | 330 个数据符号**一致位移 +448B**（相对布局不变） |

## `run_main_route_threads` 差异内容（唯一真实代码差异）

- 新增 `clock_gettime(CLOCK_MONOTONIC)`、`ucvtf/fmul`（`× 10.0` double 换算）、时间差 `madd` 纳秒换算、`fcmp/b.lt` 超时判定与 dirty 返回分支。
- `PiRace::run()`/`join()` 因 LTO 内联并入该函数；差异与 `bd9a57f` 的源码 diff 一一对应（仅超时分支 + join detach）。

## 归因

1. **逻辑面**：PI-TIMEOUT-01 没有改变任何攻击关键函数；超时分支在 panic 窗口（首条 route 之前）不可能执行。
2. **布局面**：该改动使 `.text` +416B、数据段整体 +448B；`run_main_route_threads` 之后的函数与全局数据地址整体位移。数据符号为**一致位移**，相对布局不变，因此影响面主要是代码地址/I-cache 对齐，而非数据相对布局。
3. **设备对照**（同一设备状态 `enforce=unreadable`、W1 路径）：
   - 无 TI 版（B）：`CPP12-20260917d-multicast-pass`，6/6 route、无重试（1/1 pass）；
   - 含 TI 版（A）：`CPP12-20260917b`/`-c` 两次 kernel panic（均在首条 route 前）。
   - 样本小，但结合“该攻击对时序/对齐高度敏感”的历史（CPP06c、CPP07 间歇 panic），**不能排除布局位移的相关性**。

## 建议

- PI-TIMEOUT-01 的逻辑保留，但实现应**最小化布局扰动**后再门禁：
  1. 用 `route_status.step == 62` 判断超时，删除新增的 `run_timed_out` 字段（消除 bss/字段位移）；
  2. 把计时与判定移入 `[[gnu::noinline]]` 辅助函数，把 `run_main_route_threads` 的膨胀从 228B 降到调用点级别；
  3. 先验证回退基线稳定（已 PASS），再引入 v2 并对比二进制位移后复跑门禁。
- 或者：延后至 CPP14 收尾统一实施（届时一次性承担布局变化并做多次门禁）。
