# B2 真机门禁：per-backend 状态构造/析构（RAII）（multicast_waiter）— PASS

对应提交 `ec58b1a`（B2）。候选二进制 `build/native/ghostlock` sha256
`cbc8ffa3bb3ae7be5e86854c26dfbe63c13b8045a45ef435c50b765ad6fd99cf`；
profile sha256 `8aa3200035b3768100cdf660be3b5a5a62b18936c4f653c35ee8a9243bb1b7af`。

## 设备与入口

- A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；serial `QV770MFGJ1`；route = multicast_waiter。
- 冷机：reboot → `sys.boot_completed=1`（38644 ms）→ sleep 8；post-boot 无 kernelsu、SELinux Enforcing。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp setsid timeout 60 /data/local/tmp/ghostlock-b2 --load-prebuilt-profile /data/local/tmp/p0.bin`（`setsid` + 设备端文件重定向避免 root child 占 pty）；`EXPLOIT_EXIT=0`。

## 结果

PASS（run-1，一次冷机；完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4（W1/W1b/W2/W2b）
[+] child is root!
[*] [T+18617ms] exploit complete
[+] KernelSU ready
```

无 `panic`/`BUG:`/`Call trace`；运行前后 uptime 连续；事后 `su -c id` = root、`kernelsu` 在 `/proc/modules`。

## 变更说明

`BackendState` 概念（可选状态契约）+ `Pipeline::run` 的 `BackendStateGuard` RAII：组合根不再无条件构造 43499 状态，
改由 pipeline 按所选 backend 构造/析构；43499 攻击步骤未改，`cmp_disasm` 中 `do_one_write` 无形状变化。
