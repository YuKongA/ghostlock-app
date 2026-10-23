# 2026-10-21 fd sentinel 修复后运行（PARTIAL / kernel panic）

## 设备

```text
A301SO / QV770MFGJ1
5.15.189-android13-8-00016-g51bba4309aac-ab14546557
```

本次是 warm run（设备已 boot，SELinux 已 permissive），不是最终冷机门禁。

## 修复

- `runtime_init` 把 `reclaim_sv/quarantined_reclaim_sv/prebuilt_reclaim_sv` 初始化为 -1；
- `retry_write_stage` 的 prebuild/activation/repair 单次失败不再直接 return。

## 第一次 warm run

日志关键段：

```text
W1b: private scratch repair attempt 1/3
...
private scratch repaired; releasing quarantine
W1: SELinux ...
W2: cred attempt 1/15
W2 fast repair payload prebuilt
=== W2: cred ===
...
child is root!
no app seccomp filter (adb/shell flow); skipping W3
[T+9193ms] exploit complete
```

结果：

- W1 scratch quarantine 通过；
- W2 prebuild 通过；
- child uid=0；
- 但进程 `rc=255`，随后设备消失，重启后：
  `ro.boot.bootreason=kernel_panic,null`。

## 第二次 warm run（retry 修复）

```text
W1b: private scratch repair attempt 1/3
private scratch repaired; releasing quarantine
W2: cred attempt 1/15
W2 fast repair payload prebuilt
...
[route] ...
```

随后设备再次 panic/重启，日志在 W2 route 处截断。

## 判读

- 用户报告的“W2 只跑 1 轮”根因已确认并修复：fd sentinel 为 0，prebuild 立即失败并 return 0。
- 修复后 15 轮机制恢复，prebuild 成功，并能拿到 child uid=0。
- 仍存在独立的 W2/post-root kernel panic；不能标 supported。
- 本次 warm，不是冷机；冷机结论需在 panic 修复后重跑。

## 证据

- [C/C++ 稳定性差异研究](../c-to-cpp-stability-gap.md)
- [KernelSnitch 历史审计](../kernelsnitch-history-audit.md)
- 原始日志：`/tmp/fd-init-warm.log`、`/tmp/retry-warm.log`（未入库）
