# CPP07 Multicast 真机门禁（PayloadPage move-only）

- APK：`versionCode=201`（安装 20:32:54），提交 `43531d9`，native `cfeda33bb…`
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 入口：Direct；CPU main=0、consumer=1；开机约 4 分钟，系统负载偏高
- 温度：日志不可判定
- 结论：通过

## 验证要点

- 6 次 route 全部 `route_done status=0 clean=1/1`；
- 6 次均为 `prepare_kernel_page ok attempt=1`，无页面验收重试；
- `child is root`、`child seccomp filter bypassed`、`KernelSU ready`、`enforce=1`。

## Heap 时序对比

| 指标 | CPP04-06 基线 | CPP06d | CPP07（本次） |
|---|---|---|---|
| collision | 1928–2149ms | 1991–2665ms | 1997–4013ms |
| payload ready | 2002–2263ms | ≈2.0s | 2053–4045ms |
| prepare 重试 | W1 首轮 1 次 | 0 | 0 |

本次设备负载偏高，绝对耗时略长但量级一致；`PayloadPage` move-only 重构未改变 Heap 分配/释放顺序与成功路径行为。

原始日志：[`CPP07-20260917-multicast-pass.native.log`](CPP07-20260917-multicast-pass.native.log)
