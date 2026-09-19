# CPP12d 真机门禁：Multicast（PI-TIMEOUT-01 隔离复跑）— PASS

- 构建：APK 226；native `a5b2d151343f38229c61726056434c2cbaadc3e7372a574a6c72c3f10b44ba4c`
  （与 `CPP12-20260917-multicast-pass` 的二进制逐字节一致；已回退 `bd9a57f` 的改动）
- 入口：Direct；`enforce=unreadable`（完整 W1→W3 路径）
- 日志：`CPP12-20260917d-multicast-pass.native.log`（190 行）
- 结论：**PASS**

## 结果

- 6/6 route `OK clean=1/1 step=0 errno=0 calls=1 success=1`（W1、W1b、W2、W2b、W3、W3b）。
- 全部 `prepare_kernel_page ok attempt=1`（无重试）。
- `exploit complete` T+28340ms、`KernelSU ready`、`enforce=1 (enforcing)`、无 panic。

## 对照（同一设备状态 `enforce=unreadable`）

| 运行 | 二进制 | 结果 |
|---|---|---|
| CPP12d（本次） | `a5b2d151…`（无 PI-TIMEOUT） | PASS：6/6 route、无重试 |
| CPP12b | `6bd2291a…`（含 PI-TIMEOUT） | kernel panic（首条 route 前，`null`） |
| CPP12c | `6bd2291a…`（含 PI-TIMEOUT） | kernel panic（首条 route 前，`bug`） |

隔离实验结论：同一设备、同一 W1 路径下，回退后的二进制通过、含 PI-TIMEOUT 的二进制连续两次 panic；
`bd9a57f` 的代码在 panic 窗口中未执行（首条 route 前），因此归因指向**二进制层面的差异
（代码布局/时序）**，需反汇编对比进一步定位（见 `pi-timeout-binary-diff.md`）。
