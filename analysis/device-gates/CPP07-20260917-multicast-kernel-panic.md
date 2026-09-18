# CPP07 失败记录（第二次内核 panic）

- APK：`versionCode=201`（native `cfeda33b…`）；执行 20:34:05，失败 20:34:58
- 重启原因：`kernel_panic`（`ro.boot.bootreason` 与 `persist.sys.boot.reason.history` 时间戳 `1789691698`）
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 入口：Direct；CPU main=0、consumer=1；开机约 3 分钟，系统负载高
- 温度：日志不可判定
- 结论：失败（内核 panic）

## 时间线

- 20:34:05 执行开始；W1 首轮 spray 成功但 page 被 `selinux_state.initialized` 验收拒绝（预期行为），`prepare_kernel_page retry 1/4`；
- 第二次 spray：`early collision screen 16/7 at 31%` 后日志中断，无任何 route 记录；
- 20:34:58 设备因 `kernel_panic` 重启。

## 与 CPP06c 的对照

两次 panic 的断点一致：**W1 首轮页面验收拒绝后的重试 spray，且都在 KernelSnitch 碰撞筛选阶段**。

- CPP06c：native `b6245955…`（CPP07 改动前），20:18:27 panic；
- 本次：native `cfeda33b…`，20:34:58 panic；
- 两者都各伴随至少一次成功运行（CPP06d、本次 20:39 的 pass），因此 panic 与 native 版本及 CPP07 改动无关，呈间歇性；
- 两次失败前的共同环境：设备刚重启不久、系统负载高（本次 `load average` 20.67，CPP06c 为 7.39）。

## 判定

根因不可判定：`dmesg`、`/sys/fs/pstore`、`/proc/last_kmsg` 均无权限，且无 tombstone（非用户态崩溃）。
症状一致地指向"高负载下 W1 重试 spray 的 KernelSnitch 阶段"，但不能从现有证据归因；若后续复现需 root 环境抓内核日志。

原始日志：[`CPP07-20260917-multicast-kernel-panic.native.log`](CPP07-20260917-multicast-kernel-panic.native.log)
