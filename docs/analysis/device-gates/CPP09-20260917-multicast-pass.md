# CPP09 真机门禁：Multicast one-shot（Direct 入口）

- 日期：2026-09-17 21:46（设备本地时间）
- 设备：QV770MFGJ1（kernel `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）
- 构建：APK `GhostLock-v1.1(214)`；native `19f9b5146e18a5ef85839fc9aa3fbabd43bfb6a1a18580136e8f54dafb33b141`
- 入口：Direct（uid=10510 untrusted_app；SELinux enforce 不可读，按 enforcing 处理并执行 W1）
- 日志：`CPP09-20260917-multicast-pass.native.log`（194 行）
- 结论：**PASS**，与 `CPP08-20260917-direct-pass` 行为等价

## 结果摘要

| 指标 | CPP08 direct-pass | CPP09 本次 |
|---|---|---|
| route 尝试 | 6 | 6 |
| route 结果 | 6/6 `status=0 clean=1/1 step=0 errno=0 calls=1 success=1` | 完全相同 |
| Heap spray | 5 | 5 |
| mcast ghost disarm | 6 | 6 |
| 线程收尾 | （旧实现无显式日志） | 6× `[route] threads joined`（`PiRace::join()`） |
| W 序列 | W1 1/1 → W1b 1/3 → W2 1/15 → W2b → W3 1/6 | 相同 |
| 交接 | `script open fd=3 errno=0`、`KernelSU ready`、`enforce=1` | 相同 |
| 总耗时 | `exploit complete` T+28188ms | T+34809ms（+6.6s） |

## 耗时差异归因

- 差异全部来自 KernelSnitch collision 搜索：基线 6 次为 1.9–2.0s，本次 2.1–3.2s（累计约 +6.6s）。
- route 次数、clean/disarmed、step/errno、calls/success、disarm 次数与 W 序列均与基线一致；无 prepare 重试、无 fallback、无 panic、无自动重启。
- 归因于设备状态（reboot 后首次运行）造成的自然波动，列为后续门禁观察项：若下次门禁仍稳定在 ~3s，需要单独分析 collision 搜索路径。

## CPP09 覆盖点

- `PiRace` 类化后线程生命周期由 `PthreadOwner` 管理，6 次运行全部正常 stop/join。
- `run()` 的 counts 合并路径生效：`route_done` 行后无 `PI race thread creation failed` 警告，`run_main_route_threads()` 每次返回 routed。
- 无 `waiter parked; owner started` 缺失、无线程创建失败、无 dirty failure。
