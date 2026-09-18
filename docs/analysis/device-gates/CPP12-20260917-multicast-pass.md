# CPP12 真机门禁：Multicast one-shot（Direct 入口，SELinux permissive 路径）

- 日期：2026-09-17（设备本地时间）
- 设备：QV770MFGJ1（kernel `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）
- 构建：APK `GhostLock-v1.1(221)`；native `a5b2d151343f38229c61726056434c2cbaadc3e7372a574a6c72c3f10b44ba4c`
- 入口：Direct（uid=10510 untrusted_app；`enforce=0`，`SELinux already permissive`）
- 日志：`CPP12-20260917-multicast-pass.native.log`（146 行）
- 结论：**PASS**；覆盖 W2→W3 主链与 `SESSION-01`/`SESSION-03` 等价性

## 与基线差异（环境导致，非回归）

本次启动时设备 SELinux 已为 permissive，按设计跳过 W1/W1b：

| 指标 | CPP09（assuming enforcing） | 本次（already permissive） |
|---|---|---|
| route 尝试 | 6（W1/W1b/W2/W2b/W3/W3b） | 4（W2/W2b/W3/W3b） |
| route 结果 | 6/6 `OK clean=1/1 step=0 errno=0 calls=1 success=1` | 4/4 完全相同 |
| KernelSnitch collision | 1.9–3.2s | 1.9–2.0s（回到基线区间） |
| 总耗时 | `exploit complete` T+34809ms | T+19274ms（少 2 个 route，约 -8s，吻合） |
| Heap spray | 5 | 3（少 W1/W1b 两轮） |
| handoff | `script open fd=3 errno=0`、`KernelSU ready` | 相同 |
| seccomp | `child seccomp fully bypassed` | 相同 |
| panic/自动重启 | 无 | 无 |

## 覆盖点

- `CORE` 宏与 `runtime_config_snapshot()`（`SESSION-01`/`SESSION-03`）在 W2/W3 全链执行无偏差。
- 每次 route 的 `threads joined`、`mcast ghost disarm`、`calls/success` 与基线一致；无 prepare 重试、无 fallback。
- `enforce=1 (enforcing)`（KernelSU 交接后恢复 enforcing）与基线一致。

## 未覆盖

- W1/W1b（SELinux route 仅在 enforcing/不可读时执行）本轮未跑；其机制与 W2/W3 相同（同一 PiRace/Heap/payload 通道），待下一次 enforcing 启动时复验。
