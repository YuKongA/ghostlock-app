# CPP12c 真机门禁：Multicast（PI-TIMEOUT-01 复验）— kernel panic（第 2 次）

- 构建：APK 223；native `6bd2291acca483861664d0d945c098e45e7dfadf26a9680fa5b75d110b6c08c6`
- 入口：Direct；`enforce=unreadable`（W1/SELinux 路径）
- bootreason：`kernel_panic,bug`
- 日志：`CPP12-20260917c-multicast-kernel-panic.native.log`（64 行，panic 时截断）
- 结论：FAIL（间歇性，第 2 次）；超时分支不可能执行

## panic 位置

- 最后两行：`prepare_kernel_page ok attempt=1 +2172ms` → `[T+2454ms] heap spray done`，此后截断。
- 无 `[route] creating waiter/owner/consumer`、无 `route_done`、无 `route_done wait timed out`。
- `PiRace::run()` 的超时阈值是 10s，而 panic 发生在 T+2.5s 附近，PI-TIMEOUT-01 分支在本次运行中不可能执行。

## 与 CPP12b 对比（位置一致）

| 证据 | CPP12b | CPP12c（本次） |
|---|---|---|
| 日志最后 | `[T+10203ms] heap spray done` | `[T+2454ms] heap spray done` |
| prepare retry | 2 次（selinux_state 冲突） | 无（attempt=1） |
| bootreason | `kernel_panic,null` | `kernel_panic,bug` |
| route 日志 | 无 | 无 |
| enforce | unreadable（W1） | unreadable（W1） |

## 归因与风险

- 两次 panic 都落在**第一次 W1 route 的启动窗口**（`heap spray done` 后、首条 route 日志前）；日志缓冲可能吞掉未 sync 的行，因此无法区分“route 创建前”与“route 已建立、等待/竞态中”。
- 同一窗口在 PI-TIMEOUT-01 之前也发生过 panic（CPP06c、CPP07 各一次，根因不可判定）。
- 逻辑上 PI-TIMEOUT-01 的超时分支不可能执行；但 `run()` 的轮询循环体新增了每个 poll 周期的 `clock_gettime` + `runtime_elapsed_ms` 调用，在 1ms 轮询下引入极小的时序差异，在竞态窗口下不能仅凭推理排除影响。
- 连续 2 次 panic 使“正常路径行为不变”的门禁证据无法取得。

## 建议：隔离验证

1. 临时回退 `run()` 的等待循环改动（`join()` 的 detach 逻辑是否保留视构建成本决定），重跑 Multicast；
2. 若 pass：PI-TIMEOUT-01 的引入需要重新设计（例如每 N 次轮询才读时钟、或把计时移到轮询外），再复验；
3. 若仍 panic：与该改动无关（设备/W1 状态），恢复改动并等待更稳定的复跑窗口。
