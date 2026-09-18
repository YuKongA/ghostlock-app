# CPP17 真机门禁：Multicast（别名收归构建）— PASS（panic 重启后 + 成功冷机）

- 构建：native `55863311e8e551df`（同 `CPP17-20260918-w1-panic` 的构建）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1

## 运行 1：panic 自动重启后（非冷机）

- 入口：Direct；`boot_ms=66132`
- 日志：`CPP17-20260918-multicast-pass.native.log`（228 行）
- 6/6 route `status=0 clean=1/1`；W1 页验收 `attempt=3 +10693ms`（其余 `attempt=1`）；
  `Write 1 complete` T+18458ms、`exploit complete` T+39135ms；`KernelSU ready`。

## 运行 2：成功冷机（门禁主证据）

- 入口：Direct；`boot_ms=127370`（冷启动后运行）
- 日志：`CPP17-20260918-multicast-pass-2.native.log`（210 行）
- 6/6 route `status=0 clean=1/1 step=0 errno=0`；`mcast ghost disarm`/`threads joined` 各 6 次。
- **零页重试**：6 次 `prepare_kernel_page ok attempt=1`（+2.2s~3.5s each）。
- `Write 1 complete` T+11277ms、`exploit complete` T+31264ms。
- `KernelSU module loaded`、`enforce=1 (enforcing)`、`KernelSU ready`；运行后 pstore 为空（无 panic）。

## 判定

- 别名收归构建同日经历一次 W1 panic（见 `CPP17-20260918-w1-panic.md`）后连续两次完整 PASS，
  其中运行 2 为成功冷机；
- **用户于 2026-09-18 豁免第二次冷机**（同 `CPP16`/`CPP12o`/`CPP12w` 先例）：本页与 panic 页
  作为完整证据，`55863311…` 登记为新 Multicast 基线。
