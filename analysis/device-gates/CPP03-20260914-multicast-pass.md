# CPP03 Multicast 真机门禁

- APK：versionCode 185，提交 `df901ec`
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 入口：Direct；CPU main=0、consumer=1
- 结论：通过

验证要点：

- resolved profile、execution 参数及地址转换成功；
- W1、W1b、W2、W2b、W3 两次写入全部得到 `multicast route status=0 clean=1/1`；
- 每次路线均记录 `calls=1 success=1` 并完成线程 join；
- child UID 为 0，seccomp probe 返回预期结果；
- KernelSU module loaded，最终 `KernelSU ready`，SELinux 恢复 enforcing。

`mcast ghost disarm ret=-1 errno=110` 与此前成功基线一致；最终路线状态仍为 clean/disarmed，不判定为回归。

原始日志：[`CPP03-20260914-multicast-pass.native.log`](CPP03-20260914-multicast-pass.native.log)
