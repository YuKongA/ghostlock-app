# A2-2d 真机门禁：write_root_script 迁到 terminal（multicast_waiter）— PASS

对应提交 `c771242`（Phase A2-2d）。候选二进制 SHA-256
`b547fe5547f493c5e7b504ac993a9deff28a6600a49d41ef29522fbc7f759a68`。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；冷启动 `boot_ms=31781`；
  KernelSU 未加载、Enforcing、uid 2000。route = multicast_waiter；CPU 对 0/1。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a2e --load-prebuilt-profile
  /data/local/tmp/p0.bin`。

## 结果

PASS（一次冷机跑通过；完整输出见 `.native.log`）：

```
[*] root script written path=/data/local/tmp/.ghostlock_root.sh bytes=6081
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[*] [T+20704ms] exploit complete
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root)`；无 panic。

## 变更说明

`write_root_script`（含 12KB 内嵌 shell 脚本）从 `attack::ops.cpp` 抽出到 `terminal/root_script.{hpp,cpp}`
（namespace `ghostlock::terminal`）。backend 调用点 `attack::write_root_script()` → `terminal::write_root_script()`
（backend→terminal 属允许边）。host 数据流 harness 用 `attack_stub.cpp` 提供 `terminal::write_root_script` 空桩，
`root_script.cpp` 只进设备 `CXX_SRCS`。脚本字节数 6081 与迁移前一致。`cmp_disasm --reviewed` 6 函数骨架一致。
