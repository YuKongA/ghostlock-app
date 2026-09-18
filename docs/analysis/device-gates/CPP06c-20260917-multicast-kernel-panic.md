# CPP06c Multicast 失败记录（内核 panic）

- APK：`versionCode=198`（与 `git rev-list --count HEAD` 及 `ebb2c71` 一致），`lastUpdateTime=2026-09-17 20:17:17`
- 提交：`ebb2c71`（CPP02 收尾）；native 与 `CPP06b` 成功证据为同一构建（`b6245955…`）
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 入口：Direct；CPU main=0、consumer=1；`boot_ms=118596`（重启后约 2 分钟，load average 7.39）
- 温度：日志不可判定
- 结论：失败（内核 panic，设备自动重启）

## 时间线

- 20:17:17 安装 198；
- 20:17:24 执行开始；resolved profile、地址转换与启动诊断正常；
- W1 第一次 spray 成功，但 page 被 `selinux_state.initialized` 页面验收拒绝（预期行为），`prepare_kernel_page retry 1/4`；
- 第二次 spray：`[spray] finding futex collisions... +1062ms` 后日志中断，无任何 route 执行记录；
- 20:18:27 设备因 `kernel_panic` 重启（`ro.boot.bootreason`、`sys.boot.reason`、`persist.sys.boot.reason.history` 三处一致）。

## 根因判定

- `ro.boot.bootreason=kernel_panic,null` 确证内核崩溃，非用户态 crash（`/data/tombstones` 无新记录）；
- `dmesg`、`/sys/fs/pstore`、`/proc/last_kmsg` 均无权限或不存在，**崩溃栈不可判定**；
- panic 发生在 KernelSnitch 碰撞搜索阶段；该阶段只做 futex 计时与 mmap，正常路径不应触发内核崩溃；
- 同一 native（`b6245955…`）在 20:13 的 196 执行中完整通过，且 CPP02 收尾为**零二进制变化**，因此该失败与代码版本差异无关；
- 是否为设备刚重启后的高负载时序或内核既有缺陷触发，日志不可判定，不臆测。

## 后续

- 设备已因 panic 自动重启（20:18:27），按更新后的规则不再补 `adb reboot`；
- 建议冷机后复跑至少一次；若 panic 复现，需要带 `dmesg`/pstore 访问权限的 root 环境才能定位。

原始日志：[`CPP06c-20260917-multicast-kernel-panic.native.log`](CPP06c-20260917-multicast-kernel-panic.native.log)
