# CPP12b 真机门禁：Multicast（PI-TIMEOUT-01 复验）— kernel panic

- 构建：APK 223；native `6bd2291acca483861664d0d945c098e45e7dfadf26a9680fa5b75d110b6c08c6`
- 入口：Direct；`enforce=unreadable`（按 enforcing 处理并执行 W1）
- bootreason：`kernel_panic,null`（设备自动重启）
- 日志：`CPP12-20260917b-multicast-kernel-panic.native.log`（83 行，panic 时截断）
- 结论：FAIL（间歇性）；**与 PI-TIMEOUT-01 改动无因果路径**

## panic 位置

- 最后一行：`[T+10203ms] heap spray done`（W1 前第一次 spray 完成），此后日志截断。
- 无任何 `route_done` 行、无 `[route] creating waiter/owner/consumer` → panic 发生在**第一条 route 之前**。
- 无 `route_done wait timed out` → PI-TIMEOUT-01 的超时分支从未执行。

## 与历史 panic 对比（同一模式）

| 证据 | CPP06c | CPP07 | 本次 CPP12b |
|---|---|---|---|
| 阶段 | KernelSnitch/spray | spray 重试后 | spray done 后 |
| 日志尾部 | spray 阶段截断 | `prepare_kernel_page retry 1/4` 后截断 | `retry 1/4` → `retry 2/4` → `ok attempt=3` → `heap spray done` 后截断 |
| bootreason | kernel_panic | kernel_panic | `kernel_panic,null` |
| 可归因版本 | 与当轮一致 | 与当轮一致 | 与当轮一致 |

## 本次运行的特殊点

- `prepare_kernel_page` 连续 2 次因 `page ... stores an even byte over selinux_state.initialized` 重试，第 3 次成功（`attempt=3 +9968ms`）。
- 基线 `CPP12-20260917-multicast-pass` 的 4 次 spray 均为 `ok attempt=1`，无重试。
- 上轮门禁时设备 SELinux 为 permissive（`enforce=0`）；本次为 `enforce=unreadable`。**待观察**：页面命中 `selinux_state` 附近与重试是否与设备内核状态（enforcing/permissive、`selinux_state.initialized` 值）相关。

## 结论与后续

- PI-TIMEOUT-01 的代码路径（`run()` 超时分支、`join()` detach）在本次运行中完全未执行；panic 不能归因于该改动。
- PI-TIMEOUT-01 的门禁目标（正常路径行为不变）尚未获得证据，需要重跑 Multicast 直到一次完整 route 流程。
- 设备已自动重启（跳过 reboot），直接重跑；若再次在同一 spray 阶段 panic，按间歇性内核崩溃记录，并评估 `prepare_kernel_page` 重试路径的加固（独立登记项）。
