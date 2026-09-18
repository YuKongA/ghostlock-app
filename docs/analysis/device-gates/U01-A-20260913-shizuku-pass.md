# U01-A 真机门禁：扫描边界与日志耐久性通过

- 设备：Sony A301SO
- 内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 入口：Shizuku，main 0 / consumer 1
- 原始日志：[U01-A-20260913-shizuku-pass.native.log](U01-A-20260913-shizuku-pass.native.log)
- 设备侧日志：`Download/GhostLock/ghostlock-20260913-122023-310-0-shizuku.log.txt`

日志包含 `boot_ms=42449`，所有现有 `[T+…]` 阶段边界均完整保存。KernelSnitch 每次扫描正常完成，没有 range 尾部异常。W1、W1b、W2、W2b 的 route 均为 `step=0/calls=1/success=1`，最终 `child uid = 0`、`KernelSU ready`，总耗时约 19.7 秒。因此 U01-A 没有改变已验证攻击时序和结果。

本次成功运行未触发 panic，不能直接观察回滚后的文件耐久性；Native 文件入口与 Shizuku 持久文件的 fd sync 路径由代码边界和构建结果确认。
