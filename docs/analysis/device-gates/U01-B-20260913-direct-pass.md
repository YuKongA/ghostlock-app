# U01-B 真机门禁：compact payload 与 W1 页面验收通过

- 设备：Sony A301SO
- 内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 入口：Direct，main 0 / consumer 1
- 安全拒绝日志：[U01-B-20260913-direct-safe-reject.native.log](U01-B-20260913-direct-safe-reject.native.log)
- 成功日志：[U01-B-20260913-direct-pass.native.log](U01-B-20260913-direct-pass.native.log)

## W1 页面验收

第一次运行依次发现 4 个 `right` 地址的 byte 2 为偶数，全部输出 `stores an even byte over selinux_state.initialized; taking another`。达到 profile 的 `prepare_max_attempts=4` 后返回 heap spray failed，未触发错误 arm 或危险 route，证明拒绝路径保持安全且没有用上游硬编码扩大重试。

第二次运行先拒绝 1 个危险页面，随后接受安全页面。W1 和 W1b 均成功，说明验收失败后的 current/reclaim 清理由下一次 Heap prepare 正确接管。

## 端到端结果

成功运行中 W1、W1b、W2、W2b 及后续 route 共 6 次均为 `step=0/calls=1/success=1`，没有 `payload arm mismatch`。最终 `child uid = 0`、`KernelSU ready`，总耗时约 30.4 秒。由此确认 route-neutral compact value 编码没有破坏已验证的 5.15 Multicast 行为。

TCP Zerocopy 与 Select Stack 的 compact 编码仍等待对应设备/协作者补证；这不扩大本门禁的路线覆盖范围。
