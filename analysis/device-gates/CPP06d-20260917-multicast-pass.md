# CPP06d Multicast 复跑（CPP06c panic 后）

- APK：`versionCode=198`（与 `git rev-list --count HEAD`/`e57d9e5` 一致，native 同 `b6245955…`）；`lastUpdateTime=2026-09-17 20:28:46`
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 入口：Direct；CPU main=0、consumer=1；panic 重启后 uptime ≈10 分钟
- 温度：日志不可判定
- 结论：通过

## 验证要点

- 6 次 route 全部 `route_done status=0 clean=1/1`；
- KernelSnitch：collision `1991–2665ms`、leak 约 `2.0s`，与 CPP04-06 基线一致；
- W1–W3、`child is root`、`seccomp filter bypassed`、`KernelSU ready` 全部完成。

## 对 CPP06c 的结论

同一 native、同一设备、同一入口下本次无异常完成，确认 CPP06c 的 kernel panic 为一次性随机事件。
panic 根因仍不可判定（无 `dmesg`/pstore 权限）；若再次出现需要 root 环境定位。

原始日志：[`CPP06d-20260917-multicast-pass.native.log`](CPP06d-20260917-multicast-pass.native.log)
