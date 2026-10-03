# A2-2c 真机门禁：profile install/resolve 迁到 backend bootstrap（multicast_waiter）— PASS

对应提交 `d791acb`（Phase A2-2c）。候选二进制 SHA-256
`e331ed35adf916133b9026f8f74beafdec61c036201f3f33dc944e8e2ef22109`。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；冷启动 `boot_ms=34246`；
  KernelSU 未加载、Enforcing、uid 2000。route = multicast_waiter；CPU 对 0/1。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a2d --load-prebuilt-profile
  /data/local/tmp/p0.bin`。

## 结果

PASS（一次冷机跑通过；完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[*] [T+17259ms] exploit complete
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root)`；无 panic。

## 变更说明

`log_execution_settings`/`resolve_profile_addresses`/`install_profile` 从 `attack::ops.*` 迁到
`backend/cve_2026_43499/bootstrap.*`（namespace `ghostlock::backend`）。

**落点修正**：计划原写“→ pipeline bootstrap”，但 backend 不得依赖 pipeline（ADR-0004 R1），且这三个函数
直接读写 43499 backend state（profile/addresses/mcast_tuning），故落在 backend 内部。backend.cpp 调用点改为
同命名空间直接调用；host 数据流 harness 用 `attack_stub.cpp` 提供 `backend::install_profile` 空桩，
`bootstrap.cpp` 只进设备 `CXX_SRCS`。`cmp_disasm --reviewed` 6 函数骨架一致（`do_one_write` 126）。
