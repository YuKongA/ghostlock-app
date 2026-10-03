# GLKv3 真机门禁（MessagePack 生产切换）— PASS

对应提交 `af0c9c0`（GLKv3-4）。候选二进制 `build/native/ghostlock` sha256 `189f7ee66e1f58d7d16885dc3b7c6b6c51805e2a77efa1715e5098b5f480d695`；
v3 profile `build/kernel-profiles/5.15.189-android13-8-00016-g51bba4309aac-ab14546557.bin` sha256 `b1dbcc97f86e5000dbdee3a8ff502326a866edddb8130376e54adef77ac423e3`（1592B，首字节 `0x86` = MessagePack fixmap 根）。

## 设备与入口

- A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；serial `QV770MFGJ1`；route = multicast_waiter。
- 冷机：reboot → `sys.boot_completed=1` → sleep 8；post-boot `/proc/modules` 无 kernelsu。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp setsid timeout 60 /data/local/tmp/ghostlock-v3 --load-prebuilt-profile /data/local/tmp/p0-v3.bin`。

## 结果：PASS（第一次冷机即通过）

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[+] KernelSU ready
```

- 事后 `su -c id` → `uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0`；`/proc/modules` 有 `kernelsu ... (OE)`。
- 日志字节 9644，与 v2 门禁同构；无 panic/BUG。完整日志见 `.native.log`。

## 判定意义

- native `profile_entry::decode` 的 **v3 自动判别生效**：首字节为 MessagePack map ⇒ 走 `parse_v3`（Production schema 校验），
  v3 profile 在真机被正确解析并命中 route，与 v2 行为等价。
- 同构建冷机一次通过（未触发复跑）。

## 说明

首次委派的门禁子智能体因当时 adb 列表为空判为 BLOCKED（设备实际稍后恢复在线）；本次由主智能体在设备恢复后执行同一流程，
离线项（export 58/58 v3、profile sha256、首字节）沿用其结论。
