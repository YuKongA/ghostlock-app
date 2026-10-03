# A2-2a 真机门禁：计时器族迁移到 support/timing.hpp（multicast_waiter，direct）— PASS

对应提交 `a1cedd1`（Phase A2-2a）。候选二进制 SHA-256
`e06b0b0322c69f7eb8284dfdd25f3ffa10fc3847aac1eb09bfb4dc6158c44df0`。
设备端 profile `p0.bin` SHA-256 `dac9e63132b40e7fcca362bddc1bb5e26c124df772d8e26b6e03b566a0997b72`（未修改）。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；冷启动 `boot_ms=31989`；
  KernelSU 未加载、Enforcing、uid 2000。route = multicast_waiter；CPU 对 0/1。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a2b --load-prebuilt-profile
  /data/local/tmp/p0.bin`。

## 结果

PASS（完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x3
[+] child is root!
[*] [T+18622ms] exploit complete
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root) context=u:r:ksu:s0`；无 panic。

## 变更说明

`exploit_t0`/`timer_reset`/`timer_ms`/`timer_mark` 从 `attack::ops.hpp` 迁到 `support/timing.hpp`，
**保持 inline**（关键：`timer_mark` 在 `attack_write` 内联，外联会改变 `do_one_write` 指令数 126→125，
违反攻击路径门禁）。host 数据流 harness 加 `support/timing.hpp` 影集头（no-op）。
`cmp_disasm --reviewed`：`do_one_write` 126 指令骨架一致，PASS。
