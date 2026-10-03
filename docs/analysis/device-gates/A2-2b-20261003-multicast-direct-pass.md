# A2-2b 真机门禁：apply_iomem_cache 迁到 platform::runtime（multicast_waiter）— PASS（冷机复跑）

对应提交 `6d7e2a2`（Phase A2-2b）。候选二进制 SHA-256
`84c2043e94176038a7419acb7346616d5538a79a2a323677117eb455f2e94a28`。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；route = multicast_waiter；CPU 对 0/1。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a2c --load-prebuilt-profile
  /data/local/tmp/p0.bin`；`GHOSTLOCK_HOME` 下已有上次 root 跑出的 `.ghostlock_iomem`。

## 结果

**第一次冷机跑**：`iomem cache: direct_map_end=ffffff8a80000000`（与历史门禁一致）、`W1: SELinux attempt 1/1`，
首条 route clean 后 `Write 1 failed` → 失败。日志 `.native.log` 备份见
`A2-2b-20261003-multicast-rerun-fail.native.log`。

**同构建冷机复跑**：PASS（`.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[*] [T+16673ms] exploit complete
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root)`；无 panic。

## 判定

首跑失败归为已知竞争/时序抖动（与 `KERNEL-PANIC-01` 同类），**非本批引入**：

- 同构建冷机复跑 PASS；
- 关键环境量（`iomem cache: direct_map_end`、`W1: SELinux attempt 1/1`）与历史 PASS 日志逐字一致；
- `cmp_disasm --reviewed` 6 函数骨架一致（`do_one_write` 126）。

## 变更说明

`apply_iomem_cache` 与 `iomem_map_span` 从 `attack::ops.cpp` 迁到 `platform::runtime`
（`platform/runtime.cpp`）；签名改为 `(const char *home_dir, const char *release)`，由 backend 调用点传入，
使 `platform` 不依赖 config/backend（遵守 ADR-0004 允许边）。host 数据流 harness 的 `platform/runtime.hpp`
影集加 `apply_iomem_cache` 声明与空桩。
