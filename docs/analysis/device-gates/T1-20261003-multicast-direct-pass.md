# T1 真机门禁：三维 selection + 稀疏 catalog（multicast_waiter）— PASS

对应提交 `b24dbb5`（分支总 plan S2 T1）。候选二进制 SHA-256
`ef983b1c8fbabce059f17ffa50fe93e941e38cd024859c21cb9715dc9a71c869`。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；冷启动 `boot_ms=35091`；
  KernelSU 未加载、Enforcing、uid 2000。route = multicast_waiter；CPU 对 0/1。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-t1 --load-prebuilt-profile
  /data/local/tmp/p0.bin`。

## 结果

PASS（一次冷机跑通过；完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[*] [T+16853ms] exploit complete
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root)`；无 panic。

## 变更说明

`ComponentSelection` 升为 `{BackendKind, StepSetKind, TerminalKind}`；新增 `StepSetKind`（`W1W2`/`W1W3`/`PageCacheWrite`）；
`combination_supported`/`DispatchTarget`/`dispatch_target_of` 按三元组；catalog 仍**稀疏枚举**（当前唯一合法三元组
`{Cve2026_43499, W1W3, RootChild}`，`DispatchTarget::Cve43499W1W3_RootChild`）。`Pipeline` 从 `Backend::steps` 取步骤集。
`main.cpp` 暂以 `W1W3` 常量过渡（T3 改为读 profile 私有 section）。`cmp_disasm --reviewed` 6 函数骨架一致（`do_one_write` 126）。
